#include <windows.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include "audio.h"
#include "game.h"
#include "splitrender.h"
#include "log.h"

// The game's sound is FMOD Ex + FMOD Event, one listener, set every frame from the main camera: in
// the split it jumped between the players' views, so sounds at Player 2 were heard from Player 1 half
// the time and everything was filtered as if far off once they were apart. FMOD has several
// listeners for this: each sound is then heard from the nearest. The game makes the EventSystem
// through its import, and calls it virtually (set3DListenerAttributes): the object gets a private
// vtable copy (fmod_event.dll's, not the exe's) and the call sets both views' listeners instead.

namespace {

const uintptr_t kEventSystemCreateIat = 0x00D93844; // _FMOD_EventSystem_Create@4
const int kVtableSlots = 96;

struct FmodVector { float x, y, z; };
typedef int(__stdcall* EventSystemCreate_t)(void** system);
typedef int(__stdcall* SetListener_t)(void* system, int listener, const FmodVector* position, const FmodVector* velocity,
                                      const FmodVector* forward, const FmodVector* up);
typedef int(__stdcall* SetListenerCount_t)(void* system, int count);

extern void* g_eventSystem;
EventSystemCreate_t g_origCreate;
SetListener_t g_origSetListener;
SetListenerCount_t g_origSetCount;
uintptr_t g_vtable[1 + kVtableSlots];
int g_listeners = 1;
volatile bool g_twoListeners = true; // "p2audio on|off" (testing)
volatile LONG g_calls, g_twoCalls;
volatile int g_listenerMode;          // testing ("p2audio mode <n>"): 0 both, 1 swapped, 2 only Player 2's view
bool g_logged;

bool ViewListener(int view, FmodVector* position, FmodVector* forward, FmodVector* up)
{
    float p[3], d[3];
    CameraMatrix m;
    if (!SplitViewCamera(view, p, d) || !SplitViewMatrix(view, &m)) return false;
    *position = { p[0], p[1], p[2] };
    *forward = { d[0], d[1], d[2] };
    *up = { m.up[0], m.up[1], m.up[2] };
    return true;
}

int __stdcall SetListenerHook(void* system, int listener, const FmodVector* position, const FmodVector* velocity,
                              const FmodVector* forward, const FmodVector* up)
{
    InterlockedIncrement(&g_calls);
    if (!g_logged && position && forward && up) { // the game's first calls leave the position out
        g_logged = true;
        Log("Audio: game listener %d at %.1f %.1f %.1f forward %.3f %.3f %.3f up %.3f %.3f %.3f", listener, position->x, position->y,
            position->z, forward->x, forward->y, forward->z, up->x, up->y, up->z);
    }
    FmodVector p[2], f[2], u[2];
    bool two = g_twoListeners && g_origSetCount && listener == 0 && ViewListener(0, &p[0], &f[0], &u[0]) &&
               ViewListener(1, &p[1], &f[1], &u[1]);
    if (!two) {
        if (g_listeners != 1 && g_origSetCount) {
            g_origSetCount(system, 1);
            g_listeners = 1;
        }
        return g_origSetListener(system, listener, position, velocity, forward, up);
    }
    int mode = g_listenerMode, wanted = mode == 2 ? 1 : 2;
    if (g_listeners != wanted) {
        int r = g_origSetCount(system, wanted);
        Log("Audio: %d listeners, result %d", wanted, r);
        g_listeners = wanted;
    }
    InterlockedIncrement(&g_twoCalls);
    FmodVector still = { 0.0f, 0.0f, 0.0f }; // FMOD has no doppler with several listeners
    for (int i = 0; i < wanted; i++) {
        int v = mode == 0 ? i : 1 - i;
        g_origSetListener(system, i, &p[v], &still, &f[v], &u[v]);
    }
    return 0;
}

int __stdcall EventSystemCreateHook(void** system)
{
    int result = g_origCreate(system);
    HMODULE fmod = GetModuleHandleA("fmod_event.dll");
    if (result != 0 || !system || !*system || !fmod) return result;
    g_eventSystem = *system;
    void* setListener = (void*)GetProcAddress(fmod, "?set3DListenerAttributes@EventSystemI@FMOD@@UAG?AW4FMOD_RESULT@@HPBUFMOD_VECTOR@@000@Z");
    void* setCount = (void*)GetProcAddress(fmod, "?set3DNumListeners@EventSystemI@FMOD@@UAG?AW4FMOD_RESULT@@H@Z");
    uintptr_t* vptr = *(uintptr_t**)*system;
    memcpy(g_vtable, vptr - 1, sizeof(g_vtable));
    int slot = -1, countSlot = -1;
    for (int i = 0; i < kVtableSlots; i++) {
        if ((void*)vptr[i] == setListener) slot = i;
        if ((void*)vptr[i] == setCount) countSlot = i;
    }
    Log("Audio: FMOD EventSystem %p, listener slot %d, count slot %d (exports %p %p)", *system, slot, countSlot, setListener, setCount);
    if (slot < 0 || countSlot < 0) return result;
    g_origSetListener = (SetListener_t)setListener;
    g_origSetCount = (SetListenerCount_t)setCount;
    g_vtable[1 + slot] = (uintptr_t)SetListenerHook;
    *(uintptr_t**)*system = &g_vtable[1];
    return result;
}

void* g_eventSystem;
volatile bool g_dumpAll;

// The game's own listener (NFMODdesigner::CListener, vtable 0x00F0971C) has one position (+4), read
// through slot 0 (FUN_00A0D1A0, __thiscall(listener, float out[3])) thousands of times a second.
// Most of that is FUN_009DD490, "distance from this point to the listener" (19 callers), which the
// sounds are started and kept by: nothing played near Player 2, whatever FMOD's listeners were.
// For that caller the point is in its frame (first argument), and the nearer view is returned;
// the others get Player 1's view, which unlike the listener's position doesn't jump.
const uintptr_t kListenerVtable = 0x00F0971C;
const int kListenerSlots = 9;
const uintptr_t kDistanceToListenerReturn = 0x009DD4E1;
uintptr_t g_listenerVtable[1 + kListenerSlots];
typedef float*(__fastcall* GetPosition_t)(void* listener, void* edx, float* out); // returns out
GetPosition_t g_origGetPosition;
volatile LONG g_nearerP2, g_distanceCalls;

float* __fastcall GetPositionHook(void* listener, void* edx, float* out)
{
    uintptr_t* frame;
    __asm mov frame, ebp
    float v0[3], v1[3], d[3];
    if (!g_twoListeners || !SplitViewCamera(0, v0, d) || !SplitViewCamera(1, v1, d)) {
        return g_origGetPosition(listener, edx, out);
    }
    memcpy(out, v0, sizeof(v0));
    if (frame[1] != kDistanceToListenerReturn) return out;
    InterlockedIncrement(&g_distanceCalls);
    const float* point = *(const float**)(frame[0] + 8); // FUN_009DD490's argument, in its own frame
    float d0 = 0.0f, d1 = 0.0f;
    for (int i = 0; i < 3; i++) {
        d0 += (point[i] - v0[i]) * (point[i] - v0[i]);
        d1 += (point[i] - v1[i]) * (point[i] - v1[i]);
    }
    if (d1 < d0) {
        memcpy(out, v1, sizeof(v1));
        InterlockedIncrement(&g_nearerP2);
    }
    return out;
}

} // namespace

void AudioListenerMode(int mode)
{
    g_listenerMode = mode;
}

uintptr_t AudioListenerVtable()
{
    return kListenerVtable;
}

uintptr_t AudioPrivateListenerVtable()
{
    return (uintptr_t)&g_listenerVtable[1];
}

// Testing ("p2audio dump"): every FMOD channel in use, with its place and how audible it is, against
// both views' cameras.
std::string AudioDump()
{
    HMODULE ex = GetModuleHandleA("fmodex.dll"), ev = GetModuleHandleA("fmod_event.dll");
    typedef int(__stdcall* GetSystem_t)(void*, void**);
    typedef int(__stdcall* GetChannel_t)(void*, int, void**);
    typedef int(__stdcall* IsPlaying_t)(void*, int*);
    typedef int(__stdcall* Get3D_t)(void*, FmodVector*, FmodVector*);
    typedef int(__stdcall* GetFloat_t)(void*, float*);
    auto getSystem = (GetSystem_t)GetProcAddress(ev, "_FMOD_EventSystem_GetSystemObject@8");
    auto getChannel = (GetChannel_t)GetProcAddress(ex, "FMOD_System_GetChannel");
    auto isPlaying = (IsPlaying_t)GetProcAddress(ex, "FMOD_Channel_IsPlaying");
    auto isVirtual = (IsPlaying_t)GetProcAddress(ex, "FMOD_Channel_IsVirtual");
    auto get3d = (Get3D_t)GetProcAddress(ex, "FMOD_Channel_Get3DAttributes");
    auto audibility = (GetFloat_t)GetProcAddress(ex, "FMOD_Channel_GetAudibility");
    auto volume = (GetFloat_t)GetProcAddress(ex, "FMOD_Channel_GetVolume");
    void* system = nullptr;
    if (!getSystem || !getChannel || !isPlaying || !get3d || !audibility || !volume || !g_eventSystem || getSystem(g_eventSystem, &system) || !system) {
        char e[120];
        sprintf_s(e, "error FMOD exports %p %p %p %p %p %p %p", getSystem, getChannel, isPlaying, get3d, audibility, volume, g_eventSystem);
        return e;
    }
    float view[2][3] = {}, d[3];
    SplitViewCamera(0, view[0], d);
    SplitViewCamera(1, view[1], d);
    Log("Audio dump: view 0 at %.0f %.0f %.0f, view 1 at %.0f %.0f %.0f", view[0][0], view[0][1], view[0][2], view[1][0], view[1][1], view[1][2]);
    int playing = 0, nearby[2] = {};
    for (int i = 0; i < 512; i++) {
        void* channel = nullptr;
        int on = 0, virt = 0;
        if (getChannel(system, i, &channel) || !channel || isPlaying(channel, &on) || !on) continue;
        FmodVector p = {}, v = {};
        float a = -1.0f, vol = -1.0f;
        get3d(channel, &p, &v);
        audibility(channel, &a);
        volume(channel, &vol);
        if (isVirtual) isVirtual(channel, &virt);
        float d0 = sqrtf((p.x - view[0][0]) * (p.x - view[0][0]) + (p.z - view[0][2]) * (p.z - view[0][2]));
        float d1 = sqrtf((p.x - view[1][0]) * (p.x - view[1][0]) + (p.z - view[1][2]) * (p.z - view[1][2]));
        if (!virt && d0 < 80.0f) nearby[0]++;
        if (!virt && d1 < 80.0f) nearby[1]++;
        if (g_dumpAll || d1 < d0) Log("Audio ch %3d at %.0f %.0f %.0f: to views %.0f / %.0f m, audibility %.3f, volume %.3f, virtual %d", i, p.x, p.y, p.z, d0, d1, a, vol, virt);
        playing++;
    }
    char text[100];
    sprintf_s(text, "ok %d channels playing, audible within 80 m: view 0 %d, view 1 %d", playing, nearby[0], nearby[1]);
    return text;
}

void AudioInstallHooks()
{
    DWORD old;
    if (!VirtualProtect((void*)kEventSystemCreateIat, 4, PAGE_READWRITE, &old)) return;
    g_origCreate = *(EventSystemCreate_t*)kEventSystemCreateIat;
    *(void**)kEventSystemCreateIat = (void*)EventSystemCreateHook;
    VirtualProtect((void*)kEventSystemCreateIat, 4, old, &old);
    memcpy(g_listenerVtable, (const void*)(kListenerVtable - 4), sizeof(g_listenerVtable));
    g_origGetPosition = (GetPosition_t)g_listenerVtable[1];
    g_listenerVtable[1] = (uintptr_t)GetPositionHook;
    Log("Audio: FMOD EventSystem import hooked");
}

std::string AudioTwoListeners(bool on)
{
    g_twoListeners = on;
    char text[120];
    sprintf_s(text, "ok two listeners %s, now %d, calls %ld, with two %ld, distance checks %ld, nearer P2 %ld", on ? "on" : "off", g_listeners,
              g_calls, g_twoCalls, g_distanceCalls, g_nearerP2);
    return text;
}
