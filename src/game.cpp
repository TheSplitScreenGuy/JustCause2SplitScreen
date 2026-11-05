#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "game.h"
#include "player2.h"
#include "patch.h"
#include "log.h"

namespace {

// [JustCause2.exe+0xD919F4] -> NGraphicsEngine::CCameraManager
const uintptr_t kCameraManagerPtr = 0x00400000 + 0xD919F4;
const uintptr_t kCameraManagerVtable = 0x00F21FD8;
const uintptr_t kCameraVtable = 0x00F21ED8;
const uintptr_t kManagerMainCamera = 0x18;   // CCamera embedded in the manager
const uintptr_t kManagerRenderCamera = 0x14; // CCamera* read by the render thread
const uintptr_t kManagerLogicCamera = 0x10;  // CCamera* read by game logic (aiming)
const uintptr_t kCameraWorldMatrix = 0x24;
// CCamera::Update (vtable slot 1, FUN_00933a10) derives view/projection from this matrix.
const uintptr_t kCameraPendingWorld = 0x264;
const uintptr_t kCameraPreviousSet = 0x364;
const int kCameraUpdateSlot = 1;

typedef void(__thiscall* CameraUpdate_t)(void* camera, char flag);
CameraUpdate_t g_origCameraUpdate;
CameraOverrideFn volatile g_override;

// Motion blur pass: __thiscall with 6 stack arguments (ends with "ret 0x18").
// Starts with push ebp / mov ebp,esp / sub esp,0x1D0 (9 bytes, no relative addressing).
const uintptr_t kMotionBlurPass = 0x0096A420;
const unsigned char kMotionBlurPrologue[] = { 0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xD0, 0x01, 0x00, 0x00 };
const uintptr_t kMotionBlurPreviousViewProj = 0x10;

typedef DWORD(__thiscall* MotionBlur_t)(void*, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD);
MotionBlur_t g_origMotionBlur; // trampoline
MotionBlurFn volatile g_motionBlurFn;

// Returns the original's EAX in case a caller uses it.
DWORD __fastcall HookedMotionBlur(void* self, void* /*edx*/, DWORD a, DWORD b, DWORD c, DWORD d, DWORD e, DWORD f)
{
    MotionBlurFn fn = g_motionBlurFn;
    if (fn) fn((float*)((uintptr_t)self + kMotionBlurPreviousViewProj));
    return g_origMotionBlur(self, a, b, c, d, e, f);
}

uintptr_t MainCamera()
{
    uintptr_t manager = *(uintptr_t*)kCameraManagerPtr;
    if (!manager || *(uintptr_t*)manager != kCameraManagerVtable) return 0;
    uintptr_t camera = manager + kManagerMainCamera;
    return *(uintptr_t*)camera == kCameraVtable ? camera : 0;
}

// __fastcall with a dummy edx parameter matches __thiscall (this in ecx, args on stack).
void __fastcall HookedCameraUpdate(void* camera, void* /*edx*/, char flag)
{
    bool main = (uintptr_t)camera == MainCamera();
    if (main) Player2GameTick();
    CameraOverrideFn fn = g_override;
    if (!fn || !main) {
        g_origCameraUpdate(camera, flag);
        return;
    }
    uintptr_t base = (uintptr_t)camera;
    CameraUpdate update = { (CameraMatrix*)(base + kCameraPendingWorld), (CameraMatrixSet*)(base + kCameraPendingWorld),
                            (CameraMatrixSet*)(base + kCameraPreviousSet) };
    fn(update, false);
    g_origCameraUpdate(camera, flag);
    fn(update, true);
}

} // namespace

bool ReadMainCameraDerived(CameraMatrix* out)
{
    __try {
        uintptr_t camera = MainCamera();
        if (!camera) return false;
        memcpy(out, (void*)(camera + kCameraPendingWorld), sizeof(*out));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadMainCamera(CameraMatrix* out)
{
    // Checking vtables guards against the layout being wrong (e.g. a different game version).
    __try {
        uintptr_t camera = MainCamera();
        if (!camera) return false;
        memcpy(out, (void*)(camera + kCameraWorldMatrix), sizeof(*out));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The camera game logic reads (the camera manager's +0x10; usually the embedded main camera).
uintptr_t LogicCamera()
{
    uintptr_t manager = *(uintptr_t*)kCameraManagerPtr;
    if (!manager || *(uintptr_t*)manager != kCameraManagerVtable) return 0;
    uintptr_t camera = *(uintptr_t*)(manager + kManagerLogicCamera);
    return camera && *(uintptr_t*)camera == kCameraVtable ? camera : 0;
}

bool SwapMainCameraWorld(const CameraMatrix& with, CameraMatrix* previous)
{
    __try {
        uintptr_t camera = LogicCamera();
        if (!camera) return false;
        memcpy(previous, (void*)(camera + kCameraWorldMatrix), sizeof(*previous));
        memcpy((void*)(camera + kCameraWorldMatrix), &with, sizeof(with));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void RestoreMainCameraWorld(const CameraMatrix& previous, const CameraMatrix& swappedIn)
{
    __try {
        uintptr_t camera = LogicCamera();
        // If the camera update wrote a new matrix meanwhile, that one stays.
        if (camera && !memcmp((void*)(camera + kCameraWorldMatrix), &swappedIn, sizeof(swappedIn)))
            memcpy((void*)(camera + kCameraWorldMatrix), &previous, sizeof(previous));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

bool ReadRenderCamera(CameraMatrix* out, bool derivedSet)
{
    __try {
        uintptr_t manager = *(uintptr_t*)kCameraManagerPtr;
        if (!manager || *(uintptr_t*)manager != kCameraManagerVtable) return false;
        uintptr_t camera = *(uintptr_t*)(manager + kManagerRenderCamera);
        if (!camera || *(uintptr_t*)camera != kCameraVtable) return false;
        memcpy(out, (void*)(camera + (derivedSet ? kCameraPendingWorld : kCameraWorldMatrix)), sizeof(*out));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SetMainCameraWorld(const CameraMatrix& world)
{
    __try {
        uintptr_t camera = MainCamera();
        if (!camera) return false;
        memcpy((void*)(camera + kCameraWorldMatrix), &world, sizeof(world));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool InstallCameraHooks()
{
    void** slot = (void**)kCameraVtable + kCameraUpdateSlot;
    DWORD oldProtect;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect)) return false;
    g_origCameraUpdate = (CameraUpdate_t)*slot;
    *slot = (void*)&HookedCameraUpdate;
    VirtualProtect(slot, sizeof(void*), oldProtect, &oldProtect);
    Log("CCamera::Update hooked (original %p)", g_origCameraUpdate);

    g_origMotionBlur = (MotionBlur_t)InlineHook(kMotionBlurPass, kMotionBlurPrologue, sizeof(kMotionBlurPrologue),
                                                (void*)&HookedMotionBlur);
    if (!g_origMotionBlur) {
        Log("Motion blur hook FAILED (unexpected code at %08X)", (unsigned)kMotionBlurPass);
        return false;
    }
    Log("Motion blur pass hooked");
    return true;
}

void SetMotionBlurCallback(MotionBlurFn fn)
{
    g_motionBlurFn = fn;
}

void SetCameraOverride(CameraOverrideFn fn)
{
    g_override = fn;
}
