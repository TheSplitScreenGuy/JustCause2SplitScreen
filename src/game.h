#pragma once

// Addresses and layouts found in JustCause2.exe (see docs/re-notes.md).

struct CameraMatrix {
    float right[4];
    float up[4];
    float forward[4];
    float position[4];
};

// Copies the main camera's world matrix. Returns false if the camera isn't available yet
// (e.g. during loading).
bool ReadMainCamera(CameraMatrix* out);
// Copies the world matrix the main camera's last update derived its view from (+0x264): the view
// of the half last updated (what HUD projections use), where +0x24 may be back to Player 1's.
bool ReadMainCameraDerived(CameraMatrix* out);

// Temporarily puts another world matrix into the main camera (the one game logic such as aiming
// reads through the camera manager's +0x10), keeping the old one; restore it with
// RestoreMainCameraWorld, which leaves the camera alone if the camera update replaced it meanwhile.
bool SwapMainCameraWorld(const CameraMatrix& with, CameraMatrix* previous);
void RestoreMainCameraWorld(const CameraMatrix& previous, const CameraMatrix& swappedIn);

// Copies the world matrix of the render thread's camera (the copy the frame being drawn uses):
// the camera's world (+0x24), or with derivedSet the world of the matrix set CCamera::Update
// derives the view from (+0x264).
bool ReadRenderCamera(CameraMatrix* out, bool derivedSet = false);

// Overwrites the main camera's world matrix (+0x24, what game logic reads), leaving the matrix set
// the last CCamera::Update derived (+0x264..) alone.
bool SetMainCameraWorld(const CameraMatrix& world);

// Hooks the camera update so the main camera's view can be replaced before the game
// derives its view/projection matrices from it. Call once from DllMain.
bool InstallCameraHooks();

// The four matrices CCamera::Update works with: world, projection, view, view-projection.
struct CameraMatrixSet {
    float m[4][16];
};

// What the camera hook exposes of the main camera while CCamera::Update runs.
struct CameraUpdate {
    CameraMatrix* world;      // +0x264: world matrix the update derives everything from (the
                              // camera object starts 0x264 before it)
    CameraMatrixSet* current; // +0x264..+0x363: this frame's set (filled in by the update)
    CameraMatrixSet* previous;// +0x364..+0x463: last frame's set (motion blur uses it)
};

// Called on the game thread for the main camera: once before CCamera::Update (after == false,
// the world matrix may be changed) and once after it. Pass nullptr to remove.
typedef void (*CameraOverrideFn)(const CameraUpdate& update, bool after);
void SetCameraOverride(CameraOverrideFn fn);

// Called on the render thread when the motion blur pass (FUN_0096a420) starts, with the
// previous-frame view-projection matrix it is about to use (it stores the current one
// there when it finishes). The callback may replace it. Pass nullptr to remove.
typedef void (*MotionBlurFn)(float previousViewProj[16]);
void SetMotionBlurCallback(MotionBlurFn fn);
