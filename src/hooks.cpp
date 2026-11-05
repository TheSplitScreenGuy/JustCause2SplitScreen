#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#include <d3d10.h>
#include "hooks.h"
#include "overlay.h"
#include "splitscreen.h"
#include "control.h"
#include "splitrender.h"
#include "player2.h"
#include "log.h"

// IDXGIFactory vtable: IUnknown 0-2, IDXGIObject 3-6, EnumAdapters 7,
// MakeWindowAssociation 8, GetWindowAssociation 9, CreateSwapChain 10.
static const int kCreateSwapChainSlot = 10;
// IDXGISwapChain vtable: IUnknown 0-2, IDXGIObject 3-6, IDXGIDeviceSubObject 7, Present 8.
static const int kPresentSlot = 8;

typedef HRESULT(WINAPI* CreateDXGIFactory_t)(REFIID, void**);
typedef HRESULT(STDMETHODCALLTYPE* CreateSwapChain_t)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
typedef HRESULT(STDMETHODCALLTYPE* Present_t)(IDXGISwapChain*, UINT, UINT);

static CreateDXGIFactory_t g_origCreateDXGIFactory;
static CreateSwapChain_t g_origCreateSwapChain;
static Present_t g_origPresent;

static bool PatchPointer(void** slot, void* hook, void** orig)
{
    if (*slot == hook) return true;

    DWORD oldProtect;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect)) return false;
    *orig = *slot;
    InterlockedExchangePointer(slot, hook);
    VirtualProtect(slot, sizeof(void*), oldProtect, &oldProtect);
    return true;
}

// Frame cap (frames per second, 0 = off). JC2's game logic runs once per frame, and some of it
// (parachute momentum among others) behaves differently at high frame rates.
static volatile LONG g_fpsCap;
static LARGE_INTEGER g_nextFrame;

void SetFpsCap(int fps)
{
    InterlockedExchange(&g_fpsCap, fps > 0 ? fps : 0);
    g_nextFrame.QuadPart = 0;
    Log("Frame cap: %d fps", fps);
}

static void WaitForFrameSlot()
{
    LONG fps = g_fpsCap;
    int needed = Player2FrameCapNeeded(); // the parachute needs <= 60
    if (needed > 0 && (fps <= 0 || fps > needed)) fps = needed;
    if (fps <= 0) {
        g_nextFrame.QuadPart = 0;
        return;
    }
    static bool timerPeriod = (timeBeginPeriod(1), true);
    (void)timerPeriod;
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    LONGLONG step = freq.QuadPart / fps;
    if (g_nextFrame.QuadPart == 0 || now.QuadPart - g_nextFrame.QuadPart > step) g_nextFrame = now; // first frame, or far behind
    while (now.QuadPart < g_nextFrame.QuadPart) {
        LONGLONG left = (g_nextFrame.QuadPart - now.QuadPart) * 1000 / freq.QuadPart;
        if (left > 1) Sleep((DWORD)(left - 1));
        QueryPerformanceCounter(&now);
    }
    g_nextFrame.QuadPart += step;
}

static HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)
{
    if (!(flags & DXGI_PRESENT_TEST)) {
        SplitscreenUpdate();
        SplitRenderOnPresent(swapChain);
        OverlayOnPresent(swapChain);
        ControlOnPresent(swapChain);
        WaitForFrameSlot();
    }
    return g_origPresent(swapChain, syncInterval, flags);
}

static HRESULT STDMETHODCALLTYPE HookedCreateSwapChain(IDXGIFactory* factory, IUnknown* device,
                                                       DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** out)
{
    HRESULT hr = g_origCreateSwapChain(factory, device, desc, out);
    if (SUCCEEDED(hr) && out && *out && !g_origPresent) {
        void** vtable = *(void***)*out;
        if (PatchPointer(&vtable[kPresentSlot], (void*)&HookedPresent, (void**)&g_origPresent))
            Log("Present hooked on game swap chain %p (original %p)", *out, g_origPresent);
        else
            Log("Patching Present failed (error %lu)", GetLastError());
    }
    return hr;
}

static HRESULT WINAPI HookedCreateDXGIFactory(REFIID riid, void** out)
{
    if (!g_origCreateDXGIFactory) {
        HMODULE dxgi = LoadLibraryA("dxgi.dll");
        g_origCreateDXGIFactory = dxgi ? (CreateDXGIFactory_t)GetProcAddress(dxgi, "CreateDXGIFactory") : nullptr;
        if (!g_origCreateDXGIFactory) return E_FAIL;
    }

    HRESULT hr = g_origCreateDXGIFactory(riid, out);
    if (SUCCEEDED(hr) && out && *out && !g_origCreateSwapChain) {
        void** vtable = *(void***)*out;
        if (PatchPointer(&vtable[kCreateSwapChainSlot], (void*)&HookedCreateSwapChain, (void**)&g_origCreateSwapChain))
            Log("CreateSwapChain hooked on game factory %p", *out);
        else
            Log("Patching CreateSwapChain failed (error %lu)", GetLastError());
    }
    return hr;
}

// Delay-load descriptor (delayimp.h ImgDelayDescr), with RVA fields.
struct DelayImportDescriptor {
    DWORD attributes;
    DWORD dllNameRva;
    DWORD moduleHandleRva;
    DWORD iatRva;
    DWORD intRva;
    DWORD boundIatRva;
    DWORD unloadIatRva;
    DWORD timeStamp;
};

// JustCause2.exe delay-loads dxgi.dll, so its import lives in the delay-load table.
static void** FindDelayImport(HMODULE module, const char* dllName, const char* funcName)
{
    BYTE* base = (BYTE*)module;
    auto dos = (IMAGE_DOS_HEADER*)base;
    auto nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
    if (!dir.VirtualAddress) return nullptr;

    for (auto desc = (DelayImportDescriptor*)(base + dir.VirtualAddress); desc->dllNameRva; desc++) {
        if (!(desc->attributes & 1)) continue; // pre-VC7 descriptors use VAs; not used by JC2
        if (_stricmp((const char*)(base + desc->dllNameRva), dllName) != 0) continue;

        auto names = (IMAGE_THUNK_DATA*)(base + desc->intRva);
        auto iat = (void**)(base + desc->iatRva);
        for (int i = 0; names[i].u1.AddressOfData; i++) {
            if (IMAGE_SNAP_BY_ORDINAL(names[i].u1.Ordinal)) continue;
            auto byName = (IMAGE_IMPORT_BY_NAME*)(base + names[i].u1.AddressOfData);
            if (strcmp((const char*)byName->Name, funcName) == 0) return &iat[i];
        }
    }
    return nullptr;
}

bool InstallHooks()
{
    void** slot = FindDelayImport(GetModuleHandleA(nullptr), "dxgi.dll", "CreateDXGIFactory");
    if (!slot) {
        Log("CreateDXGIFactory import not found");
        return false;
    }

    // The slot still holds the delay-load stub; our hook resolves the real function itself.
    void* stub = nullptr;
    if (!PatchPointer(slot, (void*)&HookedCreateDXGIFactory, &stub)) {
        Log("Patching CreateDXGIFactory import failed (error %lu)", GetLastError());
        return false;
    }
    return true;
}
