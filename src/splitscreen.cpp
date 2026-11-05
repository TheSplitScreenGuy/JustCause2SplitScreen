#include <windows.h>
#include <xinput.h>
#include <stdlib.h>
#include <stdio.h>
#include <intrin.h>
#include "splitscreen.h"
#include "player2.h"
#include "log.h"

namespace {

typedef DWORD(WINAPI* XInputGetState_t)(DWORD, XINPUT_STATE*);
typedef DWORD(WINAPI* XInputSetState_t)(DWORD, XINPUT_VIBRATION*);

XInputGetState_t g_getState;
XInputSetState_t g_setState;
// Pad 2's rumble (the game only rumbles Player 1's pad): from Player 2's free fall strength
// (Player2FallShake, speed * 0.002) times these, per motor ("p2rumble <low> <high>", testing).
volatile float g_rumbleLowScale = 3.0f, g_rumbleHighScale = 1.5f; // 6 / 3 felt a little high
WORD g_rumbleSent[2];
DWORD g_rumbleSentTime;
int g_slot = 1; // "controller 2"

// The game's own XInput imports (xinput1_3 ordinals 2 and 3), hooked in the import table to see
// which slots it reads and rumbles (diagnostics, logged every 10 s / on changes).
const uintptr_t kGameXInputGetStateIat = 0x00D93738;
const uintptr_t kGameXInputSetStateIat = 0x00D9373C;
XInputGetState_t g_gameGetState;
XInputSetState_t g_gameSetState;
volatile LONG g_gameReads[XUSER_MAX_COUNT];
DWORD g_gameReadsLogged;
WORD g_gameRumble[XUSER_MAX_COUNT][2];

// The black market menu Player 2 called (user: P2 controls the menu if he triggered it): while it
// is open (UI mode [0x011958C4] 5), the game's reads of pad 1 get pad 2. Claimed by his call
// (SplitscreenPlayer2CalledDealer), for the menu that opens within kMenuClaimMs; ends when it closes.
const uintptr_t kUiMode = 0x011958C4;
const int kUiModeBlackMarket = 5;
const DWORD kMenuClaimMs = 30000;
volatile DWORD g_menuClaimUntil;
volatile bool g_menuForPlayer2;

DWORD WINAPI GameGetStateHook(DWORD slot, XINPUT_STATE* state)
{
    if (slot < XUSER_MAX_COUNT) InterlockedIncrement(&g_gameReads[slot]);
    if (g_menuForPlayer2 && slot == 0 && g_getState) return g_getState(g_slot, state);
    return g_gameGetState(slot, state);
}

void UpdatePlayer2Menu()
{
    int mode = *(volatile int*)kUiMode;
    bool claimed = (LONG)(g_menuClaimUntil - GetTickCount()) > 0;
    bool want = mode == kUiModeBlackMarket && (g_menuForPlayer2 || claimed);
    if (want != g_menuForPlayer2) {
        g_menuForPlayer2 = want;
        Log("Black market menu: %s", want ? "player 2's pad drives it" : "back to player 1's pad");
        if (!want) g_menuClaimUntil = GetTickCount(); // used
    }
}

DWORD WINAPI GameSetStateHook(DWORD slot, XINPUT_VIBRATION* v)
{
    if (slot < XUSER_MAX_COUNT && v && (v->wLeftMotorSpeed != g_gameRumble[slot][0] || v->wRightMotorSpeed != g_gameRumble[slot][1])) {
        Log("Game rumble: slot %lu motors %u %u (from %08X), player 2 falling %d", slot, v->wLeftMotorSpeed,
            v->wRightMotorSpeed, (unsigned)(uintptr_t)_ReturnAddress(), Player2FallShake() > 0.0f);
        g_gameRumble[slot][0] = v->wLeftMotorSpeed;
        g_gameRumble[slot][1] = v->wRightMotorSpeed;
    }
    return g_gameSetState(slot, v);
}

void HookGameXInput()
{
    DWORD old;
    if (!VirtualProtect((void*)kGameXInputGetStateIat, 8, PAGE_READWRITE, &old)) return;
    g_gameGetState = *(XInputGetState_t*)kGameXInputGetStateIat;
    g_gameSetState = *(XInputSetState_t*)kGameXInputSetStateIat;
    *(void**)kGameXInputGetStateIat = (void*)GameGetStateHook;
    *(void**)kGameXInputSetStateIat = (void*)GameSetStateHook;
    VirtualProtect((void*)kGameXInputGetStateIat, 8, old, &old);
    Log("Game XInput imports hooked (get %p, set %p)", g_gameGetState, g_gameSetState);
}

// Diagnostics (2026-10-29: setting a map waypoint closed the game with the mod in, even without
// Player 2, and nothing was logged): the game's imports of the process exit paths log the
// game-code return addresses on the stack before passing the call on.
const uintptr_t kExitIats[] = { 0x00D930E0 /* ExitProcess */, 0x00D931D8 /* TerminateProcess */,
                                0x00D93358 /* _exit */, 0x00D93380 /* exit */, 0x00D935E8 /* abort */,
                                0x00D934F0 /* terminate */, 0x00D9349C /* _amsg_exit */ };
const char* const kExitNames[] = { "ExitProcess", "TerminateProcess", "_exit", "exit", "abort", "terminate", "_amsg_exit" };
void* g_origExit[_countof(kExitIats)];

// caller: the hook's return address (the game code that called the exit); the stack is scanned
// up to the thread's stack base only.
void LogExitStack(int which, const void* frame, void* caller, unsigned code)
{
    char line[300] = "";
    int n = 0, len = 0;
    const DWORD* stack = (const DWORD*)frame;
    const DWORD* top = (const DWORD*)((NT_TIB*)NtCurrentTeb())->StackBase;
    __try {
        for (const DWORD* p = stack; n < 20 && p < top && p < stack + 4000; p++)
            if (*p >= 0x00401000 && *p < 0x00D92000) {
                len += sprintf_s(line + len, sizeof(line) - len, " %08X", *p);
                n++;
            }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    Log("Game exit: %s(%u) called from %08X (thread %lu), stack%s", kExitNames[which], code,
        (unsigned)(uintptr_t)caller, GetCurrentThreadId(), line);
}

// Each hook has the callee's own convention: all of these are __stdcall or __cdecl with at most
// two dword arguments, so one forwarding shape per argument count is enough.
template <int I>
void __stdcall ExitHook1(UINT code)
{
    LogExitStack(I, &code, _ReturnAddress(), code);
    ((void(__stdcall*)(UINT))g_origExit[I])(code);
}
void __stdcall TerminateHook(HANDLE process, UINT code)
{
    if (process == GetCurrentProcess() || GetProcessId(process) == GetCurrentProcessId())
        LogExitStack(1, &process, _ReturnAddress(), code);
    ((BOOL(__stdcall*)(HANDLE, UINT))g_origExit[1])(process, code);
}
template <int I>
void __cdecl CrtExitHook(int code)
{
    LogExitStack(I, &code, _ReturnAddress(), (unsigned)code);
    ((void(__cdecl*)(int))g_origExit[I])(code);
}
template <int I>
void __cdecl CrtNoArgHook()
{
    int marker = 0;
    LogExitStack(I, &marker, _ReturnAddress(), 0);
    ((void(__cdecl*)())g_origExit[I])();
}

void HookGameExits()
{
    void* hooks[] = { (void*)ExitHook1<0>, (void*)TerminateHook, (void*)CrtExitHook<2>, (void*)CrtExitHook<3>,
                      (void*)CrtNoArgHook<4>, (void*)CrtNoArgHook<5>, (void*)CrtExitHook<6> };
    for (size_t i = 0; i < _countof(kExitIats); i++) {
        DWORD old;
        if (!VirtualProtect((void*)kExitIats[i], 4, PAGE_READWRITE, &old)) continue;
        g_origExit[i] = *(void**)kExitIats[i];
        *(void**)kExitIats[i] = hooks[i];
        VirtualProtect((void*)kExitIats[i], 4, old, &old);
    }
    Log("Game exit imports hooked (diagnostics)");
}

void LogGameXInputReads()
{
    if (!g_gameGetState || GetTickCount() - g_gameReadsLogged < 10000) return;
    g_gameReadsLogged = GetTickCount();
    LONG n[XUSER_MAX_COUNT];
    for (int i = 0; i < XUSER_MAX_COUNT; i++) n[i] = InterlockedExchange(&g_gameReads[i], 0);
    Log("Game XInput reads in 10 s: slot 0 %ld, 1 %ld, 2 %ld, 3 %ld (player 2 on slot %d)", n[0], n[1], n[2], n[3], g_slot);
}

void UpdateRumble(bool active)
{
    if (!g_setState) return;
    float strength = active ? Player2FallShake() : 0.0f;
    auto motor = [](float v) { return (WORD)(v <= 0.0f ? 0 : v >= 1.0f ? 65535 : v * 65535.0f); };
    WORD low = motor(strength * g_rumbleLowScale), high = motor(strength * g_rumbleHighScale);
    // XInput keeps the last setting; resend on a change, or now and then.
    if (abs(low - g_rumbleSent[0]) < 1000 && abs(high - g_rumbleSent[1]) < 1000 &&
        GetTickCount() - g_rumbleSentTime < 500 && (low || !g_rumbleSent[0]) && (high || !g_rumbleSent[1]))
        return;
    XINPUT_VIBRATION v = { low, high };
    g_setState(g_slot, &v);
    g_rumbleSent[0] = low;
    g_rumbleSent[1] = high;
    g_rumbleSentTime = GetTickCount();
}
bool g_initialized;

bool g_connected;
bool g_joined;
bool g_forced;
WORD g_prevButtons;
DWORD g_nextDisconnectedPoll;
Player2Sticks g_sticks;

float Axis(SHORT v, SHORT deadZone)
{
    if (v > -deadZone && v < deadZone) return 0.0f;
    float f = v < 0 ? (v + deadZone) / (32768.0f - deadZone) : (v - deadZone) / (32767.0f - deadZone);
    return f < -1.0f ? -1.0f : f > 1.0f ? 1.0f : f;
}

void Init()
{
    g_initialized = true;

    // Optional jc2ss.ini next to the exe:
    //   [Player2]
    //   ControllerSlot=1   ; 0 = first controller, 1 = second, ...
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) *(slash + 1) = '\0';
    strcat_s(path, "jc2ss.ini");
    int slot = (int)GetPrivateProfileIntA("Player2", "ControllerSlot", 1, path);
    g_slot = (slot >= 0 && slot < XUSER_MAX_COUNT) ? slot : 1;

    // The game already uses xinput1_3, so this just returns the loaded module.
    const char* names[] = { "xinput1_3.dll", "xinput1_4.dll", "xinput9_1_0.dll" };
    for (const char* name : names) {
        if (HMODULE mod = LoadLibraryA(name)) {
            g_getState = (XInputGetState_t)GetProcAddress(mod, "XInputGetState");
            g_setState = (XInputSetState_t)GetProcAddress(mod, "XInputSetState");
            if (g_getState) {
                Log("Player 2 uses XInput slot %d (%s)", g_slot, name);
                if (!GetPrivateProfileIntA("Debug", "SkipGameXInput", 0, path)) HookGameXInput();
                HookGameExits();
                return;
            }
        }
    }
    Log("XInput not available, Player 2 cannot join");
}

// Rejoining puts Player 2 back next to Player 1 (wherever either of them went meanwhile). On the
// first join there is no Player 2 yet; he is created next to Player 1 anyway.
void BringPlayer2Back()
{
    std::string result = Player2RequestTeleport(2.0f, 0.2f, 0.0f);
    if (result.compare(0, 2, "ok") == 0) {
        Log("Player 2 rejoined: back next to player 1");
        Player2RefillAmmoSoon();
    }
}

// Placeholder until step 2 finds the game's own state. It must return false
// during cutscenes (in-engine and video), loading screens and menus, so the
// split never appears there.
// The game's UI mode ([0x011958C4]: 3 = gameplay, 4 = pause menu, 5 = map) and a pending switch
// ([0x0105F754], 6 = none). Menus and the map get the whole screen.
bool IsInGameplay()
{
    const int mode = *(volatile int*)0x011958C4, pending = *(volatile int*)0x0105F754;
    return mode == 3 && pending == 6 && !SplitscreenCutscene() && !SplitscreenPlayer1Held();
}

bool g_player1Held;

} // namespace

// The cutscene manager (NCutscene::CCutsceneManager, [0x0118FB84]): +0x5C is set from a cutscene's
// loading to its end (+0x10 while it plays, +0x1C while one is loaded). Measured with the black
// market dealer's two cutscenes.
void SplitscreenPlayer2CalledDealer()
{
    g_menuClaimUntil = GetTickCount() + kMenuClaimMs;
    Log("Black market: player 2 called the dealer");
}

bool SplitscreenCutscene()
{
    uintptr_t manager = *(volatile uintptr_t*)0x0118FB84;
    return manager && *(volatile uint8_t*)(manager + 0x5C) != 0;
}

// Byte [0x01196071]: set by a script event (handler FUN_006386E0, the event at +0x358: it also
// disables the player through the character manager's slots 0x10 / 0x18 and ducks the game sound
// via FUN_006E9E40) and cleared by its partner event (+0x35C, player enabled again). Measured with
// a race flag (places.txt race_flag): set while the "Race challenge" screen is up, clear before
// and after "Challenge Started", when Player 1 can move.
bool SplitscreenPlayer1Held()
{
    return *(volatile uint8_t*)0x01196071 != 0;
}

void SplitscreenUpdate()
{
    if (!g_initialized) Init();
    if (!g_getState) return;
    LogGameXInputReads();
    UpdatePlayer2Menu();
    if (SplitscreenPlayer1Held() != g_player1Held) {
        g_player1Held = !g_player1Held;
        Log(g_player1Held ? "Player 1's control taken by a script (race screen?): split off" : "Player 1's control back: split allowed");
    }

    // Polling an empty slot is slow in XInput, so only retry it once a second.
    DWORD now = GetTickCount();
    if (!g_connected && now < g_nextDisconnectedPoll) return;

    XINPUT_STATE state = {};
    bool connected = g_getState(g_slot, &state) == ERROR_SUCCESS;
    if (connected != g_connected) {
        g_connected = connected;
        Log("Controller in slot %d %s", g_slot, connected ? "connected" : "disconnected");
        if (!connected && g_joined) {
            g_joined = false;
            Log("Player 2 left (controller disconnected)");
        }
    }
    if (!connected) {
        g_nextDisconnectedPoll = now + 1000;
        g_prevButtons = 0;
        g_sticks = {};
        return;
    }

    UpdateRumble(g_joined && IsInGameplay());
    const XINPUT_GAMEPAD& pad = state.Gamepad;
    g_sticks.leftX = Axis(pad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    g_sticks.leftY = Axis(pad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    g_sticks.rightX = Axis(pad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
    g_sticks.rightY = Axis(pad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
    g_sticks.leftTrigger = pad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? pad.bLeftTrigger / 255.0f : 0.0f;
    g_sticks.rightTrigger = pad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? pad.bRightTrigger / 255.0f : 0.0f;

    WORD buttons = state.Gamepad.wButtons;
    // Start and Back are for joining/leaving, not game actions.
    g_sticks.buttons = buttons & ~(XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_BACK);
    bool startPressed = (buttons & XINPUT_GAMEPAD_START) && !(g_prevButtons & XINPUT_GAMEPAD_START);
    bool backHeld = (buttons & XINPUT_GAMEPAD_BACK) != 0;
    g_prevButtons = buttons;

    if (startPressed) {
        if (!g_joined && !backHeld) {
            g_joined = true;
            Log("Player 2 joined");
            BringPlayer2Back();
        } else if (g_joined && backHeld) {
            g_joined = false;
            Log("Player 2 left");
        }
    }
}

Player2Status SplitscreenPlayer2Status()
{
    if (g_joined) return Player2Status::Joined;
    return g_connected ? Player2Status::Waiting : Player2Status::NoController;
}

int SplitscreenPlayer2Slot()
{
    return g_slot;
}

bool SplitscreenInGameplay()
{
    return IsInGameplay();
}

bool SplitscreenActive()
{
    return (g_joined || g_forced) && IsInGameplay();
}

void SplitscreenForce(bool on)
{
    g_forced = on;
}

void SplitscreenSimulateJoin(bool joined)
{
    g_joined = joined;
    Log(joined ? "Player 2 joined (simulated)" : "Player 2 left (simulated)");
    if (joined) BringPlayer2Back();
}

Player2Sticks SplitscreenPlayer2Sticks()
{
    return g_sticks;
}

void SplitscreenSetRumbleScale(float low, float high)
{
    g_rumbleLowScale = low;
    g_rumbleHighScale = high;
}
