// dinput8.dll proxy: JustCause2.exe imports only DirectInput8Create from dinput8,
// so we forward that to the real system DLL and use DllMain to start the mod.

#include <windows.h>
#include <unknwn.h>
#include "log.h"
#include "hooks.h"
#include "game.h"
#include "player2.h"
#include "terrain2.h"
#include "world2.h"
#include "trace.h"
#include "patch.h"
#include "audio.h"

typedef HRESULT(WINAPI* DirectInput8Create_t)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
static DirectInput8Create_t g_realDirectInput8Create;

// Loaded lazily: LoadLibrary is not safe inside DllMain.
static bool LoadRealDInput8()
{
    if (g_realDirectInput8Create) return true;

    char path[MAX_PATH];
    GetSystemDirectoryA(path, MAX_PATH);
    strcat_s(path, "\\dinput8.dll");

    HMODULE real = LoadLibraryA(path);
    if (!real) {
        Log("Failed to load real dinput8.dll from %s (error %lu)", path, GetLastError());
        return false;
    }
    g_realDirectInput8Create = (DirectInput8Create_t)GetProcAddress(real, "DirectInput8Create");
    return g_realDirectInput8Create != nullptr;
}

extern "C" HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE inst, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer)
{
    if (!LoadRealDInput8()) return E_FAIL;
    return g_realDirectInput8Create(inst, version, riid, out, outer);
}

// Testing: jc2ss.ini next to the exe, [Debug] Skip<Group>=1 leaves a hook group out (to find which
// one a problem comes from, e.g. the map waypoint closing the game).
bool DebugSkip(const char* key)
{
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) *(slash + 1) = '\0';
    strcat_s(path, "jc2ss.ini");
    bool skip = GetPrivateProfileIntA("Debug", key, 0, path) != 0;
    if (skip) Log("Debug: %s set, that hook group is left out", key);
    return skip;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        LogInit();
        TraceInitMainThread();
        Log("JC2 Splitscreen v1.0 loaded%s", RunningUnderWine() ? " (under Wine/Proton)" : "");

        // Only patches a pointer in the exe's import table, which is safe under the loader lock.
        if (InstallHooks())
            Log("CreateDXGIFactory import hooked");
        else
            Log("Hook install FAILED, mod inactive");
        if (!DebugSkip("SkipCamera") && !InstallCameraHooks())
            Log("Camera hook install FAILED");
        if (!Player2InstallHooks())
            Log("Player 2 hook install FAILED");
        if (!DebugSkip("SkipTerrain2") && !Terrain2InstallHooks())
            Log("Player 2 terrain hook install FAILED");
        if (!DebugSkip("SkipWorld2") && !World2InstallHooks())
            Log("Player 2 world streaming hook install FAILED");
        if (!DebugSkip("SkipPhysicsSize")) World2EnlargePhysicsWorld();
        if (!DebugSkip("SkipAudio")) AudioInstallHooks();
    }
    return TRUE;
}
