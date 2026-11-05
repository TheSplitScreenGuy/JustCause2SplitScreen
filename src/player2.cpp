#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "player2.h"
#include <xinput.h>
#include <math.h>
#include <intrin.h>
#include <vector>
#include <algorithm>
#include "splitscreen.h"
#include "audio.h"
#include "splitrender.h"
#include "game.h"
#include "patch.h"
#include "terrain2.h"
#include "world2.h"
#include "trace.h"
#include "log.h"

bool DebugSkip(const char* key); // dllmain.cpp: jc2ss.ini [Debug] switches (testing)

namespace {

// [JustCause2.exe+0xD8FB24] -> CCharacterManager (vtable 0xEB88DC)
const uintptr_t kCharacterManagerPtr = 0x0118FB24;
const uintptr_t kCharacterManagerVtable = 0x00EB88DC;
const uintptr_t kManagerPlayerSlot = 0x3570;  // boost::shared_ptr<CPlayer> (pointer, control block)
// CCharacterManager::CreatePlayer (__thiscall, no stack args): builds the CPlayer and its Rico
// into the manager's player slot if that slot is empty.
const uintptr_t kCreatePlayer = 0x00673C40;

// Globals that are zeroed while Player 2 is created, to skip player-1-only registration.
// 0x01190AEC: statistics system; when set, CBlackmarketHook (part of CPlayer) registers
// the "CHAOS"/"MONEY" stats, which crashes for a second player.
const uintptr_t kSuppressDuringSpawn[] = { 0x01190AEC };

const uintptr_t kPlayerVtable = 0x00EBC2D4;
const uintptr_t kPlayerCharacter = 0x48;      // boost::shared_ptr<CCharacter>
const uintptr_t kCharacterVtable = 0x00EB881C;
const int kCharacterSlots = 120;
uintptr_t g_characterVtable[1 + kCharacterSlots]; // the players' private copy (KeepPlayerHitHooks)
const uintptr_t kCharacterPosition = 0x18C;   // last row of the world matrix at +0x15C

typedef void(__thiscall* CreatePlayer_t)(void* manager);

// Resource cache lookup/load (FUN_004c7670): __thiscall(cache, shared_ptr* out, std::string* path).
const uintptr_t kResourceGet = 0x004C7670;
const unsigned char kResourceGetPrologue[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x58 };
// Callers use the return value (EAX = out), so hooks must pass it through.
typedef uintptr_t(__thiscall* ResourceGet_t)(void* cache, uintptr_t* out, void* path);
ResourceGet_t g_origResourceGet;
volatile bool g_tracing;

// Paper-doll body builder (FUN_007e6c60): __thiscall(CCharacter*, desc), ret 4. It builds
// body parts from the character's models (count +0x46C, shared_ptrs from +0x48C), which a
// runtime-created player doesn't get, so Player 2 borrows Player 1's. Hooked with a hardware
// breakpoint during the spawn only: patching its code makes the game quit at startup.
const uintptr_t kBuildBody = 0x007E6C60;
const uintptr_t kCharacterModelCount = 0x46C;
const uintptr_t kCharacterModels = 0x48C; // 5 boost::shared_ptr<CCharacterRules>
const uintptr_t kCharacterInfo = 0x6C4;   // CCharacterInfo (model, animation and effect paths)
uintptr_t g_modelDonor; // Player 1's character while Player 2 is being created

// CCharacterInfo std::string fields (character-relative) that a runtime-created player leaves
// empty but Player 1 gets from the save: model, eyelid model, the four animation state
// machines and the effect settings. Without them Player 2's setup re-binds Player 1's own
// animation state machines to Player 2, which leaves Player 1 invisible.
const uintptr_t kInfoStrings[] = { 0x6F0, 0x730, 0x7D8, 0x7F4, 0x810, 0x82C, 0x87C };

// std::string::assign(const std::string&) from the game's C++ runtime (same heap as the game).
typedef void*(__thiscall* StringAssign_t)(void* self, const void* other);
StringAssign_t StringAssign()
{
    static StringAssign_t fn;
    if (!fn) {
        HMODULE m = GetModuleHandleA("MSVCP80.dll");
        if (m)
            fn = (StringAssign_t)GetProcAddress(
                m, "?assign@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@ABV12@@Z");
    }
    return fn;
}

typedef void*(__thiscall* StringAssignC_t)(void* self, const char* text);
StringAssignC_t StringAssignC()
{
    static StringAssignC_t fn;
    if (!fn) {
        HMODULE m = GetModuleHandleA("MSVCP80.dll");
        if (m)
            fn = (StringAssignC_t)GetProcAddress(
                m, "?assign@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@PBD@Z");
    }
    return fn;
}

const char* StdString(void* s);

void CopyInfoStrings(uintptr_t from, uintptr_t to)
{
    StringAssign_t assign = StringAssign();
    if (!assign) { Log("  P2 info: std::string::assign not found"); return; }
    for (uintptr_t off : kInfoStrings) assign((void*)(to + off), (const void*)(from + off));
    Log("  P2 info: copied %u strings from player 1 (model %s)", (unsigned)(sizeof(kInfoStrings) / sizeof(kInfoStrings[0])),
        StdString((void*)(to + 0x6F0)));
}

// MSVC 2008 std::string: +4 inline buffer or pointer, +0x14 size, +0x18 capacity.
const char* StdString(void* s)
{
    uintptr_t base = (uintptr_t)s;
    return *(uint32_t*)(base + 0x18) < 16 ? (const char*)(base + 4) : *(const char**)(base + 4);
}

uintptr_t __fastcall HookedResourceGet(void* cache, void* /*edx*/, uintptr_t* out, void* path)
{
    uintptr_t r = g_origResourceGet(cache, out, path);
    if (g_tracing) Log("  resource %s -> %08X (cache %08X)", StdString(path), (unsigned)out[0], (unsigned)(uintptr_t)cache);
    return r;
}

// CCharacter constructor (FUN_007ebd20): __thiscall(void* mem, CCharacterInfo* desc). The
// player setup passes a descriptor built from player settings, which at runtime lacks the
// model, state machine and effect paths that Player 1 got from the save. Filling them in at the
// constructor's entry lets the game build Player 2 completely (model, render blocks, rules).
const uintptr_t kCharacterCtor = 0x007EBD20;

// Creating a player's character (FUN_00664440, after the constructor) shifts the physics origin
// (FUN_009B3950, __thiscall(physics, float* position): moves it by position - origin on x/z) to
// the new character's start point near the world's centre. The shift took static bodies out of
// the world for good (a base's helipad around Player 1: both players fell through it until the
// base was reloaded); putting the origin back and adding them again afterwards flung both players
// kilometres away. So for Player 2's creation the shift is given the current origin: no shift.
const uintptr_t kPhysicsShiftOrigin = 0x009B3950;
const uintptr_t kPhysicsOrigin = 0x28; // physics system: float x, y, z

void OnOriginShift(CONTEXT* ctx)
{
    RearmBreakpoint(ctx, 0, 0, nullptr); // once
    if (!g_modelDonor) return;
    __try {
        const float* origin = (const float*)(ctx->Ecx + kPhysicsOrigin);
        float* position = (float*)((DWORD*)ctx->Esp)[1];
        Log("  P2 spawn: physics origin shift to %.0f %.0f skipped (origin stays at %.0f %.0f)", position[0],
            position[2], origin[0], origin[2]);
        position[0] = origin[0];
        position[2] = origin[2];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("  P2 spawn: could not keep the physics origin");
    }
}

// Breakpoint at the CCharacter constructor's entry: [esp+4] is the descriptor.
void OnCharacterCtor(CONTEXT* ctx)
{
    uintptr_t donor = g_modelDonor;
    if (!donor) return;
    RearmBreakpoint(ctx, 0, kPhysicsShiftOrigin, OnOriginShift); // the constructor's slot is free now
    __try {
        uintptr_t desc = ((DWORD*)ctx->Esp)[1];
        // Descriptor has the CCharacterInfo layout: character offset - 0x6C4.
        CopyInfoStrings(donor, desc - kCharacterInfo);
        Log("  P2 ctor: filled descriptor %08X from player 1's info", (unsigned)desc);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("  P2 ctor: could not fill descriptor");
    }
}

// FUN_00572270: __thiscall(CCharacter*, CCharacterInfo*): creates a CCharacterRules (animation
// state machine + rules) for each non-empty state machine path in the info and assigns them to
// the character's rule slots (+0x48C...). Each rule's state machine gets bound to the character
// it belongs to, so every character needs its own.
const uintptr_t kCreateRules = 0x00572270;
typedef void(__thiscall* CreateRules_t)(void* character, void* info);

// Breakpoint at the body builder's entry: ECX is the character.
void OnBuildBody(CONTEXT* ctx)
{
    uintptr_t c = ctx->Ecx;
    uintptr_t donor = g_modelDonor;
    __try {
        // At entry: [esp] = return address, [esp+4] = desc; earlier frames via the ebp chain.
        DWORD* esp = (DWORD*)ctx->Esp;
        char chain[256];
        int n = sprintf_s(chain, "%08lX", esp[0]);
        DWORD* ebp = (DWORD*)ctx->Ebp;
        for (int i = 0; i < 8 && ebp && n < 230; i++) {
            n += sprintf_s(chain + n, sizeof(chain) - n, " %08lX", ebp[1]);
            ebp = (DWORD*)ebp[0];
        }
        Log("  P2 body builder: character %08X desc %08lX, called from %s", (unsigned)c, esp[1], chain);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    if (!donor || c == donor || *(int*)(c + kCharacterModelCount) != 0) return;

    // The setup counts rules from settings strings that are empty at runtime, so the count is
    // 0 even when the constructor created rules from the filled-in descriptor. Never borrow
    // Player 1's rules: each one's state machine is bound to a single character.
    const uintptr_t* rules = (const uintptr_t*)(c + kCharacterModels);
    int count = 0;
    while (count < 4 && rules[count * 2]) count++;
    if (!count) {
        CopyInfoStrings(donor, c);
        __try {
            ((CreateRules_t)kCreateRules)((void*)c, (void*)(c + kCharacterInfo));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("  P2 rules: creation raised an exception");
        }
        while (count < 4 && rules[count * 2]) count++;
    }
    *(int*)(c + kCharacterModelCount) = count;
    Log("  P2 body: %d own rules (%08X %08X %08X %08X)", count, (unsigned)rules[0], (unsigned)rules[2],
        (unsigned)rules[4], (unsigned)rules[6]);
}

// FUN_006F77A0 (__thiscall(controller registry, property container), ret 4), called by
// CreatePlayer, sets the camera setup ids from the player's camera parameters: several globals
// (0x011FCCD0.., 0x01076ACC) and +4 of the shared character controllers (on foot, skydive,
// parachute, swim, dive, ragdoll, ...; each controller asks the camera for its setup). For a
// player created mid-game the parameters are empty, so every id became -1 and Player 1's camera
// stopped switching (no skydive/parachute camera, stuck in the reel camera after a grapple).
// They're saved before Player 2 is created and put back after. The controllers come from the
// controller registry (character manager +0x35A0) by type hash: FUN_0084EC30 (__thiscall
// registry, shared_ptr* out, uint hash), ret 8.
const uintptr_t kCameraSetupGlobals[] = { 0x011FCCD0, 0x011FCCD4, 0x011FCCD8, 0x011FCCDC, 0x011FCCEC, 0x01076ACC };
const uint32_t kCameraSetupControllers[] = { 0x1596B4DD, 0x2BF15C1B, 0x0428A68B, 0xD9DE768F,
                                             0x895869E1, 0x20681196, 0x9E6D7A42, 0xE6DE5942 };
const uintptr_t kManagerControllerRegistry = 0x35A0;
const uintptr_t kFindController = 0x0084EC30;
const uintptr_t kControllerCameraSetup = 4;
typedef uintptr_t*(__thiscall* FindController_t)(void* registry, uintptr_t* out, uint32_t hash);
int LogException(EXCEPTION_POINTERS* info, const char* what);

struct CameraSetupIds {
    int globals[6];
    uintptr_t controllers[8];
    int controllerIds[8];
};

void SaveCameraSetupIds(uintptr_t manager, CameraSetupIds* ids)
{
    for (int i = 0; i < 6; i++) ids->globals[i] = *(int*)kCameraSetupGlobals[i];
    for (int i = 0; i < 8; i++) {
        ids->controllers[i] = 0;
        __try {
            uintptr_t sp[2] = {};
            ((FindController_t)kFindController)((void*)(manager + kManagerControllerRegistry), sp, kCameraSetupControllers[i]);
            // The registry keeps its own reference; ours is dropped without releasing (a leaked
            // count on a game-lifetime object).
            ids->controllers[i] = sp[0];
            if (sp[0]) ids->controllerIds[i] = *(int*)(sp[0] + kControllerCameraSetup);
        } __except (LogException(GetExceptionInformation(), "P2 spawn: find controller")) {
        }
    }
}

void RestoreCameraSetupIds(const CameraSetupIds* ids)
{
    int changed = 0;
    for (int i = 0; i < 6; i++) {
        changed += *(int*)kCameraSetupGlobals[i] != ids->globals[i];
        *(int*)kCameraSetupGlobals[i] = ids->globals[i];
    }
    for (int i = 0; i < 8; i++)
        if (uintptr_t c = ids->controllers[i]) {
            changed += *(int*)(c + kControllerCameraSetup) != ids->controllerIds[i];
            *(int*)(c + kControllerCameraSetup) = ids->controllerIds[i];
        }
    Log("  P2 spawn: restored player 1's camera setup ids (%d had changed)", changed);
}

int LogException(EXCEPTION_POINTERS* info, const char* what)
{
    const CONTEXT* c = info->ContextRecord;
    const EXCEPTION_RECORD* r = info->ExceptionRecord;
    Log("%s: exception %08lX at %08X (access %08X) eax %08lX ebx %08lX ecx %08lX edx %08lX esi %08lX edi %08lX esp %08lX",
        what, r->ExceptionCode, (unsigned)(uintptr_t)r->ExceptionAddress,
        r->NumberParameters > 1 ? (unsigned)r->ExceptionInformation[1] : 0, c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Esp);
    DWORD* stack = (DWORD*)c->Esp;
    for (int i = 0; i < 48; i += 8)
        Log("  stack %08lX %08lX %08lX %08lX %08lX %08lX %08lX %08lX", stack[i], stack[i + 1], stack[i + 2], stack[i + 3],
            stack[i + 4], stack[i + 5], stack[i + 6], stack[i + 7]);
    return EXCEPTION_EXECUTE_HANDLER;
}

// Logs the body-model fields of a character (count at +0x46C, shared_ptrs from +0x48C).
void LogCharacterModels(const char* who, uintptr_t character)
{
    __try {
        if (!character) { Log("  %s: no character", who); return; }
        int count = *(int*)(character + 0x46C);
        const uintptr_t* m = (const uintptr_t*)(character + 0x48C);
        Log("  %s %08X: model count %d, models %08X %08X %08X %08X %08X, anim sets %08X %08X %08X %08X", who,
            (unsigned)character, count, (unsigned)m[0], (unsigned)m[2], (unsigned)m[4], (unsigned)m[6], (unsigned)m[8],
            *(unsigned*)(character + 0x4B4), *(unsigned*)(character + 0x4BC), *(unsigned*)(character + 0x4C4),
            *(unsigned*)(character + 0x4CC));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("  %s: unreadable", who);
    }
}

volatile bool g_spawnRequested;
volatile bool g_teleportRequested;

// CCharacter vtable slot 30 (FUN_005ae3a0): places the character in the world at a matrix.
// This is what the player respawn/teleport flow (FUN_0078e1a0) calls on Rico when a save loads;
// besides setting the transform it activates the character (interface slots 9 and 25).
const int kPlaceInWorldSlot = 30;
typedef void(__thiscall* PlaceInWorld_t)(void* character, const float* matrix);
const uintptr_t kCharacterWorldMatrix = 0x15C;

uintptr_t g_player;      // second CPlayer
uintptr_t g_playerCount; // its shared_ptr control block (we hold that reference)

uintptr_t Manager()
{
    uintptr_t m = *(uintptr_t*)kCharacterManagerPtr;
    return m && *(uintptr_t*)m == kCharacterManagerVtable ? m : 0;
}

// A CCharacter: the game's vtable, or our copy the two players have (KeepPlayerHitHooks).
bool IsCharacter(uintptr_t c)
{
    uintptr_t vt = *(uintptr_t*)c;
    return vt == kCharacterVtable || vt == (uintptr_t)&g_characterVtable[1];
}

uintptr_t Character(uintptr_t player)
{
    if (!player || *(uintptr_t*)player != kPlayerVtable) return 0;
    uintptr_t c = *(uintptr_t*)(player + kPlayerCharacter);
    return c && IsCharacter(c) ? c : 0;
}

// The game's object registry ([0x012AAAFC], std::map name hash -> weak_ptr<object>; node: left
// +0, parent +4, right +8, key +0xC, object +0x18, control block +0x1C, "is head" byte +0x21)
// holds the player's character under one name. Player 2's CreatePlayer put his character in
// Player 1's entry, and a respawn after Player 1's death walks the registry calling a reset on each
// object (CGameWorld slot 0xC8, FUN_00536660 -> FUN_008FD210): with Player 2 there it crashed
// (FUN_008AC3C0, the walk's next node null). Player 1's entry is put back after the spawn.
const uintptr_t kObjectRegistry = 0x012AAAFC;

uintptr_t FindRegistryNode(uintptr_t object)
{
    uintptr_t head = *(uintptr_t*)(kObjectRegistry + 4);
    if (!head) return 0;
    uintptr_t stack[128];
    int depth = 0;
    uintptr_t n = *(uintptr_t*)(head + 4);
    while ((n && !*(uint8_t*)(n + 0x21)) || depth) {
        if (n && !*(uint8_t*)(n + 0x21)) {
            if (depth == 128) return 0;
            stack[depth++] = n;
            n = *(uintptr_t*)n;
        } else {
            n = stack[--depth];
            if (*(uintptr_t*)(n + 0x18) == object) return n;
            n = *(uintptr_t*)(n + 8);
        }
    }
    return 0;
}

bool Spawn()
{
    uintptr_t manager = Manager();
    if (!manager) { Log("P2 spawn: character manager not found"); return false; }
    uintptr_t* slot = (uintptr_t*)(manager + kManagerPlayerSlot);
    uintptr_t saved[2] = { slot[0], slot[1] };
    if (!saved[0]) { Log("P2 spawn: no player 1 yet"); return false; }

    uintptr_t c1 = *(uintptr_t*)(saved[0] + kPlayerCharacter);
    uintptr_t registryNode = 0, registryObject = 0, registryCount = 0;
    __try {
        registryNode = c1 ? FindRegistryNode(c1 + 4) : 0;
        if (registryNode) {
            registryObject = *(uintptr_t*)(registryNode + 0x18);
            registryCount = *(uintptr_t*)(registryNode + 0x1C);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        registryNode = 0;
    }
    // With the slot empty, CreatePlayer builds a brand-new player into it.
    slot[0] = slot[1] = 0;
    const size_t suppressCount = sizeof(kSuppressDuringSpawn) / sizeof(kSuppressDuringSpawn[0]);
    uintptr_t suppressed[suppressCount];
    for (size_t i = 0; i < suppressCount; i++) {
        suppressed[i] = *(uintptr_t*)kSuppressDuringSpawn[i];
        *(uintptr_t*)kSuppressDuringSpawn[i] = 0;
    }
    bool ok = true;
    g_tracing = true;
    g_modelDonor = *(uintptr_t*)(saved[0] + kPlayerCharacter);
    CameraSetupIds cameraIds;
    SaveCameraSetupIds(manager, &cameraIds);
    const Breakpoint bps[2] = { { kCharacterCtor, OnCharacterCtor }, { kBuildBody, OnBuildBody } };
    if (!SetThreadBreakpoints(GetCurrentThreadId(), bps, 2)) Log("P2 spawn: breakpoints FAILED");
    __try {
        ((CreatePlayer_t)kCreatePlayer)((void*)manager);
    } __except (LogException(GetExceptionInformation(), "CreatePlayer")) {
        ok = false;
    }
    g_tracing = false;
    ClearThreadBreakpoints(GetCurrentThreadId());
    RestoreCameraSetupIds(&cameraIds);
    g_modelDonor = 0;
    for (size_t i = 0; i < suppressCount; i++) *(uintptr_t*)kSuppressDuringSpawn[i] = suppressed[i];
    g_player = slot[0];
    g_playerCount = slot[1];
    LogCharacterModels("player 2", g_player ? *(uintptr_t*)(g_player + kPlayerCharacter) : 0);
    LogCharacterModels("player 1", *(uintptr_t*)(saved[0] + kPlayerCharacter));
    slot[0] = saved[0];
    slot[1] = saved[1];
    if (registryNode && *(uintptr_t*)(registryNode + 0x18) != registryObject) {
        // Swap the weak_ptr back: Player 1's control block gains the weak reference, Player 2's
        // (alive, so never released here) loses it.
        uintptr_t count2 = *(uintptr_t*)(registryNode + 0x1C);
        if (registryCount) InterlockedIncrement((volatile LONG*)(registryCount + 8));
        *(uintptr_t*)(registryNode + 0x18) = registryObject;
        *(uintptr_t*)(registryNode + 0x1C) = registryCount;
        if (count2) InterlockedDecrement((volatile LONG*)(count2 + 8));
        Log("P2 spawn: object registry entry %08X back to player 1's character", (unsigned)registryNode);
    }

    if (!ok) {
        uintptr_t partial = g_player && *(uintptr_t*)g_player == kPlayerVtable ? *(uintptr_t*)(g_player + kPlayerCharacter) : 0;
        Log("P2 spawn: CreatePlayer raised an exception (partial player %08X, character %08X)", (unsigned)g_player,
            (unsigned)partial);
        g_player = 0; // half-built: don't use it
        return false;
    }
    uintptr_t c = Character(g_player);
    Log("P2 spawn: player %08X character %08X (player 1 is %08X)", (unsigned)g_player, (unsigned)c, (unsigned)saved[0]);
    return c != 0;
}

} // namespace

bool Player2InstallHooks()
{
    // Patching game code here (resource trace, body builder) makes the game quit at startup,
    // probably an integrity check, so Player 2 uses hardware breakpoints during the spawn
    // instead. Nothing to install up front.
    return true;
}

namespace {

// --- Possessing an NPC as Player 2's body ---------------------------------------------------
// Player 2's own character never becomes active in the world (not animated or drawn), so
// Player 2 can instead take over a nearby NPC, which the game already fully simulates.

// FUN_0056f900: __thiscall(CCharacter*, CCharacterInfo*, shared_ptr oldRules[5],
// shared_ptr oldAnimSets[5], void** oldAnimControl), ret 0x10. The game's outfit-change rebuild:
// moves the old rules/animation sets out, creates rules from the info (FUN_00572270), a new
// animation control, rebuilds the body and binds the rules to the character.
const uintptr_t kRebuildCharacter = 0x0056F900;
typedef void(__thiscall* Rebuild_t)(void* character, void* info, uintptr_t* oldRules, uintptr_t* oldSets,
                                    uintptr_t* oldAnimControl);
const uintptr_t kCharacterSelfCount = 0x24;   // control block of the character's own shared_ptr
const uintptr_t kCharacterController = 0x2F0; // shared_ptr to the active movement controller
const uintptr_t kCharacterBrain = 0x9C8;      // interface the controllers read input from
const uintptr_t kPlayerBrainIface = 8;        // CPlayer's brain interface
const uintptr_t kPedestrianControllerVtable = 0x00EC2B2C;
const uintptr_t kAgentOnFootControllerVtable = 0x00EC2258;
const uintptr_t kCivilianSOControllerVtable = 0x00EC2794;
const uintptr_t kCharacterType = 0x584;       // 4 = player (player setup), 2/1 for NPCs
const int kCharacterTypePlayer = 4;
const uintptr_t kCharacterPlayerDriven = 0x728; // byte flag, 1 on Rico
const uintptr_t kCharacterInventory = 0x484;  // CInventory* (players only)
const uintptr_t kInventoryOwner = 0xC8;       // CInventory -> owning CCharacter

uintptr_t g_possessed;        // NPC character used as Player 2's body
uintptr_t g_ownController[2]; // Player 2's COnFootController shared_ptr (pointer, control block)
volatile uintptr_t g_possessRequest;
float g_moveInput[2];         // test input when controller 2 isn't connected: right, forward

// The player on-foot controller reads movement from an input context: FUN_007b6520
// (cdecl: CCharacter*, CInputContext*) reads actions 0x1E/0x1F (forward/back) and 0x20/0x21
// (right/left). For the possessed NPC the context is its AI avatar's (avatar+0x88), which the AI
// clears every frame, so Player 2's input is written into it right as this function starts
// (hardware breakpoint on the main thread). Context: +0x10/+0x14 vector of per-action entry
// lists; an entry's first dword is (state << 24) | value, state 3 = held, value 0..0xFFF.
const uintptr_t kOnFootInput = 0x007B6520;
// Named after the game's action table (0x20 = MOVE_LEFT, 0x21 = MOVE_RIGHT).
const int kActionForward = 0x1E, kActionBack = 0x1F, kActionLeft = 0x20, kActionRight = 0x21;

void SetAction(uintptr_t context, int action, float value)
{
    uintptr_t begin = *(uintptr_t*)(context + 0x10), end = *(uintptr_t*)(context + 0x14);
    if (!begin || begin + action * 4 >= end) return;
    uint32_t* entry = *(uint32_t**)(begin + action * 4);
    if (!entry) return;
    if (value > 1.0f) value = 1.0f;
    *entry = value > 0.02f ? (0x03000000u | (uint32_t)(value * 4095.0f)) : 0;
}

uintptr_t g_inputBody; // character whose on-foot input is replaced with Player 2's
volatile LONG g_inputCalls, g_inputHits, g_updateCalls;

// Writes Player 2's movement (controller 2's left stick, else the test input) into a context.
void WritePlayer2Move(uintptr_t context)
{
    Player2Sticks s = SplitscreenPlayer2Sticks();
    float right = s.leftX, forward = s.leftY;
    if (right == 0.0f && forward == 0.0f) {
        right = g_moveInput[0];
        forward = g_moveInput[1];
    }
    SetAction(context, kActionForward, forward);
    SetAction(context, kActionBack, -forward);
    SetAction(context, kActionRight, right);
    SetAction(context, kActionLeft, -right);
}

void OnOnFootInput(CONTEXT* ctx)
{
    __try {
        const DWORD* esp = (const DWORD*)ctx->Esp;
        InterlockedIncrement(&g_inputCalls);
        if (!g_inputBody || esp[1] != g_inputBody) return;
        InterlockedIncrement(&g_inputHits);
        WritePlayer2Move(esp[2]);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// --- Player 2's own input context ----------------------------------------------------------
// A CPlayer reads input from *(*(CPlayer+0x188) + 4) (its CSteeringPlayer's context), and a
// runtime-created player shares Player 1's. The context keeps, per action (vector at +0x10/+0x14),
// a list of entries, one per input device (entry: +0 (state << 24) | value, +0xC device, +0x10
// next); FUN_009828a0 (action, device -1) returns the entry with the highest value. So Player 1's
// gamepad entry beats anything written into the first entry, and P1's buttons reach P2 too.
// Player 2 gets a copy of the context whose lists have one entry each that only we write; the
// game's input system doesn't know about it, so nothing else changes them.
const uintptr_t kPlayerSteering = 0x188;
const uintptr_t kSteeringInput = 4;
const size_t kInputContextSize = 0x24;
const int kMaxInputActions = 0x200;
struct PrivateInput {
    uint32_t context[0x30 / 4];
    uint32_t* heads[kMaxInputActions];
    uint32_t entries[kMaxInputActions][5];
};
PrivateInput g_p2Input;
uintptr_t g_p2InputContext; // set once Player 2 uses g_p2Input

// Action ids = index in the game's action name table at 0x01071DE8 (PAUSE, MAP, ..., MOVE_FORWARD
// = 0x1E, ...). Pad 2's buttons, laid out like the game's own gamepad controls.
enum : int {
    kActFire = 0x0B, kActFireLeft = 0x0C, kActFireRight = 0x0D, kActThrowGrenade = 0x0F, kActReload = 0x10,
    kActJump = 0x23, kActCrouch = 0x24, kActEnterVehicle = 0x25, kActParachute = 0x47, kActStuntJumpOff = 0x49,
    kActFireGrapple = 0x51, kActEquipExplosive = 0x71, kActEquipRight = 0x72, kActEquipLeft = 0x73,
    kActEquipTwoHanded = 0x75, kActReeledInJump = 0x90, kActReeledInRelease = 0x91,
    kActDeployParachuteWhileReeling = 0x92, kActPickupRight = 0x94, kActKick = 0x7C, kActStuntJump = 0x77, kActUseItem = 0x26,
    kActHandbrake = 0x2B, kActExitVehicle = 0x2D, kActFireVehicleWeapon = 0x0E,
    kActVehicleFireLeft = 0x8A, kActVehicleFireRight = 0x8B,
    kActHorn = 0x2C, kActExitToStuntParachute = 0x2E, kActGrapplingAction = 0x52, kActEquipDual = 0x74,
    kActDash = 0x76, kActEvade = 0x81, kActMCFire = 0x89, kActPickupLeft = 0x93, kActParachuteThrusters = 0x98,
    // The button prompts (heli hijack, code entry: CDisarmAction's sequence, FUN_00813AD0 / FUN_008103E0).
    kActSequence1 = 0x85, kActSequence2 = 0x86, kActSequence3 = 0x87, kActSequence4 = 0x88,
    kActEquipBeacon = 0x95, kActActivateBeacon = 0x96, // EQUIP / ACTIVATE_BLACK_MARKET_BEACON (FUN_007FA910)
};
// Controllers with the weapon part (FUN_005DD210: aim stance, zoom), and where it is in them.
const struct { uintptr_t vtable, part; } kAimControllers[] = {
    { 0x00EC2470, 0xC },  // COnFootController
    { 0x00EC3A4C, 0xC },  // CRoofTopStuntController
    { 0x00EC2CE4, 0xC },  // CStuntAreaController
    { 0x00EC27D0, 0x24 }, // CClingStuntController
    { 0x00EC389C, 0x20 }, // CThreeNodeClingController
};
const unsigned kPadLeftTrigger = 0x10000, kPadRightTrigger = 0x20000; // beside the XInput button bits
// The black market beacon a character holds (a CFlare), and the beacon's "can call here" byte
// (FUN_007DF8A0() +0xD3, set by the held beacon's update FUN_00839480); see KeepBlackMarketForPlayer2.
const uintptr_t kCharacterBeacon = 0x470;
const uintptr_t kBeaconCallOk = 0x011979F8 + 0xD3;
const struct {
    unsigned input;
    int action;
} kP2Bindings[] = {
    // Player 1's own pad layout (read from the game's bindings, tools/tests/padbinds.py), plus a
    // few extras (ENTER_VEHICLE, FIRE, STUNTPOS_JUMP_OFF, FIRE_VEHICLE_WEAPON) that do no harm.
    { XINPUT_GAMEPAD_A, kActJump },          { XINPUT_GAMEPAD_A, kActParachute },
    { XINPUT_GAMEPAD_A, kActStuntJumpOff },  { XINPUT_GAMEPAD_A, kActReeledInJump },
    { XINPUT_GAMEPAD_A, kActDeployParachuteWhileReeling }, { XINPUT_GAMEPAD_A, kActGrapplingAction },
    { XINPUT_GAMEPAD_B, kActKick },          { XINPUT_GAMEPAD_B, kActReeledInRelease }, // B melee, like P1's pad
    { XINPUT_GAMEPAD_B, kActStuntJump },     // and the jump onto a vehicle (stunt position)
    { XINPUT_GAMEPAD_LEFT_THUMB, kActCrouch }, { XINPUT_GAMEPAD_LEFT_THUMB, kActHorn },
    { XINPUT_GAMEPAD_X, kActReload },
    { XINPUT_GAMEPAD_X, kActPickupLeft },    { XINPUT_GAMEPAD_Y, kActPickupRight },
    { XINPUT_GAMEPAD_Y, kActEnterVehicle },
    { XINPUT_GAMEPAD_Y, kActUseItem },       // the vehicle seat actions wait for USE_ITEM (0x26)
    { XINPUT_GAMEPAD_Y, kActExitVehicle },   { XINPUT_GAMEPAD_Y, kActExitToStuntParachute },
    { XINPUT_GAMEPAD_X, kActHandbrake },
    // The prompted button sequences: X, Y, B, A.
    { XINPUT_GAMEPAD_X, kActSequence1 },     { XINPUT_GAMEPAD_Y, kActSequence2 },
    { XINPUT_GAMEPAD_B, kActSequence3 },     { XINPUT_GAMEPAD_A, kActSequence4 },
    // Vehicle weapons (jets, helicopters, bikes): LB / RB; RB also the single weapon. RB dashes /
    // evades on foot and fires the parachute thrusters.
    { XINPUT_GAMEPAD_LEFT_SHOULDER, kActVehicleFireLeft }, { XINPUT_GAMEPAD_RIGHT_SHOULDER, kActVehicleFireRight },
    { XINPUT_GAMEPAD_RIGHT_SHOULDER, kActFireVehicleWeapon }, { XINPUT_GAMEPAD_RIGHT_SHOULDER, kActMCFire },
    { XINPUT_GAMEPAD_RIGHT_SHOULDER, kActDash }, { XINPUT_GAMEPAD_RIGHT_SHOULDER, kActEvade },
    { XINPUT_GAMEPAD_RIGHT_SHOULDER, kActParachuteThrusters },
    { kPadRightTrigger, kActFire },          { kPadRightTrigger, kActFireRight },
    { kPadLeftTrigger, kActFireLeft },       { kPadLeftTrigger, kActThrowGrenade },
    { XINPUT_GAMEPAD_LEFT_SHOULDER, kActFireGrapple },
    // D-pad down takes out the black market beacon, Y (aimed at the ground) calls the dealer, as on
    // Player 1's pad (user request; dual wield is holding D-pad right).
    { XINPUT_GAMEPAD_DPAD_UP, kActEquipTwoHanded },  { XINPUT_GAMEPAD_DPAD_DOWN, kActEquipBeacon },
    { XINPUT_GAMEPAD_Y, kActActivateBeacon },
    { XINPUT_GAMEPAD_DPAD_LEFT, kActEquipLeft },     { XINPUT_GAMEPAD_DPAD_RIGHT, kActEquipRight },
};
bool g_p2Held[kMaxInputActions]; // per action, held at Player 2's last update
volatile bool g_padLog;           // testing ("p2 padlog on|off")
volatile unsigned g_testButtons;  // testing ("p2 padpress <hex> [ms]"): pad 2 buttons held until g_testButtonsUntil
volatile DWORD g_testButtonsUntil;
Player2Sticks g_testSticks;        // testing ("p2 sticks lx ly rx ry lt rt ms"): pad 2's sticks until g_testSticksUntil
volatile DWORD g_testSticksUntil;
volatile int g_testAction = -1;   // testing: an action held until g_testActionUntil
volatile int g_testAction2 = -1;  // testing: optional second action held with it
volatile DWORD g_testActionUntil;

// New weapons lie at the character's feet until an equip attaches them (holsters and hand), and
// placing the character in the world (teleport, respawn) resets the inventory's hand state.
// Presses Player 2's equip right-hand slot for a moment, like Player 1 holds after loading.
void EquipPlayer2()
{
    g_testAction = kActEquipRight;
    g_testAction2 = -1;
    g_testActionUntil = GetTickCount() + 200;
}

// Entry states (bits 24..26): 1 idle, 2 pressed this update, 3 held, 4 released this update.
// Entry +4 is a float timer: while idle, seconds since the release (FUN_00982F50). The roof
// actions' press check (FUN_00825AC0) also counts "released less than [0x00E28850] ago" as a press,
// and Player 2's timers stayed 0: on a car roof he entered the driver seat at once, over and over.
// Here: seconds since the release (idle), held so far (held), large for never pressed.
// Released: how long it was held (FUN_00985A40). The on-foot weapon part (FUN_005DD210) takes
// EQUIP_RIGHT_SLOT held past [0x0107740C] as dual wield and a release held shorter as a tap that
// switches the right-hand weapon; with 0 there every hold ended in a tap that put the second one away.
DWORD g_p2ActionTime[kMaxInputActions]; // tick of the last press / release
const float kNeverPressed = 1000.0f;

void SetPrivateAction(int action, bool held, float value)
{
    if (action < 0 || action >= kMaxInputActions) return;
    uint32_t state = held ? (g_p2Held[action] ? 3u : 2u) : (g_p2Held[action] ? 4u : 1u);
    if (value > 1.0f) value = 1.0f;
    g_p2Input.entries[action][0] = (state << 24) | (held ? (uint32_t)(value * 4095.0f) : 0u);
    DWORD now = GetTickCount();
    float timer = (state == 1 && !g_p2ActionTime[action]) ? kNeverPressed : (now - g_p2ActionTime[action]) / 1000.0f;
    if (state == 2) timer = 0.0f;
    if (state == 2 || state == 4) g_p2ActionTime[action] = now;
    memcpy(&g_p2Input.entries[action][1], &timer, sizeof(timer));
    g_p2Held[action] = held;
}

// Whether Player 2's current action (character +0x324) is the reel-in (CReelInAction).
bool Player2Reeling()
{
    uintptr_t c = g_player ? Character(g_player) : 0;
    uintptr_t action = c ? *(uintptr_t*)(c + 0x324) : 0;
    return action && *(uintptr_t*)action == 0x00EC7834;
}

// Whether Player 2's movement controller (character +0x2F0) takes the stick as it is: the
// parachute's, and the free fall's (CSkyDiveController: dive and lean relative to the body, the
// heading follows the camera, see RecomputeFallTurn), unless "p2skyremap on" (testing).
volatile bool g_skyDiveRemap = false;

bool Player2RawStick()
{
    uintptr_t c = g_player ? Character(g_player) : 0;
    uintptr_t controller = c ? *(uintptr_t*)(c + kCharacterController) : 0;
    uintptr_t vtable = controller ? *(uintptr_t*)controller : 0;
    return vtable == 0x00EC24AC /* CParachuteController */ || (vtable == 0x00EC2C20 /* CSkyDiveController */ && !g_skyDiveRemap);
}

// Vehicle controls (analog, value 0..1): each vehicle type reads its own actions, so all are
// written every update from the sticks and triggers (the game's pad layout: triggers for gas /
// brake, left stick steers; bikes tilt and helicopters / planes pitch on the left stick's Y,
// helicopters turn and planes roll on the right stick's X, planes turn on the left stick's X, helicopters roll (strafe) on the left
// stick's X, helicopter altitude on the triggers).
// Not remapped to Player 1's camera (only the on-foot MOVE_* actions are).
enum : int {
    kActAccelerate = 0x27, kActReverse = 0x28, kActTurnLeft = 0x29, kActTurnRight = 0x2A,
    kActBikeForward = 0x2F, kActBikeBackward = 0x30, kActHeliForward = 0x31, kActHeliBackward = 0x32,
    kActHeliTurnLeft = 0x33, kActHeliTurnRight = 0x34, kActHeliRollLeft = 0x35, kActHeliRollRight = 0x36,
    kActHeliUp = 0x37, kActHeliDown = 0x38, kActPlanePitchUp = 0x3A, kActPlanePitchDown = 0x3B,
    kActPlaneTurnLeft = 0x3C, kActPlaneTurnRight = 0x3D, kActPlaneRollLeft = 0x3E, kActPlaneRollRight = 0x3F,
    kActPlaneThrustUp = 0x40, kActPlaneThrustDown = 0x41, kActBoatForward = 0x42, kActBoatBackward = 0x43,
    kActBoatTurnLeft = 0x44, kActBoatTurnRight = 0x45,
    kActLookUp = 0x03, kActLookDown = 0x04, kActLookLeft = 0x05, kActLookRight = 0x06,
};
const float kVehicleStickDeadZone = 0.1f;

void WriteAxis(int negative, int positive, float v)
{
    SetPrivateAction(positive, v > kVehicleStickDeadZone, v > 0 ? v : 0.0f);
    SetPrivateAction(negative, v < -kVehicleStickDeadZone, v < 0 ? -v : 0.0f);
}

void WriteVehicleInput(const Player2Sticks& s)
{
    auto trigger = [](int action, float v) { SetPrivateAction(action, v > 0.05f, v); };
    trigger(kActAccelerate, s.rightTrigger);
    trigger(kActReverse, s.leftTrigger);
    WriteAxis(kActTurnLeft, kActTurnRight, s.leftX);
    WriteAxis(kActBikeBackward, kActBikeForward, s.leftY);
    trigger(kActBoatForward, s.rightTrigger);
    trigger(kActBoatBackward, s.leftTrigger);
    WriteAxis(kActBoatTurnLeft, kActBoatTurnRight, s.leftX);
    WriteAxis(kActHeliBackward, kActHeliForward, s.leftY);
    // Like Player 1's mouse, the camera stick turns the helicopter (and with it the fixed guns,
    // which shot where Player 1 looked while Player 2's camera turned alone); the left stick strafes.
    WriteAxis(kActHeliTurnLeft, kActHeliTurnRight, s.rightX);
    WriteAxis(kActHeliRollLeft, kActHeliRollRight, s.leftX);
    trigger(kActHeliUp, s.rightTrigger);
    trigger(kActHeliDown, s.leftTrigger);
    WriteAxis(kActPlanePitchUp, kActPlanePitchDown, s.leftY); // stick forward pitches down
    // As on Player 1's pad: the left stick's X turns (user: P2 couldn't turn; PLANE_ROLL_LEFT/RIGHT
    // have no pad binding, so his left stick only rolled him). The right stick swings the camera
    // around the plane, as on Player 1's pad (user), so no roll either.
    WriteAxis(kActPlaneTurnLeft, kActPlaneTurnRight, s.leftX);
    trigger(kActPlaneThrustUp, s.rightTrigger);
    trigger(kActPlaneThrustDown, s.leftTrigger);
    // Mounted guns (flak cannons; CMountedGunController, FUN_00667EA0) aim with the camera look
    // actions LOOK_UP/DOWN/LEFT/RIGHT (3..6), which Player 2's pad doesn't send otherwise (his
    // right stick turns our camera): on a gun, his left stick drives them (user: with the right
    // stick the camera went round several times before the gun had turned; the right stick keeps
    // the camera).
    bool gun = Player2OnMountedGun();
    WriteAxis(kActLookDown, kActLookUp, gun ? s.leftY : 0.0f);
    WriteAxis(kActLookRight, kActLookLeft, gun ? -s.leftX : 0.0f);
}

// The weapon switch popup (show_dynamic) is set up once, from one read of the weapons, beside the
// player. Right after the press the player who pressed owns the script's weapon reads, and the
// other half doesn't show it.
DWORD g_hudPopupUntil, g_hudPopupReadsUntil;
int g_hudPopupOwner;      // 0 or 1
const DWORD kHudPopupMs = 4000, kHudPopupReadsMs = 300;

void NoteHudPopup(int owner)
{
    g_hudPopupOwner = owner;
    g_hudPopupUntil = GetTickCount() + kHudPopupMs;
    g_hudPopupReadsUntil = GetTickCount() + kHudPopupReadsMs;
}

// The weapon part of Player 2's controller, 0 for controllers without one.
uintptr_t Player2AimPart()
{
    uintptr_t c = g_player ? Character(g_player) : 0;
    uintptr_t controller = c ? *(uintptr_t*)(c + kCharacterController) : 0;
    if (!controller) return 0;
    for (const auto& a : kAimControllers)
        if (*(uintptr_t*)controller == a.vtable) return controller + a.part;
    return 0;
}

// Player 2's whole input for one update, written into their own context (player thread).
void WritePlayer2Input()
{
    // During cutscenes and while a script holds Player 1 (race screen; his view is off either way)
    // his pad does nothing.
    bool cutscene = SplitscreenCutscene() || SplitscreenPlayer1Held();
    Player2Sticks s = cutscene ? Player2Sticks{} : SplitscreenPlayer2Sticks();
    if ((LONG)(g_testSticksUntil - GetTickCount()) > 0) s = g_testSticks;
    float right = s.leftX, forward = s.leftY;
    if (right == 0.0f && forward == 0.0f && !cutscene) {
        right = g_moveInput[0];
        forward = g_moveInput[1];
    }
    // The game turns stick input into a world direction with Player 1's camera orientation (not
    // the main camera's matrix, so swapping that in doesn't change it). Take the direction
    // relative to Player 2's camera and express it relative to Player 1's instead. The parachute
    // (CParachuteController) takes the stick as it is (lean, pitch): remapped, its controls were
    // inverted whenever the two cameras faced apart. Free fall too (Player2RawStick).
    float f1[2], r1[2], f2[2], r2[2];
    if (!Player2RawStick() && SplitViewGroundAxes(0, f1, r1) && SplitViewGroundAxes(1, f2, r2)) {
        float d[2] = { forward * f2[0] + right * r2[0], forward * f2[1] + right * r2[1] };
        forward = d[0] * f1[0] + d[1] * f1[1];
        right = d[0] * r1[0] + d[1] * r1[1];
    }
    SetPrivateAction(kActionForward, forward > 0.02f, forward);
    SetPrivateAction(kActionBack, forward < -0.02f, -forward);
    SetPrivateAction(kActionRight, right > 0.02f, right);
    SetPrivateAction(kActionLeft, right < -0.02f, -right);

    WriteVehicleInput(s);

    unsigned inputs = s.buttons | (s.leftTrigger > 0 ? kPadLeftTrigger : 0) | (s.rightTrigger > 0 ? kPadRightTrigger : 0);
    if ((LONG)(g_testButtonsUntil - GetTickCount()) > 0) inputs |= g_testButtons;
    int test = g_testAction;
    bool testHeld = (LONG)(g_testActionUntil - GetTickCount()) > 0;
    bool held[kMaxInputActions] = {}, written[kMaxInputActions] = {};
    for (const auto& b : kP2Bindings)
        if (inputs & b.input) held[b.action] = true;
    // During a reel-in A is the parachute (CDeployParachuteWhileReelingAction), as Space is for
    // Player 1: the jumps bound to A with it won (the reeled-in jump 0x90 started instead).
    if (Player2Reeling()) held[kActJump] = held[kActReeledInJump] = held[kActStuntJumpOff] = false;
    int test2 = g_testAction2;
    if (test >= 0 && test < kMaxInputActions && testHeld) held[test] = true;
    if (test2 >= 0 && test2 < kMaxInputActions && testHeld) held[test2] = true;
    // Once per action, so an action bound to several inputs still gets its "pressed" update.
    auto write = [&](int action) {
        if (action < 0 || action >= kMaxInputActions || written[action]) return;
        written[action] = true;
        SetPrivateAction(action, held[action], 1.0f);
    };
    for (const auto& b : kP2Bindings) write(b.action);
    write(test);
    write(test2);
    // Y with his beacon out where it can call: the dealer comes for him, and the menu is his.
    static bool lastActivate;
    bool activate = held[kActActivateBeacon];
    if (activate && !lastActivate && *(volatile uint8_t*)kBeaconCallOk) {
        uintptr_t c2 = Character(g_player);
        if (c2 && *(uintptr_t*)(c2 + kCharacterBeacon)) SplitscreenPlayer2CalledDealer();
    }
    lastActivate = activate;
    for (int action : { kActEquipRight, kActEquipLeft, kActEquipDual, kActEquipTwoHanded })
        if (((g_p2Input.entries[action][0] >> 24) & 7) == 2) NoteHudPopup(1);
    // Right stick click: his aim zoom and stance (KeepPlayer2Stance), on foot and on stunt positions.
    static bool lastZoomButton;
    bool zoomButton = (inputs & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
    if (!Player2AimPart()) SplitRenderZoomPlayer2(false);
    else if (zoomButton && !lastZoomButton) SplitRenderZoomPlayer2(!SplitRenderPlayer2Zoomed());
    lastZoomButton = zoomButton;
    // Testing ("p2 padlog"): logs pad 2's button changes and the parachute action's state.
    static unsigned lastInputs;
    if (g_padLog && inputs != lastInputs) {
        Log("P2 pad: buttons %05X (was %05X), parachute entry %08X, jump entry %08X", inputs, lastInputs,
            g_p2Input.entries[kActParachute][0], g_p2Input.entries[kActJump][0]);
        lastInputs = inputs;
    }
}

bool GivePrivateInput(uintptr_t player)
{
    uintptr_t steering = *(uintptr_t*)(player + kPlayerSteering);
    uintptr_t shared = steering ? *(uintptr_t*)(steering + kSteeringInput) : 0;
    if (!shared) { Log("P2 input: no input context"); return false; }
    uintptr_t begin = *(uintptr_t*)(shared + 0x10), end = *(uintptr_t*)(shared + 0x14);
    int count = (int)((end - begin) / 4);
    if (!begin || count <= 0 || count > kMaxInputActions) { Log("P2 input: unexpected context (%d actions)", count); return false; }
    memset(&g_p2Input, 0, sizeof(g_p2Input));
    memcpy(g_p2Input.context, (const void*)shared, kInputContextSize);
    for (int i = 0; i < count; i++) {
        g_p2Input.entries[i][0] = 1u << 24;   // idle
        memcpy(&g_p2Input.entries[i][1], &kNeverPressed, sizeof(float)); // idle timer: never pressed
        g_p2Input.entries[i][3] = 0xFFFFFFFF; // device
        g_p2Input.heads[i] = g_p2Input.entries[i];
    }
    g_p2Input.context[0x10 / 4] = (uint32_t)(uintptr_t)&g_p2Input.heads[0];
    g_p2Input.context[0x14 / 4] = (uint32_t)(uintptr_t)&g_p2Input.heads[count];
    g_p2Input.context[0x18 / 4] = (uint32_t)(uintptr_t)&g_p2Input.heads[count];
    g_p2InputContext = (uintptr_t)g_p2Input.context;
    *(uintptr_t*)(steering + kSteeringInput) = g_p2InputContext;
    Log("P2 input: own context %08X (%d actions, was sharing %08X)", (unsigned)g_p2InputContext, count, (unsigned)shared);
    return true;
}

bool Possess(uintptr_t npc)
{
    uintptr_t manager = Manager();
    uintptr_t c1 = manager ? Character(*(uintptr_t*)(manager + kManagerPlayerSlot)) : 0;
    uintptr_t c2 = Character(g_player);
    if (!c1 || !c2 || !npc || *(uintptr_t*)npc != kCharacterVtable) { Log("P2 possess: bad characters"); return false; }

    __try {
        // 1. Player rules (and Rico's paths) via the game's own rebuild.
        CopyInfoStrings(c1, npc);
        *(int*)(npc + kCharacterModelCount) = 4;
        uintptr_t oldRules[10] = {}, oldSets[10] = {}, oldControl = 0;
        ((Rebuild_t)kRebuildCharacter)((void*)npc, (void*)(npc + kCharacterInfo), oldRules, oldSets, &oldControl);
        const uintptr_t* rules = (const uintptr_t*)(npc + kCharacterModels);
        Log("P2 possess: rebuilt %08X, rules %08X %08X %08X %08X", (unsigned)npc, (unsigned)rules[0], (unsigned)rules[2],
            (unsigned)rules[4], (unsigned)rules[6]);
        // The old rules/sets/control are left alive (leaked) in case something still uses them.

        // 2. Player 2's on-foot controller and brain. The character type must say "player" or
        // the game switches it back to a pedestrian controller; +0x728 marks it player-driven.
        *(int*)(npc + kCharacterType) = kCharacterTypePlayer;
        *(unsigned char*)(npc + kCharacterPlayerDriven) = 1;
        uintptr_t* ctrl = (uintptr_t*)(c2 + kCharacterController);
        if (ctrl[1]) InterlockedIncrement((volatile LONG*)(ctrl[1] + 4));
        ((uintptr_t*)(npc + kCharacterController))[0] = ctrl[0];
        ((uintptr_t*)(npc + kCharacterController))[1] = ctrl[1];
        g_ownController[0] = ctrl[0];
        g_ownController[1] = ctrl[1];
        *(uintptr_t*)(npc + kCharacterBrain) = g_player + kPlayerBrainIface;

        // Player-only parts the player controllers need, taken from Player 2's own (inactive)
        // character: CInventory at +0x484 (its +0xC8 points back at the owning character).
        uintptr_t inventory = *(uintptr_t*)(c2 + kCharacterInventory);
        if (inventory) {
            *(uintptr_t*)(npc + kCharacterInventory) = inventory;
            *(uintptr_t*)(inventory + kInventoryOwner) = npc;
        }

        // 3. Player 2's CPlayer now owns the NPC's character.
        uintptr_t cb = *(uintptr_t*)(npc + kCharacterSelfCount);
        if (cb) InterlockedIncrement((volatile LONG*)(cb + 4));
        *(uintptr_t*)(g_player + kPlayerCharacter) = npc;
        *(uintptr_t*)(g_player + kPlayerCharacter + 4) = cb;
        g_possessed = npc;
        g_inputBody = npc;
        const Breakpoint bp = { kOnFootInput, OnOnFootInput };
        if (!SetThreadBreakpoints(MainThreadId(), &bp, 1)) Log("P2 possess: input breakpoint FAILED");
        Log("P2 possess: Player 2 now controls %08X", (unsigned)npc);
        return true;
    } __except (LogException(GetExceptionInformation(), "P2 possess")) {
        return false;
    }
}

// The NPC's AI driver picks a new controller now and then (e.g. after being placed in the world);
// put Player 2's on-foot controller back when that happens.
void KeepPlayerController()
{
    if (!g_possessed || !g_ownController[0]) return;
    __try {
        uintptr_t* cur = (uintptr_t*)(g_possessed + kCharacterController);
        if (cur[0] == g_ownController[0]) return;
        // Only replace plain on-foot/pedestrian controllers; vehicles, parachute etc. are left alone.
        uintptr_t vt = cur[0] ? *(uintptr_t*)cur[0] : 0;
        if (vt != kPedestrianControllerVtable && vt != kAgentOnFootControllerVtable && vt != kCivilianSOControllerVtable)
            return;
        if (g_ownController[1]) InterlockedIncrement((volatile LONG*)(g_ownController[1] + 4));
        cur[0] = g_ownController[0];
        cur[1] = g_ownController[1];
        *(int*)(g_possessed + kCharacterType) = kCharacterTypePlayer;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// --- Driving Player 2's own character like the player ------------------------------------------
// The player update job (FUN_006174f0) calls CPlayer::Update (FUN_00672ce0) for the manager's
// player at 0x0061751A, which runs the player driver FUN_00853ae0 (controller update with the
// CPlayer's input context), aiming etc. It only runs when CPlayer+0x2FC is set. Jobs run on
// whichever worker thread is free. The dispatcher (FUN_004d3500) looks up a handler per job type
// in a table ([0x0118FEA8] entries of {type, handler*} from 0x0118FEAC); the handler calls the
// function pointer at handler+0x20 (__cdecl, one argument, result kept). Player 2's update hooks
// in by swapping that pointer, which is plain heap data. Hardware breakpoints on every thread made
// Steam's anti-tamper check exit the game (status 0x8000DEAD) when it ran on one of them.
const uintptr_t kPlayerUpdate = 0x00672CE0;
const uintptr_t kPlayerJob = 0x006174F0;
const uintptr_t kJobHandlerCount = 0x0118FEA8, kJobHandlerTable = 0x0118FEAC, kJobHandlerTarget = 0x20;
typedef uintptr_t(__cdecl* JobFn_t)(void* data);
// The full CPlayer::Update also runs aiming/HUD/camera parts that aren't set up for Player 2
// (crashes in camera targeting), so only its character driver is called:
// FUN_00853ae0 (__thiscall CPlayer*, float dt), with dt from FUN_004d3690(0) on [0x0118FD24]+4.
const uintptr_t kPlayerDriver = 0x00853AE0;
const uintptr_t kRunGrappleActions = 0x007D5320; // __fastcall(CCharacter*), runs CCharacter+0x628
typedef void(__fastcall* RunGrappleActions_t)(uintptr_t character);
const uintptr_t kFrameTime = 0x004D3690;
const uintptr_t kTimerPtr = 0x0118FD24;
typedef void(__thiscall* PlayerDriver_t)(void* player, float dt);
typedef float(__thiscall* FrameTime_t)(void* timer, int index);
const uintptr_t kPlayerActive = 0x2FC;
const uintptr_t kPlayerStats = 0x7A8;
const uintptr_t kStatsGameCompleted = 0x7C4;
const uintptr_t kCharacterFlagsOffset = 0x590;
const unsigned kCharacterFlagActive = 0x20;
const uintptr_t kCreateGear = 0x005BEED0;
typedef void(__thiscall* CreateGear_t)(void* player);
const uintptr_t kPlayerGearStrings[] = { 0x2A0, 0x2C0, 0x2DC };
const uintptr_t kPlayerParachutePath = 0x2A0, kPlayerGrapplePath = 0x2C0;
const uintptr_t kPlayerParachuteParams = 0x674;
// CCharacter+0x628: shared_ptr<CGrapplingActionSet>. The set holds 6 entries of {shared_ptr<
// CContextAction> (pointer, control block), action id} (fire hook, reel in, pull, deploy
// parachute while reeling, reeled-in jump/release), filled by FUN_00842080 from the grappling
// parameter file during create-gear, plus per-entry "active" bytes at +0x48.
const uintptr_t kCharacterGrappleActions = 0x628;
const int kGrappleActionCount = 6;

// The six grapple action classes: vtable, creator (CContextActionCreator<T> vtable slot 1,
// __thiscall(this unused, shared_ptr* out), ret 4) and object size (creator slot 2).
const struct {
    uintptr_t vtable, create;
} kGrappleActionTypes[] = {
    { 0x00EC3AD4, 0x005FF530 }, // CFireHookAction (0x5C bytes)
    { 0x00EC7834, 0x00676450 }, // CReelInAction (0x108)
    { 0x00EC301C, 0x0070F830 }, // CPullGrappledAction (0x40)
    { 0x00EC2FDC, 0x0051FF30 }, // CDeployParachuteWhileReelingAction (0x40)
    { 0x00EC305C, 0x00835810 }, // CReeledInJumpAction (0x44)
    { 0x00EC85CC, 0x00617460 }, // CReeledInReleaseAction (0x48)
};
typedef uintptr_t*(__thiscall* CreateAction_t)(void* unused, uintptr_t* out);
// CContextAction fields the parameter file sets (the rest are class defaults, equal on both
// players): +0x18, +0x24 input action (FIRE_GRAPPLE 0x51, ...), +0x28, +0x30 (1 = starts without
// input).
const uintptr_t kContextActionParams[] = { 0x18, 0x24, 0x28, 0x30 };

// Player 2's create-gear can't load the grappling parameter file mid-game, which leaves their
// action set empty (the hook can't fire, and the per-frame grapple update reads a null action).
// The actions keep per-use state (a fire hook's phase and timers), so sharing Player 1's let only
// one of them grapple at a time. Player 2 gets new ones of the same classes with Player 1's
// parameters.
void GiveGrappleActions(uintptr_t fromChar, uintptr_t toChar)
{
    uintptr_t from = *(uintptr_t*)(fromChar + kCharacterGrappleActions);
    uintptr_t to = *(uintptr_t*)(toChar + kCharacterGrappleActions);
    if (!from || !to) { Log("P2 grapple: missing action set (%08X, %08X)", (unsigned)from, (unsigned)to); return; }
    int made = 0;
    for (int i = 0; i < kGrappleActionCount; i++) {
        uintptr_t* src = (uintptr_t*)(from + i * 12);
        uintptr_t* dst = (uintptr_t*)(to + i * 12);
        if (dst[0] || !src[0]) continue;
        for (const auto& t : kGrappleActionTypes) {
            if (*(uintptr_t*)src[0] != t.vtable) continue;
            uintptr_t sp[2] = {};
            ((CreateAction_t)t.create)(nullptr, sp);
            if (!sp[0]) break;
            for (uintptr_t o : kContextActionParams) *(uint32_t*)(sp[0] + o) = *(const uint32_t*)(src[0] + o);
            dst[0] = sp[0]; // the set takes over our reference
            dst[1] = sp[1];
            dst[2] = src[2];
            made++;
            break;
        }
    }
    Log("P2 grapple: %d own grapple actions", made);
}

// The same missing file leaves Player 2's CGrapplingDevice (inventory +0xD0) with default
// parameters: range +0xD8 10 m instead of 120 m (the hook flew but never attached), other limits,
// effect ids (+0x218.., -1) and resources (+0x238.., shared_ptr pairs). Copied from Player 1's.
const uintptr_t kInventoryGrapplingDevice = 0xD0;
const uintptr_t kGrappleParamRanges[][2] = { { 0xD4, 0x114 }, { 0x218, 0x238 }, { 0x2B4, 0x2C0 } };
// shared_ptr pairs: effect resources +0x238..; rope/hook textures (shared_ptr<CTexture>) +0x148..
// and +0x1A4.. (missing on P2: his grapple was invisible while grappling and swinging it in melee).
const uintptr_t kGrappleSharedRanges[][2] = { { 0x238, 0x270 }, { 0x148, 0x168 }, { 0x1A4, 0x1BC } };

// The hook/rope the grapple shoots out is drawn from two model instances at device +0x140/+0x144,
// which the device's parameter load (FUN_00802EC0) makes from the file's model path with
// FUN_008701B0 (scene [0x011911F0], identity matrix) and hides (+0xC &= ~0x11) until a grapple.
// The file doesn't load for Player 2, so he grappled and swung in melee with nothing drawn.
// His instances are made from the model Player 1's use (instance +0xC4, shared_ptr<CModel>):
// FUN_00A4F4B0 (__thiscall(instance 0xCC bytes, shared_ptr*, bool), ret 8) as FUN_00A50A50 does,
// then FUN_008909B0 (__thiscall(scene, instance, matrix, 1, 1, 0), ret 0x14) as FUN_008701B0.
const uintptr_t kGrappleHookModels = 0x140; // 2 model instances
const uintptr_t kGameAlloc = 0x00444AE0;     // cdecl operator new
const uintptr_t kModelInstanceCtor = 0x00A4F4B0;
const uintptr_t kPlaceModelInstance = 0x008909B0;
const uintptr_t kModelScenePtr = 0x011911F0;
const uintptr_t kInstanceModel = 0xC4;
typedef void*(__cdecl* GameAlloc_t)(size_t size);
typedef void*(__thiscall* ModelInstanceCtor_t)(void* instance, uintptr_t* model, bool flag);
typedef void(__thiscall* PlaceModelInstance_t)(void* scene, void* instance, const float* matrix, int a, int b, int c);

int MakeGrappleHookModels(uintptr_t from, uintptr_t to)
{
    int made = 0;
    void* scene = *(void**)kModelScenePtr;
    if (!scene) return 0;
    static const float identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    for (int i = 0; i < 2; i++) {
        uintptr_t* dst = (uintptr_t*)(to + kGrappleHookModels) + i;
        uintptr_t source = ((uintptr_t*)(from + kGrappleHookModels))[i];
        if (*dst || !source || !*(uintptr_t*)(source + kInstanceModel)) continue;
        __try {
            void* instance = ((GameAlloc_t)kGameAlloc)(0xCC);
            if (!instance) continue;
            ((ModelInstanceCtor_t)kModelInstanceCtor)(instance, (uintptr_t*)(source + kInstanceModel), true);
            ((PlaceModelInstance_t)kPlaceModelInstance)(scene, instance, identity, 1, 1, 0);
            *(uint32_t*)((uintptr_t)instance + 0xC) &= ~0x11u;
            *dst = (uintptr_t)instance;
            made++;
        } __except (LogException(GetExceptionInformation(), "P2 grapple hook model")) {
        }
    }
    return made;
}

void ShareGrappleParams(uintptr_t fromChar, uintptr_t toChar)
{
    uintptr_t fromInv = *(uintptr_t*)(fromChar + kCharacterInventory), toInv = *(uintptr_t*)(toChar + kCharacterInventory);
    uintptr_t from = fromInv ? *(uintptr_t*)(fromInv + kInventoryGrapplingDevice) : 0;
    uintptr_t to = toInv ? *(uintptr_t*)(toInv + kInventoryGrapplingDevice) : 0;
    if (!from || !to) { Log("P2 grapple: missing grappling device (%08X, %08X)", (unsigned)from, (unsigned)to); return; }
    for (const auto& r : kGrappleParamRanges) memcpy((void*)(to + r[0]), (const void*)(from + r[0]), r[1] - r[0]);
    for (const auto& r : kGrappleSharedRanges) {
        for (uintptr_t o = r[0]; o < r[1]; o += 8) {
            uintptr_t* src = (uintptr_t*)(from + o);
            uintptr_t* dst = (uintptr_t*)(to + o);
            if (dst[0] || !src[0]) continue;
            if (src[1]) InterlockedIncrement((volatile LONG*)(src[1] + 4));
            dst[0] = src[0];
            dst[1] = src[1];
        }
    }
    int hooks = MakeGrappleHookModels(from, to);
    Log("P2 grapple: device parameters copied (range %.0f m), %d hook models made", *(float*)(to + 0xD8), hooks);
}
const uintptr_t kPlayerParachuteKey = 0x2BC;
const uintptr_t kCharacterHealth = 0xE8; // float health, then float max health
const uintptr_t kCharacterDamageTable = 0x78; // 9 damage types x 12 bytes
const size_t kCharacterDamageTableSize = 9 * 12;
// CPlayer health and regeneration parameters, as [begin, end) ranges (pointers at +0x21C and +0x290 left out).
const uintptr_t kPlayerHealthParams[][2] = { { 0x1B4, 0x21C }, { 0x224, 0x290 } };
typedef void(__thiscall* PlayerUpdate_t)(void* player);
volatile bool g_driveRequested;

// Weapons: CInventory +0x3C holds 5 shared_ptr<CWeapon> slots. The save loader (FUN_00710f60)
// creates each one with FUN_0072fb20 (__thiscall weapon manager [0x011917F4], shared_ptr* out,
// int id), ret 8, and stores it with FUN_007d60c0 (__thiscall inventory, shared_ptr*, int slot),
// ret 8. A CWeapon's descriptor is at +0x224, whose first dword is the weapon id. Player 2 gets
// the same weapons as Player 1; game code reads slot 3 (always filled on Rico) unchecked.
const uintptr_t kWeaponManagerPtr = 0x011917F4;
const uintptr_t kCreateWeapon = 0x0072FB20;
const uintptr_t kSetWeaponSlot = 0x007D60C0;
typedef uintptr_t*(__thiscall* CreateWeapon_t)(void* manager, uintptr_t* out, int id);
typedef void(__thiscall* SetWeaponSlot_t)(void* inventory, uintptr_t* weapon, int slot);
const uintptr_t kInventoryWeapons = 0x3C;
const int kWeaponSlots = 5;
const uintptr_t kWeaponDesc = 0x224;
// Only the reserve ammo (+0x78, 0x14 ints) is copied. Player 1's hand state (+0x64..+0x74, the
// equipped slot indices +0xD4..+0xE0) describes a pose Player 2 never entered; copying it left
// Player 2's holstered weapons lying at their feet until they equipped something.
const uintptr_t kInventoryCopyRanges[][2] = { { 0x78, 0xC8 } };

void GiveWeapons(uintptr_t fromInv, uintptr_t toInv)
{
    uintptr_t manager = *(uintptr_t*)kWeaponManagerPtr;
    if (!manager || !fromInv || !toInv) { Log("P2 weapons: missing manager or inventory"); return; }
    for (int slot = 0; slot < kWeaponSlots; slot++) {
        uintptr_t w = *(uintptr_t*)(fromInv + kInventoryWeapons + slot * 8);
        if (!w || *(uintptr_t*)(toInv + kInventoryWeapons + slot * 8)) continue;
        uintptr_t desc = *(uintptr_t*)(w + kWeaponDesc);
        int id = desc ? *(int*)desc : -1;
        uintptr_t sp[2] = {};
        ((CreateWeapon_t)kCreateWeapon)((void*)manager, sp, id);
        if (!sp[0]) { Log("P2 weapons: slot %d id %d not created", slot, id); continue; }
        ((SetWeaponSlot_t)kSetWeaponSlot)((void*)toInv, sp, slot);
        // Our sp[] reference is left alive (leaked) rather than released by hand.
        Log("P2 weapons: slot %d id %d -> %08X", slot, id, (unsigned)sp[0]);
    }
    for (const auto& r : kInventoryCopyRanges) memcpy((void*)(toInv + r[0]), (const void*)(fromInv + r[0]), r[1] - r[0]);
}

// Ammo refill, done like the save loader restores ammo:
// - clips: FUN_00604bd0 (__thiscall CWeapon*, int rounds), ret 4, loads rounds into the clip;
//   clip size = FUN_0059adf0 (__thiscall CWeapon*, 0), ret 4; rounds in the clip = vfunc 0x20 of
//   the weapon's interface at +0xA0.
// - reserve: FUN_0064f550 (__thiscall CInventory*, int ammoType, int amount), ret 8, sets the
//   reserve of an equipped weapon's ammo type, capped at clip size * that type's magazine limit.
const uintptr_t kLoadClip = 0x00604BD0;
const uintptr_t kClipSize = 0x0059ADF0;
const uintptr_t kSetReserveAmmo = 0x0064F550;
const uintptr_t kWeaponClipIface = 0xA0;
const int kAmmoTypes = 0x14;
typedef int(__thiscall* LoadClip_t)(void* weapon, int rounds);
typedef int(__thiscall* ClipSize_t)(void* weapon, int unused);
typedef int(__thiscall* ClipRounds_t)(void* iface);
typedef void(__thiscall* SetReserveAmmo_t)(void* inventory, int type, int amount);
volatile bool g_ammoRequested;
volatile LONG g_moneyRequested; // testing (F9): dollars to add on the next game tick
// The money (Lua GetMoney, FUN_00A335C0): the manager's player's CBlackmarketHook (+0x3BC) value 3
// (FUN_0062E470: +8 + 4 * index), an int.
const uintptr_t kPlayerMoney = 0x3BC + 8 + 3 * 4;
volatile bool g_gearRequested; // testing: run create-gear again
volatile bool g_killRequested; // testing: Player 2 dies
volatile bool g_killRequestedRaw; // ... without CharacterDeathHook
volatile float g_liftPlayer1;     // testing: place Player 1 this many meters higher
volatile bool g_movePlayer1ToPlayer2; // testing: ... or next to Player 2
volatile bool g_movePlayer1To;        // testing ("p1 at"): ... or to g_player1Target
float g_player1Target[3];
volatile bool g_fireCheckRequested; // testing: log the fire conditions of both characters

// Testing: the conditions FUN_006dfa90 (main-thread fire request handling) checks, per character.
void LogFireConditions(const char* who, uintptr_t c)
{
    typedef uintptr_t(__thiscall* Ptr_t)(void* c);
    typedef bool(__thiscall* Bool_t)(void* c);
    typedef bool(__thiscall* BoolArg_t)(void* c, int arg);
    __try {
        uintptr_t weapon = ((Ptr_t)0x00631BE0)((void*)c);
        bool aimReady = ((BoolArg_t)0x0067D1E0)((void*)c, 0);
        bool busy = ((Bool_t)0x007E3F10)((void*)c);
        bool ready = ((Bool_t)0x0079EEF0)((void*)c);
        uintptr_t state = *(uintptr_t*)(c + 0xA34);
        // Current animation state ids (FUN_008df200 on a rule set) of the upper body (+0x494) and
        // the base (+0x48C); the aim-ready states are [0x01196FDC], [0x01196B28] (upper), [0x01196BA4] (base).
        int upper = *(int*)((Ptr_t)0x008DF200)(*(void**)(c + 0x494));
        int base = *(int*)((Ptr_t)0x008DF200)(*(void**)(c + 0x48C));
        // FUN_0075EAD0's other conditions: the character interface's +0x28 check, whether the weapon
        // in hand is automatic (weapon desc +0x2DC) and its mode (FUN_00653D90, 2 = needs drawing).
        void* iface = (void*)(c + 0xF0);
        bool iface28 = ((Bool_t)(*(void***)iface)[0x28 / 4])(iface);
        uintptr_t inHand = 0;
        uintptr_t inv = *(uintptr_t*)(c + kCharacterInventory);
        int slot = inv ? ((int(__thiscall*)(void*))0x007B6F20)((void*)inv) : 5;
        if (inv && slot < 5) inHand = *(uintptr_t*)(inv + kInventoryWeapons + slot * 8);
        int automatic = inHand ? *(int*)(*(uintptr_t*)(inHand + kWeaponDesc) + 0x2DC) : -1;
        int wmode = inHand ? ((int(__thiscall*)(void*))0x00653D90)((void*)inHand) : -1;
        Log("fire check %s: right weapon %08X, aim ready %d, busy %d, ready %d, fire flag %d | upper %X base %X | iface28 %d, "
            "in hand slot %d %08X auto %d mode %d | state +258.. %02X %02X min %.2f max %.2f", who, (unsigned)weapon, aimReady, busy,
            ready, *(unsigned char*)(state + 0x258), upper, base, iface28, slot, (unsigned)inHand, automatic, wmode,
            *(unsigned char*)(state + 0x258), *(unsigned char*)(state + 0x259), *(float*)(state + 0x25C), *(float*)(state + 0x260));
    } __except (LogException(GetExceptionInformation(), "fire check")) {
    }
}

void RefillAmmo(const char* who, uintptr_t character)
{
    uintptr_t inv = character ? *(uintptr_t*)(character + kCharacterInventory) : 0;
    if (!inv) return;
    __try {
        for (int slot = 0; slot < kWeaponSlots; slot++) {
            uintptr_t w = *(uintptr_t*)(inv + kInventoryWeapons + slot * 8);
            if (!w) continue;
            void* iface = (void*)(w + kWeaponClipIface);
            int missing = ((ClipSize_t)kClipSize)((void*)w, 0) - ((ClipRounds_t)(*(void***)iface)[0x20 / 4])(iface);
            if (missing > 0) ((LoadClip_t)kLoadClip)((void*)w, missing);
        }
        // Some types have a cap of 0 and hold a negative special value (Rico's slot 3 item: -124);
        // leave those alone and never lower a count.
        for (int type = 1; type < kAmmoTypes; type++) {
            int* reserve = (int*)(inv + 0x78 + type * 4);
            int before = *reserve;
            if (before < 0) continue;
            ((SetReserveAmmo_t)kSetReserveAmmo)((void*)inv, type, 0x7FFFFFFF);
            if (*reserve < before) *reserve = before;
        }
        // Grenades and triggered explosives (C4) aren't ammo types: counts at inventory +0xD4 / +0xD8
        // (item 0 / 1; GetAmmoInfo's hands 3 / 4, FUN_00556250). Their maximum depends on the black
        // market upgrade: FUN_0064E790(weapon manager [0x011917F4], item, 1), thiscall, ret 8, which
        // FUN_005068A0 compares the count with.
        uintptr_t weapons = *(uintptr_t*)0x011917F4;
        for (int item = 0; weapons && item < 2; item++) {
            int max = ((int(__fastcall*)(uintptr_t, void*, int, char))0x0064E790)(weapons, nullptr, item, 1);
            int* count = (int*)(inv + 0xD4 + item * 4);
            if (max > 0 && max < 1000 && *count >= 0 && *count < max) *count = max;
        }
        Log("Ammo refilled for %s (grenades %d, C4 %d)", who, *(int*)(inv + 0xD4), *(int*)(inv + 0xD8));
    } __except (LogException(GetExceptionInformation(), "ammo refill")) {
    }
}
bool g_driving;

int g_updateFailures;
void EnableCharacter(uintptr_t c);

int UpdateFilter(EXCEPTION_POINTERS* info)
{
    if (g_updateFailures++ < 2) LogException(info, "P2 drive: CPlayer::Update");
    return EXCEPTION_EXECUTE_HANDLER;
}

// The game's UI mode: [0x011958C4] current (3 = gameplay, 4 = pause menu, 5 = map/PDA), and
// [0x0105F754] a requested switch (6 = none), done by FUN_00694ec0 on the main thread. Player 2
// only updates in plain gameplay, so they stay put while the game is paused or in the map.
const uintptr_t kUiMode = 0x011958C4, kUiModeRequest = 0x0105F754;
const int kUiModeGameplay = 3, kUiModeNoRequest = 6;

bool InGameplay()
{
    return *(volatile int*)kUiMode == kUiModeGameplay && *(volatile int*)kUiModeRequest == kUiModeNoRequest;
}

// Player 2 gets only the character driver of CPlayer::Update by default. The whole update (gear,
// HUD events, the grapple actions FUN_007d5320, which Player 2 shares with Player 1) also touches
// player-wide state, and with it Player 1's camera misbehaved while grappling. "p2 full on" turns
// it on for testing; it needs the grapple action set filled (ShareGrappleActions).
volatile bool g_fullUpdate = false;

// --- Player 2's aim ----------------------------------------------------------------------------
// CPlayer+0x184 is the CPlayerAimControl. Each player's brain (CPlayer+8, vtable 0x00E4CD64) has a
// per-frame think in slot 1 (FUN_005df870, __thiscall, main thread, from the character update):
// if the byte [0x0106EC81] is set it first has the camera system ([0x011900F8] vfunc +0x18)
// re-apply the camera controller, which follows Player 1, to the main camera; then it aims the
// player's aim control with a ray from that camera (FUN_007047a0: camera [0x011919F4]+0x10,
// position +0x54, back vector +0x44, result at aim +0x44), copies the aim point into the
// character's controller state (+0xA34 -> +0x44), where the arm pose and the shots read it, and
// runs the aim follow-up FUN_0075fa20. For Player 2's think the camera refresh is skipped and the
// main camera holds Player 2's view, so all of it uses Player 2's aim (aiming from Player 1's view
// and correcting afterwards made the aim flip every frame, restarting the aim animation so the
// gun never got ready to fire). The vtable is data, like the camera hook's.
const uintptr_t kBrainThinkSlot = 0x00E4CD68;
const uintptr_t kThinkCameraRefresh = 0x0106EC81;
typedef void(__fastcall* BrainMethod_t)(void* brain, void* edx);
BrainMethod_t g_origBrainThink;

// The aim raycasts use CPlayerAimRaycastFilter (vtable 0x00EB4D74); its slot 2 (FUN_0050b410,
// __thiscall + the hit, ret 4) skips hits on the manager's player's character (Player 1) only.
// Player 2's own rays (aim, and the line-of-fire check from the gun hand, brain slot 30
// FUN_00590140, which sets brain+0x6D "blocked" and stops all shooting) then hit Player 2's own
// body, and Player 1's aim stopped on Player 2 (his crosshair locked onto Player 2, and Player 2's
// grapple then hit Player 2 himself). The filter skips Player 2's body for every ray: Player 1
// can't target or grapple Player 2 (his bullets don't hurt Player 2 anyway).
const uintptr_t kAimFilterSlot = 0x00EB4D7C;
const uintptr_t kHitEntity = 0xC;
typedef uint32_t(__fastcall* AimFilter_t)(void* filter, void* edx, void* hit);
AimFilter_t g_origAimFilter;

uint32_t __fastcall AimFilterHook(void* filter, void* edx, void* hit)
{
    uintptr_t c2 = g_player ? Character(g_player) : 0;
    if (c2 && *(uintptr_t*)((uintptr_t)hit + kHitEntity) == c2) return 0;
    return g_origAimFilter(filter, edx, hit);
}

bool IsPlayer2Brain(void* brain)
{
    return g_player && brain == (void*)(g_player + kPlayerBrainIface);
}

// Player 1's lock-on (aim mode 2, FUN_007A0B90 in the think) walks the world's object entries
// ([0x011900F0]) and snaps the aim to one near the crosshair, whatever the aim ray hit;
// FUN_00619860 skips entries whose type (entry +8, low 7 bits) is 3 and the player's own
// character. Player 2's entry (his world handle, character +0x600) has type 3 while Player 1
// thinks.
const uintptr_t kCharacterWorldEntry = 0x600;
const uintptr_t kWorldEntryType = 8;
const unsigned char kWorldEntryTypeSkipped = 3;

// Player 2's camera sits 10 m behind him, and the aim ray starts at the camera: when the camera
// was in a slope or a wall (ground under it not readable for its collision), the ray hit that at
// once and the grapple pulled him back toward his camera. His think gets the camera moved along
// its line of sight to just in front of his head: same aim, nothing between camera and him counts.
const float kAimStartBeforeHead = 1.0f;
const float kAimHeadHeight = 1.6f;

void StartAimNearPlayer2(CameraMatrix* view)
{
    uintptr_t c = Character(g_player);
    if (!c) return;
    const float* body = (const float*)(c + kCharacterPosition);
    float look[3] = { -view->forward[0], -view->forward[1], -view->forward[2] };
    float length = sqrtf(look[0] * look[0] + look[1] * look[1] + look[2] * look[2]);
    if (length < 0.5f) return;
    float toHead[3] = { body[0] - view->position[0], body[1] + kAimHeadHeight - view->position[1],
                        body[2] - view->position[2] };
    float t = (toHead[0] * look[0] + toHead[1] * look[1] + toHead[2] * look[2]) / length - kAimStartBeforeHead;
    if (t <= 0.0f || t > 30.0f) return;
    for (int i = 0; i < 3; i++) view->position[i] += look[i] / length * t;
}

// The think sets "can grapple the aim point" (FUN_0075FA20) in one global byte for whichever
// player thought last; each player's is kept for his half's crosshair (HudLuaHook).
volatile uint8_t* const kCanGrappleTarget = (volatile uint8_t*)0x01197A78;
volatile uint8_t g_canGrapple[2];

void BrainThink(void* brain, void* edx, bool p2, bool player1);

void __fastcall BrainThinkHook(void* brain, void* edx)
{
    bool p2 = IsPlayer2Brain(brain);
    uintptr_t manager = !p2 && g_driving ? Manager() : 0;
    bool player1 = manager && brain == (void*)(*(uintptr_t*)(manager + kManagerPlayerSlot) + kPlayerBrainIface);
    BrainThink(brain, edx, p2, player1);
    // That check (FUN_0075FA20(player), the think's last call) reads the manager's player's aim
    // control (FUN_00764C90), i.e. Player 1's in Player 2's think too: run again with him there.
    uintptr_t m = p2 ? Manager() : 0;
    if (m && g_player) {
        uintptr_t* slot = (uintptr_t*)(m + kManagerPlayerSlot);
        uintptr_t p1 = *slot;
        uint8_t before = *kCanGrappleTarget;
        *slot = g_player;
        __try {
            ((void(__fastcall*)(uintptr_t))0x0075FA20)(g_player);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        *slot = p1;
        g_canGrapple[1] = *kCanGrappleTarget;
        *kCanGrappleTarget = before;
        return;
    }
    if (p2 || player1) g_canGrapple[p2] = *kCanGrappleTarget;
}

void BrainThink(void* brain, void* edx, bool p2, bool player1)
{
    CameraMatrix view, saved;
    // The physics world answers ray casts here (the aim's), not from the camera update.
    if (p2 || player1) SplitRenderCastCameraRays();
    // Each player's brain (aiming, grapple targets) thinks with his own camera: Player 2's view
    // for his, and for Player 1's his own world even on Player 2's frames, when the main camera
    // holds Player 2's world through the update (otherwise his grapple can target a point next to
    // Player 2, kilometres away, and fling him).
    bool p2View = p2 && SplitViewMatrix(1, &view);
    if (p2View) StartAimNearPlayer2(&view);
    bool swapped = p2 ? p2View && SwapMainCameraWorld(view, &saved)
                      : player1 && SplitRenderPlayer1World(&view) && SwapMainCameraWorld(view, &saved);
    if (player1) {
        uintptr_t c2 = Character(g_player);
        uintptr_t entry = c2 ? *(uintptr_t*)(c2 + kCharacterWorldEntry) : 0;
        if (entry) {
            volatile unsigned char* type = (volatile unsigned char*)(entry + kWorldEntryType);
            unsigned char before = *type;
            *type = (before & 0x80) | kWorldEntryTypeSkipped;
            g_origBrainThink(brain, edx);
            *type = before;
            if (swapped) RestoreMainCameraWorld(saved, view);
            return;
        }
    }
    unsigned char refresh = *(volatile unsigned char*)kThinkCameraRefresh;
    if (swapped && p2) *(volatile unsigned char*)kThinkCameraRefresh = 0;
    g_origBrainThink(brain, edx);
    if (swapped) {
        if (p2) *(volatile unsigned char*)kThinkCameraRefresh = refresh;
        RestoreMainCameraWorld(saved, view);
    }
}

// Weapons fire from their per-frame update (CWeapon+4 interface, vtable 0x00E3830C slot 7,
// FUN_0083f860, __thiscall(iface, float dt), ret 4; owner character at iface+0x90). When the owner
// is player-driven (character +0x728, FUN_006175a0) a shot shakes the camera (FUN_005e7670 ->
// FUN_0070d460) and rumbles the pad, both global, i.e. Player 1's. While a weapon of Player 2's
// updates, Player 2 counts as not player-driven.
const uintptr_t kWeaponIface = 4;
const uintptr_t kWeaponIfaceOwner = 0x90;
const uintptr_t kInventoryLauncher = 0x54; // inventory weapon slot 3 (shared_ptr): grapple launcher
const uintptr_t kWeaponDescShake = 0x2F4;   // float camera shake, then float pad rumble
typedef uint32_t(__fastcall* WeaponUpdate_t)(void* iface, void* edx, float dt);
WeaponUpdate_t g_origWeaponUpdate;

// The game checks this vtable's contents too (a hooked slot 7 made setting a map waypoint quit the
// game, 2026-10-29), so it isn't hooked: Player 2's weapons get a private copy (type info at [-1]
// and the 47 slots, slot 7 = WeaponUpdateHook), set on each weapon's interface (CWeapon +4) every
// tick (GivePlayer2WeaponVtable). Only the constructors store the original pointer; nothing
// compares against it.
const uintptr_t kWeaponIfaceVtable = 0x00E3830C;
const int kWeaponIfaceSlots = 47;
const int kWeaponUpdateIndex = 7;
uintptr_t g_p2WeaponVtable[1 + kWeaponIfaceSlots];

uint32_t __fastcall WeaponUpdateHook(void* iface, void* edx, float dt);

void GivePlayer2WeaponVtable()
{
    if (!g_p2WeaponVtable[1]) {
        if (DebugSkip("SkipP2Weapon")) return;
        memcpy(g_p2WeaponVtable, (const void*)(kWeaponIfaceVtable - 4), sizeof(g_p2WeaponVtable));
        g_origWeaponUpdate = (WeaponUpdate_t)g_p2WeaponVtable[1 + kWeaponUpdateIndex];
        g_p2WeaponVtable[1 + kWeaponUpdateIndex] = (uintptr_t)WeaponUpdateHook;
    }
    uintptr_t c2 = g_driving ? Character(g_player) : 0;
    uintptr_t inv = c2 ? *(uintptr_t*)(c2 + kCharacterInventory) : 0;
    if (!inv) return;
    __try {
        for (int i = 0; i < 5; i++) {
            uintptr_t weapon = *(uintptr_t*)(inv + kInventoryWeapons + 8 * i);
            uintptr_t* vptr = weapon ? (uintptr_t*)(weapon + kWeaponIface) : nullptr;
            if (vptr && *vptr == kWeaponIfaceVtable) *vptr = (uintptr_t)&g_p2WeaponVtable[1];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

uint32_t __fastcall WeaponUpdateHook(void* iface, void* edx, float dt)
{
    uintptr_t c2 = g_driving ? Character(g_player) : 0;
    bool mine = c2 && *(uintptr_t*)((uintptr_t)iface + kWeaponIfaceOwner) == c2;
    // Except the grapple launcher: it only fires for a player-driven owner (its clip is empty and
    // the unlimited rounds, FUN_006fcac0, are for player-driven owners; a loaded clip didn't help).
    // Its shot's shake and rumble strengths (weapon description +0x2F4, +0x2F8, FUN_005e7670) are
    // zeroed instead while it updates. The description is shared with Player 1's launcher, which
    // updates on the same thread.
    uintptr_t inv = mine ? *(uintptr_t*)(c2 + kCharacterInventory) : 0;
    if (inv && *(uintptr_t*)(inv + kInventoryLauncher) + kWeaponIface == (uintptr_t)iface) {
        float* shake = (float*)(*(uintptr_t*)((uintptr_t)iface - kWeaponIface + kWeaponDesc) + kWeaponDescShake);
        float saved[2] = { shake[0], shake[1] };
        shake[0] = shake[1] = 0.0f;
        uint32_t r = g_origWeaponUpdate(iface, edx, dt);
        shake[0] = saved[0];
        shake[1] = saved[1];
        return r;
    }
    unsigned char driven = mine ? *(volatile unsigned char*)(c2 + kCharacterPlayerDriven) : 0;
    if (mine) *(volatile unsigned char*)(c2 + kCharacterPlayerDriven) = 0;
    uint32_t r = g_origWeaponUpdate(iface, edx, dt);
    if (mine) *(volatile unsigned char*)(c2 + kCharacterPlayerDriven) = driven;
    return r;
}

// Rico's grapple launcher is the weapon in inventory slot 3. When the game gives Rico his loadout
// (FUN_00540C40) it sets the launcher up after storing it: FUN_0071FF60 (__thiscall weapon, int
// mode), ret 4, with mode 0x18 (kept at weapon +0x254) activates it (weapon interface SetActive,
// +0x3E bit 0; inactive weapons ignore trigger pulls) and attaches it to the owner's left hand,
// then vtable slot 24 (__thiscall weapon, bool), ret 4, with 0 (hides it until a grapple). Player
// 2's launcher is a plain new weapon: his hook spawned at his feet and hit the ground at once.
const uintptr_t kWeaponAttachMode = 0x254;
const int kLauncherAttachMode = 0x18;
const uintptr_t kSetupHandWeapon = 0x0071FF60;
const int kWeaponShowSlot = 24;
typedef void(__thiscall* SetupHandWeapon_t)(void* weapon, int mode);
typedef void(__thiscall* WeaponShow_t)(void* weapon, bool show);

void SetupPlayer2Launcher()
{
    uintptr_t c2 = Character(g_player);
    uintptr_t inv = c2 ? *(uintptr_t*)(c2 + kCharacterInventory) : 0;
    uintptr_t w = inv ? *(uintptr_t*)(inv + kInventoryLauncher) : 0;
    if (!w || *(int*)(w + kWeaponAttachMode) == kLauncherAttachMode) return;
    __try {
        ((SetupHandWeapon_t)kSetupHandWeapon)((void*)w, kLauncherAttachMode);
        ((WeaponShow_t)(*(void***)w)[kWeaponShowSlot])((void*)w, false);
        Log("P2 grapple launcher %08X set up", (unsigned)w);
    } __except (LogException(GetExceptionInformation(), "P2 launcher")) {
    }
}

// A character's death (CCharacter vtable slot 24, FUN_007dd0a0, __thiscall(character, killer),
// ret 4) posts "ply.killed" when the character is player-driven; the mission manager answers with
// the death screen and reloads the last save. Player 2 dies as a plain character instead, and
// RespawnTick brings him back.
const uintptr_t kCharacterDeathSlot = 0x00EB887C;
// The take-hit slot (6) of the same vtable, hooked in place for explosions (TakeHitInPlaceHook).
const uintptr_t kTakeHitSlot = 0x00EB8834;
typedef uint32_t(__fastcall* TakeHit_t)(uintptr_t character, void* edx, int* damage, int hit, void* out);
extern TakeHit_t g_origTakeHitInPlace;
uint32_t __fastcall TakeHitInPlaceHook(uintptr_t character, void* edx, int* damage, int hit, void* out);
typedef void(__fastcall* CharacterDeath_t)(void* character, void* edx, void* killer);
CharacterDeath_t g_origCharacterDeath;

// CPlayer's death handler (vtable 0x00E4CD64 slot 18 at 0x00E4CDAC, FUN_006B0790,
// __thiscall(player +8 interface, dword), ret 4) shows the death tips, starts the death sequence
// and has the game respawn the whole world (FUN_00660E50(1) -> FUN_008BF240 -> CGameWorld slot
// 0xC8, every object with a respawn component reset, as for a checkpoint reload). Player 2's
// CPlayer has the same vtable: his death (e.g. jumping out of a frozen helicopter far from Player
// 1) reset the world under both players and the game crashed right after in the spawn manager
// (user, 2026-10-29). It is skipped for Player 2 (RespawnTick brings him back): his CPlayer's +8
// gets a private copy of the vtable (55 slots; the game checks some vtables: a hooked slot of the
// shared one quit the game on the Steam Deck) whose slot 18 does nothing.
const uintptr_t kPlayerBrainVtable = 0x00E4CD64;
const int kPlayerBrainSlots = 55;
const int kPlayerDiedIndex = 18;
uintptr_t g_player2BrainVtable[1 + kPlayerBrainSlots];

void __fastcall PlayerDiedHook(void* player, void* edx, uint32_t arg)
{
    Log("P2's player death handler skipped (no world respawn)");
}

// Game tick: Player 2's CPlayer on the private vtable (copied from the shared one as it is, so the
// brain think hook in slot 1 stays).
void KeepPlayer2BrainVtable()
{
    if (!g_player) return;
    uintptr_t* vptr = (uintptr_t*)(g_player + kPlayerBrainIface);
    if (*vptr == (uintptr_t)&g_player2BrainVtable[1]) return;
    if (*vptr != kPlayerBrainVtable) return;
    memcpy(g_player2BrainVtable, (const void*)(kPlayerBrainVtable - 4), sizeof(g_player2BrainVtable));
    g_player2BrainVtable[1 + kPlayerDiedIndex] = (uintptr_t)PlayerDiedHook;
    *vptr = (uintptr_t)&g_player2BrainVtable[1];
    Log("P2's player on a private vtable (death handler skipped)");
}

void __fastcall CharacterDeathHook(void* character, void* edx, void* killer)
{
    uintptr_t c2 = g_player ? Character(g_player) : 0;
    bool mine = c2 && (uintptr_t)character == c2;
    unsigned char driven = mine ? *(volatile unsigned char*)(c2 + kCharacterPlayerDriven) : 0;
    if (mine) {
        *(volatile unsigned char*)(c2 + kCharacterPlayerDriven) = 0;
        Log("P2 died (no death screen)");
    }
    g_origCharacterDeath(character, edx, killer);
    if (mine) *(volatile unsigned char*)(c2 + kCharacterPlayerDriven) = driven;
}

// The player driver (FUN_00853AE0) ends by asking the camera control manager ([0x011900F8],
// vtable 0x00EB8774 slot 3 FUN_00600850, __thiscall(manager, int setup, bool transition), ret 8)
// for the camera setup that fits its character (on foot, reeling = 0x1D, parachute, ...). There
// is one game camera, Player 1's, so Player 2's update switched it to the reel camera while
// Player 2 reeled in (Player 1's view turned toward Player 2's hook). Requests made during Player
// 2's update are ignored.
const uintptr_t kSetCameraSetupSlot = 0x00EB8780;
typedef void(__fastcall* SetCameraSetup_t)(void* manager, void* edx, int setup, bool transition);
SetCameraSetup_t g_origSetCameraSetup;
__declspec(thread) bool t_inPlayer2Update;
// Testing ("p1jobcam on"): Player 1's whole update job runs with his world in the main camera
// (the old way; objects near Player 2 flickered) instead of only his camera setup switches.
volatile bool g_player1JobCamera = false;

// The new setup's camera starts from the main camera's world, which on Player 2's frames is his
// ("split p2world"): Player 1's grapple/reel camera was flung toward Player 2. So a switch made
// by Player 1 gets his own world in the main camera for the call only (a swap for his whole update
// job hid objects near Player 2: jobs running alongside measured from Player 1's camera).
const uintptr_t kCameraManagerSetup = 0x18;  // current setup id

void __fastcall SetCameraSetupHook(void* manager, void* edx, int setup, bool transition)
{
    // Testing: who switches the camera while Player 2 mans a mounted gun (user: Player 1's view went
    // to the flak cannon Player 2 used).
    if (setup != *(int*)((uintptr_t)manager + kCameraManagerSetup) && g_driving && Player2OnMountedGun()) {
        static int logs;
        if (logs++ < 40)
            Log("Camera setup %d -> %d (transition %d)%s from %08X", *(int*)((uintptr_t)manager + kCameraManagerSetup), setup,
                transition, t_inPlayer2Update ? " in P2's update (ignored)" : "", (unsigned)(uintptr_t)_ReturnAddress());
    }
    if (t_inPlayer2Update) return;
    CameraMatrix world1, before;
    bool swapped = setup != *(int*)((uintptr_t)manager + kCameraManagerSetup) && !g_player1JobCamera &&
                   SplitRenderPlayer1World(&world1) && SwapMainCameraWorld(world1, &before);
    g_origSetCameraSetup(manager, edx, setup, transition);
    if (swapped) RestoreMainCameraWorld(before, world1);
}

// Context actions (grapple actions among them) ask "was my button just pressed" through vtable
// slot 5 = FUN_008B7B80: unless +0x30 is set, the game's input lookup FUN_00984140 for action id
// +0x24, state (byte +3) & 7 == 2. That lookup is the game's own input, Player 1's, so during
// Player 2's update his presses never counted there: no parachute out of a reel-in
// (CDeployParachuteWhileReelingAction). In his update it reads his own input instead.
const uintptr_t kContextActionPressedSlots[] = {
    0x00EC2DB0, 0x00EC2E40, 0x00EC2EB8, 0x00EC2F88, 0x00EC2FF0, 0x00EC3030, 0x00EC38F0, 0x00EC3958,
    0x00EC39C0, 0x00EC3AA8, 0x00EC3AE8, 0x00EC4748, 0x00EC48F0, 0x00EC7848, 0x00EC78F0, 0x00EC7958,
    0x00EC7A28, 0x00EC7B60, 0x00EC7C98, 0x00EC7D00, 0x00EC7E98, 0x00EC80A0, 0x00EC8108, 0x00EC8180,
    0x00EC81E8, 0x00EC8250, 0x00EC83B8, 0x00EC8430, 0x00EC84A8, 0x00EC8578, 0x00F0D320,
};
// __thiscall(action, a, input context), ret 8: the lookup runs on the second argument.
const uintptr_t kContextActionPressedFn = 0x008B7B80;
typedef bool(__fastcall* ActionPressed_t)(uintptr_t action, void* edx, uint32_t a, uintptr_t input);

bool __fastcall ContextActionPressedHook(uintptr_t action, void* edx, uint32_t a, uintptr_t input)
{
    if (t_inPlayer2Update && g_p2InputContext && !*(uint8_t*)(action + 0x30)) {
        int id = *(int*)(action + 0x24);
        if (id >= 0 && id < kMaxInputActions) return ((g_p2Input.entries[id][0] >> 24) & 7) == 2;
    }
    return ((ActionPressed_t)kContextActionPressedFn)(action, edx, a, input);
}

void HookContextActionInput()
{
    int hooked = 0;
    for (uintptr_t slot : kContextActionPressedSlots) {
        if (*(uintptr_t*)slot != kContextActionPressedFn) continue; // only slots that use it
        DWORD old;
        if (!VirtualProtect((void*)slot, 4, PAGE_READWRITE, &old)) continue;
        *(void**)slot = (void*)ContextActionPressedHook;
        VirtualProtect((void*)slot, 4, old, &old);
        hooked++;
    }
    Log("P2 input: context action press check hooked in %d vtables", hooked);
}

// CFireHookAction's start (slot 6, FUN_0079DCB0) and update (slot 7, FUN_0061D6B0) attach the
// hook with FUN_0076F1D0, which takes the target (hit object +0x3C, point +0x44) from the aim
// control of the manager's player, Player 1's: Player 2's hook went where Player 1 was aiming
// whenever Player 1's crosshair had a target. While the action runs for Player 2's character
// (the shared_ptr argument points at its +0xF0 interface), Player 1's CPlayer+0x184 holds
// Player 2's aim control.
const uintptr_t kFireHookStartSlot = 0x00EC3AEC, kFireHookUpdateSlot = 0x00EC3AF0;
const uintptr_t kPlayerAimControl = 0x184;
const uintptr_t kCharacterActionIface = 0xF0;
typedef void(__fastcall* FireHookStart_t)(void* action, void* edx, uintptr_t* character);
typedef uint32_t(__fastcall* FireHookUpdate_t)(void* action, void* edx, uintptr_t* character, uint32_t a, float dt);
FireHookStart_t g_origFireHookStart;
FireHookUpdate_t g_origFireHookUpdate;
volatile bool g_lendPlayer2Aim = true;  // testing: "p2aimlend off" gives the old behaviour

// Free fall (CSkyDiveController update, slot 6 FUN_00619510, __thiscall(controller, character
// shared_ptr, input context), ret 8) smooths the stick in two globals before each character takes
// them (FUN_00517090 forward/back 0x1E/0x1F -> [0x01077C3C], FUN_007687C0 left/right 0x21/0x20 ->
// [0x011FD0AC]). With both players falling each one's update pulled the shared values toward his
// own stick: Player 1's keys steered Player 2. Player 2's update gets his own copies.
const uintptr_t kSkyDiveUpdateSlot = 0x00EC2C38;
const uintptr_t kSkyDiveSmoothed[] = { 0x01077C3C, 0x011FD0AC };
typedef void(__fastcall* SkyDiveUpdate_t)(void* controller, void* edx, uintptr_t* character, uintptr_t input);
SkyDiveUpdate_t g_origSkyDiveUpdate;
float g_p2SkyDiveSmoothed[2];
volatile DWORD g_skyDiveLogUntil; // testing: "p2skylog <ms>"
const uintptr_t kSkyDiveEffects = 0x0106EC80;      // byte: free fall speed effects on
const uintptr_t kSkyDiveEffectScale = 0x0107986C;  // speed -> effect strength (0.002)
volatile float g_p2FallShake;                      // Player 2's free fall effect strength
DWORD g_p2FallShakeTime;
float g_p2FallLast[3];
LARGE_INTEGER g_p2FallLastTime;

// Game thread (free fall update): Player 2's speed from his position since the last update.
void UpdatePlayer2FallShake(uintptr_t c2, bool effectsOn)
{
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    const float* p = (const float*)(c2 + kCharacterPosition);
    float dt = g_p2FallLastTime.QuadPart ? float(now.QuadPart - g_p2FallLastTime.QuadPart) / float(freq.QuadPart) : 0.0f;
    if (dt > 0.0005f && dt < 0.2f) {
        float d[3] = { p[0] - g_p2FallLast[0], p[1] - g_p2FallLast[1], p[2] - g_p2FallLast[2] };
        float speed = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) / dt;
        g_p2FallShake = effectsOn ? speed * *(float*)kSkyDiveEffectScale : 0.0f;
        g_p2FallShakeTime = GetTickCount();
    }
    memcpy(g_p2FallLast, p, sizeof(g_p2FallLast));
    g_p2FallLastTime = now;
}

void RecomputeFallTurn(uintptr_t player, int view);

void __fastcall SkyDiveUpdateHook(void* controller, void* edx, uintptr_t* character, uintptr_t input)
{
    uintptr_t c2 = g_player ? Character(g_player) : 0;
    bool p2 = c2 && character && *character && *character - kCharacterActionIface == c2;
    if (g_driving && character && *character) {
        uintptr_t manager = Manager();
        uintptr_t p1 = manager ? *(uintptr_t*)(manager + kManagerPlayerSlot) : 0;
        if (p2) RecomputeFallTurn(g_player, 1);
        else if (p1 && Character(p1) == *character - kCharacterActionIface) RecomputeFallTurn(p1, 0);
    }
    float saved[2];
    if (p2) {
        for (int i = 0; i < 2; i++) {
            saved[i] = *(float*)kSkyDiveSmoothed[i];
            *(float*)kSkyDiveSmoothed[i] = g_p2SkyDiveSmoothed[i];
        }
    }
    float before = *(float*)kSkyDiveSmoothed[1];
    // The update's speed effects (byte [0x0106EC80]: camera shake source FUN_00698DE0 on
    // [0x011900F8], camera wobble FUN_008CFF40 on [0x0118FB98], speed * [0x0107986C] = 0.002) act
    // on the one game camera and pad, Player 1's. For Player 2 they're skipped here and given to
    // his own camera and pad 2 instead (Player2FallShake).
    volatile uint8_t* effects = (volatile uint8_t*)kSkyDiveEffects;
    uint8_t effectsWere = *effects;
    if (p2) *effects = 0;
    g_origSkyDiveUpdate(controller, edx, character, input);
    if (p2) {
        *effects = effectsWere;
        UpdatePlayer2FallShake(c2, effectsWere != 0);
    }
    if ((LONG)(g_skyDiveLogUntil - GetTickCount()) > 0)
        Log("Sky dive update: character %08X (p2 %d) controller %08X input %08X (p2's %08X) thread %lu side %.3f -> %.3f",
            character && *character ? (unsigned)(*character - kCharacterActionIface) : 0u, p2, (unsigned)(uintptr_t)controller,
            (unsigned)input, (unsigned)g_p2InputContext, GetCurrentThreadId(), before, *(float*)kSkyDiveSmoothed[1]);
    if (p2) {
        for (int i = 0; i < 2; i++) {
            g_p2SkyDiveSmoothed[i] = *(float*)kSkyDiveSmoothed[i];
            *(float*)kSkyDiveSmoothed[i] = saved[i];
        }
    }
}

// The respawn after a death (CGameWorld slot 0xC8, FUN_00536660, __thiscall(world, arg), ret 4)
// walks the object registry and resets each live object (FUN_008FD210, ecx [0x0118FC34]). With
// Player 2 in the game a reset changed the registry during the walk and the game died in the
// iterator (null node, or the checked iterator's silent exit). While Player 2 exists the walk runs
// on a snapshot of the entries (each held by a strong reference, like the game's walk does for the
// current one) and logs the objects whose reset changed the registry.
const uintptr_t kRespawnWalkSlot = 0x00EB92EC;
const uintptr_t kResetObject = 0x008FD210;
const uintptr_t kResetObjectThis = 0x0118FC34;
typedef void(__fastcall* RespawnWalk_t)(void* world, void* edx, uint32_t arg);
typedef void(__thiscall* ResetObject_t)(void* self, uintptr_t object);
RespawnWalk_t g_origRespawnWalk;

void ReleaseStrong(uintptr_t count)
{
    if (InterlockedDecrement((volatile LONG*)(count + 4)) != 0) return;
    typedef void(__thiscall * CountFn_t)(void*);
    ((CountFn_t)(*(uintptr_t**)count)[1])((void*)count); // dispose
    if (InterlockedDecrement((volatile LONG*)(count + 8)) == 0) ((CountFn_t)(*(uintptr_t**)count)[2])((void*)count); // destroy
}

void __fastcall RespawnWalkHook(void* world, void* edx, uint32_t arg)
{
    if (!g_player) {
        g_origRespawnWalk(world, edx, arg);
        return;
    }
    // Player 1 died with Player 2 in (Player 2's own death no longer gets here, PlayerDiedHook):
    // the reset tore down what spawn points held for Player 2 listed, and the spawn manager then
    // crashed on a freed point (FUN_007999F0). The holds go first, as for a load.
    World2ReleaseSpawnPoints(5000);
    struct Entry {
        uintptr_t object, count;
    };
    std::vector<Entry> entries;
    uintptr_t head = *(uintptr_t*)(kObjectRegistry + 4);
    std::vector<uintptr_t> stack;
    for (uintptr_t n = head ? *(uintptr_t*)(head + 4) : 0; (n && !*(uint8_t*)(n + 0x21)) || !stack.empty();) {
        if (n && !*(uint8_t*)(n + 0x21)) {
            stack.push_back(n);
            n = *(uintptr_t*)n;
            continue;
        }
        n = stack.back();
        stack.pop_back();
        uintptr_t object = *(uintptr_t*)(n + 0x18), count = *(uintptr_t*)(n + 0x1C);
        // weak_ptr::lock: take a use reference unless the object is already gone.
        for (LONG uses = count ? *(volatile LONG*)(count + 4) : 0; uses && object;) {
            LONG seen = InterlockedCompareExchange((volatile LONG*)(count + 4), uses + 1, uses);
            if (seen == uses) {
                entries.push_back({ object, count });
                break;
            }
            uses = seen;
        }
        n = *(uintptr_t*)(n + 8);
    }
    int logged = 0;
    for (const Entry& e : entries) {
        size_t before = *(size_t*)(kObjectRegistry + 8);
        ((ResetObject_t)kResetObject)(*(void**)kResetObjectThis, e.object);
        size_t after = *(size_t*)(kObjectRegistry + 8);
        if (after != before && logged++ < 20)
            Log("Respawn: resetting %08X (vtable %08X) changed the object registry %u -> %u", (unsigned)e.object,
                *(unsigned*)e.object, (unsigned)before, (unsigned)after);
    }
    for (const Entry& e : entries) ReleaseStrong(e.count);
    // The game's code keeps no frame pointers: code addresses found on the raw stack.
    char callers[200] = "";
    const uintptr_t* sp = (const uintptr_t*)_AddressOfReturnAddress();
    for (int i = 0, n = 0, len = 0; i < 400 && n < 16; i++) {
        uintptr_t v = sp[i];
        if (v < 0x00401000 || v >= 0x00D90000) continue;
        len += sprintf_s(callers + len, sizeof(callers) - len, " %08X", (unsigned)v);
        n++;
    }
    Log("Respawn: object registry walked on a snapshot (%u objects, player 2 in the game), from%s",
        (unsigned)entries.size(), callers);
}

// Returns Player 1's CPlayer with Player 2's aim control swapped in (0 when not Player 2's action).
uintptr_t LendPlayer2AimControl(uintptr_t* character, uintptr_t* saved)
{
    uintptr_t c2 = g_player && g_lendPlayer2Aim ? Character(g_player) : 0;
    if (!c2 || !character || !*character || *character - kCharacterActionIface != c2) return 0;
    uintptr_t manager = Manager();
    uintptr_t p1 = manager ? *(uintptr_t*)(manager + kManagerPlayerSlot) : 0;
    if (!p1 || p1 == g_player) return 0;
    *saved = *(uintptr_t*)(p1 + kPlayerAimControl);
    *(uintptr_t*)(p1 + kPlayerAimControl) = *(uintptr_t*)(g_player + kPlayerAimControl);
    return p1;
}

// The stunt jump onto a vehicle (CGrapplingJumpAction, vtable 0x00EC7A14) is offered by its slot
// 4 (FUN_005A4230, __thiscall(action, character +0xF0 interface), ret 4) only for the object under
// the manager's player's crosshair ([[0x0118FB24]+0x3570]+0x184 -> +0x3C): Player 2 could only
// jump onto a vehicle Player 1 was looking at. Player 2's aim control is lent for his calls.
const uintptr_t kStuntJumpCanStartSlot = 0x00EC7A24;
typedef uint8_t(__fastcall* ActionCanStart_t)(void* action, void* edx, uintptr_t iface);
ActionCanStart_t g_origStuntJumpCanStart;

// The vehicle's stunt position (CSmartObject, action +0x58) has one occupant (weak_ptr +0xC0 to the
// character's +0xF0 interface, +0xC4 control block); FUN_0081D330 refuses the jump while anyone
// else holds it. Hiding the other player for the check ("p2sharestunt on", off by default) let
// the second player jump on, but the game then crashed (2 of 2): the first player's stunt-position
// update (FUN_00824550 -> FUN_00794240) locks its weak_ptr to the spot at action+0x58 and, once
// that fails, calls FUN_008B96F0 on null (read +0x58). One spot holds one user; sharing needs more.
const uintptr_t kActionSmartObject = 0x58;
const uintptr_t kSmartObjectOccupant = 0xC0;
volatile bool g_shareStuntPositions = false;

bool IsOtherPlayer(uintptr_t jumper, uintptr_t occupantIface)
{
    if (!occupantIface) return false;
    uintptr_t occupant = occupantIface - kCharacterActionIface;
    uintptr_t c2 = g_player ? Character(g_player) : 0;
    uintptr_t manager = Manager();
    uintptr_t c1 = manager ? Character(*(uintptr_t*)(manager + kManagerPlayerSlot)) : 0;
    return c1 && c2 && occupant != jumper && (occupant == c1 || occupant == c2) && (jumper == c1 || jumper == c2);
}

uint8_t __fastcall StuntJumpCanStartHook(void* action, void* edx, uintptr_t iface)
{
    uintptr_t saved = 0, p1 = LendPlayer2AimControl(&iface, &saved);
    uintptr_t so = g_shareStuntPositions ? *(uintptr_t*)((uintptr_t)action + kActionSmartObject) : 0;
    uintptr_t occupant[2] = {};
    bool hidden = false;
    __try {
        if (so && iface && IsOtherPlayer(iface - kCharacterActionIface, *(uintptr_t*)(so + kSmartObjectOccupant))) {
            memcpy(occupant, (void*)(so + kSmartObjectOccupant), sizeof(occupant));
            memset((void*)(so + kSmartObjectOccupant), 0, sizeof(occupant));
            hidden = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    uint8_t result = g_origStuntJumpCanStart(action, edx, iface);
    if (hidden) memcpy((void*)(so + kSmartObjectOccupant), occupant, sizeof(occupant));
    if (p1) *(uintptr_t*)(p1 + kPlayerAimControl) = saved;
    return result;
}


void __fastcall FireHookStartHook(void* action, void* edx, uintptr_t* character)
{
    uintptr_t saved = 0, p1 = LendPlayer2AimControl(character, &saved);
    g_origFireHookStart(action, edx, character);
    if (p1) *(uintptr_t*)(p1 + kPlayerAimControl) = saved;
}

uint32_t __fastcall FireHookUpdateHook(void* action, void* edx, uintptr_t* character, uint32_t a, float dt)
{
    uintptr_t saved = 0, p1 = LendPlayer2AimControl(character, &saved);
    uint32_t result = g_origFireHookUpdate(action, edx, character, a, dt);
    if (p1) *(uintptr_t*)(p1 + kPlayerAimControl) = saved;
    return result;
}

// Vehicle guns fire from the vehicle controller's update (slot 6 of the 14-slot controller
// vtables below, __thiscall(controller, character +0xF0 shared_ptr, dword), ret 8), which calls the
// vehicle weapon fire FUN_007E2650 directly: it aims at the manager's player's aim point
// ([[0x0118FB24]+0x3570]+0x184 -> +0x44), so Player 2's shots went where Player 1 was looking.
// Player 2's controller gets a private copy of its vtable (the game checks some vtables) whose
// slot 6 lends his aim control for the update.
struct VehicleControllerClass {
    uintptr_t vtable;
    const char* name;
};
const VehicleControllerClass kVehicleControllers[] = {
    { 0x00EC2848, "helicopter" }, // CHelicopterController (FUN_007B0F20)
    { 0x00EC22D0, "airplane" },   // CAirPlaneController (FUN_00797130)
    { 0x00EC280C, "car" },        // CDriverSeatController (FUN_00663020)
    { 0x00EC28C0, "motorbike" },  // CMCController (FUN_006E5980)
    { 0x00EC2758, "boat" },       // CBoatController (FUN_005503D0)
    { 0x00EC271C, "ATV" },        // CATVController (FUN_00548770)
    { 0x00EC23E4, "mounted gun" }, // CMountedGunController (FUN_00667EA0)
};
const int kVehicleControllerCount = sizeof(kVehicleControllers) / sizeof(kVehicleControllers[0]);
const int kVehicleControllerSlots = 14;
const int kVehicleControllerUpdateIndex = 6;
const uintptr_t kHeliControllerVtable = 0x00EC2848, kCarControllerVtable = 0x00EC280C;
const uintptr_t kPlaneControllerVtable = 0x00EC22D0;
const int kPlaneControllerIndex = 1, kCarControllerIndex = 2;
uintptr_t g_vehicleVtables[kVehicleControllerCount][1 + kVehicleControllerSlots];
typedef uint32_t(__fastcall* VehicleUpdate_t)(void* controller, void* edx, uintptr_t* iface, uint32_t a);
VehicleUpdate_t g_origVehicleUpdate[kVehicleControllerCount];

template <int I>
uint32_t __fastcall VehicleControllerUpdateHook(void* controller, void* edx, uintptr_t* iface, uint32_t a)
{
    uintptr_t saved = 0, p1 = LendPlayer2AimControl(iface, &saved);
    uint32_t result = g_origVehicleUpdate[I](controller, edx, iface, a);
    if (p1) *(uintptr_t*)(p1 + kPlayerAimControl) = saved;
    return result;
}
const void* kVehicleUpdateHooks[] = { (void*)VehicleControllerUpdateHook<0>, (void*)VehicleControllerUpdateHook<1>,
                                      (void*)VehicleControllerUpdateHook<2>, (void*)VehicleControllerUpdateHook<3>,
                                      (void*)VehicleControllerUpdateHook<4>, (void*)VehicleControllerUpdateHook<5>,
                                      (void*)VehicleControllerUpdateHook<6> };
static_assert(sizeof(kVehicleUpdateHooks) / sizeof(kVehicleUpdateHooks[0]) == kVehicleControllerCount, "one hook each");

// Whether a vtable is one of the private copies of the given class's (-1: any class).
bool IsPrivateVehicleVtable(uintptr_t vtable, int which = -1)
{
    for (int i = 0; i < kVehicleControllerCount; i++)
        if ((which < 0 || which == i) && vtable == (uintptr_t)&g_vehicleVtables[i][1]) return true;
    return false;
}

// Game tick: Player 2's vehicle controller on its private vtable.
void KeepPlayer2VehicleAim()
{
    if (!g_vehicleVtables[0][1]) {
        for (int i = 0; i < kVehicleControllerCount; i++) {
            memcpy(g_vehicleVtables[i], (const void*)(kVehicleControllers[i].vtable - 4), sizeof(g_vehicleVtables[i]));
            g_origVehicleUpdate[i] = (VehicleUpdate_t)g_vehicleVtables[i][1 + kVehicleControllerUpdateIndex];
            g_vehicleVtables[i][1 + kVehicleControllerUpdateIndex] = (uintptr_t)kVehicleUpdateHooks[i];
        }
    }
    uintptr_t c = g_player ? Character(g_player) : 0;
    uintptr_t controller = c ? *(uintptr_t*)(c + kCharacterController) : 0;
    if (!controller) return;
    uintptr_t vtable = *(uintptr_t*)controller;
    for (int i = 0; i < kVehicleControllerCount; i++) {
        if (vtable != kVehicleControllers[i].vtable) continue;
        *(uintptr_t*)controller = (uintptr_t)&g_vehicleVtables[i][1];
        Log("P2 vehicle: %s controller %08X on a private vtable (aim lent for its guns)", kVehicleControllers[i].name,
            (unsigned)controller);
        return;
    }
}

// Effects can carry a camera-shake element (vtable slot 0x00EBA3E8, FUN_0062B300, __fastcall
// (element), returns 1) that shakes the one game camera (FUN_0070D460). The end of a reel-in plays
// such an effect (strength 5, no position), so Player 2's grapples shook Player 1's camera. While
// Player 2 is in a grapple action (character +0x324, current context action) and Player 1 isn't,
// shake elements are skipped.
const uintptr_t kCameraShakeElementSlot = 0x00EBA3E8;
const uintptr_t kCharacterCurrentAction = 0x324;
typedef uint32_t(__fastcall* ShakeElement_t)(void* element);
ShakeElement_t g_origShakeElement;

bool InGrappleAction(uintptr_t character)
{
    uintptr_t action = *(uintptr_t*)(character + kCharacterCurrentAction);
    if (!action) return false;
    for (const auto& t : kGrappleActionTypes)
        if (*(uintptr_t*)action == t.vtable) return true;
    return false;
}

uint32_t __fastcall ShakeElementHook(void* element)
{
    uintptr_t c2 = g_driving ? Character(g_player) : 0;
    uintptr_t manager = c2 ? Manager() : 0;
    uintptr_t c1 = manager ? Character(*(uintptr_t*)(manager + kManagerPlayerSlot)) : 0;
    if (c1 && InGrappleAction(c2) && !InGrappleAction(c1)) return 1;
    return g_origShakeElement(element);
}

// Replaces a function pointer in a vtable (data, not code); returns the original.
void* HookVtableSlot(uintptr_t slot, void* fn)
{
    DWORD old;
    if (!VirtualProtect((void*)slot, 4, PAGE_READWRITE, &old)) return nullptr;
    void* orig = *(void**)slot;
    *(void**)slot = fn;
    VirtualProtect((void*)slot, 4, old, &old);
    return orig;
}

void HookBrainThink()
{
    static bool installed;
    if (installed) return;
    installed = true;
    // A hook left out keeps its g_orig* null, so the game's own function runs.
    if (!DebugSkip("SkipP2Brain")) {
        g_origBrainThink = (BrainMethod_t)HookVtableSlot(kBrainThinkSlot, (void*)BrainThinkHook);
        g_origAimFilter = (AimFilter_t)HookVtableSlot(kAimFilterSlot, (void*)AimFilterHook);
    }
    // Weapons: per-weapon vtable copy instead of a slot hook (GivePlayer2WeaponVtable).
    if (!DebugSkip("SkipP2Death")) g_origCharacterDeath = (CharacterDeath_t)HookVtableSlot(kCharacterDeathSlot, (void*)CharacterDeathHook);
    if (!DebugSkip("SkipP2TakeHit")) g_origTakeHitInPlace = (TakeHit_t)HookVtableSlot(kTakeHitSlot, (void*)TakeHitInPlaceHook);
    if (!DebugSkip("SkipP2CamSetup")) g_origSetCameraSetup = (SetCameraSetup_t)HookVtableSlot(kSetCameraSetupSlot, (void*)SetCameraSetupHook);
    if (!DebugSkip("SkipP2ActionInput")) HookContextActionInput();
    if (!DebugSkip("SkipP2FireHook")) {
        g_origFireHookStart = (FireHookStart_t)HookVtableSlot(kFireHookStartSlot, (void*)FireHookStartHook);
        g_origFireHookUpdate = (FireHookUpdate_t)HookVtableSlot(kFireHookUpdateSlot, (void*)FireHookUpdateHook);
    }
    if (!DebugSkip("SkipP2StuntJump"))
        g_origStuntJumpCanStart = (ActionCanStart_t)HookVtableSlot(kStuntJumpCanStartSlot, (void*)StuntJumpCanStartHook);
    DWORD effectsProtect;
    VirtualProtect((void*)kSkyDiveEffects, 1, PAGE_READWRITE, &effectsProtect);
    if (!DebugSkip("SkipP2SkyDive")) g_origSkyDiveUpdate = (SkyDiveUpdate_t)HookVtableSlot(kSkyDiveUpdateSlot, (void*)SkyDiveUpdateHook);
    if (!DebugSkip("SkipP2Respawn")) g_origRespawnWalk = (RespawnWalk_t)HookVtableSlot(kRespawnWalkSlot, (void*)RespawnWalkHook);
    if (!DebugSkip("SkipP2Shake")) g_origShakeElement = (ShakeElement_t)HookVtableSlot(kCameraShakeElementSlot, (void*)ShakeElementHook);
    Log("P2 aim: hooked brain think %p, aim filter %p", g_origBrainThink, g_origAimFilter);
}

// Rico's physics body falls at -21 m/s² (set up for the player's character only); Player 2's body
// keeps the default -9.82, so his jumps float. Whenever his body has the default, give it Player 1's.
const uintptr_t kCharacterPhysics = 0x230; // CPfxCharacterInstance*
const uintptr_t kPhysicsGravity = 0xBC;    // float, vertical gravity
const float kDefaultGravity = -9.82f;

void MatchPlayer1Gravity()
{
    uintptr_t manager = Manager();
    uintptr_t p1 = manager ? *(uintptr_t*)(manager + kManagerPlayerSlot) : 0;
    uintptr_t c1 = p1 ? Character(p1) : 0, c2 = Character(g_player);
    uintptr_t f1 = c1 ? *(uintptr_t*)(c1 + kCharacterPhysics) : 0, f2 = c2 ? *(uintptr_t*)(c2 + kCharacterPhysics) : 0;
    if (!f1 || !f2) return;
    float g1 = *(float*)(f1 + kPhysicsGravity);
    float* g2 = (float*)(f2 + kPhysicsGravity);
    if (fabsf(*g2 - kDefaultGravity) < 0.01f && g1 < kDefaultGravity) *g2 = g1;
}

// The aiming stance goes with Player 2's zoom. The game's toggle (FUN_005C2600, SHOULDER_CAM) goes by
// the manager's player and zoomed Player 1 too; its end, FUN_0058E060 on the on-foot controller's
// +0xC part (__thiscall(part, character, mode), mode at +0x10: 0 none, 1 aiming, 2 two-handed aiming),
// tells the manager's player, so it runs with him there. Kept in step every update: a weapon
// switch may clear the mode, and he can come back from a vehicle with it on.
typedef void(__fastcall* SetAimMode_t)(uintptr_t part, void* edx, uintptr_t character, int mode);
typedef uintptr_t(__fastcall* CurrentWeapon_t)(uintptr_t character, void* edx);
typedef int(__fastcall* WeaponStance_t)(uintptr_t weapon, void* edx);
const uintptr_t kAimMode = 0x10;
// Each controller is one object for all characters, so the mode and the flags after it
// (+0x10..+0x1F of the part) were Player 1's alone: Player 2's are swapped in for his update.
const size_t kAimStateSize = 0x10;
uint8_t g_p2AimState[kAimStateSize];


void KeepPlayer2Stance()
{
    uintptr_t c = Character(g_player);
    uintptr_t manager = Manager();
    uintptr_t part = Player2AimPart();
    if (!part || !manager) return;
    bool want = SplitRenderPlayer2Zoomed();
    if (want == (*(int*)(part + kAimMode) != 0)) return;
    uintptr_t weapon = ((CurrentWeapon_t)0x00631BE0)(c, nullptr);
    int stance = weapon ? ((WeaponStance_t)0x00653D90)(weapon, nullptr) : 0;
    if (want && stance == 4) return; // the toggle leaves these alone
    int mode = !want ? 0 : (stance >= 1 && stance <= 3) ? 2 : 1;
    uintptr_t* slot = (uintptr_t*)(manager + kManagerPlayerSlot);
    uintptr_t p1 = *slot;
    *slot = g_player;
    ((SetAimMode_t)0x0058E060)(part, nullptr, c, mode);
    *slot = p1;
}

// Testing ("p2 rollcheck", "p2 roll"): the combat roll FUN_00A3D500 (on-foot controller, character),
// and the checks in it.
typedef void(__fastcall* OnFootRoll_t)(uintptr_t controller, void* edx, uintptr_t character);
typedef bool(__fastcall* CharacterCheck_t)(uintptr_t character, void* edx);
typedef bool(__fastcall* AnimCheck_t)(uintptr_t character, void* edx, const void* key, int slot);
volatile bool g_rollTest, g_rollCheck;

void LogRollChecks(const char* who, uintptr_t c)
{
    bool a = ((CharacterCheck_t)0x0051F670)(c, nullptr);
    bool b = ((AnimCheck_t)0x0073F7F0)(c, nullptr, (const void*)0x01196E3C, 0);
    bool d = ((AnimCheck_t)0x00810360)(c, nullptr, (const void*)0x01196E3C, 0);
    uintptr_t table = *(uintptr_t*)0x01196074;
    Log("Roll check %s: 51F670 %d, 73F7F0 %d, 810360 %d, gate [0x1196074] +0x40 %08X, A34+0x5C %.3f %.3f %.3f", who, a, b, d,
        table ? *(uint32_t*)(table + 0x8C + 0x40) : 0xFFFFFFFFu, *(float*)(*(uintptr_t*)(c + 0xA34) + 0x5C), *(float*)(*(uintptr_t*)(c + 0xA34) + 0x60),
        *(float*)(*(uintptr_t*)(c + 0xA34) + 0x64));
}

// The regenerating part of the health ("resistance", CPlayer +0x1B0) takes hits before health;
// only the full CPlayer::Update refills it (FUN_005BF690), which Player 2 doesn't run (it also
// posts the air supply GUI events and shakes the camera, both Player 1's), so his stayed at 0 and
// hits went straight to health (user: he died in one hit). Its first part, here: after the delay
// (+0x1CC, set by hits) the meter rises by +0x1C4 per second up to the level's +0x1B4[+0x23C];
// the health share +0x240; and the decay of +0x1F0 (stuck at 73 for him).
const uintptr_t kResistMeter = 0x1B0, kResistLevels = 0x1B4, kResistRate = 0x1C4, kResistDelay = 0x1CC;
const uintptr_t kResistLevel = 0x23C, kHealthShare = 0x240, kHealthShareMin = 0x244, kHealthShareScale = 0x254;
const uintptr_t kHitAccum = 0x1F0, kHitAccumDecay = 0x1FC;
const uintptr_t kCharacterHealthBonus = 0xE4; // added to the level for the share, as the game does
float g_p2FullHealth; // his maximum when he joined (Player 1's then); a respawn restores it

void RegeneratePlayer2(float dt)
{
    uintptr_t c = Character(g_player);
    if (!c || dt <= 0.0f) return;
    uintptr_t p = g_player;
    int level = *(int*)(p + kResistLevel);
    if (level < 0 || level > 3) return;
    float max = *(float*)(p + kResistLevels + 4 * level);
    float* meter = (float*)(p + kResistMeter);
    float* delay = (float*)(p + kResistDelay);
    // Dead (waiting to respawn): no refill, and none left (his ring empties; the respawn fills it).
    if (!(*(float*)(c + kCharacterHealth) > 0.0f)) {
        *meter = 0.0f;
        return;
    }
    if (*delay <= 0.0f) {
        *meter += *(float*)(p + kResistRate) * dt;
        if (*meter > max) *meter = max;
    } else {
        *delay -= dt;
    }
    float whole = (*(float*)(c + kCharacterHealthBonus) + max) * *(float*)(p + kHealthShareScale + 4 * level);
    float have = *(float*)(c + kCharacterHealth) + *meter;
    float share = whole <= have || whole <= 0.0f ? 1.0f : have / whole;
    float minimum = *(float*)(p + kHealthShareMin + 4 * level);
    *(float*)(p + kHealthShare) = share < minimum ? minimum : share;
    float* hits = (float*)(p + kHitAccum);
    *hits -= *(float*)(p + kHitAccumDecay) * dt;
    if (*hits < 0.0f) *hits = 0.0f;
}

// The character's sounds (footsteps and the like): FUN_006FBFE0 makes the character's sound object
// (+0xB6C; CPlayerSounds for players, +0x728) on first use and updates it. The characters' update
// (FUN_005CEEE0) skips it for players, whose own update (FUN_005C5B10) runs it, which Player 2's
// doesn't: he had none and made no footsteps (user). His footstep events and effects did run.
typedef void(__thiscall* CharacterSounds_t)(void* character, float dt);
const uintptr_t kCharacterSounds = 0x006FBFE0;

void Player2Update()
{
    InterlockedIncrement(&g_updateCalls);
    __try {
        MatchPlayer1Gravity();
        if (g_p2InputContext) {
            WritePlayer2Input();
            InterlockedIncrement(&g_inputHits);
        }
        if (g_fullUpdate) {
            ((PlayerUpdate_t)kPlayerUpdate)((void*)g_player);
            return;
        }
        float dt = ((FrameTime_t)kFrameTime)((void*)(*(uintptr_t*)kTimerPtr + 4), 0);
        uint8_t p1Aim[kAimStateSize];
        uintptr_t aimPart = Player2AimPart();
        if (aimPart) {
            memcpy(p1Aim, (void*)(aimPart + kAimMode), kAimStateSize);
            memcpy((void*)(aimPart + kAimMode), g_p2AimState, kAimStateSize);
        }
        ((PlayerDriver_t)kPlayerDriver)((void*)g_player, dt);
        KeepPlayer2Stance();
        if (aimPart) {
            memcpy(g_p2AimState, (void*)(aimPart + kAimMode), kAimStateSize);
            memcpy((void*)(aimPart + kAimMode), p1Aim, kAimStateSize);
        }
        if (g_rollTest) {
            g_rollTest = false;
            uintptr_t c = Character(g_player);
            uintptr_t controller = c ? *(uintptr_t*)(c + kCharacterController) : 0;
            if (controller) ((OnFootRoll_t)0x00A3D500)(controller, nullptr, c);
            Log("Roll test: called for P2, controller %08X", (unsigned)*(uintptr_t*)controller);
        }
        // The grapple part of CPlayer::Update: offers the enabled grapple actions (fire hook, reel
        // in, ...) to the character's controller, which starts one when its button is pressed.
        if (uintptr_t c2 = Character(g_player)) ((RunGrappleActions_t)kRunGrappleActions)(c2);
        if (uintptr_t c2 = Character(g_player)) ((CharacterSounds_t)kCharacterSounds)((void*)c2, dt);
        RegeneratePlayer2(dt);
    } __except (UpdateFilter(GetExceptionInformation())) {
    }
}

uintptr_t g_playerJobHandler; // handler whose target we replaced

// While a character reels in, the reel-in action (FUN_006B8890 -> FUN_00515CB0) shakes the camera
// and rumbles the pad every frame: FUN_00648F70 calls the camera shake (FUN_0070D460) with the
// strength [0x0106D174] and the rumble (FUN_00870960) with [0x0106D160..0x0106D16C], no matter
// whose reel it is. Those strengths are zero during Player 2's update.
const uintptr_t kReelShakeStrength = 0x0106D174;
const uintptr_t kReelRumbleParams = 0x0106D160; // 4 floats

// In free fall the player's driver turns the body toward the main camera's heading
// (FUN_0077FC80, __fastcall(CPlayer*), from the driver FUN_00853AE0: the heading error between the
// character's right axis and the camera's [[0x011919F4]+0x10]+0x24/+0x2C goes to controller
// state (char +0xA34) +4, which the free fall update adds to the lean, CSkyDiveController slot 6
// -> FUN_006B9290). Half the frames the main camera holds the other player's world, so each
// player's body was pulled toward the other's camera (one stopping to steer and "righting"
// himself turned the other one too). Right before each falling player's free fall update
// (SkyDiveUpdateHook) it runs again with his own camera world in the main camera; +8 (the previous
// turn, which it copies from +4 first) is kept.
const uintptr_t kFallTurn = 0x0077FC80;
typedef void(__fastcall* FallTurn_t)(uintptr_t player);

void RecomputeFallTurn(uintptr_t player, int view)
{
    uintptr_t c = Character(player);
    uintptr_t controller = c ? *(uintptr_t*)(c + kCharacterController) : 0;
    if (!controller || *(uintptr_t*)controller != 0x00EC2C20) return; // CSkyDiveController
    uintptr_t state = *(uintptr_t*)(c + 0xA34);
    CameraMatrix world, saved;
    if (!state || !(view == 0 ? SplitRenderPlayer1World(&world) : SplitViewMatrix(1, &world))) return;
    float previous = *(float*)(state + 8), before = *(float*)(state + 4);
    if (!SwapMainCameraWorld(world, &saved)) return;
    __try {
        ((FallTurn_t)kFallTurn)(player);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    RestoreMainCameraWorld(saved, world);
    *(float*)(state + 8) = previous;
    if ((LONG)(g_skyDiveLogUntil - GetTickCount()) > 0)
        Log("Fall turn: player %d view %d turn %.3f -> %.3f (game view %d) thread %lu", view + 1, view, before,
            *(float*)(state + 4), SplitRenderGameView(), GetCurrentThreadId());
}

// The camera shake (FUN_0070D460 on the camera control manager [0x011900F8], from sprinting,
// FUN_005FF2F0, and others) has one state, applied to the game's camera (FUN_005E7B90): Player 2's
// sprint shook Player 1's view (user). Around Player 2's update the state is put aside, with the
// amplitude 0 and the x scale 0 (each shake sets it to 1, so a shake shows; with the amplitude 0
// a render in between applies nothing); his shake goes to his own camera (Player2CameraShake).
const uintptr_t kCameraControl = 0x011900F8;
const uintptr_t kShakeAmplitude = 0x24; // then rate +0x28 (per second), decay +0x2C, scale +0x30..0x38
struct Player2Shake {
    float amplitude, rate, decay, scale[3], offset[3], target[3];
} g_p2Shake;
CRITICAL_SECTION g_p2ShakeLock;
INIT_ONCE g_p2ShakeLockOnce = INIT_ONCE_STATIC_INIT;

BOOL CALLBACK InitShakeLock(PINIT_ONCE, PVOID, PVOID*)
{
    InitializeCriticalSection(&g_p2ShakeLock);
    return TRUE;
}

void TakePlayer2Shake(const float* shake)
{
    InitOnceExecuteOnce(&g_p2ShakeLockOnce, InitShakeLock, nullptr, nullptr);
    EnterCriticalSection(&g_p2ShakeLock);
    Player2Shake& s = g_p2Shake;
    if (s.amplitude - shake[0] < 1e-6f) s.amplitude = shake[0]; // keeps the larger, as the game does
    s.rate = shake[1];
    s.decay = shake[2];
    for (int i = 0; i < 3; i++) s.scale[i] = shake[3 + i];
    LeaveCriticalSection(&g_p2ShakeLock);
}

// FUN_004F0A30: toward the target at a constant speed.
float Approach(float from, float to, float speed, float dt)
{
    if (from == to) return from;
    float t = speed / fabsf(from - to) * dt;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return from + (to - from) * t;
}

} // namespace

// Render thread, from Player 2's camera: his shake's offset (yaw, pitch radians and the same in
// metres for the position, as FUN_005E7B90 does), false while he has none.
bool Player2CameraShake(float dt, float offset[3])
{
    InitOnceExecuteOnce(&g_p2ShakeLockOnce, InitShakeLock, nullptr, nullptr);
    EnterCriticalSection(&g_p2ShakeLock);
    Player2Shake& s = g_p2Shake;
    bool on = s.amplitude > 0.0f;
    if (on) {
        bool reached = true;
        for (int i = 0; i < 3; i++) reached = reached && s.offset[i] == s.target[i];
        if (reached)
            for (int i = 0; i < 3; i++) s.target[i] = s.scale[i] * s.amplitude * (rand() / (float)RAND_MAX * 2.0f - 1.0f);
        for (int i = 0; i < 3; i++) {
            s.offset[i] = Approach(s.offset[i], s.target[i], s.rate, dt);
            offset[i] = s.offset[i];
        }
        s.amplitude += s.decay * dt;
        if (s.amplitude < 0.0f) s.amplitude = 0.0f;
    }
    LeaveCriticalSection(&g_p2ShakeLock);
    return on;
}

namespace {

// Player 1's update job, then Player 2's update (worker thread).
uintptr_t __cdecl PlayerJobHook(void* data)
{
    // Off by default ("p1jobcam on"): Player 1's update with his own camera world for the whole job.
    // SetCameraSetupHook does it for his camera switches only.
    CameraMatrix world1, before;
    bool swapped = g_player1JobCamera && SplitRenderPlayer1World(&world1) && SwapMainCameraWorld(world1, &before);
    uintptr_t result = ((JobFn_t)kPlayerJob)(data);
    if (swapped) RestoreMainCameraWorld(before, world1);
    if (g_driving && g_player && InGameplay()) {
        float* shake = (float*)kReelShakeStrength;
        float* rumble = (float*)kReelRumbleParams;
        float saved[5] = { *shake, rumble[0], rumble[1], rumble[2], rumble[3] };
        *shake = 0.0f;
        for (int i = 0; i < 4; i++) rumble[i] = 0.0f;
        uintptr_t control = *(uintptr_t*)kCameraControl;
        float* cameraShake = control ? (float*)(control + kShakeAmplitude) : nullptr; // 6 floats
        float savedShake[6];
        if (cameraShake) {
            memcpy(savedShake, cameraShake, sizeof(savedShake));
            cameraShake[0] = 0.0f;
            cameraShake[3] = 0.0f;
        }
        t_inPlayer2Update = true;
        Player2Update();
        t_inPlayer2Update = false;
        if (cameraShake) {
            if (cameraShake[3] != 0.0f && cameraShake[0] > 0.0f) TakePlayer2Shake(cameraShake);
            memcpy(cameraShake, savedShake, sizeof(savedShake));
        }
        *shake = saved[0];
        for (int i = 0; i < 4; i++) rumble[i] = saved[1 + i];
    }
    return result;
}

// Points the player update job's handler at PlayerJobHook (again, if the game rebuilt it, e.g.
// after loading). Main thread, every tick while driving.
void EnsurePlayerJobHook()
{
    __try {
        uintptr_t h = g_playerJobHandler;
        if (h && *(volatile uintptr_t*)(h + kJobHandlerTarget) == (uintptr_t)PlayerJobHook) return;
        int count = *(int*)kJobHandlerCount;
        for (int i = 0; i < count && i < 256; i++) {
            h = *(uintptr_t*)(kJobHandlerTable + i * 8 + 4);
            if (!h || *(uintptr_t*)(h + kJobHandlerTarget) != kPlayerJob) continue;
            InterlockedExchange((volatile LONG*)(h + kJobHandlerTarget), (LONG)(uintptr_t)PlayerJobHook);
            g_playerJobHandler = h;
            Log("P2 drive: hooked the player update job (handler %08X)", (unsigned)h);
            return;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// --- Decal jobs (crashes 2026-11-03 15:22, 11-04 16:50 and 17:38, each right after an explosion
// near the players) -------------------------------------------------------------------------------
// The game builds impact / scorch marks on worker threads in three job types registered by
// FUN_00763280: GenerateDecalsTask FUN_006F95D0 (-> FUN_00566670 -> FUN_0096CEC0 -> FUN_00933260),
// GenerateDeformDecalsTask FUN_00841AD0 (-> FUN_0063DBE0 -> FUN_00940A80 -> FUN_009466E0) and
// GenerateSkinnedDecalsTask FUN_00828890. All three crashes read a triangle's vertices far past its
// mesh's vertex buffer (an index list and a vertex buffer that don't belong together, likely a
// wreck's model or detail level swapped under the decal; cause not found). The jobs' handler
// pointers (heap data, like the player job's) go through a guard: a fault skips that decal (it
// only writes its results at the end; its temporary buffer leaks) and is logged.
const uintptr_t kDecalJobs[] = { 0x006F95D0, 0x00841AD0, 0x00828890 };
const char* const kDecalJobNames[] = { "GenerateDecalsTask", "GenerateDeformDecalsTask", "GenerateSkinnedDecalsTask" };
volatile LONG g_decalJobFaults[3];

template <int I>
uintptr_t __cdecl DecalJobGuard(void* data)
{
    __try {
        return ((JobFn_t)kDecalJobs[I])(data);
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        LONG n = InterlockedIncrement(&g_decalJobFaults[I]);
        if (n <= 20) Log("Decal job %s faulted (data %p): decal skipped instead of a crash (%ld so far)", kDecalJobNames[I], data, n);
        return 0;
    }
}
const JobFn_t kDecalGuards[] = { DecalJobGuard<0>, DecalJobGuard<1>, DecalJobGuard<2> };

// Main thread, every tick: (re)points the decal jobs' handlers at the guards.
void EnsureDecalJobGuards()
{
    static DWORD next;
    static int enabled = -1; // jc2ss.ini [Debug] SkipDecalGuard=1 leaves it out
    if (enabled < 0) enabled = !DebugSkip("SkipDecalGuard");
    if (!enabled || (LONG)(GetTickCount() - next) < 0) return;
    next = GetTickCount() + 1000;
    __try {
        int count = *(int*)kJobHandlerCount;
        for (int i = 0; i < count && i < 256; i++) {
            uintptr_t h = *(uintptr_t*)(kJobHandlerTable + i * 8 + 4);
            if (!h) continue;
            uintptr_t target = *(uintptr_t*)(h + kJobHandlerTarget);
            for (int j = 0; j < 3; j++)
                if (target == kDecalJobs[j]) {
                    InterlockedExchange((volatile LONG*)(h + kJobHandlerTarget), (LONG)(uintptr_t)kDecalGuards[j]);
                    Log("Decal job %s guarded (handler %08X)", kDecalJobNames[j], (unsigned)h);
                }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void StartDriving()
{
    uintptr_t c2 = Character(g_player);
    if (!c2) { Log("P2 drive: no character"); return; }
    *(unsigned char*)(g_player + kPlayerActive) = 1;
    // Player 2's statistics/progress tracker (CPlayer+0x7A8) is never set up (stats registration
    // is skipped at spawn); its "game completed" flag skips the completion update that would read it.
    *(unsigned char*)(g_player + kPlayerStats + kStatsGameCompleted) = 1;
    // Gear: the parachute (inventory +0xCC) and grappling hook (+0xD0) are created by
    // FUN_005beed0 (__thiscall CPlayer*) from settings paths Player 1 gets while loading
    // (CPlayer +0x2A0 ParachuteParams.bin, +0x2C0 GrapplingParams.bin, +0x2DC hook name).
    uintptr_t manager = Manager();
    uintptr_t p1 = manager ? *(uintptr_t*)(manager + kManagerPlayerSlot) : 0;
    StringAssign_t assign = StringAssign();
    StringAssignC_t assignC = StringAssignC();
    if (p1 && assign && assignC) {
        for (uintptr_t off : kPlayerGearStrings) assign((void*)(g_player + off), (const void*)(p1 + off));
        // Create-gear prefixes the parameter file paths with the game folder and stores the
        // result, so Player 1's are already absolute; prefixed again they'd point nowhere.
        // Player 2 gets them relative to the game folder.
        char gameDir[MAX_PATH];
        DWORD n = GetModuleFileNameA(nullptr, gameDir, MAX_PATH);
        while (n && gameDir[n - 1] != '\\') n--;
        for (uintptr_t off : { kPlayerParachutePath, kPlayerGrapplePath }) {
            const char* path = StdString((void*)(g_player + off));
            if (n && _strnicmp(path, gameDir, n) == 0) {
                char relative[MAX_PATH];
                strcpy_s(relative, path + n);
                assignC((void*)(g_player + off), relative);
            }
        }
        // Even so the files don't load mid-game (they live in the game's archives), so Player 2
        // reads Player 1's parachute parameters (CPlayer+0x674: vector of 0x108-byte entries at
        // +0xC/+0x10/+0x14, default key +0x2C). Lookups only read them, and the reload in
        // create-gear adds nothing, so the shared vector is never reallocated.
        memcpy((void*)(g_player + kPlayerParachuteParams + 0xC), (const void*)(p1 + kPlayerParachuteParams + 0xC), 0x24);
        // The parachute is looked up by this key (a name hash) in the params loaded into
        // CPlayer+0x674 (FUN_00507560); without it no parachute is created.
        *(uint32_t*)(g_player + kPlayerParachuteKey) = *(const uint32_t*)(p1 + kPlayerParachuteKey);
        __try {
            ((CreateGear_t)kCreateGear)((void*)g_player);
            uintptr_t inv = *(uintptr_t*)(c2 + kCharacterInventory);
            const uintptr_t params = g_player + 0x674;
            Log("P2 drive: gear created (parachute %08X, grapple %08X), params %08X..%08X default key %08X, key %08X",
                inv ? *(unsigned*)(inv + 0xCC) : 0, inv ? *(unsigned*)(inv + 0xD0) : 0, *(unsigned*)(params + 0xC),
                *(unsigned*)(params + 0x10), *(unsigned*)(params + 0x2C), *(unsigned*)(g_player + kPlayerParachuteKey));
        } __except (LogException(GetExceptionInformation(), "P2 drive: create gear")) {
        }
        __try {
            uintptr_t c1 = Character(p1);
            // Health (+0xE8) and max health (+0xEC): a runtime-created player gets 50, Player 1
            // has their armor upgrades from the save (400 when fully upgraded).
            // With the base maximum before them (+0xE4): the character reset sets both to it.
            if (c1) memcpy((void*)(c2 + kCharacterHealth - 4), (const void*)(c1 + kCharacterHealth - 4), 12);
            if (c1) g_p2FullHealth = *(float*)(c1 + kCharacterHealth - 4);
            // Damage per type (FUN_00843800: 9 x {threshold, multiplier, kind, mode} at +0x78, from
            // the character's parameters): Player 2's were all "full damage", Player 1's take e.g.
            // half of type 4.
            if (c1) memcpy((void*)(c2 + kCharacterDamageTable), (const void*)(c1 + kCharacterDamageTable), kCharacterDamageTableSize);
            // The health and regeneration parameters (CPlayer +0x1B4..+0x290, from a parameter file
            // and the armor upgrades) are the defaults in a player created mid-game: his first hit
            // took him from 400 to the default 50 (+0x1EC), and the ring jumped about. Player 1's,
            // without the two pointer pairs in the block.
            for (const auto& r : kPlayerHealthParams)
                memcpy((void*)(g_player + r[0]), (const void*)(p1 + r[0]), r[1] - r[0]);
            GiveWeapons(c1 ? *(uintptr_t*)(c1 + kCharacterInventory) : 0, *(uintptr_t*)(c2 + kCharacterInventory));
            if (c1) GiveGrappleActions(c1, c2);
            if (c1) ShareGrappleParams(c1, c2);
        } __except (LogException(GetExceptionInformation(), "P2 drive: weapons")) {
        }
    }
    // The player driver (FUN_00853ae0) only updates the controller of a character in the world
    // (flag 0x20 at +0x590, clear on a runtime-created player). Add it the game's way.
    unsigned before = *(unsigned*)(c2 + kCharacterFlagsOffset);
    if (!(before & kCharacterFlagActive)) EnableCharacter(c2);
    Log("P2 drive: character flags %08X -> %08X, world handle %08X", before, *(unsigned*)(c2 + kCharacterFlagsOffset),
        *(unsigned*)(c2 + 0x600));
    // Player 2's input is written into their own context before each update.
    if (!GivePrivateInput(g_player)) Log("P2 drive: no own input context, player 2 gets no input");
    EquipPlayer2();
    g_driving = true;
    EnsurePlayerJobHook();
    HookBrainThink();
    Log("P2 drive: updating player 2 %08X (character %08X)", (unsigned)g_player, (unsigned)c2);
}

// --- A Rico body made by the NPC factory ---------------------------------------------------
// CCharacterManager::CreateCharacter (FUN_0077ce50): __thiscall(manager, shared_ptr<CCharacter>* out,
// const Desc*), ret 8. The spawn system (FUN_00786280) fills Desc; we pass Player 1's
// CCharacterInfo so the new NPC is built with Rico's model and Rico's four state machines.
const uintptr_t kCreateCharacter = 0x0077CE50;
struct CreateCharacterDesc {
    int type;           // 8 = single-layer civilian path, others = full NPC setup (FUN_00533c50)
    float values[7];    // per-slot multipliers (FUN_007f04d0 slots 1..7)
    void* info;         // CCharacterInfo
    void* settings;     // optional extra settings (may be null)
};
typedef uintptr_t*(__thiscall* CreateCharacter_t)(void* manager, uintptr_t* out, const CreateCharacterDesc* desc);
volatile bool g_ricoRequested;

uintptr_t CreateRicoNpc()
{
    uintptr_t manager = Manager();
    uintptr_t c1 = manager ? Character(*(uintptr_t*)(manager + kManagerPlayerSlot)) : 0;
    if (!c1) return 0;
    CreateCharacterDesc desc = {};
    desc.type = 2;
    for (float& v : desc.values) v = 1.0f;
    desc.info = (void*)(c1 + kCharacterInfo);
    uintptr_t out[2] = {};
    __try {
        ((CreateCharacter_t)kCreateCharacter)((void*)manager, out, &desc);
    } __except (LogException(GetExceptionInformation(), "P2 rico create")) {
        return 0;
    }
    Log("P2 rico: created character %08X (count %08X)", (unsigned)out[0], (unsigned)out[1]);
    return out[0]; // we keep the reference in out[1]
}

// Adding a character to the world: CCharacter(+0xF0) interface (vtable 0x00E5602C) slot 3,
// FUN_006c8370. Only while flag 0x20 at +0x590 is clear: registers the character's world handle
// (+0x600, read unchecked by the main-thread character update), adds its physics and ragdoll,
// then sets flags 0x20|1. Slot 4 (FUN_005a9430) is the reverse (removes it from the world).
// Setting flag 0x20 by hand skips all of this. The spawn system also sets the byte at +0x9B4.
const uintptr_t kCharacterObjectIface = 0xF0; // CCharacter(+0xF0) game-object interface
const int kEnableSlot = 3;
const uintptr_t kCharacterFlags = 0x590;
const uintptr_t kCharacterSpawned = 0x9B4;
typedef void(__thiscall* Enable_t)(void* iface);
volatile uintptr_t g_enableRequest;

void EnableCharacter(uintptr_t c)
{
    __try {
        Log("P2 enable: %08X flags %08X", (unsigned)c, *(unsigned*)(c + kCharacterFlags));
        void* iface = (void*)(c + kCharacterObjectIface);
        ((Enable_t)(*(void***)iface)[kEnableSlot])(iface);
        *(unsigned char*)(c + kCharacterSpawned) = 1;
        Log("P2 enable: done, flags %08X", *(unsigned*)(c + kCharacterFlags));
    } __except (LogException(GetExceptionInformation(), "P2 enable")) {
    }
}

// Experiment: make the possessed NPC look like Rico. NPCs get their look from a paper-doll
// variant (+0x2E8, shared_ptr), Rico from the model path in his info. Drop the variant and
// re-run the character initializer (CCharacter vtable slot 28, FUN_00688c90).
const uintptr_t kCharacterPaperDoll = 0x2E8;
const int kCharacterInitSlot = 28;
typedef void(__thiscall* CharacterInit_t)(void* character, void* info);
volatile bool g_lookRequested;

void MakeRicoLook()
{
    uintptr_t manager = Manager();
    uintptr_t c1 = manager ? Character(*(uintptr_t*)(manager + kManagerPlayerSlot)) : 0;
    if (!g_possessed || !c1) return;
    __try {
        CopyInfoStrings(c1, g_possessed);
        uintptr_t* pd = (uintptr_t*)(g_possessed + kCharacterPaperDoll);
        Log("P2 look: dropping paper doll %08X", (unsigned)pd[0]);
        pd[0] = pd[1] = 0; // reference leaked on purpose
        auto init = (CharacterInit_t)(*(void***)g_possessed)[kCharacterInitSlot];
        init((void*)g_possessed, (void*)(g_possessed + kCharacterInfo));
        Log("P2 look: re-initialized %08X", (unsigned)g_possessed);
    } __except (LogException(GetExceptionInformation(), "P2 look")) {
    }
}

float g_teleportOffset[3] = { 2.0f, 0.2f, 0.0f }; // along Player 1's right, up, forward
bool g_teleportAbsolute;                           // testing ("p2 at"): the offset is a world position

void HoldUntilGround(int player, const float m[16]);

// Health (user: P1 nearly invincible at times, both players' bars off, worse after P2 respawned).
// A character has a base maximum (+0xE4), health (+0xE8) and maximum (+0xEC). Place-in-world
// (vtable slot 30) runs the character reset, which sets health and maximum to the base: our
// teleports (respawn, rejoin, "p2 at", the ground hold that places the player every frame until
// his ground is in) healed a hurt player, and Player 2, whose base was still the runtime player's
// default 50 (only health and maximum were copied at the join), came out of every one of them as
// a 50-health player. Teleports keep the three values, and Player 2's base follows Player 1's.
const uintptr_t kCharacterBaseHealth = 0xE4;
const uintptr_t kCharacterDead = 0x40; // byte

void PlaceKeepingHealth(uintptr_t c, const float* m)
{
    float saved[3];
    memcpy(saved, (void*)(c + kCharacterBaseHealth), sizeof(saved));
    ((PlaceInWorld_t)(*(void***)c)[kPlaceInWorldSlot])((void*)c, m);
    if (saved[1] > 0.0f) memcpy((void*)(c + kCharacterBaseHealth), saved, sizeof(saved));
}

// Player 2's full health: Player 1's base maximum (his armor upgrades), else the one from joining.
float Player2FullHealth()
{
    uintptr_t manager = Manager();
    uintptr_t c1 = manager ? Character(*(uintptr_t*)(manager + kManagerPlayerSlot)) : 0;
    float base = c1 ? *(float*)(c1 + kCharacterBaseHealth) : 0.0f;
    if (base > 0.0f && base < 100000.0f) return base;
    return g_p2FullHealth > 0.0f ? g_p2FullHealth : 100.0f;
}

// Testing (always on, rate-limited): P2's health values whenever they change (user: after a
// respawn his ring stayed full until he suddenly died).
void LogPlayer2Health(uintptr_t c2)
{
    static float last[5];
    static DWORD nextLog;
    float now[5] = { *(float*)(c2 + kCharacterBaseHealth), *(float*)(c2 + kCharacterHealth), *(float*)(c2 + kCharacterHealth + 4),
                     *(float*)(g_player + 0x1B0), *(float*)(g_player + 0x1CC) };
    bool changed = false;
    for (int i = 0; i < 3; i++) changed |= fabsf(now[i] - last[i]) > 0.5f;
    changed |= now[3] < last[3] - 1.0f; // the meter going down (a hit), not its refill
    DWORD t = GetTickCount();
    last[3] = now[3] > last[3] ? now[3] : last[3]; // keeps the highest so a refill isn't logged
    if (!changed || (LONG)(t - nextLog) < 0) return;
    nextLog = t + 100;
    memcpy(last, now, sizeof(last));
    Log("P2 health: base %.0f health %.1f max %.1f | meter %.1f delay %.2f | char +0x100 %08X", now[0], now[1], now[2], now[3],
        now[4], *(uint32_t*)(c2 + 0x100));
}

// Game tick.
void KeepPlayer2BaseHealth()
{
    uintptr_t c2 = g_driving && g_player ? Character(g_player) : 0;
    if (!c2) return;
    __try {
        LogPlayer2Health(c2);
        float full = Player2FullHealth();
        float* base = (float*)(c2 + kCharacterBaseHealth);
        if (*base != full) *base = full;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// Teleports Player 2 next to Player 1 (offset in Player 1's local axes).
void TeleportNextToPlayer1()
{
    uintptr_t manager = Manager();
    uintptr_t c2 = Character(g_player);
    uintptr_t c1 = manager ? Character(*(uintptr_t*)(manager + kManagerPlayerSlot)) : 0;
    if (!c1 || !c2) return;

    float m[16];
    memcpy(m, (void*)(c1 + kCharacterWorldMatrix), sizeof(m));
    for (int i = 0; i < 3; i++)
        m[12 + i] = g_teleportAbsolute ? g_teleportOffset[i]
                                       : m[12 + i] + m[i] * g_teleportOffset[0] + m[4 + i] * g_teleportOffset[1] +
                                             m[8 + i] * g_teleportOffset[2];
    bool absolute = g_teleportAbsolute;
    g_teleportAbsolute = false;

    __try {
        PlaceKeepingHealth(c2, m);
        if (absolute) HoldUntilGround(1, m);
        Log("P2 teleport: placed in world at %.1f %.1f %.1f", m[12], m[13], m[14]);
        if (g_driving) EquipPlayer2();
    } __except (LogException(GetExceptionInformation(), "P2 teleport")) {
    }
}

// --- Respawning ------------------------------------------------------------------------------
// Player 2 has no death screen or safehouse respawn. A while after they die they're placed next
// to Player 1 again: place-in-world (vtable slot 30) runs the character reset FUN_006e0100
// (brain, controller, ragdoll and rule state, clears the dead flag 0x100 at +0x590), then health
// goes back to max.
const DWORD kRespawnDelayMs = 5000;
DWORD g_diedAt; // 0 while alive

void RespawnTick()
{
    uintptr_t c = g_driving ? Character(g_player) : 0;
    if (!c) return;
    float* health = (float*)(c + kCharacterHealth); // health, max health
    if (health[0] > 0.0f) {
        g_diedAt = 0;
        return;
    }
    DWORD now = GetTickCount();
    if (!g_diedAt) {
        g_diedAt = now ? now : 1;
        Log("P2 died, respawning in %lu s", kRespawnDelayMs / 1000);
        return;
    }
    if (now - g_diedAt < kRespawnDelayMs) return;
    g_diedAt = 0;
    g_teleportOffset[0] = 2.5f;
    g_teleportOffset[1] = 0.3f;
    g_teleportOffset[2] = -2.0f;
    TeleportNextToPlayer1();
    // The maximum follows health down (game behaviour), so after dying it is 0 or below (-4 once:
    // he came back with 100 and that maximum, and no hit hurt him). Both back to his full health
    // from joining (Player 1's then, with the armor upgrades), and the resistance meter full.
    health[1] = Player2FullHealth();
    health[0] = health[1];
    health[-1] = health[1]; // the base maximum (+0xE4)
    // The dead flag (+0x40), set by the health write FUN_007E87A0 at 0 health: the reset leaves it,
    // and the damage apply FUN_00681480 does nothing while it's set (user: after P2 had been killed
    // for real and respawned, hits only drained his meter, never his health). "p2 kill" doesn't set it.
    *(uint8_t*)(c + kCharacterDead) = 0;
    int level = *(int*)(g_player + kResistLevel);
    if (level >= 0 && level <= 3) *(float*)(g_player + kResistMeter) = *(float*)(g_player + kResistLevels + 4 * level);
    Log("P2 respawned with %.0f health", health[0]);
}

// Cutscenes (user: one destroyed Player 2, only his orbit camera was left; wanted: his view off and
// him kept). The split is off while one is loaded or playing (SplitscreenCutscene); his pad does
// nothing then (WritePlayer2Input) and his health stays full. After one: dead or gone far from
// Player 1, he is put back next to him. Each start and end is logged with his state.
// --- The black market beacon for Player 2 (user: P2 calls the dealer with D-pad down, then Y aimed
// at the ground, as P1 does; the drop lands by him; only if P1 has the beacon) ---------------------
// The beacon (FUN_007FA910, on-foot weapon part) needs the inventory's item counts (+0xD4..+0xE0;
// +0xDC >= 1, the beacon): Player 2's were all -1, so they follow Player 1's (none at the start of
// the game). The beacon he holds (character +0x470, a CFlare) places its target each update (+4
// interface vtable 0x00E3A1F4 slot 7, FUN_00839480: a ray from the main camera ([0x011919F4]+0x10)
// as far as the manager's player's aim control reaches) and sets "can call here" for the HUD and
// for Y; Y sends the dealer to that target. With Player 1's player and camera, Player 2's beacon
// aimed where Player 1 looked. His flare gets a private vtable copy whose update runs with him in
// the manager's player slot and his view in the main camera.
const uintptr_t kInventoryItemCounts = 0xD4; // 4 ints
const uintptr_t kFlareIfaceVtable = 0x00E3A1F4;
// 47 slots (0..46; a string follows): a 10-slot copy crashed the game the first time the beacon
// was out (a call through slot 10+ read past it).
const int kFlareIfaceSlots = 47, kFlareUpdateIndex = 7;
uintptr_t g_flareVtable[1 + kFlareIfaceSlots];
typedef uint32_t(__fastcall* FlareUpdate_t)(uintptr_t iface, void* edx, float dt);
FlareUpdate_t g_origFlareUpdate;

uint32_t __fastcall FlareUpdateHook(uintptr_t iface, void* edx, float dt)
{
    uintptr_t c2 = g_driving && g_player ? Character(g_player) : 0;
    uintptr_t manager = Manager();
    if (!c2 || !manager || *(uintptr_t*)(c2 + kCharacterBeacon) + 4 != iface) return g_origFlareUpdate(iface, edx, dt);
    uintptr_t* slot = (uintptr_t*)(manager + kManagerPlayerSlot);
    uintptr_t p1 = *slot;
    CameraMatrix view, saved;
    bool swapped = SplitViewMatrix(1, &view) && SwapMainCameraWorld(view, &saved);
    *slot = g_player;
    uint32_t result = 0;
    __try {
        result = g_origFlareUpdate(iface, edx, dt);
    } __finally {
        *slot = p1;
        if (swapped) RestoreMainCameraWorld(saved, view);
    }
    return result;
}

void KeepBlackMarketForPlayer2()
{
    uintptr_t manager = Manager();
    uintptr_t c1 = manager ? Character(*(uintptr_t*)(manager + kManagerPlayerSlot)) : 0;
    uintptr_t c2 = g_driving && g_player ? Character(g_player) : 0;
    if (!c1 || !c2) return;
    __try {
        uintptr_t inv1 = *(uintptr_t*)(c1 + kCharacterInventory), inv2 = *(uintptr_t*)(c2 + kCharacterInventory);
        if (inv1 && inv2) memcpy((void*)(inv2 + kInventoryItemCounts), (void*)(inv1 + kInventoryItemCounts), 4 * sizeof(int));
        if (!g_flareVtable[1]) {
            memcpy(g_flareVtable, (const void*)(kFlareIfaceVtable - 4), sizeof(g_flareVtable));
            g_origFlareUpdate = (FlareUpdate_t)g_flareVtable[1 + kFlareUpdateIndex];
            g_flareVtable[1 + kFlareUpdateIndex] = (uintptr_t)FlareUpdateHook;
        }
        uintptr_t flare = *(uintptr_t*)(c2 + kCharacterBeacon);
        if (flare && *(uintptr_t*)(flare + 4) == kFlareIfaceVtable) *(uintptr_t*)(flare + 4) = (uintptr_t)&g_flareVtable[1];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

bool g_inCutscene;
const float kCutsceneFarDistance = 50.0f;

void LogPlayer2State(const char* when)
{
    uintptr_t c = g_player ? Character(g_player) : 0;
    __try {
        if (!c) {
            Log("Cutscene %s: player 2 %08X has no character", when, (unsigned)g_player);
            return;
        }
        const float* p = (const float*)(c + kCharacterPosition);
        const float* health = (const float*)(c + kCharacterHealth);
        Log("Cutscene %s: player 2 character %08X vtable %08X at %.1f %.1f %.1f, health %.0f / %.0f", when, (unsigned)c,
            *(unsigned*)c, p[0], p[1], p[2], health[0], health[1]);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("Cutscene %s: player 2 character %08X unreadable", when, (unsigned)c);
    }
}

void CutsceneTick()
{
    bool cutscene = g_driving && SplitscreenCutscene();
    uintptr_t c = g_driving ? Character(g_player) : 0;
    if (cutscene != g_inCutscene) {
        g_inCutscene = cutscene;
        LogPlayer2State(cutscene ? "started" : "ended");
        if (!cutscene && c) {
            uintptr_t manager = Manager();
            uintptr_t c1 = manager ? Character(*(uintptr_t*)(manager + kManagerPlayerSlot)) : 0;
            float* health = (float*)(c + kCharacterHealth);
            bool farAway = false;
            if (c1) {
                const float* a = (const float*)(c1 + kCharacterPosition);
                const float* b = (const float*)(c + kCharacterPosition);
                float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
                farAway = dx * dx + dy * dy + dz * dz > kCutsceneFarDistance * kCutsceneFarDistance;
            }
            if (health[0] <= 0.0f || farAway) {
                g_teleportOffset[0] = 2.5f;
                g_teleportOffset[1] = 0.3f;
                g_teleportOffset[2] = -2.0f;
                TeleportNextToPlayer1();
                health[1] = Player2FullHealth();
                health[0] = health[1];
                health[-1] = health[1];
                *(uint8_t*)(c + kCharacterDead) = 0;
                g_diedAt = 0;
                Log("Cutscene ended: player 2 put back next to player 1 (%s)", farAway ? "far away" : "dead");
            }
        }
    }
    if (cutscene && c) {
        float* health = (float*)(c + kCharacterHealth);
        if (health[0] > 0.0f && health[0] < health[1]) health[0] = health[1];
    }
}

// When Player 1 jumps far in one tick (respawn after dying, loading a save, fast travel), Player 2
// is brought along.
const float kPlayer1JumpDistance = 50.0f;
float g_lastP1Pos[3];
bool g_haveLastP1Pos;

// After a long teleport the player's terrain cells take a few seconds to load (the game itself
// only teleports far behind a loading screen); until then he would fall through the ground. So
// a teleported player is held where he was put until his ground is there (at most 15 s).
struct GroundHold {
    bool on;
    float m[16];
    DWORD until;
};
GroundHold g_groundHold[2];

void HoldUntilGround(int player, const float m[16])
{
    memcpy(g_groundHold[player].m, m, sizeof(g_groundHold[player].m));
    g_groundHold[player].until = GetTickCount() + 15000;
    g_groundHold[player].on = true;
}

// Testing ("god on"): both players' health is kept far above anything one hit takes (teleports
// dropped them onto rocks and killed them).
volatile bool g_godMode;
volatile bool g_godEnded; // "god off": health was left at 100000 (the user found P1 nearly invincible)
const float kGodHealth = 100000.0f;

void KeepPlayersAlive()
{
    uintptr_t manager = Manager();
    uintptr_t c[2] = { manager ? Character(*(uintptr_t*)(manager + kManagerPlayerSlot)) : 0,
                       g_player ? Character(g_player) : 0 };
    if (g_godEnded && !g_godMode) {
        g_godEnded = false;
        for (uintptr_t ch : c) {
            if (!ch) continue;
            __try {
                float* health = (float*)(ch + kCharacterHealth);
                if (health[0] > health[1] && health[1] > 0.0f) health[0] = health[1];
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
    }
    if (!g_godMode) return;
    for (uintptr_t ch : c) {
        if (!ch) continue;
        __try {
            float* health = (float*)(ch + kCharacterHealth);
            if (health[0] > 0.0f) health[0] = kGodHealth;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

void KeepHeldPlayers()
{
    KeepPlayersAlive();
    for (int who = 0; who < 2; who++) {
        GroundHold& h = g_groundHold[who];
        if (!h.on) continue;
        uintptr_t manager = Manager();
        uintptr_t c = who == 0 ? (manager ? Character(*(uintptr_t*)(manager + kManagerPlayerSlot)) : 0)
                               : (g_player ? Character(g_player) : 0);
        if (!c) {
            h.on = false;
            continue;
        }
        bool loaded = Terrain2GroundLoaded(who, &h.m[12]);
        bool timedOut = (LONG)(GetTickCount() - h.until) >= 0;
        __try {
            PlaceKeepingHealth(c, h.m);
        } __except (LogException(GetExceptionInformation(), "ground hold")) {
            h.on = false;
        }
        if (loaded || timedOut) {
            h.on = false;
            Log("P%d held until %s", who + 1, loaded ? "the ground loaded" : "the time ran out");
        }
    }
}

// Testing (F6 / "p2 swap"): the players trade places, e.g. to compare how the same spot looks in
// either view.
volatile bool g_swapRequested;

void SwapPlayers()
{
    uintptr_t manager = Manager();
    uintptr_t c1 = manager ? Character(*(uintptr_t*)(manager + kManagerPlayerSlot)) : 0;
    uintptr_t c2 = g_driving ? Character(g_player) : 0;
    if (!c1 || !c2) return;
    __try {
        float m1[16], m2[16];
        memcpy(m1, (void*)(c1 + kCharacterWorldMatrix), sizeof(m1));
        memcpy(m2, (void*)(c2 + kCharacterWorldMatrix), sizeof(m2));
        m1[13] += 0.2f;
        m2[13] += 0.2f;
        PlaceKeepingHealth(c1, m2);
        PlaceKeepingHealth(c2, m1);
        HoldUntilGround(0, m2);
        HoldUntilGround(1, m1);
        g_haveLastP1Pos = false; // not a jump to follow
        Log("Players swapped: P1 at %.1f %.1f %.1f, P2 at %.1f %.1f %.1f", m2[12], m2[13], m2[14], m1[12], m1[13],
            m1[14]);
    } __except (LogException(GetExceptionInformation(), "swap")) {
    }
}

void FollowPlayer1Jumps()
{
    uintptr_t manager = Manager();
    uintptr_t c1 = manager ? Character(*(uintptr_t*)(manager + kManagerPlayerSlot)) : 0;
    if (!g_driving || !c1) {
        g_haveLastP1Pos = false;
        return;
    }
    const float* p = (const float*)(c1 + kCharacterPosition);
    if (g_haveLastP1Pos) {
        float dx = p[0] - g_lastP1Pos[0], dy = p[1] - g_lastP1Pos[1], dz = p[2] - g_lastP1Pos[2];
        if (dx * dx + dy * dy + dz * dz > kPlayer1JumpDistance * kPlayer1JumpDistance) {
            g_teleportOffset[0] = 2.5f;
            g_teleportOffset[1] = 0.3f;
            g_teleportOffset[2] = -2.0f;
            TeleportNextToPlayer1();
            Log("P2 follow: player 1 jumped %.0f m, player 2 brought along", sqrtf(dx * dx + dy * dy + dz * dz));
        }
    }
    memcpy(g_lastP1Pos, p, sizeof(g_lastP1Pos));
    g_haveLastP1Pos = true;
}

// --- Joining with Start on controller 2 ----------------------------------------------------
// Spawns Player 2's Rico next to Player 1 and drives him (retried every couple of seconds if the
// game isn't ready, e.g. still loading).
DWORD g_autoJoinRetry;
const DWORD kAutoJoinRetryMs = 2000;

DWORD g_joinAmmoAt; // 0: none pending
const DWORD kJoinAmmoDelayMs = 1000;

void AutoJoinTick()
{
    if (g_joinAmmoAt && (LONG)(GetTickCount() - g_joinAmmoAt) >= 0) {
        g_joinAmmoAt = 0;
        if (g_driving) {
            Log("P2 join: ammo refilled (as F5)");
            g_ammoRequested = true;
        }
    }
    if (g_driving || g_possessed || SplitscreenPlayer2Status() != Player2Status::Joined) return;
    DWORD now = GetTickCount();
    if ((LONG)(now - g_autoJoinRetry) < 0) return;
    g_autoJoinRetry = now + kAutoJoinRetryMs;
    if (!g_player) {
        if (!Spawn()) { Log("P2 join: spawn failed, retrying"); return; }
        g_teleportOffset[0] = 2.5f;
        g_teleportOffset[1] = 0.3f;
        g_teleportOffset[2] = -2.0f;
        TeleportNextToPlayer1();
    }
    StartDriving();
    // User: joining (also again after Back+Start) gives full ammo, as F5 does for both players.
    // A second later, once his weapons are equipped.
    if (g_driving) g_joinAmmoAt = GetTickCount() + kJoinAmmoDelayMs;
}

// Hits between the players (user: P2's melee hurt P1, P1's didn't hurt P2; wanted: melee both
// ways, bullets neither way). A character takes a hit in CCharacter slot 6 (FUN_006B3210,
// __thiscall(char, damage, hit info, out), ret 0xC), which drops it when the damage's source team
// (damage[3]) equals the character's (+0x480): both players are team 9, so Player 1's hits never
// reached Player 2. The two players' characters get a private vtable copy (of kCharacterVtable)
// with that slot hooked.
const int kTakeHitIndex = 6;
typedef uint32_t(__fastcall* TakeHit_t)(uintptr_t character, void* edx, int* damage, int hit, void* out);
TakeHit_t g_origTakeHit;
volatile LONG g_hitLogs;  // testing: hits on a player logged

uint32_t __fastcall TakeHitHook(uintptr_t character, void* edx, int* damage, int hit, void* out)
{
    uintptr_t manager = Manager();
    uintptr_t c1 = manager ? Character(*(uintptr_t*)(manager + kManagerPlayerSlot)) : 0;
    uintptr_t c2 = g_player ? Character(g_player) : 0;
    if (InterlockedIncrement(&g_hitLogs) <= 60) {
        __try {
            Log("Hit on %s (%08X): damage %08X vtable %08X [%d %d %d %d %d %d %d %d] hit %08X [%08X %08X %08X %08X %08X %08X]",
                character == c1 ? "P1" : character == c2 ? "P2" : "?", (unsigned)character, (unsigned)(uintptr_t)damage,
                (unsigned)damage[0], damage[1], damage[2],
                damage[3], damage[4], damage[5], damage[6], damage[7], damage[8], (unsigned)hit, *(unsigned*)hit,
                *(unsigned*)(hit + 4), *(unsigned*)(hit + 8), *(unsigned*)(hit + 0xC), *(unsigned*)(hit + 0x10),
                *(unsigned*)(hit + 0x14));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return g_origTakeHit(character, edx, damage, hit, out);
}

// Explosions (user: let Player 1's explosives hurt Player 2; his bullets still don't). The same
// take-hit method, hooked in place (its slot 0x00EB8834; slot 24 of this vtable is hooked in place
// already without the integrity check minding; a private vtable on the character stops all hits):
// a hit on Player 2 whose team is his own passes when its damage object's class is one of the
// explosion classes (g_explosionDamage, learnt from the log: "P2 hit by team mate").
const uintptr_t kCharacterTeam = 0x480;
TakeHit_t g_origTakeHitInPlace;
volatile bool g_explosionsHurtPlayer2 = true; // "p2 explosions on|off"
uintptr_t g_explosionDamage[8];                // damage object vtables that count as explosions
long g_teamHitLogs;

uint32_t __fastcall TakeHitInPlaceHook(uintptr_t character, void* edx, int* damage, int hit, void* out)
{
    uintptr_t c2 = g_player ? Character(g_player) : 0;
    if (c2 && character == c2 && damage) {
        __try {
            int* team = (int*)(c2 + kCharacterTeam);
            if (damage[3] == *team) {
                uintptr_t vtable = (uintptr_t)damage[0];
                bool explosion = false;
                for (uintptr_t v : g_explosionDamage) explosion |= v && v == vtable;
                if (g_teamHitLogs++ < 40)
                    Log("P2 hit by team mate: damage class vtable %08X [%d %d %d %d %d %d] amount %.1f%s", (unsigned)vtable, damage[1],
                        damage[2], damage[3], damage[4], damage[5], damage[6], *(float*)&damage[1],
                        explosion ? " (explosion: lets it through)" : "");
                if (explosion && g_explosionsHurtPlayer2) {
                    int saved = *team;
                    *team = saved + 1000; // any other team, for this hit only
                    uint32_t result = g_origTakeHitInPlace(character, edx, damage, hit, out);
                    *team = saved;
                    return result;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return g_origTakeHitInPlace(character, edx, damage, hit, out);
}

// Game tick: both players' characters on the private vtable.
void KeepPlayerHitHooks()
{
    if (!g_characterVtable[1]) {
        memcpy(g_characterVtable, (const void*)(kCharacterVtable - 4), sizeof(g_characterVtable));
        g_origTakeHit = (TakeHit_t)g_characterVtable[1 + kTakeHitIndex];
        g_characterVtable[1 + kTakeHitIndex] = (uintptr_t)TakeHitHook;
    }
    uintptr_t manager = Manager();
    uintptr_t players[2] = { manager ? Character(*(uintptr_t*)(manager + kManagerPlayerSlot)) : 0,
                             g_player ? Character(g_player) : 0 };
    for (uintptr_t c : players)
        if (c) InterlockedCompareExchange((volatile LONG*)c, (LONG)(uintptr_t)&g_characterVtable[1], (LONG)kCharacterVtable);
}

} // namespace

void KeepMinimapPerPlayer();
void KeepMarkersPerPlayer();
void KeepHudPassHooked();

void FireRequestedGuiEvent();

void KeepMissionPopupPlace(bool split);

void Player2GameTick()
{
    FireRequestedGuiEvent();
    KeepMissionPopupPlace(g_driving && g_player && SplitscreenActive());
    ParachuteDragTick();
    if (uintptr_t npc = g_possessRequest) {
        g_possessRequest = 0;
        if (g_player && !g_possessed) Possess(npc);
    }
    if (g_lookRequested) {
        g_lookRequested = false;
        MakeRicoLook();
    }
    if (g_driveRequested) {
        g_driveRequested = false;
        if (g_player && !g_driving) StartDriving();
    }
    EnsureDecalJobGuards();
    if (g_driving) {
        EnsurePlayerJobHook();
        GivePlayer2WeaponVtable();
        KeepMinimapPerPlayer();
        KeepMarkersPerPlayer();
        KeepHudPassHooked();
        // KeepPlayerHitHooks() is off: with the players' characters on a private vtable no hit
        // reached them at all (hit detection checks the vtable itself; user: P2 took no damage).
        if (!DebugSkip("SkipP2VehicleAim")) KeepPlayer2VehicleAim();
        KeepPlayer2BrainVtable();
    }
    if (g_rollCheck) {
        g_rollCheck = false;
        uintptr_t manager = Manager();
        if (uintptr_t c1 = manager ? Character(*(uintptr_t*)(manager + kManagerPlayerSlot)) : 0) LogRollChecks("P1", c1);
        if (uintptr_t c2 = Character(g_player)) LogRollChecks("P2", c2);
    }
    if (g_fireCheckRequested) {
        g_fireCheckRequested = false;
        uintptr_t manager = Manager();
        if (uintptr_t c1 = manager ? Character(*(uintptr_t*)(manager + kManagerPlayerSlot)) : 0) LogFireConditions("P1", c1);
        if (uintptr_t c2 = Character(g_player)) LogFireConditions("P2", c2);
    }
    if (g_gearRequested) {
        g_gearRequested = false;
        __try {
            ((CreateGear_t)kCreateGear)((void*)g_player);
            uintptr_t set = *(uintptr_t*)(Character(g_player) + 0x628);
            Log("P2 gear: action set %08X actions %08X %08X %08X %08X %08X %08X", (unsigned)set,
                set ? *(unsigned*)(set) : 0, set ? *(unsigned*)(set + 12) : 0, set ? *(unsigned*)(set + 24) : 0,
                set ? *(unsigned*)(set + 36) : 0, set ? *(unsigned*)(set + 48) : 0, set ? *(unsigned*)(set + 60) : 0);
        } __except (LogException(GetExceptionInformation(), "P2 gear")) {
        }
    }
    if (float lift = g_liftPlayer1) {
        g_liftPlayer1 = 0;
        uintptr_t m = Manager();
        if (uintptr_t c1 = m ? Character(*(uintptr_t*)(m + kManagerPlayerSlot)) : 0) {
            __try {
                float w[16];
                memcpy(w, (void*)(c1 + kCharacterWorldMatrix), sizeof(w));
                w[13] += lift;
                float p2[3];
                if (g_movePlayer1ToPlayer2 && Player2Position(p2)) {
                    w[12] = p2[0] + 3.0f;
                    w[13] = p2[1] + 1.0f;
                    w[14] = p2[2];
                }
                if (g_movePlayer1To) memcpy(&w[12], g_player1Target, sizeof(g_player1Target));
                bool moved = g_movePlayer1To || g_movePlayer1ToPlayer2;
                g_movePlayer1ToPlayer2 = false;
                g_movePlayer1To = false;
                PlaceKeepingHealth(c1, w);
                if (moved) {
                    HoldUntilGround(0, w);
                    g_haveLastP1Pos = false; // not a jump to follow
                }
                Log("P1 lifted %.0f m", lift);
            } __except (LogException(GetExceptionInformation(), "P1 lift")) {
            }
        }
    }
    if (g_swapRequested) {
        g_swapRequested = false;
        SwapPlayers();
    }
    KeepHeldPlayers();
    if (g_killRequested) {
        g_killRequested = false;
        if (uintptr_t c2 = Character(g_player)) {
            __try {
                *(float*)(c2 + kCharacterHealth) = 0.0f;
                auto die = (CharacterDeath_t)(*(void***)c2)[(kCharacterDeathSlot - kCharacterVtable) / 4];
                if (g_killRequestedRaw) die = g_origCharacterDeath; // the game's own death (death screen)
                g_killRequestedRaw = false;
                die((void*)c2, nullptr, (void*)c2);
            } __except (LogException(GetExceptionInformation(), "P2 kill")) {
            }
        }
    }
    if (uintptr_t c = g_enableRequest) {
        g_enableRequest = 0;
        EnableCharacter(c);
    }
    if (g_ricoRequested) {
        g_ricoRequested = false;
        if (g_player && !g_possessed) {
            uintptr_t rico = CreateRicoNpc();
            if (rico) EnableCharacter(rico);
            if (rico && Possess(rico)) {
                g_teleportOffset[0] = 2.5f;
                g_teleportOffset[1] = 0.3f;
                g_teleportOffset[2] = -2.0f;
                TeleportNextToPlayer1();
            }
        }
    }
    KeepPlayerController();
    if (g_driving) SetupPlayer2Launcher();
    AutoJoinTick();
    KeepPlayer2BaseHealth();
    RespawnTick();
    CutsceneTick();
    KeepBlackMarketForPlayer2();
    FollowPlayer1Jumps();
    if (LONG dollars = InterlockedExchange(&g_moneyRequested, 0)) {
        uintptr_t manager = Manager();
        uintptr_t p1 = manager ? *(uintptr_t*)(manager + kManagerPlayerSlot) : 0;
        if (p1) {
            int* money = (int*)(p1 + kPlayerMoney);
            Log("Money: %d + %ld", *money, dollars);
            *money += dollars;
        }
    }
    if (g_ammoRequested) {
        g_ammoRequested = false;
        uintptr_t manager = Manager();
        RefillAmmo("player 1", manager ? Character(*(uintptr_t*)(manager + kManagerPlayerSlot)) : 0);
        if (g_driving) RefillAmmo("player 2", Character(g_player));
    }
    if (g_teleportRequested) {
        g_teleportRequested = false;
        TeleportNextToPlayer1();
    }
    if (g_spawnRequested) {
        g_spawnRequested = false;
        if (!g_player) Spawn();
    }
}

std::string Player2RequestPossess(uintptr_t npc)
{
    if (!g_player) return "error no player 2";
    if (g_possessed) return "error already possessing";
    g_possessRequest = npc;
    return "ok possess requested";
}

std::string Player2RequestDrive()
{
    if (!g_player) return "error no player 2";
    g_driveRequested = true;
    return "ok drive requested";
}

std::string Player2RequestEnable(uintptr_t character)
{
    if (!character) character = g_possessed ? g_possessed : Character(g_player);
    if (!character) return "error no character";
    g_enableRequest = character;
    return "ok enable requested";
}

std::string Player2RequestRicoBody()
{
    if (!g_player) return "error no player 2";
    if (g_possessed) return "error already possessing";
    g_ricoRequested = true;
    return "ok rico body requested";
}

std::string Player2RequestRicoLook()
{
    if (!g_possessed) return "error not possessing";
    g_lookRequested = true;
    return "ok look requested";
}

std::string Player2RollTest(bool roll)
{
    if (roll) g_rollTest = true;
    else g_rollCheck = true;
    return "ok roll test requested";
}

std::string Player2RequestFireCheck()
{
    g_fireCheckRequested = true;
    return "ok fire check requested";
}

std::string Player1RequestLift(float meters)
{
    g_liftPlayer1 = meters;
    return "ok lift requested";
}

std::string Player2PadPress(unsigned buttons, unsigned ms)
{
    g_testButtons = buttons;
    g_testButtonsUntil = GetTickCount() + ms;
    return "ok pad 2 buttons pressed";
}

std::string Player2PadLog(bool on)
{
    g_padLog = on;
    return std::string("ok pad 2 log ") + (on ? "on" : "off");
}

std::string Player1JobCamera(bool on)
{
    g_player1JobCamera = on;
    return std::string("ok player 1's update job runs with his camera world ") + (on ? "on" : "off");
}

// FUN_0068BAF0: __thiscall(CPlayer*, float dt), ret 4: the grappling device's frame update
// (FUN_0083FE20), ending in FUN_006A1750, which hands the ropes queued at device +0x174 (each
// grapple's rope, built by FUN_007DE2E0) to the scene and frees them. The frame (FUN_00781D60) runs
// it for the manager's player only: Player 2's ropes were never drawn and piled up (~1000 after a
// few grapples) until an allocation for the next rope failed (crash in FUN_00937030).
const uintptr_t kPlayerDeviceUpdate = 0x0068BAF0;
typedef void(__thiscall* PlayerDeviceUpdate_t)(void* player, float dt);

void Player2DeviceUpdate()
{
    if (!g_driving || !g_player || !Character(g_player)) return;
    __try {
        float dt = ((FrameTime_t)kFrameTime)((void*)(*(uintptr_t*)kTimerPtr + 4), 0);
        ((PlayerDeviceUpdate_t)kPlayerDeviceUpdate)((void*)g_player, dt);
    } __except (LogException(GetExceptionInformation(), "P2 grappling device update")) {
    }
}

float Player2FallShake()
{
    // Stale (no free fall update for a moment, e.g. the parachute opened): none.
    return GetTickCount() - g_p2FallShakeTime < 100 ? g_p2FallShake : 0.0f;
}

// --- Minimap -------------------------------------------------------------------------------------
// The HUD minimap (CGuiMiniMap, vtable 0x00EB911C, one instance) updates in slot 7 (FUN_00802D40,
// __thiscall(minimap, one dword), ret 4): it copies the main camera's matrix ([[0x011919F4]+0x10]+0x24, heading) and
// the manager's player's matrix (FUN_0068E1F0 on [[0x0118FB24]+0x3570], position; the camera's in
// a vehicle, player +0x300) and moves the map. Every frame showed Player 1's surroundings, also on
// Player 2's frames. On his frames the update runs with his player in the manager's slot and his
// view in the main camera. The vtable is checked by the game (like the despawn functor's, see
// HANDOFF), so the instance gets a private copy (type info + 47 slots) instead of a slot hook; it is
// found by a scan of the writable heap (background thread) for its vtable with the interface vtable
// 0x00E38A88 at +0x30, again whenever it changes (e.g. a load).
const uintptr_t kMinimapVtable = 0x00EB911C;
const uintptr_t kMinimapIfaceVtable = 0x00E38A88;
const int kMinimapSlots = 47;
const int kMinimapUpdateIndex = 7;
uintptr_t g_minimapVtable[1 + kMinimapSlots];
typedef uint32_t(__fastcall* MinimapUpdate_t)(void* minimap, void* edx, uint32_t arg); // ret 4
MinimapUpdate_t g_origMinimapUpdate;
volatile uintptr_t g_minimap;    // the instance (0 = not found yet)
volatile LONG g_minimapScanning;
volatile bool g_minimapPerPlayer = true; // "p2minimap on|off" (testing)

bool IsMinimap(uintptr_t p)
{
    __try {
        uintptr_t vt = *(uintptr_t*)p;
        return (vt == kMinimapVtable || vt == (uintptr_t)&g_minimapVtable[1]) && *(uintptr_t*)(p + 0x30) == kMinimapIfaceVtable;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

DWORD WINAPI ScanForMinimap(void*)
{
    uintptr_t found = 0;
    MEMORY_BASIC_INFORMATION mbi;
    for (uintptr_t a = 0x00010000; !found && a < 0x7FFF0000 && VirtualQuery((void*)a, &mbi, sizeof(mbi)); a += mbi.RegionSize) {
        if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE || !(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) ||
            (mbi.Protect & PAGE_GUARD))
            continue;
        __try {
            const uintptr_t* p = (const uintptr_t*)mbi.BaseAddress;
            const uintptr_t* end = (const uintptr_t*)((uintptr_t)mbi.BaseAddress + mbi.RegionSize) - 13;
            for (; p < end; p++)
                if (*p == kMinimapVtable && p[12] == kMinimapIfaceVtable) { found = (uintptr_t)p; break; }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    if (found) {
        g_minimap = found;
        Log("Minimap: found CGuiMiniMap at %08X", (unsigned)found);
    }
    InterlockedExchange(&g_minimapScanning, 0);
    return 0;
}

// --- Mission markers -----------------------------------------------------------------------------
// The HUD's 3D markers ("Reapers HQ 1.50 km") are CTargetIndicators, a list from [0x011910A0]
// linked by +0x40. The HUD script (Lua, Game.GetTargetIndicatorInfo -> FUN_007EA800) projects
// each one with the main camera and shows its distance, an int at +0x54 that FUN_006B0350
// (__fastcall(indicator)) sets from the manager's player's position; its name/distance flags also
// come from +0x54. The game sets them every frame before the script runs, so setting them from the
// game tick or the minimap update showed Player 1's on both halves. Instead the script's function
// is redirected: it is a Lua 5.1 C closure on the heap (several Lua states: next, tt 6 at +4, isC 1
// at +6, env, function at +0xC), found by a background heap scan and pointed at
// TargetIndicatorHook, which first sets every distance for the half being projected (the view
// the main camera last derived) once per game frame, and on Player 2's frames puts his view in the
// main camera for the projection.
const uintptr_t kTargetIndicatorList = 0x011910A0;
const int kTargetIndicatorNext = 0x40;
typedef void(__fastcall* TargetIndicatorDistance_t)(uintptr_t indicator);
const TargetIndicatorDistance_t TargetIndicatorDistance = (TargetIndicatorDistance_t)0x006B0350;
typedef int(__cdecl* LuaCFunction_t)(void* L);
const uintptr_t kTargetIndicatorInfo = 0x00A39D70; // Game:GetTargetIndicatorInfo
const uintptr_t kTargetIndicatorCount = 0x00A39CA0; // Game:GetNumberOfTargetIndicators
const int kClosureFunction = 0xC;
volatile bool g_markersPerPlayer = true; // "p2markers on|off" (testing)
const int kMaxMarkerClosures = 256;
volatile uintptr_t g_markerClosures[kMaxMarkerClosures]; // closures pointed at the hook
volatile LONG g_markerScanning;
long g_markerFrame = -1;
bool g_markerP2;                         // this frame's script projects for Player 2's half
LONG g_markerHalves[2];                  // testing: frames set for each half

void UpdateMarkerDistances()
{
    __try {
        int n = 0;
        for (uintptr_t t = *(uintptr_t*)kTargetIndicatorList; t && n < 4096; t = *(uintptr_t*)(t + kTargetIndicatorNext), n++)
            TargetIndicatorDistance(t);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// The HUD's weapon panels only read weapons and ammo on the game's GUI events (a queue, FUN_00976F60
// on [0x0118FB78]: name, then "UIControl..." at 0x00E4409C). Tables of event names by hand:
// weapon changed 0x0106EBEC, ammo changed 0x0106EC3C.
const uintptr_t kGuiEvents = 0x0118FB78;
typedef void(__fastcall* FireGuiEvent_t)(void* gui, void* edx, const char* name, const char* context);
const FireGuiEvent_t FireGuiEvent = (FireGuiEvent_t)0x00976F60;
const char* const kGuiEventContext = (const char*)0x00E4409C;
const char* const* const kGuiWeaponChanged = (const char* const*)0x0106EBEC;
const char* const* const kGuiAmmoChanged = (const char* const*)0x0106EC3C;
const int kGuiHands = 8;
const char kGuiHealthChanged[] = "player_health_changed_event";
const char kGuiIndicatorsShow[] = "gui_target_indicator_show_event";
char g_guiEventRequest[64]; // testing ("guievent <name>"): fired on the next game tick
volatile bool g_guiEventPending;

void FireRequestedGuiEvent()
{
    if (!g_guiEventPending) return;
    g_guiEventPending = false;
    if (void* gui = *(void**)kGuiEvents) FireGuiEvent(gui, nullptr, g_guiEventRequest, kGuiEventContext);
}
volatile bool g_proximityPerPlayer = true; // proximity icons per player ("p2hud proximity on|off", testing)
volatile unsigned g_hudWeaponMask = 0x1F, g_hudAmmoMask; // "p2hud refresh <weapon mask> <ammo mask>" (testing)
DWORD g_hudPanelsUntil;   // the panels are refreshed until then
const DWORD kHudPanelMs = 6000;

// Input that brings the weapon panels up. Refreshing them keeps them up (update_hand_* -> show_hands),
// so only for a while after any of these, for either player; then they fade as usual.
const int kHudPanelActions[] = {
    kActFireLeft, kActFireRight, kActFire, kActThrowGrenade, kActReload, kActEquipExplosive, kActEquipRight, kActEquipLeft,
    kActEquipDual, kActEquipTwoHanded, kActPickupLeft, kActPickupRight, 0x11 /* NEXT_WEAPON */, 0x12 /* PREV_WEAPON */,
};
typedef uint32_t*(__fastcall* InputEntry_t)(uintptr_t context, void* edx, int action);
const InputEntry_t InputEntry = (InputEntry_t)0x00984140;

const int kHudPopupActions[] = { kActEquipRight, kActEquipLeft, kActEquipDual, kActEquipTwoHanded, 0x11 /* NEXT_WEAPON */, 0x12 /* PREV_WEAPON */ };

bool Held(uint32_t state)
{
    return state == 2 || state == 3;
}

bool PanelInputHeld(uintptr_t manager)
{
    uintptr_t p1 = *(uintptr_t*)(manager + kManagerPlayerSlot);
    uintptr_t steering = p1 ? *(uintptr_t*)(p1 + kPlayerSteering) : 0;
    uintptr_t context = steering ? *(uintptr_t*)(steering + kSteeringInput) : 0;
    auto p1State = [&](int action) { return context ? *((uint8_t*)InputEntry(context, nullptr, action) + 3) & 7u : 0u; };
    auto p2State = [](int action) { return (g_p2Input.entries[action][0] >> 24) & 7; };
    for (int action : kHudPopupActions)
        if (p1State(action) == 2) NoteHudPopup(0);
    for (int action : kHudPanelActions)
        if (Held(p2State(action)) || Held(p1State(action))) return true;
    return false;
}

bool HudPopupUp()
{
    return (LONG)(g_hudPopupUntil - GetTickCount()) > 0;
}

bool HudPopupReads()
{
    return (LONG)(g_hudPopupReadsUntil - GetTickCount()) > 0;
}

// GetWeaponInfo's own function (FUN_00587B80: the manager's player; hand, weapon id, three more ints,
// enabled). An event for a hand not in use clears the panel it shares with the others (the right
// hand's with the two-handed one).
typedef void(__fastcall* WeaponInfo_t)(void* self, void* edx, int hand, int* id, int* a, int* b, int* c, bool* enabled);
const WeaponInfo_t WeaponInfo = (WeaponInfo_t)0x00587B80;

bool HandInUse(uintptr_t manager, uintptr_t player, int hand)
{
    uintptr_t* slot = (uintptr_t*)(manager + kManagerPlayerSlot);
    uintptr_t p1 = *slot;
    int id = -1, a, b, c;
    bool enabled = false;
    *slot = player;
    __try {
        WeaponInfo(nullptr, nullptr, hand, &id, &a, &b, &c, &enabled);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    *slot = p1;
    return id != -1 && enabled;
}

// The script keeps one set of values: it re-reads them for this half through these events, which
// run later in this same frame.
void RefreshHudPanels(uintptr_t manager, uintptr_t player)
{
    void* gui = *(void**)kGuiEvents;
    if (!gui) return;
    FireGuiEvent(gui, nullptr, kGuiHealthChanged, kGuiEventContext);
    // The marker script shows the indicators only after this event, and takes how many from
    // GetNumberOfTargetIndicators then (the game sends it with Player 1's count when one turns on
    // or off; with none near him it sent the hide event, and a crate's icon by Player 2 never came).
    // Every frame, so each half gets its own count.
    if (g_proximityPerPlayer) FireGuiEvent(gui, nullptr, kGuiIndicatorsShow, kGuiEventContext);
    DWORD now = GetTickCount();
    if (PanelInputHeld(manager)) g_hudPanelsUntil = now + kHudPanelMs;
    SplitRenderHidePopup(HudPopupUp() ? 1 - g_hudPopupOwner : -1);
    if ((LONG)(g_hudPanelsUntil - now) <= 0 || HudPopupReads()) return;
    for (int i = 0; i < kGuiHands; i++) {
        if (!((g_hudWeaponMask | g_hudAmmoMask) >> i & 1) || !HandInUse(manager, player, i)) continue;
        if (g_hudWeaponMask >> i & 1) FireGuiEvent(gui, nullptr, kGuiWeaponChanged[i], kGuiEventContext);
        if (g_hudAmmoMask >> i & 1) FireGuiEvent(gui, nullptr, kGuiAmmoChanged[i], kGuiEventContext);
    }
}

// Two places in the scripts assume one player and are patched in the loaded bytecode (Lua 5.0, the
// Proto's code array) while the split is on:
// - get_ammo_data: ammo above the last read shows a "+N" pickup popup; values alternating between
//   the players are not pickups, so the difference stays 0 (the SUB becomes LOADK of its zero).
// - the health ring low-pass (white_meter += (target - white_meter) * dt * 5): it averaged the
//   players; the factor (MUL dt, 5) becomes LOADK 1, so the meter follows each half's value.
struct LuaOp { uint8_t op, a; };
enum : uint8_t {
    kLuaMove = 0, kLuaLoadK = 1, kLuaLoadBool = 2, kLuaGetGlobal = 5, kLuaGetTable = 6, kLuaSetTable = 9, kLuaSelf = 11,
    kLuaAdd = 12, kLuaSub = 13, kLuaMul = 14, kLuaJmp = 20, kLuaEq = 21, kLuaLt = 22, kLuaCall = 25,
};
const LuaOp kAmmoDataCode[] = {
    { kLuaLoadK, 2 }, { kLuaLoadK, 3 }, { kLuaLoadK, 4 }, { kLuaGetTable, 5 }, { kLuaGetTable, 6 }, { kLuaLoadBool, 7 },
    { kLuaEq, 0 }, { kLuaJmp, 0 }, { kLuaGetGlobal, 6 }, { kLuaSelf, 6 }, { kLuaMove, 8 }, { kLuaCall, 6 }, { kLuaMove, 4 },
    { kLuaMove, 3 }, { kLuaJmp, 0 }, { kLuaLoadK, 3 }, { kLuaEq, 0 }, { kLuaJmp, 0 }, { kLuaLoadK, 4 }, { kLuaGetTable, 6 },
    { kLuaGetTable, 6 }, { kLuaEq, 1 }, { kLuaJmp, 0 }, { kLuaGetTable, 6 }, { kLuaGetTable, 6 }, { kLuaLt, 0 }, { kLuaJmp, 0 },
    { kLuaGetTable, 6 }, { kLuaGetTable, 6 }, { kLuaSub, 2 },
};
const int kAmmoDataSub = 29;
const LuaOp kHealthRingCode[] = {
    { kLuaGetGlobal, 7 }, { kLuaSelf, 7 }, { kLuaCall, 7 }, { kLuaMove, 6 }, { kLuaMul, 7 }, { kLuaGetTable, 8 }, { kLuaGetTable, 9 },
    { kLuaMul, 10 }, { kLuaAdd, 9 }, { kLuaGetTable, 10 }, { kLuaSub, 9 }, { kLuaMul, 9 }, { kLuaAdd, 8 }, { kLuaSetTable, 1 },
    { kLuaGetTable, 8 }, { kLuaGetTable, 9 }, { kLuaSub, 9 }, { kLuaSetTable, 8 }, { kLuaLt, 0 },
};
const int kHealthRingFactor = 4, kHealthRingOne = 18; // the instruction, and "health < 1" for the 1's constant
// The floating buttons (B / Y on a car): each frame their opacity and size ease toward shown or
// hidden (factors dt * 12 and dt * 19). Shown in one half only they alternated every frame and
// flickered at about a third; both factors become LOADK 1 (the loop's own "1.0" constant).
const uint8_t kLuaForLoop = 28;
const LuaOp kFloatingFadeCode[] = {
    { kLuaMul, 10 }, { kLuaMul, 11 }, { kLuaLoadK, 12 }, { kLuaLoadK, 13 }, { kLuaLoadK, 14 }, { kLuaSub, 12 }, { kLuaJmp, 0 },
};
const int kFloatingFadeOne = 3; // LOADK r13 1.0
const uint32_t kLuaKBase = 250;
inline uint32_t LuaLoadK(uint32_t a, uint32_t k) { return kLuaLoadK | (k << 6) | (a << 24); }

struct ScriptPatch { uint32_t* at; uint32_t original, patched; };
const int kMaxScriptPatches = 32;
ScriptPatch g_scriptPatches[kMaxScriptPatches];
volatile LONG g_scriptPatchCount;
volatile bool g_scriptPatchesOn;

template <size_t N> bool MatchLuaCode(const uint32_t* code, const LuaOp (&ops)[N])
{
    for (size_t i = 0; i < N; i++)
        if ((code[i] & 63) != ops[i].op || (code[i] >> 24) != ops[i].a) return false;
    return true;
}

void AddScriptPatch(uint32_t* at, uint32_t patched)
{
    for (int i = 0; i < g_scriptPatchCount; i++)
        if (g_scriptPatches[i].at == at) return;
    if (g_scriptPatchCount == kMaxScriptPatches) return;
    g_scriptPatches[g_scriptPatchCount] = { at, *at, patched };
    InterlockedIncrement(&g_scriptPatchCount);
}

// Background scan, every word of the writable heap: found with the HUD passes.
void FindScriptPatches(const uint32_t* p)
{
    if ((*p & 63) == kLuaLoadK && (*p >> 24) == 2 && MatchLuaCode(p, kAmmoDataCode))
        AddScriptPatch((uint32_t*)p + kAmmoDataSub, LuaLoadK(2, (*p >> 6) & 0x3FFFF));
    else if ((*p & 63) == kLuaGetGlobal && (*p >> 24) == 7 && MatchLuaCode(p, kHealthRingCode))
        AddScriptPatch((uint32_t*)p + kHealthRingFactor, LuaLoadK(7, ((p[kHealthRingOne] >> 6) & 511) - kLuaKBase));
    else if ((*p & 63) == kLuaMul && (*p >> 24) == 10 && (p[-1] & 63) == kLuaForLoop && (p[-1] >> 24) == 10 &&
             MatchLuaCode(p, kFloatingFadeCode)) {
        uint32_t one = (p[kFloatingFadeOne] >> 6) & 0x3FFFF;
        AddScriptPatch((uint32_t*)p, LuaLoadK(10, one));
        AddScriptPatch((uint32_t*)p + 1, LuaLoadK(11, one));
    }
}

// Game tick: on while the split is on, the game's own code otherwise. A load frees the scripts: the
// list is dropped when one no longer holds what was written (the scan fills it again).
void KeepScriptPatches(bool on)
{
    for (int i = 0; i < g_scriptPatchCount; i++) {
        ScriptPatch& s = g_scriptPatches[i];
        __try {
            if (*s.at != s.original && *s.at != s.patched) { g_scriptPatchCount = 0; return; }
            *s.at = on ? s.patched : s.original;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_scriptPatchCount = 0;
            return;
        }
    }
    g_scriptPatchesOn = on;
}

void UpdatePromptPanel(); // the button prompt panel's owner, below

// Once per game frame, from the HUD script's first call: which half it runs for (the view the main
// camera last derived), and the marker distances for that half.
void BeginHudFrame(uintptr_t manager)
{
    g_markerP2 = false;
    CameraMatrix projecting, view0, view1;
    if (ReadMainCameraDerived(&projecting) && SplitViewMatrix(0, &view0) && SplitViewMatrix(1, &view1)) {
        float d0 = 0, d1 = 0;
        for (int i = 0; i < 3; i++) {
            d0 += (projecting.position[i] - view0.position[i]) * (projecting.position[i] - view0.position[i]);
            d1 += (projecting.position[i] - view1.position[i]) * (projecting.position[i] - view1.position[i]);
        }
        g_markerP2 = d1 < d0;
        g_markerHalves[g_markerP2]++;
        uintptr_t* slot = (uintptr_t*)(manager + kManagerPlayerSlot);
        uintptr_t p1 = *slot;
        if (g_markerP2) *slot = g_player;
        UpdateMarkerDistances();
        *slot = p1;
        RefreshHudPanels(manager, g_markerP2 ? g_player : p1); // the player this frame's weapon reads are for
    }
    UpdatePromptPanel();
}

// The HUD script's CLuaPlayer functions (health ring, weapon panels, ammo, the pickup prompt) read
// the manager's player: on Player 2's frames they run with him there. Their Lua closures are
// redirected like the markers' above.
const uintptr_t kHudLuaFunctions[] = {
    0x00A33930, // GetMaxHealth
    0x00A339D0, // GetHealth
    0x00A33A70, // GetResistanceUnits
    0x00A33B40, // GetAirSupplyUnits
    0x00A30310, // GetWeaponName
    0x00A3A2A0, // GetWeaponInfo
    0x00A3A4C0, // GetAmmoInfo
    0x00A3A620, // GetWeaponPosition
    0x00A35E10, // GetPickupWeaponInfo
    0x00A39B90, // GetFlareStatus
    0x00A3A9B0, // GetThrustMeter
    0x00A33CC0, // GetBonePosition
    // The crosshair script, every frame: colour (aim control +0xA8: red on a target), spread, and
    // the spinning grapple ring (CanGrappleTarget returns the global byte 0x01197A78, which each
    // player's brain think sets in turn, FUN_0075FA20: the later one showed in both halves).
    0x00A33880, // GetCrosshairLook
    0x00A34410, // GetAccuracy
    0x00A39AD0, // CanGrappleTarget
};
const int kHudLuaFunctionCount = sizeof(kHudLuaFunctions) / sizeof(kHudLuaFunctions[0]);
const int kHudResistance = 2;                  // GetResistanceUnits: +0x1B0 of the +0x1B4[+0x23C] maximum
const int kHudFirstWeapon = 4, kHudBonePosition = 11, kHudFirstCrosshair = 12, kHudCanGrapple = 14;
const uintptr_t kPlayerResistance = 0x1B0, kPlayerResistanceIndex = 0x23C;
volatile bool g_hudPerPlayer = true; // "p2hud on|off" (testing)
volatile bool g_hudLog;              // "p2hud log" / "p2hud nolog" (testing)
volatile bool g_hudPopupFixed = true; // "p2hud popup <x> <y>" / "p2hud popup off" (testing)
float g_hudPopupX = -190.0f, g_hudPopupY = -40.0f; // centred screen units
typedef void(__cdecl* LuaPushNumber_t)(void* L, float value);
const LuaPushNumber_t LuaPushNumber = (LuaPushNumber_t)0x00A9FB20;
typedef float(__cdecl* LuaToNumber_t)(void* L, int index, float fallback);
const LuaToNumber_t LuaToNumber = (LuaToNumber_t)0x00A9EE90;
LONG g_hudCalls[kHudLuaFunctionCount][2]; // testing: calls per function for each half

template <int I> int __cdecl HudLuaHook(void* L)
{
    uintptr_t manager = Manager();
    long frame = SplitRenderGameFrames();
    bool active = g_hudPerPlayer && g_driving && g_player && manager && SplitscreenActive();
    if (active && frame != g_markerFrame) {
        g_markerFrame = frame;
        BeginHudFrame(manager);
    }
    // The ring's health shows one frame later, in the frame of the other view (as for the minimap):
    // an event sets it after that frame's update used it. The weapon panels are written by the
    // events and show in this frame, and so does the resistance part of the ring, which the update
    // reads itself (it took the other player's: Player 1 being shot lowered Player 2's ring).
    bool sameFrame = I >= kHudFirstWeapon || I == kHudResistance;
    bool forPlayer2 = active && (sameFrame ? g_markerP2 : !g_markerP2);
    // Every frame the popup moves (smoothed) toward the player's bone on the screen: with the two
    // cameras' values alternating it wandered off, the more the further they faced apart. In the
    // split it stays at one spot beside the character, which sits about still on the screen.
    if (I == kHudBonePosition && active && g_hudPopupFixed) {
        LuaPushNumber(L, g_hudPopupX);
        LuaPushNumber(L, g_hudPopupY);
        return 2;
    }
    bool popup = active && I >= kHudFirstWeapon && I < kHudFirstCrosshair && HudPopupReads();
    if (popup) forPlayer2 = g_hudPopupOwner == 1;
    if (active) g_hudCalls[I][forPlayer2]++;
    uintptr_t* slot = (uintptr_t*)(manager + kManagerPlayerSlot);
    // Another hook may be running on another thread: if he is already there, it restores the slot.
    bool swap = forPlayer2 && *slot != g_player;
    uintptr_t p1 = swap ? *slot : 0;
    if (swap) *slot = g_player;
    // Player 2's meter (+0x1B0) is kept by RegeneratePlayer2 and drained by hits like Player 1's,
    // so the ring reads it as it is. (It used to be shown full for every read, from before
    // RegeneratePlayer2: user saw his ring stay up when a hit killed him.)
    float* meter = nullptr;
    float savedMeter = 0.0f;
    uint8_t grapple = 0;
    if (I == kHudCanGrapple && active) {
        grapple = *kCanGrappleTarget;
        *kCanGrappleTarget = g_canGrapple[forPlayer2];
    }
    int results = ((LuaCFunction_t)kHudLuaFunctions[I])(L);
    if (I == kHudCanGrapple && active) *kCanGrappleTarget = grapple;
    if (g_hudLog && I == kHudBonePosition)
        Log("HUD bone position %.3f %.3f", LuaToNumber(L, -2, 0.0f), LuaToNumber(L, -1, 0.0f));
    if (meter) *meter = savedMeter;
    if (swap) *slot = p1;
    return results;
}

const LuaCFunction_t kHudLuaHooks[kHudLuaFunctionCount] = {
    HudLuaHook<0>, HudLuaHook<1>, HudLuaHook<2>, HudLuaHook<3>, HudLuaHook<4>, HudLuaHook<5>,
    HudLuaHook<6>, HudLuaHook<7>, HudLuaHook<8>, HudLuaHook<9>, HudLuaHook<10>, HudLuaHook<11>,
    HudLuaHook<12>, HudLuaHook<13>, HudLuaHook<14>,
};

// The hook for a CLuaPlayer function, 0 for others.
uintptr_t HudHookFor(uintptr_t function)
{
    if (function < 0x00A30000 || function >= 0x00A3B000) return 0;
    for (int i = 0; i < kHudLuaFunctionCount; i++)
        if (kHudLuaFunctions[i] == function) return (uintptr_t)kHudLuaHooks[i];
    return 0;
}

bool IsHudHook(uintptr_t function)
{
    for (int i = 0; i < kHudLuaFunctionCount; i++)
        if ((uintptr_t)kHudLuaHooks[i] == function) return true;
    return false;
}

// Proximity icons (pickups such as money crates, flag 0x20): a CDistanceTrigger (interface vtable
// 0x00E4C20C at +8, slot 7 FUN_005F0070 -> FUN_006EEFF0) compares the manager's player, i.e.
// Player 1, with its sphere (+0x70 centre, +0x3C radius; +0x38 bit 0 inside, bits 4/8/0x10 ignore
// x/y/z) and on entry/exit posts the messages in its lists at +0x8C/+0x90. The indicator's handler
// (+0x30 interface, FUN_00610580) matches them against its keys (list at +0x30+0x94 activates,
// +0x98 deactivates) and calls SetActive (vtable +0x94, FUN_00749AA0: bit 0 of +0x4C). So the icon
// showed in both halves while Player 1 was near. The triggers drive mission logic too, so they
// stay Player 1's; for Player 2's HUD frames bit 0 of each paired indicator is set from his own
// distance to its trigger, and put back after the call. The pairs (trigger values, not pointers:
// streaming frees triggers) come from the background HUD pass scan.
const uintptr_t kDistanceTriggerIface = 0x00E4C20C;
const uintptr_t kTriggerFlags = 0x38, kTriggerRadius = 0x3C, kTriggerCentre = 0x70, kTriggerEnter = 0x8C;
const uintptr_t kIndicatorFlags = 0x4C, kIndicatorActivateKeys = 0x30 + 0x94;
const uint8_t kIndicatorProximity = 0x20;
struct ProximityIcon {
    uintptr_t indicator;
    float centre[3];
    float radius;
    uint8_t ignoreAxes; // trigger flag bits 4, 8, 0x10
    uintptr_t pickup;   // the pickup's CShapeTrigger +0x40 interface at the same spot, if any
    uint8_t bit;        // the flag bit his distance sets: 1 (active), kIndicatorInRange for weapon boxes
};
// Weapon box icons (user: they didn't show for Player 2 far from Player 1) have no activate keys and
// stay active; they show while bit 0x08 is set, which the game sets for Player 1 only, within the
// 30 m CDistanceTrigger at the box. Paired by place (the box's icon trigger links the indicator).
const uint8_t kIndicatorInRange = 0x08;
const float kBoxIconRadius = 30.0f;
const int kMaxProximityIcons = 512;
ProximityIcon g_proximityIcons[2][kMaxProximityIcons];
volatile LONG g_proximityCount[2];
volatile LONG g_proximityCurrent; // which buffer the HUD reads

// The message list a trigger posts or an indicator matches: count in the low 6 bits of the first
// byte, the messages from +4.
bool SharesMessage(uintptr_t listA, uintptr_t listB)
{
    if (!listA || !listB) return false;
    int a = *(uint8_t*)listA & 0x3F, b = *(uint8_t*)listB & 0x3F;
    for (int i = 0; i < a; i++)
        for (int j = 0; j < b; j++)
            if (*(uintptr_t*)(listA + 4 + 4 * i) == *(uintptr_t*)(listB + 4 + 4 * j)) return true;
    return false;
}

uintptr_t PickupTriggerAt(const float* centre);
int PairBoxIcons(ProximityIcon* icons, int n, const uintptr_t* triggers, int triggerCount);

// Scan thread: pairs the proximity indicators with the triggers found.
void PairProximityIcons(const uintptr_t* triggers, int triggerCount)
{
    int next = 1 - g_proximityCurrent, n = 0;
    __try {
        int walked = 0;
        for (uintptr_t t = *(uintptr_t*)kTargetIndicatorList; t && walked < 4096 && n < kMaxProximityIcons;
             t = *(uintptr_t*)(t + kTargetIndicatorNext), walked++) {
            if (!(*(uint8_t*)(t + kIndicatorFlags) & kIndicatorProximity)) continue;
            uintptr_t keys = *(uintptr_t*)(t + kIndicatorActivateKeys);
            for (int i = 0; i < triggerCount; i++) {
                uintptr_t trigger = triggers[i];
                if (!SharesMessage(*(uintptr_t*)(trigger + kTriggerEnter), keys)) continue;
                ProximityIcon& p = g_proximityIcons[next][n++];
                p.indicator = t;
                memcpy(p.centre, (void*)(trigger + kTriggerCentre), sizeof(p.centre));
                p.radius = *(float*)(trigger + kTriggerRadius);
                p.ignoreAxes = *(uint8_t*)(trigger + kTriggerFlags) & 0x1C;
                p.pickup = PickupTriggerAt(p.centre);
                p.bit = 1;
                break;
            }
        }
        n = PairBoxIcons(g_proximityIcons[next], n, triggers, triggerCount);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    g_proximityCount[next] = n;
    InterlockedExchange(&g_proximityCurrent, next);
}

struct SavedIconBit {
    uintptr_t indicator;
    uint8_t flags;
    uint8_t mask; // the bits that were ours
};

bool PickupTriggerDisabled(uintptr_t pickup);

// Game thread, a HUD frame: on Player 2's each paired indicator active as if he were the one
// checked. On both players' frames an icon whose pickup was collected (its trigger disabled) is off:
// the game only switches it off for the player who was near it then.
int SetProximityIcons(bool forPlayer2, SavedIconBit* saved, int max)
{
    uintptr_t c = g_proximityPerPlayer && g_player ? Character(g_player) : 0;
    if (!c) return 0;
    const float* p2 = (const float*)(c + kCharacterPosition);
    int current = g_proximityCurrent, count = g_proximityCount[current], n = 0;
    const ProximityIcon* icons = g_proximityIcons[current];
    if (g_hudLog && forPlayer2) {
        static DWORD next;
        if ((LONG)(GetTickCount() - next) >= 0) {
            next = GetTickCount() + 1000;
            int boxIcons = 0, inRange = 0;
            float nearest = 1e9f;
            for (int i = 0; i < count; i++) {
                if (icons[i].bit != kIndicatorInRange) continue;
                boxIcons++;
                float d2 = 0;
                for (int k = 0; k < 3; k++) d2 += (icons[i].centre[k] - p2[k]) * (icons[i].centre[k] - p2[k]);
                if (d2 < icons[i].radius * icons[i].radius) inRange++;
                if (d2 < nearest) nearest = d2;
            }
            Log("Box icons for P2: %d of %d paired icons, %d in range, nearest %.0f m", boxIcons, count, inRange, sqrtf(nearest));
        }
    }
    int walked = 0;
    for (uintptr_t t = *(uintptr_t*)kTargetIndicatorList; t && walked < 4096 && n < max;
         t = *(uintptr_t*)(t + kTargetIndicatorNext), walked++) {
        uint8_t flags = *(uint8_t*)(t + kIndicatorFlags);
        if (!(flags & kIndicatorProximity)) continue;
        for (int i = 0; i < count; i++) {
            if (icons[i].indicator != t) continue;
            uint8_t want = flags;
            if (forPlayer2) {
                float d2 = 0;
                for (int k = 0; k < 3; k++) {
                    float d = (icons[i].ignoreAxes & (4 << k)) ? 0.0f : icons[i].centre[k] - p2[k];
                    d2 += d * d;
                }
                uint8_t bit = icons[i].bit;
                want = (flags & ~bit) | (d2 < icons[i].radius * icons[i].radius ? bit : 0);
            }
            if (icons[i].pickup && PickupTriggerDisabled(icons[i].pickup)) want &= ~1;
            if (want != flags) {
                saved[n++] = { t, flags, (uint8_t)(icons[i].bit | 1) };
                *(uint8_t*)(t + kIndicatorFlags) = want;
            }
            break;
        }
    }
    return n;
}

void RestoreProximityIcons(const SavedIconBit* saved, int n)
{
    for (int i = 0; i < n; i++) {
        uint8_t* flags = (uint8_t*)(saved[i].indicator + kIndicatorFlags);
        *flags = (*flags & ~saved[i].mask) | (saved[i].flags & saved[i].mask); // only these bits were ours
    }
}

// Pickups (weapon/armor/vehicle parts, money; CResourceItem vtable 0x00EBD42C, position +0x7C) are
// collected by walking into a CShapeTrigger at the item: its volume (FUN_0098AD10) hands each new
// overlapping object to the enter handler (+0x40 interface, vtable 0x00E31AA4 slot 4,
// FUN_005766A0), which asks the filter (slot 9, FUN_005014B0); for "player" triggers (flags +0x30 &
// 6) that is FUN_00601800: only the manager's player's character, i.e. Player 1. The same triggers
// guard mission areas, so only those at a pickup (centre +0xA4 of the object, within 1 m of an
// item) let Player 2 through: the filter runs again with him in the manager's player slot. The
// triggers get a private vtable copy and are found by the background HUD pass scan.
const uintptr_t kShapeTriggerIface = 0x00E31AA4; // CShapeTrigger(+40)
const uintptr_t kShapeTriggerIfaceOffset = 0x40, kShapeTriggerCentre = 0xA4;
const int kShapeTriggerFilterIndex = 9;
const uintptr_t kResourceItemVtable = 0x00EBD42C, kResourceItemPosition = 0x7C;
const int kShapeTriggerSlots = 10;
uintptr_t g_shapeTriggerVtable[1 + kShapeTriggerSlots];
typedef bool(__fastcall* ShapeTriggerFilter_t)(uintptr_t iface, void* edx, uintptr_t object);
ShapeTriggerFilter_t g_origShapeTriggerFilter;
const int kMaxPickupTriggers = 1024;
uintptr_t g_pickupTriggers[2][kMaxPickupTriggers]; // their +0x40 interfaces, sorted
volatile LONG g_pickupTriggerCount[2];
volatile LONG g_pickupTriggersCurrent;
volatile bool g_pickupsForPlayer2 = true; // "p2hud pickups on|off" (testing)
LONG g_pickupsLetThrough;                 // testing: P2 entries allowed

bool IsPickupTrigger(uintptr_t iface)
{
    int current = g_pickupTriggersCurrent;
    const uintptr_t* list = g_pickupTriggers[current];
    int lo = 0, hi = g_pickupTriggerCount[current] - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (list[mid] == iface) return true;
        if (list[mid] < iface) lo = mid + 1;
        else hi = mid - 1;
    }
    return false;
}

bool __fastcall ShapeTriggerFilterHook(uintptr_t iface, void* edx, uintptr_t object)
{
    if (g_origShapeTriggerFilter(iface, edx, object)) return true;
    uintptr_t manager = Manager();
    if (!g_pickupsForPlayer2 || !g_driving || !g_player || !manager || !object || object != Character(g_player) ||
        !IsPickupTrigger(iface))
        return false;
    uintptr_t* slot = (uintptr_t*)(manager + kManagerPlayerSlot);
    uintptr_t p1 = *slot;
    *slot = g_player;
    bool in = g_origShapeTriggerFilter(iface, edx, object);
    *slot = p1;
    if (in) InterlockedIncrement(&g_pickupsLetThrough);
    return in;
}

// Weapon boxes (user: they opened for Player 1 only) open from a shape trigger of the same kind,
// with no CResourceItem at it (the weapon inside is a CWeapon); unlike the pickups' and the mission
// areas' triggers, theirs links (+0x20) the box's proximity icon (CTargetIndicator, flags +0x4C).
const uintptr_t kShapeTriggerIndicator = 0x20;
const uintptr_t kTargetIndicatorVtable = 0x00EB8FA4;

bool IsWeaponBoxTrigger(uintptr_t iface)
{
    __try {
        uintptr_t indicator = *(uintptr_t*)(iface - kShapeTriggerIfaceOffset + kShapeTriggerIndicator);
        return indicator && *(uintptr_t*)indicator == kTargetIndicatorVtable &&
               (*(uint8_t*)(indicator + kIndicatorFlags) & kIndicatorProximity);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The box itself is a smart object (CWorldsimSpawnableSmartObject) whose CSmartObjectTrigger offers
// its CWeaponPickupAction to characters inside; that trigger's phantom is only in the physics world
// while the box is active, and the box is activated (message handler FUN_008CDFB0 -> FUN_008EF6E0)
// by another player-only shape trigger at it (no icon). So the shape triggers within 1.5 m of a
// box's icon trigger count as the box's too, and for Player 2 their entry (slot 4, FUN_005766A0,
// posts the messages) runs with him in the manager's player slot.
const float kBoxTriggerRadius = 1.5f;
uintptr_t g_boxTriggers[2][kMaxPickupTriggers]; // sorted, indexed like g_pickupTriggers
volatile LONG g_boxTriggerCount[2];

bool IsBoxTrigger(uintptr_t iface)
{
    int current = g_pickupTriggersCurrent;
    return std::binary_search(g_boxTriggers[current], g_boxTriggers[current] + g_boxTriggerCount[current], iface);
}

const int kShapeTriggerEnterIndex = 4;
typedef void(__fastcall* ShapeTriggerEnter_t)(uintptr_t iface, void* edx, uintptr_t object);
ShapeTriggerEnter_t g_origShapeTriggerEnter;

void __fastcall ShapeTriggerEnterHook(uintptr_t iface, void* edx, uintptr_t object)
{
    uintptr_t manager = Manager();
    if (!g_pickupsForPlayer2 || !g_driving || !g_player || !manager || !object || object != Character(g_player) ||
        !IsBoxTrigger(iface)) {
        g_origShapeTriggerEnter(iface, edx, object);
        return;
    }
    uintptr_t* slot = (uintptr_t*)(manager + kManagerPlayerSlot);
    uintptr_t p1 = *slot;
    *slot = g_player;
    __try {
        g_origShapeTriggerEnter(iface, edx, object);
    } __finally {
        *slot = p1;
    }
}

// Race checkpoints (user: Player 2 flying through a ring didn't count; wanted for any race). Each ring
// is a CCheckpointMarker (vtable 0x00EB9D3C, position +0xB4, drawing only) with player shape
// triggers centred on it (flags +0x30 & 6; FUN_00601800 also checks on foot / in a vehicle through
// the manager's player). Those triggers are handled like the weapon boxes': the filter and the entry
// (which posts the challenge's checkpoint message) run with Player 2 in the manager's player slot.
const uintptr_t kCheckpointMarkerVtable = 0x00EB9D3C, kCheckpointMarkerPosition = 0xB4;
const float kRingTriggerRadius = 1.0f;
volatile LONG g_ringTriggerCount; // testing: shape triggers found at rings in the last scan

// Scan thread: the shape triggers whose centre is at a pickup, the weapon boxes' and the race rings'.
void FindPickupTriggers(const uintptr_t* triggers, int triggerCount, const uintptr_t* items, int itemCount,
                        const uintptr_t* markers, int markerCount)
{
    int next = 1 - g_pickupTriggersCurrent, n = 0, boxes = 0, nb = 0, rings = 0;
    auto centre = [](uintptr_t iface) { return (const float*)(iface - kShapeTriggerIfaceOffset + kShapeTriggerCentre); };
    __try {
        const int kMaxBoxes = 64;
        const float* boxCentres[kMaxBoxes];
        for (int i = 0; i < triggerCount && boxes < kMaxBoxes; i++)
            if (IsWeaponBoxTrigger(triggers[i])) boxCentres[boxes++] = centre(triggers[i]);
        for (int i = 0; i < triggerCount && n < kMaxPickupTriggers; i++) {
            const float* c = centre(triggers[i]);
            bool box = false;
            for (int b = 0; b < boxes && !box; b++) {
                float dx = c[0] - boxCentres[b][0], dy = c[1] - boxCentres[b][1], dz = c[2] - boxCentres[b][2];
                box = dx * dx + dy * dy + dz * dz < kBoxTriggerRadius * kBoxTriggerRadius;
            }
            for (int m = 0; m < markerCount && !box; m++) {
                if (*(uintptr_t*)markers[m] != kCheckpointMarkerVtable) continue;
                const float* p = (const float*)(markers[m] + kCheckpointMarkerPosition);
                float dx = c[0] - p[0], dy = c[1] - p[1], dz = c[2] - p[2];
                if (dx * dx + dy * dy + dz * dz < kRingTriggerRadius * kRingTriggerRadius) {
                    box = true;
                    rings++;
                }
            }
            if (box) {
                g_pickupTriggers[next][n++] = triggers[i];
                g_boxTriggers[next][nb++] = triggers[i];
                continue;
            }
            for (int j = 0; j < itemCount; j++) {
                const float* p = (const float*)(items[j] + kResourceItemPosition);
                float dx = c[0] - p[0], dy = c[1] - p[1], dz = c[2] - p[2];
                if (dx * dx + dy * dy + dz * dz < 1.0f) {
                    g_pickupTriggers[next][n++] = triggers[i];
                    break;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    std::sort(g_pickupTriggers[next], g_pickupTriggers[next] + n);
    std::sort(g_boxTriggers[next], g_boxTriggers[next] + nb);
    g_pickupTriggerCount[next] = n;
    g_boxTriggerCount[next] = nb;
    if (rings != g_ringTriggerCount) Log("Race rings: %d shape triggers at %d checkpoint markers let player 2 in", rings, markerCount);
    g_ringTriggerCount = rings;
    InterlockedExchange(&g_pickupTriggersCurrent, next);
}

// Scan thread, after FindPickupTriggers: the weapon boxes' icons (see kIndicatorInRange) after the
// first n paired ones; the radius from the box's CDistanceTrigger. Returns the new count.
int PairBoxIcons(ProximityIcon* icons, int n, const uintptr_t* triggers, int triggerCount)
{
    int boxes = g_pickupTriggersCurrent;
    for (int b = 0; b < g_boxTriggerCount[boxes] && n < kMaxProximityIcons; b++) {
        uintptr_t iface = g_boxTriggers[boxes][b];
        if (!IsWeaponBoxTrigger(iface)) continue;
        ProximityIcon& p = icons[n++];
        p.indicator = *(uintptr_t*)(iface - kShapeTriggerIfaceOffset + kShapeTriggerIndicator);
        memcpy(p.centre, (void*)(iface - kShapeTriggerIfaceOffset + kShapeTriggerCentre), sizeof(p.centre));
        p.radius = kBoxIconRadius;
        p.ignoreAxes = 0;
        for (int i = 0; i < triggerCount; i++) {
            const float* c = (const float*)(triggers[i] + kTriggerCentre);
            float dx = c[0] - p.centre[0], dy = c[1] - p.centre[1], dz = c[2] - p.centre[2];
            if (dx * dx + dy * dy + dz * dz < kBoxTriggerRadius * kBoxTriggerRadius) {
                p.radius = *(float*)(triggers[i] + kTriggerRadius);
                p.ignoreAxes = *(uint8_t*)(triggers[i] + kTriggerFlags) & 0x1C;
                break;
            }
        }
        p.pickup = 0;
        p.bit = kIndicatorInRange;
    }
    return n;
}

// Scan thread, after FindPickupTriggers: the pickup trigger centred within 1 m of a point, 0 if none.
uintptr_t PickupTriggerAt(const float* centre)
{
    int current = g_pickupTriggersCurrent;
    for (int i = 0; i < g_pickupTriggerCount[current]; i++) {
        uintptr_t iface = g_pickupTriggers[current][i];
        const float* c = (const float*)(iface - kShapeTriggerIfaceOffset + kShapeTriggerCentre);
        float dx = c[0] - centre[0], dy = c[1] - centre[1], dz = c[2] - centre[2];
        if (dx * dx + dy * dy + dz * dz < 1.0f) return iface;
    }
    return 0;
}

// Collecting a pickup disables its trigger (+0x30 bit 0 of the interface). Only while the object
// still has our vtable copy (streaming frees triggers between scans).
bool PickupTriggerDisabled(uintptr_t pickup)
{
    __try {
        if (*(uintptr_t*)pickup != (uintptr_t)&g_shapeTriggerVtable[1]) return false;
        return !(*(uint16_t*)(pickup + 0x30) & 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// HUD units: the half shows y -180..180 of -360..360. Less the marker's own half height (icon and
// label ~20 units): a marker part over the edge comes back inside the half through the HUD strip
// (user: still seen when half out of frame).
const float kBandHalfY = 180.0f - 20.0f;
volatile bool g_markersInBand = true;    // "p2hud band on|off" (testing)

// Both indicator functions pick the shown indicators by walking the list (FUN_00789650 counts,
// FUN_0068E300 finds the n-th). Indicators with flag 0x80 (+0x4C; a destructible's name and health
// bar, "Statue Torso") show only while the manager's player aims at their target (FUN_00769DE0:
// player +0x184 aim control): on Player 2's frames he is put there, so each half shows the label
// for its own player's aim.
template <uintptr_t Function> int __cdecl TargetIndicatorHook(void* L)
{
    uintptr_t manager = Manager();
    long frame = SplitRenderGameFrames();
    bool active = g_markersPerPlayer && g_driving && g_player && manager && SplitscreenActive();
    if (active && frame != g_markerFrame) {
        g_markerFrame = frame;
        BeginHudFrame(manager);
    }
    // The projection (FUN_0079FC10 -> FUN_0094B590) first moves the point in front of the main
    // camera's world (+0x24), which is Player 1's logic camera on Player 2's frames: his markers
    // moved when Player 1 moved or turned. His view goes there for the call.
    CameraMatrix view, saved;
    bool forPlayer2 = active && g_markerP2;
    bool swapped = forPlayer2 && Function == kTargetIndicatorInfo && SplitViewMatrix(1, &view) &&
                   SwapMainCameraWorld(view, &saved);
    uintptr_t* slot = (uintptr_t*)(manager + kManagerPlayerSlot);
    // Another hook may be running on another thread: if he is already there, it restores the slot.
    bool swapPlayer = forPlayer2 && *slot != g_player;
    uintptr_t p1 = swapPlayer ? *slot : 0;
    if (swapPlayer) *slot = g_player;
    SavedIconBit icons[64];
    int iconCount = active ? SetProximityIcons(forPlayer2, icons, 64) : 0;
    int results = ((LuaCFunction_t)Function)(L);
    // Each half shows the full frame's middle rows only (y -180..180 of the HUD's -360..360). A
    // marker above or below that (user: it showed again near the top of the half, from the HUD
    // strip) is hidden like the game hides one off the whole screen: its last result, the opacity
    // factor, is 0. Lua 5.0 stack: L->top at +8, 8-byte objects {tt, value}, numbers are floats (tt 3).
    if (active && Function == kTargetIndicatorInfo && results >= 9 && g_markersInBand) {
        float y = LuaToNumber(L, -8, 0.0f);
        if (y > kBandHalfY || y < -kBandHalfY) {
            uint8_t* top = *(uint8_t**)((uintptr_t)L + 8);
            if (*(int*)(top - 8) == 3 && *(float*)(top - 4) == LuaToNumber(L, -1, -1.0f)) *(float*)(top - 4) = 0.0f;
        }
    }
    if (g_hudLog && Function == kTargetIndicatorCount && results >= 1) {
        static DWORD next[2];
        DWORD now = GetTickCount();
        if ((LONG)(now - next[forPlayer2]) >= 0) {
            next[forPlayer2] = now + 1000;
            Log("Markers %s: %.0f shown (cap [0x0106B55C] %d)", forPlayer2 ? "P2" : "P1", LuaToNumber(L, -1, -1.0f),
                *(int*)0x0106B55C);
        }
    }
    if (g_hudLog && Function == kTargetIndicatorInfo && results >= 9) {
        static DWORD next[2];
        DWORD now = GetTickCount();
        if ((LONG)(now - next[forPlayer2]) >= 0) {
            next[forPlayer2] = now + 500;
            Log("Marker %s: x %.1f y %.1f z %.2f  dist %.0f visible %.0f alpha %.2f", forPlayer2 ? "P2" : "P1",
                LuaToNumber(L, -9, 0.0f), LuaToNumber(L, -8, 0.0f), LuaToNumber(L, -7, 0.0f), LuaToNumber(L, -3, 0.0f),
                LuaToNumber(L, -2, 0.0f), LuaToNumber(L, -1, 0.0f));
        }
    }
    RestoreProximityIcons(icons, iconCount);
    if (swapPlayer) *slot = p1;
    if (swapped) RestoreMainCameraWorld(saved, view);
    return results;
}

// --- Button prompts per player -----------------------------------------------------------------
// The floating buttons on objects (B to enter a car, Y for the stunt jump) and the panel at the
// right edge ("Stunt jump B") come from one double-buffered block of the HUD object [0x0118FC20]
// at +0x1C0. Each world update, Player 1's HUD update (FUN_00792510) walks his character's context
// actions (vector at character +0x314, 12-byte entries {action id, shared_ptr}) and has
// FUN_00624920(hud, &entry.action, id) write the prompt of each into the write half (index byte
// +0 & 1); the GUI update flips the halves (FUN_0083D520: clears the new write half, sends
// gui_facebuttons_changed when the strings differ), and the script reads the other half with
// Game:GetFloatingButton / GetStuntButtonInfo (every frame) and GetFacebuttonString (on that event).
// So both halves showed Player 1's prompts. Player 2's prompts are written the same way into a
// private copy of the HUD object (Player2UpdatePrompts, main thread, right after Player 1's), and
// the readers swap his read half in on his frames. The panel animates on its own (fade in, 14 s
// timer), so it can't alternate per frame: it shows one player's prompts, Player 1's when he has
// any, else Player 2's, and the other half hides it (SplitRenderHideFacePanel).
// Block layout, b = half: +1+b stunt shown, +3+b floating shown, +5+b struggle, +8+4b struggle
// index, ints (4+b+2k)*4 and strings (4+b+2k)*0x1C for the 6 panel rows k (std::string, 0x1C),
// +0x40+0xC*b stunt position, +0x58+0xC*b floating position.
const uintptr_t kHudPromptObject = 0x0118FC20;
const uintptr_t kPromptBlock = 0x1C0, kPromptBlockSize = 0x1C0, kPromptObjectSize = kPromptBlock + kPromptBlockSize;
const uintptr_t kGetStuntButtonInfo = 0x00A29540, kGetFacebuttonString = 0x00A29740, kGetFloatingButton = 0x00A31E00;
const uintptr_t kGetFaceButtonKey = 0x00A36680; // the panel rows' keys (keyboard), the block's ints
const int kPromptRows = 6;
typedef void(__fastcall* PromptWrite_t)(void* hud, void* edx, void* action, uint32_t id); // FUN_00624920, ret 8
const PromptWrite_t PromptWrite = (PromptWrite_t)0x00624920;
typedef void*(__fastcall* WeakLock_t)(void* weak, void* edx, void* out);                   // FUN_006D4A10
const WeakLock_t WeakLock = (WeakLock_t)0x006D4A10;
typedef void(__fastcall* ReleaseShared_t)(void* shared);                                   // FUN_00555BB0
const ReleaseShared_t ReleaseShared = (ReleaseShared_t)0x00555BB0;
typedef bool(__fastcall* CanShowPrompts_t)(void* self);                                    // FUN_008A1860
const CanShowPrompts_t CanShowPrompts = (CanShowPrompts_t)0x008A1860;
StringAssignC_t g_stringAssign;
const char kGuiFacebuttonsChanged[] = "gui_facebuttons_changed";

volatile bool g_promptsPerPlayer = true; // "p2hud prompts on|off" (testing)
uint8_t* g_p2Prompts;                    // Player 2's copy of the HUD object (kPromptObjectSize)
uint8_t* g_blankPrompts;                 // a block with no rows: the panel reads it once on an owner change
// Each player's panel rows as last seen (half 0). Player 1's own rows come and go every other frame
// while the split is on (his context actions are re-evaluated on the other view's frames too), so
// the panel reads these, kept for kPromptHoldMs after they were last there.
uint8_t* g_heldPrompts[2];
DWORD g_heldPromptsAt[2];
char g_heldPromptsText[2][kPromptRows * 32];
const DWORD kPromptHoldMs = 400;
volatile bool g_promptBlankPending;
long g_promptBlankFrame = -1;
volatile LONG g_promptLock;
volatile bool g_p2PromptsValid;
long g_p2PromptWrites, g_p2PromptActions; // testing
int g_promptPanelOwner = -1;             // whose prompts the panel shows (-1: nobody's)
char g_promptPanelShown[kPromptRows * 32];   // the strings the panel script last read
char g_promptPanelWanted[kPromptRows * 32];

// Player 2's copy is written on the main thread and swapped in by the readers on the GUI thread.
void LockPrompts() { while (InterlockedCompareExchange(&g_promptLock, 1, 0)) YieldProcessor(); }
void UnlockPrompts() { InterlockedExchange(&g_promptLock, 0); }

inline uint8_t* PromptString(uint8_t* block, int half, int row) { return block + (4 + half + 2 * row) * 0x1C; }
inline const char* StringText(const uint8_t* s)
{
    return *(const uint32_t*)(s + 0x18) < 16 ? (const char*)(s + 4) : *(const char* const*)(s + 4);
}

void SwapBytes(uint8_t* a, uint8_t* b, size_t n)
{
    for (size_t i = 0; i < n; i++) { uint8_t t = a[i]; a[i] = b[i]; b[i] = t; }
}

// Swaps one half of two blocks: the floating / stunt buttons or the panel rows. Strings move by
// their bytes (std::string has no pointer to itself), so ownership moves with them.
void SwapPromptHalf(uint8_t* a, int ha, uint8_t* b, int hb, bool rows)
{
    if (!rows) {
        for (int f = 1; f <= 5; f += 2) SwapBytes(a + f + ha, b + f + hb, 1);
        SwapBytes(a + 8 + 4 * ha, b + 8 + 4 * hb, 4);
        SwapBytes(a + 0x40 + 0xC * ha, b + 0x40 + 0xC * hb, 0xC);
        SwapBytes(a + 0x58 + 0xC * ha, b + 0x58 + 0xC * hb, 0xC);
        return;
    }
    for (int k = 0; k < kPromptRows; k++) {
        SwapBytes(a + (4 + ha + 2 * k) * 4, b + (4 + hb + 2 * k) * 4, 4);
        SwapBytes(PromptString(a, ha, k), PromptString(b, hb, k), 0x1C);
    }
}

// The panel rows of a block's read half, joined (empty: no prompt).
void PromptRowsText(uint8_t* block, char* out, size_t size)
{
    int read = ~block[0] & 1;
    out[0] = 0;
    bool any = false;
    for (int k = 0; k < kPromptRows; k++) {
        const char* s = StringText(PromptString(block, read, k));
        if (strcmp(s, "-1")) any = true;
        strncat_s(out, size, s, _TRUNCATE);
        strncat_s(out, size, "|", _TRUNCATE);
    }
    if (!any) out[0] = 0;
}

bool InitPlayer2Prompts()
{
    if (g_p2Prompts) return true;
    if (!g_stringAssign) {
        g_stringAssign = StringAssignC();
        if (!g_stringAssign) {
            static bool logged;
            if (!logged) Log("Prompts: no std::string assignment in msvcp80.dll, prompts stay shared");
            logged = true;
            return false;
        }
    }
    uint8_t* object = (uint8_t*)calloc(1, kPromptObjectSize);
    uint8_t* blank = (uint8_t*)calloc(1, kPromptBlockSize);
    uint8_t* held0 = (uint8_t*)calloc(1, kPromptBlockSize);
    uint8_t* held1 = (uint8_t*)calloc(1, kPromptBlockSize);
    if (!object || !blank || !held0 || !held1) {
        free(object);
        free(blank);
        free(held0);
        free(held1);
        return false;
    }
    g_heldPrompts[0] = held0;
    g_heldPrompts[1] = held1;
    for (uint8_t* block : { object + kPromptBlock, blank, held0, held1 })
        for (int half = 0; half < 2; half++)
            for (int k = 0; k < kPromptRows; k++) {
                uint8_t* s = PromptString(block, half, k);  // a short std::string "-1" (in place)
                memcpy(s + 4, "-1", 3);
                *(uint32_t*)(s + 0x14) = 2;
                *(uint32_t*)(s + 0x18) = 15;
                *(int32_t*)(block + (4 + half + 2 * k) * 4) = -1;
            }
    g_blankPrompts = blank;
    g_p2Prompts = object;
    return true;
}

// Like the GUI update's flip (FUN_0083D520), without its event: the halves trade places and the new
// write half is cleared.
void FlipPlayer2Prompts(uint8_t* block)
{
    block[0] ^= 1;
    int write = block[0] & 1;
    block[1 + write] = 0;
    block[3 + write] = 0;
    for (int k = 0; k < kPromptRows; k++) {
        g_stringAssign(PromptString(block, write, k), "-1");
        *(int32_t*)(block + (4 + write + 2 * k) * 4) = -1;
    }
}

// Under the lock. Returns false on a fault.
bool WritePlayer2Prompts(uint8_t* hud, uintptr_t character)
{
    __try {
        // The writer reads a few of the HUD object's own fields (+0x15D enabled, its key lookup):
        // Player 1's, fresh each frame.
        memcpy(g_p2Prompts, hud, kPromptBlock);
        FlipPlayer2Prompts(g_p2Prompts + kPromptBlock);
        if (!(*(float*)(character + 0xE8) > 0.0f)) return true;
        // As FUN_00792510: only with the character's +0x2F0 (weak) alive and FUN_008A1860 agreeing.
        uintptr_t shared[2] = {};
        WeakLock((void*)(character + 0x2F0), nullptr, shared);
        if (shared[0] && CanShowPrompts((void*)(character + 0xF0))) {
            uint8_t* entry = *(uint8_t**)(character + 0x318);
            uint8_t* end = *(uint8_t**)(character + 0x31C);
            for (int n = 0; entry && entry < end && n < 64; entry += 12, n++) {
                PromptWrite(g_p2Prompts, nullptr, entry + 4, *(uint32_t*)entry);
                g_p2PromptActions++;
            }
        }
        if (shared[1]) ReleaseShared(shared);
        g_p2PromptWrites++;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Player2UpdatePrompts()
{
    if (!g_promptsPerPlayer || !g_driving || !g_player || !SplitscreenActive()) {
        g_p2PromptsValid = false;
        return;
    }
    uint8_t* hud = *(uint8_t**)kHudPromptObject;
    uintptr_t character = *(uintptr_t*)(g_player + 0x48);
    if (!hud || !character || !InitPlayer2Prompts()) return;
    LockPrompts();
    bool ok = WritePlayer2Prompts(hud, character);
    g_p2PromptsValid = ok;
    UnlockPrompts();
    if (!ok) {
        static bool logged;
        if (!logged) Log("Prompts: fault writing Player 2's prompts");
        logged = true;
    }
}

// Under the lock.
bool ReadPromptRows(uint8_t* hud, char* p1, char* p2, size_t size)
{
    __try {
        if (hud) PromptRowsText(hud + kPromptBlock, p1, size);
        if (g_p2Prompts) PromptRowsText(g_p2Prompts + kPromptBlock, p2, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Under the lock: a block's read half rows into a held block (half 0).
bool HoldPromptRows(uint8_t* held, uint8_t* block)
{
    __try {
        int read = ~block[0] & 1;
        for (int k = 0; k < kPromptRows; k++) {
            g_stringAssign(PromptString(held, 0, k), StringText(PromptString(block, read, k)));
            *(int32_t*)(held + (4 + 2 * k) * 4) = *(int32_t*)(block + (4 + read + 2 * k) * 4);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool PromptsActive()
{
    return g_promptsPerPlayer && g_p2PromptsValid && g_p2Prompts && g_driving && g_player && SplitscreenActive();
}

// GUI thread, once per HUD frame: whose prompts the panel shows, and a refresh when that changed.
void UpdatePromptPanel()
{
    if (!PromptsActive()) {
        if (g_promptPanelOwner != -1) SplitRenderHideFacePanel(-1);
        g_promptPanelOwner = -1;
        return;
    }
    uint8_t* hud = *(uint8_t**)kHudPromptObject;
    if (!hud) return;
    char rows[2][sizeof(g_promptPanelWanted)] = {};
    DWORD now = GetTickCount();
    LockPrompts();
    bool ok = ReadPromptRows(hud, rows[0], rows[1], sizeof(rows[0]));
    for (int i = 0; ok && i < 2; i++)
        if (rows[i][0] && HoldPromptRows(g_heldPrompts[i], i ? g_p2Prompts + kPromptBlock : hud + kPromptBlock)) {
            g_heldPromptsAt[i] = now;
            strcpy_s(g_heldPromptsText[i], rows[i]);
        }
    UnlockPrompts();
    if (!ok) return;
    bool has[2];
    for (int i = 0; i < 2; i++) has[i] = g_heldPromptsText[i][0] && now - g_heldPromptsAt[i] < kPromptHoldMs;
    int owner = has[0] ? 0 : has[1] ? 1 : -1;
    if (owner != g_promptPanelOwner) {
        if (g_hudLog) Log("Prompt panel: %s", owner < 0 ? "nobody's" : owner ? "P2's" : "P1's");
        // The same rows for the new owner would not count as a change (the panel may have timed
        // out): the script reads no rows once, then the owner's, and fades them in.
        if (owner >= 0 && g_promptPanelOwner >= 0) g_promptBlankPending = true;
    }
    g_promptPanelOwner = owner;
    SplitRenderHideFacePanel(owner < 0 ? -1 : 1 - owner);
    strcpy_s(g_promptPanelWanted, owner >= 0 ? g_heldPromptsText[owner] : "");
    // The game sends the event only when Player 1's rows change.
    if (strcmp(g_promptPanelWanted, g_promptPanelShown) || g_promptBlankPending)
        if (void* gui = *(void**)kGuiEvents) FireGuiEvent(gui, nullptr, kGuiFacebuttonsChanged, kGuiEventContext);
}

template <uintptr_t Function> int __cdecl PromptHook(void* L)
{
    uintptr_t manager = Manager();
    bool active = PromptsActive() && manager;
    long frame = SplitRenderGameFrames();
    if (active && frame != g_markerFrame) {
        g_markerFrame = frame;
        BeginHudFrame(manager);
    }
    bool rows = Function == kGetFacebuttonString || Function == kGetFaceButtonKey;
    if (active && rows && Function == kGetFacebuttonString && g_promptBlankPending) {
        g_promptBlankPending = false;
        g_promptBlankFrame = frame;
    }
    if (active && rows && g_promptBlankFrame == frame) {
        LockPrompts();
        uint8_t* block = *(uint8_t**)kHudPromptObject + kPromptBlock;
        int hudRead = ~block[0] & 1;
        SwapPromptHalf(block, hudRead, g_blankPrompts, 0, true);
        int results = ((LuaCFunction_t)Function)(L);
        SwapPromptHalf(block, hudRead, g_blankPrompts, 0, true);
        UnlockPrompts();
        g_promptPanelShown[0] = 0; // the owner's rows next frame
        return results;
    }
    if (active && rows && g_promptPanelOwner >= 0) {
        // The panel shows the owner's rows as held (steady while Player 1's flicker).
        LockPrompts();
        uint8_t* block = *(uint8_t**)kHudPromptObject + kPromptBlock;
        int hudRead = ~block[0] & 1;
        uint8_t* held = g_heldPrompts[g_promptPanelOwner];
        SwapPromptHalf(block, hudRead, held, 0, true);
        int results = ((LuaCFunction_t)Function)(L);
        SwapPromptHalf(block, hudRead, held, 0, true);
        UnlockPrompts();
        if (Function == kGetFacebuttonString) strcpy_s(g_promptPanelShown, g_promptPanelWanted);
        return results;
    }
    bool forPlayer2 = active && !rows && g_markerP2;
    if (g_hudLog && active && !rows) { // "p2hud log": both blocks' button flags per half, twice a second
        static DWORD next[4];
        int slot = Function == kGetStuntButtonInfo ? 0 : 1;
        DWORD now = GetTickCount();
        if ((LONG)(now - next[slot * 2 + g_markerP2]) >= 0) {
            next[slot * 2 + g_markerP2] = now + 500;
            uint8_t* hud = *(uint8_t**)kHudPromptObject;
            uint8_t* b = hud + kPromptBlock;
            uint8_t* m = g_p2Prompts + kPromptBlock;
            int hr = ~b[0] & 1, mr = ~m[0] & 1;
            Log("Prompt %s half %s: P1 stunt %d floating %d, P2 stunt %d floating %d (P2 at %.1f %.1f %.1f)",
                slot ? "floating" : "stunt", g_markerP2 ? "P2" : "P1", b[1 + hr], b[3 + hr], m[1 + mr], m[3 + mr],
                *(float*)(m + 0x40 + 0xC * mr), *(float*)(m + 0x44 + 0xC * mr), *(float*)(m + 0x48 + 0xC * mr));
        }
    }
    if (!forPlayer2) {
        int results = ((LuaCFunction_t)Function)(L);
        if (Function == kGetFacebuttonString) strcpy_s(g_promptPanelShown, g_promptPanelWanted);
        return results;
    }
    LockPrompts();
    uint8_t* hud = *(uint8_t**)kHudPromptObject;
    uint8_t* block = hud + kPromptBlock;
    uint8_t* mine = g_p2Prompts + kPromptBlock;
    int hudRead = ~block[0] & 1, myRead = ~mine[0] & 1;
    SwapPromptHalf(block, hudRead, mine, myRead, rows);
    int results = ((LuaCFunction_t)Function)(L);
    SwapPromptHalf(block, hudRead, mine, myRead, rows);
    UnlockPrompts();
    if (g_hudLog && Function == kGetStuntButtonInfo && results >= 3) {
        static DWORD next;
        DWORD now = GetTickCount();
        if ((LONG)(now - next) >= 0) {
            next = now + 500;
            uint8_t* top = *(uint8_t**)((uintptr_t)L + 8);
            uint8_t* first = top - 8 * results;
            Log("Prompt P2 stunt read: shown %d (tt %d) x %.1f y %.1f", *(int*)(first + 4), *(int*)first,
                LuaToNumber(L, -results + 1, -999.0f), LuaToNumber(L, -results + 2, -999.0f));
        }
    }
    if (Function == kGetFacebuttonString) strcpy_s(g_promptPanelShown, g_promptPanelWanted);
    return results;
}

std::string Player2HudPrompts(const char* args)
{
    while (*args == ' ') args++;
    float l, r, t, b;
    if (!strcmp(args, "on") || !strcmp(args, "off")) g_promptsPerPlayer = !strcmp(args, "on");
    else if (sscanf_s(args, "rect %f %f %f %f", &l, &r, &t, &b) == 4) SplitRenderSetFacePanelRect(l, r, t, b);
    char text[512];
    char p1[sizeof(g_promptPanelWanted)] = "", p2[sizeof(g_promptPanelWanted)] = "";
    LockPrompts();
    ReadPromptRows(*(uint8_t**)kHudPromptObject, p1, p2, sizeof(p1));
    UnlockPrompts();
    uint8_t flags[2] = {};
    if (g_p2Prompts) {
        uint8_t* mine = g_p2Prompts + kPromptBlock;
        int read = ~mine[0] & 1;
        flags[0] = mine[1 + read];
        flags[1] = mine[3 + read];
    }
    sprintf_s(text, "ok prompts per player %s, valid %d, P2 writes %ld actions %ld, P2 stunt %d floating %d, panel owner %d, P1 rows [%s] P2 rows [%s]",
              g_promptsPerPlayer ? "on" : "off", (int)g_p2PromptsValid, g_p2PromptWrites, g_p2PromptActions, flags[0], flags[1],
              g_promptPanelOwner, p1, p2);
    return text;
}

uintptr_t TargetIndicatorHookFor(uintptr_t function)
{
    if (function == kTargetIndicatorInfo) return (uintptr_t)TargetIndicatorHook<kTargetIndicatorInfo>;
    if (function == kTargetIndicatorCount) return (uintptr_t)TargetIndicatorHook<kTargetIndicatorCount>;
    if (function == kGetStuntButtonInfo) return (uintptr_t)PromptHook<kGetStuntButtonInfo>;
    if (function == kGetFacebuttonString) return (uintptr_t)PromptHook<kGetFacebuttonString>;
    if (function == kGetFloatingButton) return (uintptr_t)PromptHook<kGetFloatingButton>;
    if (function == kGetFaceButtonKey) return (uintptr_t)PromptHook<kGetFaceButtonKey>;
    return 0;
}

bool IsTargetIndicatorHook(uintptr_t function)
{
    return function == (uintptr_t)TargetIndicatorHook<kTargetIndicatorInfo> ||
           function == (uintptr_t)TargetIndicatorHook<kTargetIndicatorCount> ||
           function == (uintptr_t)PromptHook<kGetStuntButtonInfo> ||
           function == (uintptr_t)PromptHook<kGetFacebuttonString> ||
           function == (uintptr_t)PromptHook<kGetFloatingButton> ||
           function == (uintptr_t)PromptHook<kGetFaceButtonKey>;
}

DWORD WINAPI ScanForMarkerClosures(void*)
{
    MEMORY_BASIC_INFORMATION mbi;
    for (uintptr_t a = 0x00010000; a < 0x7FFF0000 && VirtualQuery((void*)a, &mbi, sizeof(mbi)); a += mbi.RegionSize) {
        if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE || !(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) ||
            (mbi.Protect & PAGE_GUARD))
            continue;
        __try {
            uint8_t* p = (uint8_t*)mbi.BaseAddress;
            uint8_t* end = p + mbi.RegionSize - 0x10;
            for (; p < end; p += 4) {
                uintptr_t function = *(uintptr_t*)(p + kClosureFunction);
                uintptr_t hook = TargetIndicatorHookFor(function);
                if (!hook) hook = HudHookFor(function);
                if (!hook || p[4] != 6 || p[6] != 1) continue;
                uintptr_t c = (uintptr_t)p;
                int i = 0;
                while (i < kMaxMarkerClosures && g_markerClosures[i] && g_markerClosures[i] != c) i++;
                if (i == kMaxMarkerClosures) continue;
                g_markerClosures[i] = c;
                InterlockedExchange((volatile LONG*)(p + kClosureFunction), (LONG)hook);
                Log("HUD: closure at %08X of %08X now per player", (unsigned)c, (unsigned)function);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    InterlockedExchange(&g_markerScanning, 0);
    return 0;
}

// Game tick: keeps the script's closures on the hook (scans again when one is gone or changed, e.g.
// after a load, and now and then for new Lua states).
void KeepMarkersPerPlayer()
{
    bool lost = false, any = false;
    for (int i = 0; i < kMaxMarkerClosures; i++) {
        uintptr_t c = g_markerClosures[i];
        if (!c) continue;
        bool ok;
        __try {
            uintptr_t function = *(uintptr_t*)(c + kClosureFunction);
            ok = *(uint8_t*)(c + 4) == 6 && (IsTargetIndicatorHook(function) || IsHudHook(function));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            ok = false;
        }
        if (ok) any = true;
        else { g_markerClosures[i] = 0; lost = true; }
    }
    static DWORD nextScan;
    DWORD now = GetTickCount();
    bool due = (LONG)(now - nextScan) >= 0;
    if ((lost || (!any && due) || (any && (LONG)(now - nextScan) >= 50000)) &&
        InterlockedCompareExchange(&g_markerScanning, 1, 0) == 0) {
        nextScan = now + 10000;
        CloseHandle(CreateThread(nullptr, 0, ScanForMarkerClosures, nullptr, 0, nullptr));
    }
}

uint32_t __fastcall MinimapUpdateHook(void* minimap, void* edx, uint32_t arg)
{
    uintptr_t manager = Manager();
    // The minimap updates before the frame's camera update: the HUD it feeds ends up in the frame
    // of the *other* view than the last camera update's (with "== 1" P2's map showed in P1's half).
    bool p2 = g_minimapPerPlayer && g_driving && g_player && manager && SplitscreenActive() && SplitRenderGameView() == 0;
    CameraMatrix view, saved;
    bool swapped = p2 && SplitViewMatrix(1, &view) && SwapMainCameraWorld(view, &saved);
    uintptr_t* slot = p2 ? (uintptr_t*)(manager + kManagerPlayerSlot) : nullptr;
    uintptr_t p1 = slot ? *slot : 0;
    if (slot) *slot = g_player;
    // Each player's own map tiles: FUN_00745990 takes them from [0x01191224], which is the ground
    // state in, often the other view's (its frame is being drawn): chunks of the other player's
    // cells showed where their slots met.
    bool split = g_minimapPerPlayer && g_driving && g_player && manager && SplitscreenActive();
    if (split) Terrain2WaitFrameDrawn();
    unsigned mapTexture = split ? Terrain2UseMapTexture(p2 ? 1 : 0) : 0;
    uint32_t r = g_origMinimapUpdate(minimap, edx, arg);
    Terrain2RestoreMapTexture(mapTexture);
    if (slot) *slot = p1;
    if (swapped) RestoreMainCameraWorld(saved, view);
    return r;
}

// Holstered weapons sway with the character's movement: FUN_005E3100 (weapon placement) smooths it
// in function statics, one block for every character (0x01195F74..0x0119601B), so each Rico's
// weapons moved with the other's running. CCombatantComponent's update (slot 4, FUN_005CEEE0,
// __thiscall(component, dt, stage, character), ret 0xC) runs one character at a time and places
// his weapons: Player 2's run has his own copy of the block. Private vtable copies, found with
// the HUD passes; the lock keeps the two players' runs apart.
const uintptr_t kCombatantVtable = 0x00EC293C;
const int kCombatantSlots = 11, kCombatantUpdateIndex = 4;
uintptr_t g_combatantVtable[1 + kCombatantSlots];
typedef void(__fastcall* CombatantUpdate_t)(void* component, void* edx, float dt, int stage, uintptr_t character);
CombatantUpdate_t g_origCombatantUpdate;
const uintptr_t kSwayStatics = 0x01195F74;
const size_t kSwayStaticsSize = 0x01196018 + 4 - kSwayStatics;
uint8_t g_p2Sway[kSwayStaticsSize];
bool g_p2SwaySet;
CRITICAL_SECTION g_swayLock;
volatile bool g_swayPerPlayer = true; // "p2hud sway on|off" (testing)

void __fastcall CombatantUpdateHook(void* component, void* edx, float dt, int stage, uintptr_t character)
{
    uintptr_t manager = Manager();
    uintptr_t p1 = manager ? Character(*(uintptr_t*)(manager + kManagerPlayerSlot)) : 0;
    uintptr_t p2 = g_driving ? Character(g_player) : 0;
    if (!g_swayPerPlayer || !p2 || (character != p1 && character != p2)) {
        g_origCombatantUpdate(component, edx, dt, stage, character);
        return;
    }
    EnterCriticalSection(&g_swayLock);
    __try {
        if (character == p2) {
            uint8_t game[kSwayStaticsSize];
            memcpy(game, (void*)kSwayStatics, kSwayStaticsSize);
            if (g_p2SwaySet) memcpy((void*)kSwayStatics, g_p2Sway, kSwayStaticsSize);
            __try {
                g_origCombatantUpdate(component, edx, dt, stage, character);
            } __finally {
                memcpy(g_p2Sway, (void*)kSwayStatics, kSwayStaticsSize);
                g_p2SwaySet = true;
                memcpy((void*)kSwayStatics, game, kSwayStaticsSize);
            }
        } else {
            g_origCombatantUpdate(component, edx, dt, stage, character);
        }
    } __finally {
        LeaveCriticalSection(&g_swayLock);
    }
}

// The HUD is drawn by CRenderPassAnark passes (vtable 0x00F28F94, ~50 instances, few with anything
// to draw); slot 15 is Draw (FUN_0097A3D0, __thiscall(pass), no arguments). The render code keeps
// the frame from right before the first one that draws (SplitRenderBeforeHud). Private vtable copies,
// as for the minimap; a background scan finds the instances.
const uintptr_t kHudPassVtable = 0x00F28F94;
const int kHudPassSlots = 37;
const int kHudPassDrawIndex = 15;
uintptr_t g_hudPassVtable[1 + kHudPassSlots];
typedef void(__fastcall* HudPassDraw_t)(uintptr_t pass, void* edx);
HudPassDraw_t g_origHudPassDraw;
volatile LONG g_hudPassScanning, g_hudPassDraws;
volatile DWORD g_lastScanMs, g_lastFastScanMs; // how long the last full / fast background scan took
volatile int g_lastScanCounts[3];              // testing: shape triggers, items, distance triggers found

// Testing ("p2hud passes"): the passes that drew, in order, with the present they belong to.
struct HudPassDrawn {
    uintptr_t pass;
    int queued;
    long present;
};
HudPassDrawn g_hudPassLog[256];
volatile LONG g_hudPassLogNext;

void __fastcall HudPassDrawHook(uintptr_t pass, void* edx)
{
    // As the draw's own check: FUN_0097A3D0 skips passes with an empty queue.
    int queued = (*(uint8_t*)(pass + 0xCD0) & 8) ? *(int*)(pass + 0x1A6C + 4 * *(uint16_t*)(pass + 0x1A82)) : 0;
    if (queued > 0) {
        InterlockedIncrement(&g_hudPassDraws);
        LONG i = InterlockedIncrement(&g_hudPassLogNext) - 1;
        g_hudPassLog[i & 255] = { pass, queued, SplitRenderPresents() };
        SplitRenderHudPassBegin(pass, queued);
    }
    g_origHudPassDraw(pass, edx);
    if (queued > 0) SplitRenderHudPassEnd(pass);
}

// The "Agency Mission Started" popup (completion script: all.position.y = 138 at the HUD's init,
// after each load) sits in the band's top rows, so in each half it was pressed against the top
// edge, under the mod's status line in Player 1's (user: move it lower, fully visible). Its node is
// an Anark CAttributeMapGroup (vtable 0x00F5F854) with y at +0x60; while the split is on it gets 60
// (user: "that looked better"), 138 otherwise. A load frees it, so it is looked for again.
const uintptr_t kAttributeGroupVtable = 0x00F5F854;
const uintptr_t kPopupY = 0x60;
const float kPopupGameY = 138.0f, kPopupSplitY = 60.0f;
volatile uintptr_t g_popupNode;
volatile LONG g_popupScanning;

bool IsPopupNode(uintptr_t node)
{
    __try {
        float y = *(float*)(node + kPopupY);
        return *(uintptr_t*)node == kAttributeGroupVtable && (y == kPopupGameY || y == kPopupSplitY);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

DWORD WINAPI ScanForPopupNode(void*)
{
    MEMORY_BASIC_INFORMATION mbi;
    for (uintptr_t a = 0x00010000; a < 0x7FFF0000 && VirtualQuery((void*)a, &mbi, sizeof(mbi)); a += mbi.RegionSize) {
        if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE || !(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) ||
            (mbi.Protect & PAGE_GUARD) || mbi.RegionSize <= kPopupY + 4)
            continue;
        __try {
            uintptr_t* p = (uintptr_t*)mbi.BaseAddress;
            uintptr_t* end = (uintptr_t*)((uintptr_t)mbi.BaseAddress + mbi.RegionSize - kPopupY - 4);
            for (; p < end; p++)
                if (*p == kAttributeGroupVtable && *(float*)((uintptr_t)p + kPopupY) == kPopupGameY) {
                    g_popupNode = (uintptr_t)p;
                    Log("HUD: mission popup node %08X", (unsigned)(uintptr_t)p);
                    InterlockedExchange(&g_popupScanning, 0);
                    return 0;
                }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    InterlockedExchange(&g_popupScanning, 0);
    return 0;
}

// Game tick.
void KeepMissionPopupPlace(bool split)
{
    static DWORD nextScan, searchingSince;
    static bool wasSplit;
    DWORD now = GetTickCount();
    uintptr_t node = g_popupNode;
    if (node && !IsPopupNode(node)) {
        g_popupNode = node = 0; // freed by a load
        searchingSince = now;
    }
    if (split && !wasSplit) searchingSince = now;
    wasSplit = split;
    if (node) {
        *(float*)(node + kPopupY) = split ? kPopupSplitY : kPopupGameY;
        return;
    }
    // Often for a minute after the split comes on or a load, then now and then (a whole-heap scan).
    if (split && (LONG)(now - nextScan) >= 0 && InterlockedCompareExchange(&g_popupScanning, 1, 0) == 0) {
        nextScan = now + (now - searchingSince < 60000 ? 2000 : 30000);
        CloseHandle(CreateThread(nullptr, 0, ScanForPopupNode, nullptr, 0, nullptr));
    }
}

std::string Player2FireGuiEvent(const char* name)
{
    strncpy_s(g_guiEventRequest, name, _TRUNCATE);
    g_guiEventPending = true;
    return std::string("ok firing ") + g_guiEventRequest;
}

std::string Player2HudPasses()
{
    long last = SplitRenderPresents() - 1;
    std::string out = "passes drawn before present " + std::to_string(last) + ":";
    LONG end = g_hudPassLogNext;
    for (LONG i = end > 256 ? end - 256 : 0; i < end; i++) {
        const HudPassDrawn& d = g_hudPassLog[i & 255];
        if (d.present != last) continue;
        char text[64];
        sprintf_s(text, "\n  %08X queued %d", (unsigned)d.pass, d.queued);
        out += text;
    }
    return out;
}


// Fixed guns' cameras (user: Player 1's view went to the flak cannon Player 2 used). The flak
// cannon's script sends its CCameraObject messages when someone gets on or off; the object's
// handler (+0x30 interface, vtable 0x00E4A9D0 slot 1, FUN_00626630, __thiscall(iface, message,
// dword), ret 8: a 1-argument hook corrupted the stack) then switches the one game camera, Player 1's, to the gun's fixed camera (FUN_00548F30 on
// the camera control manager: setup -1, +0x10/+0x14 toggled; manager slot 2 with the object's
// camera) and back. It runs from the message queue, not in Player 2's update. The objects get a
// private copy of that vtable (scan below); while Player 2 is getting on, on or getting off a gun
// and Player 1 isn't on one, the messages are dropped. Player 2's own view stays his orbit camera.
const uintptr_t kCameraObjectIface = 0x00E4A9D0; // CCameraObject(+30)
const int kCameraObjectSlots = 2, kCameraObjectMessageIndex = 1;
const uintptr_t kEnterFixedGunAction = 0x00EC7C84, kExitFixedGunAction = 0x00EC7CEC;
uintptr_t g_cameraObjectVtable[1 + kCameraObjectSlots];
typedef void(__fastcall* CameraObjectMessage_t)(uintptr_t iface, void* edx, uint32_t message, uint32_t extra); // ret 8
CameraObjectMessage_t g_origCameraObjectMessage;
volatile bool g_gunCameraPerPlayer = true; // "p2 guncam on|off" (testing)
long g_gunCameraDropped;                   // testing

// The character is getting on, on, or getting off a fixed gun.
bool CharacterOnFixedGun(uintptr_t c)
{
    if (!c) return false;
    __try {
        uintptr_t action = *(uintptr_t*)(c + kCharacterCurrentAction);
        uintptr_t vtable = action ? *(uintptr_t*)action : 0;
        if (vtable == kEnterFixedGunAction || vtable == kExitFixedGunAction) return true;
        uintptr_t controller = *(uintptr_t*)(c + kCharacterController);
        if (!controller || *(uintptr_t*)controller != 0x00EC26C0) return false; // CEventCharacterController
        const uintptr_t* entry = *(const uintptr_t* const*)(controller + 0x10);
        const uintptr_t* end = *(const uintptr_t* const*)(controller + 0x14);
        for (int n = 0; entry && entry < end && n < 16; entry += 2, n++) {
            uint32_t id = entry[0] ? *(const uint32_t*)entry[0] : 0;
            if (id >= 3 && id <= 6) return true; // aims with the look actions
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
}

void __fastcall CameraObjectMessageHook(uintptr_t iface, void* edx, uint32_t message, uint32_t extra)
{
    if (g_gunCameraPerPlayer && g_driving && g_player) {
        uintptr_t manager = Manager();
        uintptr_t c1 = manager ? Character(*(uintptr_t*)(manager + kManagerPlayerSlot)) : 0;
        uintptr_t c2 = Character(g_player);
        // Getting on, the gun's script sends it from Player 2's update (his enter action).
        if (c2 && (t_inPlayer2Update || CharacterOnFixedGun(c2)) && !CharacterOnFixedGun(c1)) {
            if (g_gunCameraDropped++ < 20) Log("P2 gun: camera message %08X to %08X dropped (Player 1's view stays)", message, (unsigned)iface);
            return;
        }
    }
    g_origCameraObjectMessage(iface, edx, message, extra);
}

// full: also looks for the script patches (FindScriptPatches at every dword), most of a scan's time.
DWORD WINAPI ScanForHudPasses(void* full)
{
    DWORD start = GetTickCount();
    int swapped = 0;
    static uintptr_t triggers[4096], shapeTriggers[4096], items[4096], markers[512];
    int triggerCount = 0, shapeTriggerCount = 0, itemCount = 0, markerCount = 0;
    // Fast scans: every value looked for is one of the exe's vtables (one range) or our shape
    // trigger copy; the rest is skipped with one compare.
    const uintptr_t listener = AudioListenerVtable(), privateListener = AudioPrivateListenerVtable();
    const uintptr_t privateShape = (uintptr_t)&g_shapeTriggerVtable[1];
    uintptr_t lo = kHudPassVtable, hi = kHudPassVtable;
    const uintptr_t looked[] = { listener, kCombatantVtable, kDistanceTriggerIface, kShapeTriggerIface, kResourceItemVtable,
                                 kCameraObjectIface, kCheckpointMarkerVtable };
    for (uintptr_t v : looked) {
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    // Not this thread's own stack: the vtables in lo / hi / looked were swapped for our copies there,
    // which broke the fast scans' range.
    MEMORY_BASIC_INFORMATION mbi;
    void* ownStack = VirtualQuery(&lo, &mbi, sizeof(mbi)) ? mbi.AllocationBase : nullptr;
    for (uintptr_t a = 0x00010000; a < 0x7FFF0000 && VirtualQuery((void*)a, &mbi, sizeof(mbi)); a += mbi.RegionSize) {
        if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE || !(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) ||
            (mbi.Protect & PAGE_GUARD) || mbi.AllocationBase == ownStack)
            continue;
        __try {
            uintptr_t* p = (uintptr_t*)mbi.BaseAddress;
            uintptr_t* end = (uintptr_t*)((uintptr_t)mbi.BaseAddress + mbi.RegionSize);
            for (; p < end; p++) {
                uintptr_t v = *p;
                if (!full && (v < lo || v > hi) && v != privateShape) continue;
                if (*p == kHudPassVtable) {
                    InterlockedCompareExchange((volatile LONG*)p, (LONG)(uintptr_t)&g_hudPassVtable[1], (LONG)kHudPassVtable);
                    swapped++;
                } else if (*p == listener) {
                    InterlockedCompareExchange((volatile LONG*)p, (LONG)privateListener, (LONG)listener);
                } else if (*p == kCombatantVtable) {
                    InterlockedCompareExchange((volatile LONG*)p, (LONG)(uintptr_t)&g_combatantVtable[1], (LONG)kCombatantVtable);
                } else if (*p == kDistanceTriggerIface) {
                    if (triggerCount < (int)_countof(triggers) && (uintptr_t)p >= 8) triggers[triggerCount++] = (uintptr_t)p - 8;
                } else if (*p == kShapeTriggerIface || *p == privateShape) {
                    InterlockedCompareExchange((volatile LONG*)p, (LONG)(uintptr_t)&g_shapeTriggerVtable[1], (LONG)kShapeTriggerIface);
                    if (shapeTriggerCount < (int)_countof(shapeTriggers)) shapeTriggers[shapeTriggerCount++] = (uintptr_t)p;
                } else if (*p == kResourceItemVtable) {
                    if (itemCount < (int)_countof(items)) items[itemCount++] = (uintptr_t)p;
                } else if (*p == kCheckpointMarkerVtable) {
                    if (markerCount < (int)_countof(markers)) markers[markerCount++] = (uintptr_t)p;
                } else if (*p == kCameraObjectIface && g_cameraObjectVtable[1]) {
                    InterlockedCompareExchange((volatile LONG*)p, (LONG)(uintptr_t)&g_cameraObjectVtable[1], (LONG)kCameraObjectIface);
                } else if (full && (uintptr_t)(end - p) > 32) {
                    FindScriptPatches((const uint32_t*)p);
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    g_lastScanCounts[0] = shapeTriggerCount;
    g_lastScanCounts[1] = itemCount;
    g_lastScanCounts[2] = triggerCount;
    FindPickupTriggers(shapeTriggers, shapeTriggerCount, items, itemCount, markers, markerCount);
    PairProximityIcons(triggers, triggerCount);
    (full ? g_lastScanMs : g_lastFastScanMs) = GetTickCount() - start;
    if (swapped) Log("HUD: %d HUD passes hooked, %ld script patches, %ld proximity icons of %d triggers (scan %lu ms)", swapped,
                     g_scriptPatchCount, g_proximityCount[g_proximityCurrent], triggerCount, GetTickCount() - start);
    InterlockedExchange(&g_hudPassScanning, 0);
    return 0;
}

// Game tick: hooks new passes now and then.
void KeepHudPassHooked()
{
    if (!g_hudPassVtable[1]) {
        memcpy(g_hudPassVtable, (const void*)(kHudPassVtable - 4), sizeof(g_hudPassVtable));
        g_origHudPassDraw = (HudPassDraw_t)g_hudPassVtable[1 + kHudPassDrawIndex];
        g_hudPassVtable[1 + kHudPassDrawIndex] = (uintptr_t)HudPassDrawHook;
        InitializeCriticalSection(&g_swayLock);
        memcpy(g_combatantVtable, (const void*)(kCombatantVtable - 4), sizeof(g_combatantVtable));
        g_origCombatantUpdate = (CombatantUpdate_t)g_combatantVtable[1 + kCombatantUpdateIndex];
        g_combatantVtable[1 + kCombatantUpdateIndex] = (uintptr_t)CombatantUpdateHook;
        memcpy(g_shapeTriggerVtable, (const void*)(kShapeTriggerIface - 4), sizeof(g_shapeTriggerVtable));
        g_origShapeTriggerFilter = (ShapeTriggerFilter_t)g_shapeTriggerVtable[1 + kShapeTriggerFilterIndex];
        g_shapeTriggerVtable[1 + kShapeTriggerFilterIndex] = (uintptr_t)ShapeTriggerFilterHook;
        g_origShapeTriggerEnter = (ShapeTriggerEnter_t)g_shapeTriggerVtable[1 + kShapeTriggerEnterIndex];
        g_shapeTriggerVtable[1 + kShapeTriggerEnterIndex] = (uintptr_t)ShapeTriggerEnterHook;
        memcpy(g_cameraObjectVtable, (const void*)(kCameraObjectIface - 4), sizeof(g_cameraObjectVtable));
        g_origCameraObjectMessage = (CameraObjectMessage_t)g_cameraObjectVtable[1 + kCameraObjectMessageIndex];
        g_cameraObjectVtable[1 + kCameraObjectMessageIndex] = (uintptr_t)CameraObjectMessageHook;
    }
    KeepScriptPatches(g_hudPerPlayer && g_driving && g_player && SplitscreenActive());
    // Triggers streamed in near Player 2 (weapon boxes, pickups) only let him in once a scan has
    // found them (user: at the Reapers HQ some boxes opened for him only on a second try). A full
    // scan (every 20 s) takes ~7 s, most of it the script patch search; between them, fast scans
    // without it (~3 s), every 2 s or twice their own time.
    static DWORD nextScan, nextFullScan;
    DWORD now = GetTickCount();
    if ((LONG)(now - nextScan) >= 0 && InterlockedCompareExchange(&g_hudPassScanning, 1, 0) == 0) {
        bool full = (LONG)(now - nextFullScan) >= 0;
        if (full) nextFullScan = now + 20000;
        DWORD interval = g_lastFastScanMs * 2;
        nextScan = now + (interval < 2000 ? 2000 : interval > 20000 ? 20000 : interval);
        CloseHandle(CreateThread(nullptr, 0, ScanForHudPasses, full ? (void*)1 : nullptr, 0, nullptr));
    }
}

// Game tick: keeps the minimap instance on the private vtable (finds it first).
void KeepMinimapPerPlayer()
{
    if (!g_minimapVtable[1]) {
        memcpy(g_minimapVtable, (const void*)(kMinimapVtable - 4), sizeof(g_minimapVtable));
        g_origMinimapUpdate = (MinimapUpdate_t)g_minimapVtable[1 + kMinimapUpdateIndex];
        g_minimapVtable[1 + kMinimapUpdateIndex] = (uintptr_t)MinimapUpdateHook;
    }
    uintptr_t m = g_minimap;
    if (!m || !IsMinimap(m)) {
        g_minimap = 0;
        static DWORD nextScan;
        if ((LONG)(GetTickCount() - nextScan) >= 0 && InterlockedCompareExchange(&g_minimapScanning, 1, 0) == 0) {
            nextScan = GetTickCount() + 10000; // a missing minimap (menus, loading) is looked for again later
            CloseHandle(CreateThread(nullptr, 0, ScanForMinimap, nullptr, 0, nullptr));
        }
        return;
    }
    if (*(uintptr_t*)m == kMinimapVtable) *(uintptr_t*)m = (uintptr_t)&g_minimapVtable[1];
}

bool Player2PilotingHelicopter()
{
    uintptr_t c = g_player ? Character(g_player) : 0;
    uintptr_t controller = c ? *(uintptr_t*)(c + kCharacterController) : 0;
    uintptr_t vtable = controller ? *(uintptr_t*)controller : 0;
    return vtable && (vtable == kHeliControllerVtable || IsPrivateVehicleVtable(vtable, 0));
}

bool Player2PilotingPlane()
{
    uintptr_t c = g_player ? Character(g_player) : 0;
    uintptr_t controller = c ? *(uintptr_t*)(c + kCharacterController) : 0;
    uintptr_t vtable = controller ? *(uintptr_t*)controller : 0;
    return vtable && (vtable == kPlaneControllerVtable || IsPrivateVehicleVtable(vtable, kPlaneControllerIndex));
}

bool Player2DrivingCar()
{
    uintptr_t c = g_player ? Character(g_player) : 0;
    uintptr_t controller = c ? *(uintptr_t*)(c + kCharacterController) : 0;
    uintptr_t vtable = controller ? *(uintptr_t*)controller : 0;
    return vtable && (vtable == kCarControllerVtable || IsPrivateVehicleVtable(vtable, kCarControllerIndex));
}

// The vehicle Player 2 sits in: character +0x2E0 (smart object) -> +0x18, less 4. Its model name
// is a std::string at +0x3F8 (e.g. "V114_JumboJet" for the Aeroliner 474).
const uintptr_t kCharacterSmartObject = 0x2E0, kVehicleModelName = 0x3F8;

bool Player2VehicleModel(char* out, size_t size)
{
    out[0] = 0;
    uintptr_t c = g_player ? Character(g_player) : 0;
    __try {
        uintptr_t so = c ? *(uintptr_t*)(c + kCharacterSmartObject) : 0;
        uintptr_t link = so ? *(uintptr_t*)(so + 0x18) : 0;
        if (!link) return false;
        const uint8_t* s = (const uint8_t*)(link - 4 + kVehicleModelName);
        uint32_t length = *(const uint32_t*)(s + 0x14), capacity = *(const uint32_t*)(s + 0x18);
        const char* text = capacity < 16 ? (const char*)(s + 4) : *(const char* const*)(s + 4);
        if (length == 0 || length >= size) return false;
        memcpy(out, text, length);
        out[length] = 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out[0] = 0;
        return false;
    }
}

// Player 2 mans a gun that aims with the look actions: a vehicle's mounted gun
// (CMountedGunController 0x00EC23E4, or its private copy), or a fixed flak cannon. The flak cannon
// is a scripted object: its user gets a CEventCharacterController (0x00EC26C0) whose update
// (FUN_007B4980) reads the actions in its list (+0x10..+0x14, 8-byte entries, first dword ->
// action id) from the input context and passes them on: FIRE_RIGHT and LOOK_UP/DOWN/LEFT/RIGHT.
bool Player2OnMountedGun()
{
    const int kMountedGunControllerIndex = 6;
    uintptr_t c = g_player ? Character(g_player) : 0;
    uintptr_t controller = c ? *(uintptr_t*)(c + kCharacterController) : 0;
    uintptr_t vtable = controller ? *(uintptr_t*)controller : 0;
    if (!vtable) return false;
    if (vtable == 0x00EC23E4 || IsPrivateVehicleVtable(vtable, kMountedGunControllerIndex)) return true;
    if (vtable != 0x00EC26C0) return false;
    __try {
        const uintptr_t* entry = *(const uintptr_t* const*)(controller + 0x10);
        const uintptr_t* end = *(const uintptr_t* const*)(controller + 0x14);
        for (int n = 0; entry && entry < end && n < 16; entry += 2, n++) {
            uint32_t action = entry[0] ? *(const uint32_t*)entry[0] : 0;
            if (action >= 3 && action <= 6) return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
}

bool Player2RidingMotorbike()
{
    const uintptr_t kBikeControllerVtable = 0x00EC28C0; // CMCController
    const int kBikeControllerIndex = 3;
    uintptr_t c = g_player ? Character(g_player) : 0;
    uintptr_t controller = c ? *(uintptr_t*)(c + kCharacterController) : 0;
    uintptr_t vtable = controller ? *(uintptr_t*)controller : 0;
    return vtable && (vtable == kBikeControllerVtable || IsPrivateVehicleVtable(vtable, kBikeControllerIndex));
}

bool Player2InFreeFall(float* lean)
{
    uintptr_t c = g_player ? Character(g_player) : 0;
    uintptr_t controller = c ? *(uintptr_t*)(c + kCharacterController) : 0;
    if (!controller || *(uintptr_t*)controller != 0x00EC2C20) return false; // CSkyDiveController
    uintptr_t state = *(uintptr_t*)(c + 0xA34);
    if (lean) *lean = state ? *(float*)(state + 0x14C) : 0.0f;
    return true;
}

std::string Player2MinimapPerPlayer(bool on)
{
    g_minimapPerPlayer = on;
    return std::string("ok player 2's frames show his own minimap ") + (on ? "on" : "off");
}

std::string Player2MarkersPerPlayer(bool on)
{
    g_markersPerPlayer = on;
    int closures = 0;
    for (int i = 0; i < kMaxMarkerClosures; i++) closures += g_markerClosures[i] != 0;
    char line[160];
    sprintf_s(line, "ok each half's mission marker distances are its own player's %s (%d closures, frames P1 %ld P2 %ld)",
              on ? "on" : "off", closures, g_markerHalves[0], g_markerHalves[1]);
    return line;
}

std::string Player2HudRefresh(unsigned weapons, unsigned ammo)
{
    g_hudWeaponMask = weapons;
    g_hudAmmoMask = ammo;
    return "ok HUD refresh masks set";
}

void Player2HudSway(bool on)
{
    g_swayPerPlayer = on;
}

void Player2HudProximity(bool on)
{
    g_proximityPerPlayer = on;
}

std::string Player2Pickups(bool on)
{
    g_pickupsForPlayer2 = on;
    char text[192];
    sprintf_s(text, "ok pickups for player 2 %s (%ld pickup triggers, %ld of them weapon boxes', %ld let through; scans %lu / %lu ms, last found %d shape triggers, %d items, %d triggers)",
              on ? "on" : "off", g_pickupTriggerCount[g_pickupTriggersCurrent],
              g_boxTriggerCount[g_pickupTriggersCurrent], g_pickupsLetThrough, g_lastScanMs, g_lastFastScanMs,
              g_lastScanCounts[0], g_lastScanCounts[1], g_lastScanCounts[2]);
    return text;
}

void Player2HudLog(bool on)
{
    g_hudLog = on;
}

void Player2HudPopup(bool fixed, float x, float y)
{
    g_hudPopupFixed = fixed;
    if (fixed) {
        g_hudPopupX = x;
        g_hudPopupY = y;
    }
}

std::string Player2HudPerPlayer(bool on)
{
    g_hudPerPlayer = on;
    char text[80];
    sprintf_s(text, "ok HUD per player %s, %ld script patches, HUD pass draws %ld, calls P1/P2:", on ? "on" : "off", g_scriptPatchCount,
              g_hudPassDraws);
    std::string result = text;
    for (int i = 0; i < kHudLuaFunctionCount; i++) {
        sprintf_s(text, " %08X %ld/%ld", (unsigned)kHudLuaFunctions[i], g_hudCalls[i][0], g_hudCalls[i][1]);
        result += text;
    }
    return result;
}

std::string Player2ShareStuntPositions(bool on)
{
    g_shareStuntPositions = on;
    return std::string("ok both players can use one vehicle stunt position ") + (on ? "on" : "off");
}

std::string Player2SkyDiveLog(unsigned ms)
{
    g_skyDiveLogUntil = GetTickCount() + ms;
    return "ok logging free fall updates for " + std::to_string(ms) + " ms";
}

std::string Player2SkyDiveRemap(bool on)
{
    g_skyDiveRemap = on;
    return std::string("ok player 2's free fall stick remapped to player 1's camera ") + (on ? "on" : "off");
}

std::string Player2AimLend(bool on)
{
    g_lendPlayer2Aim = on;
    return std::string("ok player 2's fire hook uses his own aim target ") + (on ? "on" : "off");
}

std::string PlayersGodMode(bool on)
{
    g_godMode = on;
    if (!on) g_godEnded = true; // health back to the maximum on the next tick
    return std::string("ok both players' health kept at ") + (on ? "100000" : "normal (off)");
}

std::string PlayersRequestSwap()
{
    if (!g_driving) return "error no player 2";
    g_swapRequested = true;
    return "ok players swap places";
}

std::string Player1RequestTeleportTo(float x, float y, float z)
{
    g_player1Target[0] = x;
    g_player1Target[1] = y;
    g_player1Target[2] = z;
    g_movePlayer1To = true;
    g_liftPlayer1 = 1.0f; // runs the placement
    return "ok player 1 moves there";
}

std::string Player1RequestMoveToPlayer2()
{
    g_movePlayer1ToPlayer2 = true;
    g_liftPlayer1 = 1.0f; // runs the placement
    return "ok player 1 moves next to player 2";
}

std::string Player2RequestKill(bool raw)
{
    if (!g_player) return "error no player 2";
    g_killRequestedRaw = raw;
    g_killRequested = true;
    return "ok kill requested";
}

std::string Player2RequestGear()
{
    if (!g_player) return "error no player 2";
    g_gearRequested = true;
    return "ok gear requested";
}

void Player2FullUpdate(bool on)
{
    g_fullUpdate = on;
}

void Player2PressAction(int action, unsigned ms, int action2)
{
    g_testAction = action;
    g_testAction2 = action2;
    g_testActionUntil = GetTickCount() + ms;
}

void RequestAmmoRefill()
{
    g_ammoRequested = true;
}

void RequestMoney(int dollars)
{
    InterlockedExchangeAdd(&g_moneyRequested, dollars);
}

void Player2SetTestSticks(float lx, float ly, float rx, float ry, float lt, float rt, unsigned ms)
{
    Player2Sticks s = {};
    s.leftX = lx;
    s.leftY = ly;
    s.rightX = rx;
    s.rightY = ry;
    s.leftTrigger = lt;
    s.rightTrigger = rt;
    g_testSticks = s;
    g_testSticksUntil = GetTickCount() + ms;
}

void Player2SetMoveInput(float right, float forward)
{
    g_moveInput[0] = right;
    g_moveInput[1] = forward;
}

uintptr_t Player2Body()
{
    return g_possessed;
}

bool Player2BodyMatrix(float out[16])
{
    __try {
        uintptr_t body = g_possessed ? g_possessed : g_driving ? Character(g_player) : 0;
        if (!body || !IsCharacter(body)) return false;
        memcpy(out, (void*)(body + kCharacterWorldMatrix), 16 * sizeof(float));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Player2RefillAmmoSoon()
{
    g_joinAmmoAt = GetTickCount() + kJoinAmmoDelayMs;
}

std::string Player2RequestTeleport(float right, float up, float forward)
{
    if (!g_player) return "error no player 2";
    g_teleportOffset[0] = right;
    g_teleportOffset[1] = up;
    g_teleportOffset[2] = forward;
    g_teleportRequested = true;
    return "ok teleport requested";
}

std::string Player2RequestTeleportTo(float x, float y, float z)
{
    if (!g_player) return "error no player 2";
    g_teleportOffset[0] = x;
    g_teleportOffset[1] = y;
    g_teleportOffset[2] = z;
    g_teleportAbsolute = true;
    g_teleportRequested = true;
    return "ok teleport requested";
}


std::string Player2RequestSpawn()
{
    if (g_player) return "error player 2 already exists";
    g_spawnRequested = true;
    return "ok spawn requested";
}

std::string Player2Describe()
{
    char buf[256];
    float p[3];
    if (!g_player) return "no player 2";
    if (!Player2Position(p)) {
        sprintf_s(buf, "player 2 %08X has no character", (unsigned)g_player);
        return buf;
    }
    sprintf_s(buf, "player 2 %08X character %08X pos %.2f %.2f %.2f | updates %ld input %ld/%ld frames %ld", (unsigned)g_player,
              (unsigned)Character(g_player), p[0], p[1], p[2], g_updateCalls, g_inputHits, g_inputCalls, SplitRenderGameFrames());
    std::string out = buf;
    for (int view = 0; view < 2; view++) {
        float f[2], r[2];
        if (!SplitViewGroundAxes(view, f, r)) continue;
        sprintf_s(buf, " | cam%d f %.3f %.3f r %.3f %.3f", view + 1, f[0], f[1], r[0], r[1]);
        out += buf;
    }
    return out;
}

bool Player1CharacterPosition(float out[3])
{
    __try {
        uintptr_t m = Manager();
        uintptr_t c = m ? Character(*(uintptr_t*)(m + kManagerPlayerSlot)) : 0;
        if (!c) return false;
        memcpy(out, (void*)(c + kCharacterPosition), 12);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool Player2Position(float out[3])
{
    __try {
        uintptr_t c = Character(g_player);
        if (!c) return false;
        memcpy(out, (void*)(c + kCharacterPosition), 12);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Momentum out of a reel at high frame rates: while a character opens the parachute from a reel
// (current action CDeployParachuteWhileReelingAction) and for a moment after, the parachute's drag
// along the canopy ([0x010776F0], 3.0 by default, used by FUN_005A9950) is lowered when the frame
// rate is above 60 (game thread, every frame).
static const uintptr_t kParachuteDrag = 0x010776F0;
static const uintptr_t kDeployFromReelVtable = 0x00EC2FDC;
static const DWORD kReelChuteWindowMs = 1000;
static float g_parachuteDragBase;
static DWORD g_reelChuteUntil;
static volatile float g_reelChuteDragScale = 0.4f; // testing: "chutedrag <scale>"

void SetReelChuteDragScale(float scale)
{
    g_reelChuteDragScale = scale;
}

void ParachuteDragTick()
{
    __try {
        float* drag = (float*)kParachuteDrag;
        if (g_parachuteDragBase == 0.0f) {
            DWORD old;
            if (!VirtualProtect(drag, sizeof(float), PAGE_READWRITE, &old)) return;
            g_parachuteDragBase = *drag;
        }
        uintptr_t manager = Manager();
        uintptr_t players[2] = { manager ? *(uintptr_t*)(manager + kManagerPlayerSlot) : 0, g_driving ? g_player : 0 };
        for (uintptr_t p : players) {
            uintptr_t c = Character(p);
            uintptr_t action = c ? *(uintptr_t*)(c + kCharacterCurrentAction) : 0;
            if (action && *(uintptr_t*)action == kDeployFromReelVtable) g_reelChuteUntil = GetTickCount() + kReelChuteWindowMs;
        }
        uintptr_t timer = *(uintptr_t*)kTimerPtr;
        float frame = timer ? ((FrameTime_t)kFrameTime)((void*)(timer + 4), 0) : 0.0f;
        bool fast = frame > 0.0f && frame < 1.0f / 61.0f;
        *drag = (int)(g_reelChuteUntil - GetTickCount()) > 0 && fast ? g_parachuteDragBase * g_reelChuteDragScale : g_parachuteDragBase;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// The parachute's flight (CParachuteController, vtable 0x00EC24AC, update slot 6 FUN_005A9950)
// depends on the frame rate: opening the parachute out of a grapple reel, Rico keeps gliding
// forward at 30-60 fps, but at ~144 fps the speed turns into a climb and he stops. (Running that
// update at a fixed rate by skipping frames froze the parachute: it has to run every frame.)
// Alternative to ParachuteDragTick, off by default ("chutecap on"): while either player is under a
// parachute the frame rate is capped at 60.
static const uintptr_t kParachuteControllerVtable = 0x00EC24AC;
static const int kParachuteFps = 60;
static volatile bool g_parachuteCap = false;

void SetParachuteCap(bool on)
{
    g_parachuteCap = on;
}

int Player2FrameCapNeeded()
{
    if (!g_parachuteCap) return 0;
    __try {
        uintptr_t manager = Manager();
        uintptr_t players[2] = { manager ? *(uintptr_t*)(manager + kManagerPlayerSlot) : 0, g_driving ? g_player : 0 };
        for (uintptr_t p : players) {
            uintptr_t c = Character(p);
            uintptr_t controller = c ? *(uintptr_t*)(c + kCharacterController) : 0;
            if (controller && *(uintptr_t*)controller == kParachuteControllerVtable) return kParachuteFps;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return 0;
}
