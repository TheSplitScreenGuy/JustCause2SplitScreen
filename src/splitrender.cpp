#include <windows.h>
#include <d3d10.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "splitrender.h"
#include "splitscreen.h"
#include "player2.h"
#include "terrain2.h"
#include "world2.h"
#include "game.h"
#include "log.h"

namespace {

volatile LONG g_frame;   // main camera updates since the split turned on (game thread)
volatile LONG g_viewFlip; // testing: 1 skips a view once, swapping which frames draw which view
float g_forward[2][3];   // forward vector each view was last given (game thread writes)
float g_right[2][3];     // right vector each view was last given (game thread writes)
float g_position[2][3];  // camera position each view was last given (game thread writes)
bool g_havePosition[2];  // kept through a pause while Player 2 is in (SplitViewCamera)
CameraMatrix g_world[2]; // whole world matrix each view was last given (game thread writes)
float g_viewFov[2];       // the vertical FOV (radians) each view was last drawn with
float g_frameAspect = 16.0f / 9.0f; // the back buffer's width / height
bool g_enabled;

// Each view's last few final camera matrices (game thread writes, render thread reads).
const int kHistory = 8;
struct History {
    CameraMatrix m[kHistory];
    unsigned next;
};
History g_history[2];
unsigned g_exactMatches, g_fallbackMatches; // render frames identified exactly / by closeness
DWORD g_nextMatchLog;
CameraMatrix g_logicWorld; // Player 1's camera world after their last update
bool g_haveLogicWorld;
// On Player 2's frames his world matrix stays in the main camera through the game update, so
// whatever measures detail and draw distances from its position (+0x54: roads, signs, props)
// does it from him; Player 1's goes back after the frame hand-off. Systems that must stay
// Player 1's get his position from SplitRenderPlayer1Position (terrain streaming, locations,
// decorations, roads, area sets, the physics origin). "split p2world off" to compare.
volatile bool g_keepPlayer2World = true;
bool g_logicWorldPending;

// Each view's own previous-frame matrices, so motion blur compares a view with itself
// rather than with the other player's view.
CameraMatrixSet g_previous[2];
bool g_havePrevious[2];
int g_updatingView;

ID3D10Device* g_device;
ID3D10Texture2D* g_cache[2];
// HUD corner (minimap + heat) copied into each view's band ("split hudcorner on|off", testing).
volatile bool g_hudCorner = true;
volatile bool g_hudMask = true; // "split hudmask on|off" (testing: the whole corner rectangle)
const float kHudCornerWidth = 0.15f, kHudCornerHeight = 0.27f; // of the full frame
volatile bool g_hudStrips = true; // "split hudstrips on|off" (testing)
const float kHudStripGain = 16.0f; // difference from the HUD-less frame that counts as fully HUD
volatile float g_hudTopStripShift = 0.0f; // of the half's height ("split hudtop <fraction>")
volatile int g_hudHidePopup = -1;
// Where the weapon switch popup is, of the band (player2's fixed spot), clear of the crosshair:
// left, right, top, bottom.
const float kHudPopupRects[2][4] = { { 0.2f, 0.47f, 0.3f, 0.97f }, { 0.47f, 0.53f, 0.6f, 0.97f } };
// The button prompt panel's rows (right edge), same layout ("p2hud prompts rect <l> <r> <t> <b>").
volatile int g_hudHideFacePanel = -1;
float g_hudFacePanelRect[4] = { 0.84f, 1.0f, 0.15f, 0.8f };
bool g_haveView[2];
UINT g_width, g_height;
DXGI_FORMAT g_format;

// Player 2's orbit camera around their body: yaw/pitch from the right stick.
const float kP2Fov = 0.6981f, kP2ZoomFov = 0.3491f; // the game's default and aim zoom
const float kP2ZoomRate = 18.0f;                    // per second of real time
float g_p2Fov = kP2Fov;
float g_p2Yaw;            // radians, relative to world
// Starts nearly level like Player 1's camera: the screen centre is where Player 2 aims, and
// pitched down it lands on the ground right in front of them, which the game treats as a
// blocked line of fire (no shots).
float g_p2Pitch = 0.08f;  // radians above horizontal
bool g_p2YawSet;
const float kP2Distance = 10.0f, kP2TargetHeight = 1.8f;
const float kP2FallFollowRate = 3.0f; // 1/s: free fall camera catching up behind the body
const float kP2HeliFollowRate = 6.0f; // 1/s: helicopter camera catching up behind it
// "split helicam <distance> [height]" (testing). Tuned with the user in game (2026-10-29): 40 m
// back, 3 m up ("perfect"); 16 m showed too little of the helicopter.
volatile float g_p2HeliDistance = 40.0f, g_p2HeliTargetHeight = 3.0f;
// Planes (user: "move the plane camera back", then "down a little"): the same follow camera,
// "split planecam <d> [h]".
volatile float g_p2PlaneDistance = 30.0f, g_p2PlaneTargetHeight = 2.0f;
// Big planes need much more room (user: the Aeroliner 474 "way back"): by model name, read once a
// second; "split bigplanecam <d> [h]" tunes the Aeroliner's.
struct PlaneCamera { const char* model; float distance, height; };
PlaneCamera g_bigPlanes[] = {
    { "V114_JumboJet", 90.0f, 10.0f }, // Aeroliner 474
};
float g_planeDistanceNow = 30.0f, g_planeHeightNow = 2.0f; // eased toward the plane's
const float kP2HeliPitchLimit = 0.7f; // radians up or down
const float kP2CarExtraDistance = 5.0f, kP2CarDistanceRate = 4.0f; // metres; 1/s easing in and out
float g_p2CarExtra;
// Motorbikes (user: "a little back and down"): this much farther back and lower than on foot,
// eased like the car's ("split bikecam <back> <down>").
volatile float g_p2BikeExtraDistance = 2.5f, g_p2BikeLower = 0.6f;
float g_p2BikeBlend; // 0 on foot, 1 on a bike
volatile float g_p2FallLead = 0.3f;   // radians per unit of lean ("split falllead <r>", testing)
volatile float g_p2ShakeScale = 2.0f; // degrees of jitter per unit of free fall effect strength
// Player 1's camera is drawn as far back as Player 2's, moved back along its own view axis (the
// crosshair line, so his aim doesn't change); game logic keeps the camera where the game put it.
// "split p1near" to turn it off.
volatile bool g_pushPlayer1Back = true;
CameraMatrix g_player1Unpushed;
bool g_player1Pushed;

// A camera that would be in the terrain comes in along the line to its target until it is above
// the ground again (terrain only; buildings and other objects aren't checked).
const float kCameraGroundClearance = 0.6f;

void KeepCameraAboveGround(const float target[3], float position[3])
{
    float height;
    if (!Terrain2GroundHeight(position, &height) || position[1] >= height + kCameraGroundClearance) return;
    for (float t = 0.95f; t > 0.0f; t -= 0.05f) {
        float p[3];
        for (int i = 0; i < 3; i++) p[i] = target[i] + (position[i] - target[i]) * t;
        if (Terrain2GroundHeight(p, &height) && p[1] < height + kCameraGroundClearance) continue;
        memcpy(position, p, sizeof(p));
        return;
    }
    memcpy(position, target, 3 * sizeof(float));
}

// Buildings, rocks, trees, vehicles: the game's ray cast (FUN_009A9A50 on the physics world
// [0x0118FFF4], thiscall(ray {origin, unit direction}, min, max, result*, filter*, 0, 0, 0), ret 0x20)
// with the aim's filter (CPlayerAimRaycastFilter, vtable 0x00EB4D74, set up as in FUN_007047A0:
// FUN_009ACCA0(0, 0, 0), then FUN_005EF990(1) for its layer mask). That filter skips Player 1's body
// and vehicle, and our AimFilterHook skips Player 2's body. Result: +0 body hit, +0x10 fraction of
// min..max (1: nothing). The camera comes in along its line to where the ray hit, less a clearance
// for the near plane; it moves in at once and eases back out.
const uintptr_t kPhysicsWorld = 0x0118FFF4, kAimRaycastFilterVtable = 0x00EB4D74;
typedef void(__fastcall* RayFilterInit_t)(void* filter, void* edx, int a, int b, int c);
typedef void(__fastcall* RayFilterLayers_t)(void* filter, void* edx, char wide);
typedef void(__fastcall* CastRay_t)(void* world, void* edx, const float* ray, float min, float max, void* result,
                                    void* filter, int a, int b, int c);
const RayFilterInit_t RayFilterInit = (RayFilterInit_t)0x009ACCA0;
const RayFilterLayers_t RayFilterLayers = (RayFilterLayers_t)0x005EF990;
const CastRay_t CastRay = (CastRay_t)0x009A9A50;
const float kCameraWallClearance = 0.4f, kCameraWallMinDistance = 0.6f, kCameraWallOutRate = 4.0f; // m, m, 1/s
volatile bool g_cameraCollision = true; // "split camwalls on|off" (testing)
volatile int g_wallLayers = 1;          // FUN_005EF990's argument, -1: not called ("split camwalls layers <n>")
float g_wallDistance[2];                // per view: the distance the camera is allowed now (0: none yet)
LARGE_INTEGER g_wallLast[2];
long g_wallHits[2];                     // testing: frames the camera was pulled in

// Distance from `from` along the unit direction `dir` that is clear, up to `length` (skipping the
// first `skip` metres, e.g. the player's own vehicle).
uint32_t g_lastRayResult[11]; // testing

float ClearDistance(const float from[3], const float dir[3], float length, float skip)
{
    uintptr_t world = *(uintptr_t*)kPhysicsWorld;
    if (!world) return length;
    uint8_t filter[0xC0] = {};
    float ray[9] = { from[0], from[1], from[2], dir[0], dir[1], dir[2], from[0], from[1], from[2] };
    struct { uintptr_t hit; float normal[3]; float fraction; float extra[2]; int32_t keys[4]; } result =
        { 0, { 1.0f, 0.0f, 0.0f }, 1.0f, { 0.0f, 0.0f }, { -1, -1, -1, -1 } };
    float max = length + kCameraWallClearance;
    if (skip >= max) return length;
    __try {
        RayFilterInit(filter, nullptr, 0, 0, 0);
        *(uintptr_t*)filter = kAimRaycastFilterVtable;
        if (g_wallLayers >= 0) RayFilterLayers(filter, nullptr, (char)g_wallLayers);
        CastRay((void*)world, nullptr, ray, skip, max, &result, filter, 0, 0, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return length;
    }
    memcpy(g_lastRayResult, &result, sizeof(g_lastRayResult));
    if (!(result.fraction < 1.0f) || !(result.fraction >= 0.0f)) return length;
    float hit = skip + (max - skip) * result.fraction;
    float clear = hit - kCameraWallClearance;
    return clear < length ? clear : length;
}

// The ray finds nothing when cast from the camera update (or at Present): the physics world only
// answers at the time the game casts its own rays. So each camera leaves its ray here, and the
// brain think (main thread, where the aim ray is cast; SplitRenderCastCameraRays) casts it; the
// camera uses that answer on its next update.
struct WallRay { float from[3], dir[3], length, skip; volatile bool wanted; float clear; bool answered; };
WallRay g_wallRays[3]; // [2]: "split camwalls test ..." (testing)

// Moves `position` toward `from` (on the same line) to the clear distance, for this view.
void KeepCameraOutOfWalls(int view, const float from[3], float position[3], float skip)
{
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    float dt = g_wallLast[view].QuadPart ? float(now.QuadPart - g_wallLast[view].QuadPart) / float(frequency.QuadPart) : 0.0f;
    g_wallLast[view] = now;
    if (dt > 0.1f) dt = 0.1f;
    float d[3] = { position[0] - from[0], position[1] - from[1], position[2] - from[2] };
    float length = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!g_cameraCollision || length < 0.05f) {
        g_wallDistance[view] = 0.0f;
        return;
    }
    for (int i = 0; i < 3; i++) d[i] /= length;
    WallRay& ray = g_wallRays[view];
    float allowed = ray.answered && ray.clear < ray.length ? ray.clear : length;
    memcpy(ray.from, from, sizeof(ray.from));
    memcpy(ray.dir, d, sizeof(ray.dir));
    ray.length = length;
    ray.skip = skip;
    ray.wanted = true;
    if (allowed < kCameraWallMinDistance) allowed = length < kCameraWallMinDistance ? length : kCameraWallMinDistance;
    float& current = g_wallDistance[view];
    if (current <= 0.0f || current > length) current = length;
    if (allowed < current) current = allowed;
    else current += (allowed - current) * (1.0f - expf(-kCameraWallOutRate * dt));
    if (current >= length - 0.001f) return;
    g_wallHits[view]++;
    for (int i = 0; i < 3; i++) position[i] = from[i] + d[i] * current;
}

void PushPlayer1Back(CameraMatrix* world)
{
    float target[3];
    g_player1Pushed = false;
    if (!g_pushPlayer1Back || !Player1CharacterPosition(target)) return;
    target[1] += kP2TargetHeight;
    float d[3] = { world->position[0] - target[0], world->position[1] - target[1], world->position[2] - target[2] };
    float distance = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (distance >= kP2Distance || distance > 50.0f) return; // e.g. cutscene or free camera
    g_player1Unpushed = *world;
    for (int i = 0; i < 3; i++) world->position[i] += world->forward[i] * (kP2Distance - distance);
    // From where the game had it (clear of the ground and walls) back along the same line.
    KeepCameraAboveGround(g_player1Unpushed.position, world->position);
    KeepCameraOutOfWalls(0, g_player1Unpushed.position, world->position, 0.0f);
    g_player1Pushed = true;
}

void Normalize(float* v)
{
    float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 1e-6f) for (int i = 0; i < 3; i++) v[i] /= len;
}

void MakePlayer2Camera(CameraMatrix* m)
{
    float body[16];
    if (!Player2BodyMatrix(body)) {
        // No Player 2 body yet: Player 1's camera turned around as a stand-in.
        float* rows[2] = { m->right, m->forward };
        for (float* v : rows) {
            v[0] = -v[0];
            v[2] = -v[2];
        }
        return;
    }
    if (!g_p2YawSet) {
        g_p2Yaw = atan2f(body[8], body[10]); // start behind the body (its back vector)
        g_p2YawSet = true;
    }
    Player2Sticks s = SplitscreenPlayer2Sticks();
    // Flying a helicopter, Player 1's camera stays behind it and his mouse turns it; Player 2's
    // right stick turns it (WriteVehicleInput) and his camera follows behind, farther back.
    // A plane's camera follows like the helicopter's, but his right stick swings it around the plane
    // (user: as Player 1's) and it goes back behind once the stick is let go.
    bool plane = Player2PilotingPlane();
    bool heli = plane || Player2PilotingHelicopter();
    bool planeLook = plane && fabsf(s.rightX) >= 0.05f;
    float aim = g_p2Fov / kP2Fov; // slower while zoomed
    if (!heli || planeLook) g_p2Yaw -= s.rightX * 0.05f * aim;
    // In free fall the body turns toward the camera's heading (FUN_0077FC80) and Player 1's camera
    // swings round behind his body as it turns; Player 2's stayed put, which held his body to it
    // (leaning left or right barely turned him). It follows behind the body while the right
    // stick is idle.
    static LARGE_INTEGER s_last, s_freq;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (!s_freq.QuadPart) QueryPerformanceFrequency(&s_freq);
    float dt = s_last.QuadPart ? float(now.QuadPart - s_last.QuadPart) / float(s_freq.QuadPart) : 0.0f;
    s_last = now;
    if (dt > 0.1f) dt = 0.1f;
    float lean = 0.0f;
    if (heli && !planeLook) {
        float diff = atan2f(body[8], body[10]) - g_p2Yaw;
        while (diff > 3.14159265f) diff -= 6.2831853f;
        while (diff < -3.14159265f) diff += 6.2831853f;
        g_p2Yaw += diff * (1.0f - expf(-kP2HeliFollowRate * dt));
    } else if (Player2InFreeFall(&lean) && fabsf(s.rightX) < 0.05f) {
        // Player 1's camera also leads the turn (his heading error +4 has the lean's sign, ~0.14
        // at full lean), which helps the lean; trailing behind held Player 2 back.
        float behind = atan2f(body[8], body[10]) + g_p2FallLead * lean;
        float diff = behind - g_p2Yaw;
        while (diff > 3.14159265f) diff -= 6.2831853f;
        while (diff < -3.14159265f) diff += 6.2831853f;
        g_p2Yaw += diff * (1.0f - expf(-kP2FallFollowRate * dt));
    }
    g_p2Pitch -= s.rightY * 0.03f; // stick up looks up: camera goes lower behind the body
    // Nearly straight up and down, like Player 1's camera.
    float pitchLimit = heli ? kP2HeliPitchLimit : 1.45f;
    if (g_p2Pitch < -pitchLimit) g_p2Pitch = -pitchLimit;
    if (g_p2Pitch > pitchLimit) g_p2Pitch = pitchLimit;

    // Free fall shake (Player 1's view jitters ~0.1 deg a frame at 30 m/s from his own fall's
    // effects, which Player 2's update no longer adds to the shared camera): a random jitter of
    // the strength (speed * 0.002) * g_p2ShakeScale degrees, not kept in the orbit angles.
    float yaw = g_p2Yaw, pitch = g_p2Pitch;
    if (float shake = Player2FallShake() * g_p2ShakeScale * 0.0174533f) {
        yaw += shake * (rand() / (float)RAND_MAX * 2.0f - 1.0f);
        pitch += shake * (rand() / (float)RAND_MAX * 2.0f - 1.0f);
    }
    float shakeOffset[3] = {};
    bool shaking = Player2CameraShake(dt, shakeOffset);
    if (shaking) {
        yaw += shakeOffset[0];
        pitch += shakeOffset[1];
    }
    float ease = 1.0f - expf(-kP2CarDistanceRate * dt);
    g_p2BikeBlend += ((Player2RidingMotorbike() ? 1.0f : 0.0f) - g_p2BikeBlend) * ease;
    if (plane) {
        static DWORD nextModelRead;
        static float wantDistance, wantHeight;
        DWORD now = GetTickCount();
        if ((LONG)(now - nextModelRead) >= 0) {
            nextModelRead = now + 1000;
            char model[64];
            wantDistance = g_p2PlaneDistance;
            wantHeight = g_p2PlaneTargetHeight;
            if (Player2VehicleModel(model, sizeof(model)))
                for (const PlaneCamera& p : g_bigPlanes)
                    if (!strcmp(model, p.model)) {
                        wantDistance = p.distance;
                        wantHeight = p.height;
                    }
        }
        g_planeDistanceNow += (wantDistance - g_planeDistanceNow) * ease;
        g_planeHeightNow += (wantHeight - g_planeHeightNow) * ease;
    } else {
        g_planeDistanceNow = g_p2PlaneDistance;
        g_planeHeightNow = g_p2PlaneTargetHeight;
    }
    float targetHeight = plane ? g_planeHeightNow : heli ? g_p2HeliTargetHeight : kP2TargetHeight - g_p2BikeLower * g_p2BikeBlend;
    float target[3] = { body[12], body[13] + targetHeight, body[14] };
    g_p2CarExtra += ((Player2DrivingCar() ? kP2CarExtraDistance : 0.0f) - g_p2CarExtra) * ease;
    float distance = plane ? g_planeDistanceNow : heli ? g_p2HeliDistance
                           : kP2Distance + g_p2CarExtra + g_p2BikeExtraDistance * g_p2BikeBlend;
    float back[3] = { sinf(yaw) * cosf(pitch), sinf(pitch), cosf(yaw) * cosf(pitch) };
    float worldUp[3] = { 0, 1, 0 };
    float right[3] = { worldUp[1] * back[2] - worldUp[2] * back[1], worldUp[2] * back[0] - worldUp[0] * back[2],
                       worldUp[0] * back[1] - worldUp[1] * back[0] };
    Normalize(right);
    float up[3] = { back[1] * right[2] - back[2] * right[1], back[2] * right[0] - back[0] * right[2],
                    back[0] * right[1] - back[1] * right[0] };
    for (int i = 0; i < 3; i++) {
        m->right[i] = right[i];
        m->up[i] = up[i];
        m->forward[i] = back[i]; // the camera looks along -forward
        m->position[i] = target[i] + back[i] * distance;
    }
    KeepCameraAboveGround(target, m->position);
    // In a vehicle the ray starts past it (Player 1's own vehicle is skipped by the filter, Player
    // 2's isn't).
    // (a big plane's own tail is far behind it: 60% of the camera distance).
    float skip = plane ? (std::max)(12.0f, g_planeDistanceNow * 0.6f) : heli ? 10.0f : Player2DrivingCar() ? 3.5f : 0.0f;
    KeepCameraOutOfWalls(1, target, m->position, skip);
    if (shaking)
        for (int i = 0; i < 3; i++) m->position[i] += shakeOffset[i];
    m->right[3] = m->up[3] = m->forward[3] = 0.0f;
    m->position[3] = 1.0f;
}

// Game thread, around CCamera::Update for the main camera.
// The game has one FOV: the camera's +0x618 (radians). Setting it marks the camera (+4), and then
// the update rebuilds the projection from it (FUN_00967660). Player 2's frames run with his own, and
// the rebuild is forced around them, so nothing of the game's zoom shows in his half.
const uintptr_t kCameraWorldOffset = 0x264, kCameraRebuild = 0x4, kCameraFov = 0x618;
volatile bool g_p2Zoom;
volatile bool g_p2OwnFov = true;                    // "split p2fov on|off" (testing)
float g_gameFov;
bool g_fovSwapped;

void UsePlayer2Fov(const CameraUpdate& update)
{
    if (!g_p2OwnFov) return;
    uintptr_t camera = (uintptr_t)update.world - kCameraWorldOffset;
    static LARGE_INTEGER frequency, last;
    LARGE_INTEGER now;
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&now);
    float dt = last.QuadPart ? (float)(now.QuadPart - last.QuadPart) / (float)frequency.QuadPart : 0.0f;
    last = now;
    if (dt > 0.1f) dt = 0.1f;
    g_p2Fov += ((g_p2Zoom ? kP2ZoomFov : kP2Fov) - g_p2Fov) * (1.0f - expf(-kP2ZoomRate * dt));
    g_gameFov = *(float*)(camera + kCameraFov);
    g_fovSwapped = true;
    *(float*)(camera + kCameraFov) = g_p2Fov;
    *(uint8_t*)(camera + kCameraRebuild) = 1;
}

void RestoreGameFov(const CameraUpdate& update)
{
    if (!g_fovSwapped) return;
    g_fovSwapped = false;
    uintptr_t camera = (uintptr_t)update.world - kCameraWorldOffset;
    *(float*)(camera + kCameraFov) = g_gameFov;
    *(uint8_t*)(camera + kCameraRebuild) = 1;
}

void OnGameCamera(const CameraUpdate& update, bool after)
{
    if (!after) {
        SplitRenderRestoreLogicWorld();
        int view = (InterlockedIncrement(&g_frame) + g_viewFlip) & 1;
        g_updatingView = view;
        if (view == 1) {
            MakePlayer2Camera(update.world);
            UsePlayer2Fov(update);
        } else {
            PushPlayer1Back(update.world);
        }
        g_viewFov[view] = *(float*)((uintptr_t)update.world - kCameraWorldOffset + kCameraFov);
        memcpy(g_forward[view], update.world->forward, sizeof(g_forward[view]));
        memcpy(g_right[view], update.world->right, sizeof(g_right[view]));
        memcpy(g_position[view], update.world->position, sizeof(g_position[view]));
        g_havePosition[view] = true;
        g_world[view] = *update.world;
        if (g_havePrevious[view]) *update.previous = g_previous[view];
    } else {
        RestoreGameFov(update);
        g_previous[g_updatingView] = *update.current;
        g_havePrevious[g_updatingView] = true;
        // The matrix the view was derived from, which the render camera copies later.
        History& h = g_history[g_updatingView];
        memcpy(&h.m[h.next % kHistory], update.current->m[0], sizeof(CameraMatrix));
        h.next++;
        // Game logic reads the camera's world matrix (+0x24): Player 1's camera controller blends
        // from it (Player 1's camera jumped to Player 2's during grapples) and aiming casts from
        // it. After Player 2's frame it goes back to Player 1's; the derived set keeps Player 2's
        // view for rendering.
        if (g_updatingView == 0) {
            if (g_player1Pushed) {
                // The game (aiming, the camera controller next frame) keeps the camera where it put it.
                g_logicWorld = g_player1Unpushed;
                g_haveLogicWorld = true;
                SetMainCameraWorld(g_logicWorld);
            } else {
                g_haveLogicWorld = ReadMainCamera(&g_logicWorld);
            }
        } else {
            // Player 2's terrain copies are updated with the whole camera as it is now.
            Terrain2CapturePlayer2Camera((const uint8_t*)update.world - 0x264);
            if (g_keepPlayer2World) g_logicWorldPending = g_haveLogicWorld;
            else if (g_haveLogicWorld) SetMainCameraWorld(g_logicWorld);
        }
        World2SyncRoadCell();
    }
}

} // namespace

int SplitRenderGameView()
{
    return g_enabled ? g_updatingView : 0;
}

long SplitRenderGameFrames()
{
    return g_frame;
}

void SplitRenderSetHeliCamera(float distance, float height)
{
    if (distance > 1.0f && distance < 200.0f) g_p2HeliDistance = distance;
    if (height > -20.0f && height < 50.0f) g_p2HeliTargetHeight = height;
}

void SplitRenderSetPlaneCamera(float distance, float height)
{
    if (distance > 1.0f && distance < 200.0f) g_p2PlaneDistance = distance;
    if (height > -20.0f && height < 50.0f) g_p2PlaneTargetHeight = height;
}

void SplitRenderSetBigPlaneCamera(float distance, float height)
{
    if (distance > 1.0f && distance < 500.0f) g_bigPlanes[0].distance = distance;
    if (height > -50.0f && height < 100.0f) g_bigPlanes[0].height = height;
}

void SplitRenderSetBikeCamera(float back, float down)
{
    if (back > -10.0f && back < 50.0f) g_p2BikeExtraDistance = back;
    if (down > -5.0f && down < 1.7f) g_p2BikeLower = down;
}

void SplitRenderSetFallLead(float radiansPerLean)
{
    g_p2FallLead = radiansPerLean;
}

void SplitRenderSetHudCorner(bool on)
{
    g_hudCorner = on;
}

void SplitRenderSetHudMask(bool on)
{
    g_hudMask = on;
}

void SplitRenderSetFallShake(float degreesPerStrength)
{
    g_p2ShakeScale = degreesPerStrength;
}

void SplitRenderFlipViews()
{
    InterlockedXor(&g_viewFlip, 1);
}

namespace {

float Dot3(const float* a, const float* b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// Which player's camera the render thread is drawing with right now (-1 if unknown). The render
// camera's matrix is a copy of one the game thread just gave a view, so it's matched exactly
// against each view's recent matrices. (Comparing directions alone mixed the views up whenever
// both cameras pointed the same way, e.g. during Player 1's grapple, showing Player 2's picture
// in Player 1's half.) If nothing matches, the closest position and direction wins.
int CurrentRenderView()
{
    CameraMatrix rc;
    if (!ReadRenderCamera(&rc, true)) return -1;
    for (int v = 0; v < 2; v++)
        for (int i = 0; i < kHistory; i++)
            if (!memcmp(&g_history[v].m[i], &rc, sizeof(rc))) {
                g_exactMatches++;
                return v;
            }
    g_fallbackMatches++;
    float score[2];
    for (int v = 0; v < 2; v++) {
        float d[3] = { rc.position[0] - g_position[v][0], rc.position[1] - g_position[v][1], rc.position[2] - g_position[v][2] };
        score[v] = Dot3(d, d) - 4.0f * Dot3(rc.forward, g_forward[v]);
    }
    return score[1] < score[0] ? 1 : 0;
}

// Motion blur keeps one previous view-projection; keep one per player instead.
float g_blurPrevious[2][16];
bool g_haveBlurPrevious[2];
int g_lastBlurView = -1;

// The present count when the motion blur pass last ran (the end of the 3D scene, before the HUD).
volatile long g_blurPresent = -1000;
volatile bool g_hudBlurGate = true;  // "split hudblurgate on|off" (testing)
long g_hudBeforeLastPresent, g_hudBeforeLastBlur, g_hudBeforeTaken, g_hudBeforeSkipped; // testing

// Render thread, at the start of the motion blur pass.
void OnMotionBlur(float previous[16])
{
    g_blurPresent = SplitRenderPresents();
    int view = CurrentRenderView();
    if (view < 0) return;
    // What's stored now was left by the last pass, i.e. the last view drawn.
    if (g_lastBlurView >= 0) {
        memcpy(g_blurPrevious[g_lastBlurView], previous, sizeof(g_blurPrevious[0]));
        g_haveBlurPrevious[g_lastBlurView] = true;
    }
    if (g_haveBlurPrevious[view]) memcpy(previous, g_blurPrevious[view], sizeof(g_blurPrevious[0]));
    g_lastBlurView = view;
}

// --- HUD corner in each half ---------------------------------------------------------------------
// The minimap (and the heat indicator beside it) is drawn into the full frame's top-left corner,
// which the band leaves out. That corner of each view's frame is copied into a scratch texture and
// drawn into the view's cached band through a mask: only a circle around the minimap (its ring and
// the icons on it) replaces the band's pixels, the rest of the corner (scenery from another part of
// the frame) is discarded. Sizes are fractions of the frame height (measured at 1920x1080).
const char kHudShader[] = R"(
cbuffer Params : register(b0) { float4 rect; float4 circle; float4 icon; float4 uvRect; };  // rect: NDC; circles: centre, radius (texels)
Texture2D tex : register(t0);
Texture2D before : register(t1);
SamplerState smp : register(s0);
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut VSMain(uint id : SV_VertexID)
{
    float2 uv = float2(id & 1, id >> 1);
    VSOut o;
    o.pos = float4(lerp(rect.xy, rect.zw, uv), 0, 1);
    o.uv = lerp(uvRect.xy, uvRect.zw, uv);
    return o;
}
float4 PSMain(VSOut i) : SV_Target
{
    float2 size;
    tex.GetDimensions(size.x, size.y);
    float2 p = i.uv * size;
    if (circle.w > 0.5 && distance(p, circle.xy) > circle.z && distance(p, icon.xy) > icon.z) discard;
    return tex.Sample(smp, i.uv);
}
// The HUD alone: where the finished frame differs from the frame before the HUD pass. With a
// full-screen overlay pass among them (circle.y = 1), only what changed before or after it.
Texture2D preOverlay : register(t2);
Texture2D postOverlay : register(t3);
float4 PSStrip(VSOut i) : SV_Target
{
    float3 a = tex.Sample(smp, i.uv).rgb, d = abs(a - before.Sample(smp, i.uv).rgb);
    if (circle.y > 0.5)
        d = max(abs(preOverlay.Sample(smp, i.uv).rgb - before.Sample(smp, i.uv).rgb),
                abs(a - postOverlay.Sample(smp, i.uv).rgb));
    float m = saturate(max(d.r, max(d.g, d.b)) * circle.x);
    if (m <= 0.0) discard;
    return float4(a, m);
}
)";
// The minimap's outer ring, and the icon at its lower left (just outside the ring).
const float kMinimapCenterX = 0.124f, kMinimapCenterY = 0.117f, kMinimapRadius = 0.093f; // of the height
const float kMinimapIconX = -0.082f, kMinimapIconY = 0.066f, kMinimapIconRadius = 0.018f; // from the centre

struct HudResources {
    ID3D10VertexShader* vs;
    ID3D10PixelShader* ps;
    ID3D10Buffer* params;
    ID3D10SamplerState* sampler;
    ID3D10BlendState* blend;
    ID3D10RasterizerState* raster;
    ID3D10DepthStencilState* depth;
    ID3D10StateBlock* stateBlock;
    ID3D10Texture2D* corner; // scratch copy of the frame's corner
    ID3D10ShaderResourceView* cornerSrv;
    ID3D10RenderTargetView* cacheRtv[2];
    ID3D10PixelShader* stripPs;
    ID3D10BlendState* stripBlend;
    ID3D10Texture2D* before, *after;  // the whole frame right before the HUD pass, and finished
    ID3D10ShaderResourceView* beforeSrv, *afterSrv;
    ID3D10Texture2D* preOverlay, *postOverlay; // around a full-screen overlay pass
    ID3D10ShaderResourceView* preOverlaySrv, *postOverlaySrv;
    bool haveBefore;                  // before holds this frame
    bool haveOverlay;                 // an overlay pass drew this frame (pre/post hold it)
    bool failed;
};
HudResources g_hud;

template <typename T> void SafeRelease(T*& p)
{
    if (p) { p->Release(); p = nullptr; }
}

// Full-screen overlay passes (the low-health heartbeat tint; user saw the scenery of the rows
// outside the band bleed into both halves while it pulsed: everything the overlay touched counted
// as HUD). Each GUI pass's drawn samples are counted with an occlusion query, read back on a later
// frame without waiting; a pass with at most two queued items that covers more than half the
// screen is an overlay, and the strips leave its change out.
struct HudPassQuery {
    uintptr_t pass;
    ID3D10Query* query;
    bool pending;   // begun and ended, result not read yet
    bool running;   // begun this draw
    bool overlay;
    float coverage; // testing: last measured share of the screen
    int queued;     // testing
};
const int kMaxHudPassQueries = 64;
HudPassQuery g_hudPassQueries[kMaxHudPassQueries];
const float kOverlayCoverage = 0.5f;
const int kOverlayMaxQueued = 2;
long g_overlayFrames;  // testing: frames with an overlay pass

void ReleaseHudPassQueries()
{
    for (HudPassQuery& q : g_hudPassQueries) {
        SafeRelease(q.query);
        q = {};
    }
}

HudPassQuery* HudPassQueryFor(uintptr_t pass)
{
    HudPassQuery* free = nullptr;
    for (HudPassQuery& q : g_hudPassQueries) {
        if (q.pass == pass) return &q;
        if (!q.pass && !free) free = &q;
    }
    if (free) *free = { pass };
    return free;
}

// The render target the GUI draws to, when it is the back buffer (copies only then).
ID3D10Texture2D* HudTarget()
{
    ID3D10RenderTargetView* rtv = nullptr;
    g_device->OMGetRenderTargets(1, &rtv, nullptr);
    if (!rtv) return nullptr;
    ID3D10Resource* target = nullptr;
    rtv->GetResource(&target);
    rtv->Release();
    ID3D10Texture2D* texture = nullptr;
    if (target) {
        target->QueryInterface(__uuidof(ID3D10Texture2D), (void**)&texture);
        target->Release();
    }
    if (!texture) return nullptr;
    D3D10_TEXTURE2D_DESC d = {};
    texture->GetDesc(&d);
    if (d.Width == g_width && d.Height == g_height && d.Format == g_format && d.SampleDesc.Count == 1) return texture;
    texture->Release();
    return nullptr;
}

void ReleaseHud()
{
    SafeRelease(g_hud.vs); SafeRelease(g_hud.ps); SafeRelease(g_hud.params); SafeRelease(g_hud.sampler);
    SafeRelease(g_hud.blend); SafeRelease(g_hud.raster); SafeRelease(g_hud.depth); SafeRelease(g_hud.stateBlock);
    SafeRelease(g_hud.corner); SafeRelease(g_hud.cornerSrv);
    SafeRelease(g_hud.cacheRtv[0]); SafeRelease(g_hud.cacheRtv[1]);
    SafeRelease(g_hud.stripPs); SafeRelease(g_hud.stripBlend);
    SafeRelease(g_hud.before); SafeRelease(g_hud.after); SafeRelease(g_hud.beforeSrv); SafeRelease(g_hud.afterSrv);
    SafeRelease(g_hud.preOverlay); SafeRelease(g_hud.postOverlay);
    SafeRelease(g_hud.preOverlaySrv); SafeRelease(g_hud.postOverlaySrv);
    ReleaseHudPassQueries();
    g_hud.haveBefore = g_hud.haveOverlay = false;
    g_hud.failed = false;
}

void ReleaseCache()
{
    ReleaseHud();
    for (int i = 0; i < 2; i++) {
        if (g_cache[i]) g_cache[i]->Release();
        g_cache[i] = nullptr;
        g_haveView[i] = false;
    }
    if (g_device) g_device->Release();
    g_device = nullptr;
}

typedef HRESULT(WINAPI* D3DCompile_t)(const void*, SIZE_T, const char*, const void*, void*, const char*, const char*,
                                      UINT, UINT, ID3D10Blob**, ID3D10Blob**);

ID3D10Blob* CompileHud(const char* entry, const char* target)
{
    static D3DCompile_t compile;
    if (!compile)
        for (const char* name : { "d3dcompiler_47.dll", "d3dcompiler_43.dll" })
            if (HMODULE m = LoadLibraryA(name))
                if ((compile = (D3DCompile_t)GetProcAddress(m, "D3DCompile")) != nullptr) break;
    if (!compile) return nullptr;
    ID3D10Blob *code = nullptr, *errors = nullptr;
    if (FAILED(compile(kHudShader, sizeof(kHudShader) - 1, "jc2ss_hud", nullptr, nullptr, entry, target, 0, 0, &code, &errors)))
        Log("HUD shader %s failed: %s", entry, errors ? (const char*)errors->GetBufferPointer() : "?");
    SafeRelease(errors);
    return code;
}

// Created with the cache (same device and size); the corner texture has the corner's size.
bool EnsureHud(ID3D10Device* device, UINT cornerWidth, UINT cornerHeight)
{
    if (g_hud.failed) return false;
    if (g_hud.corner) return true;
    g_hud.failed = true; // until everything below exists
    ID3D10Blob* vs = CompileHud("VSMain", "vs_4_0");
    ID3D10Blob* ps = CompileHud("PSMain", "ps_4_0");
    ID3D10Blob* strip = CompileHud("PSStrip", "ps_4_0");
    bool ok = vs && ps && strip && SUCCEEDED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), &g_hud.vs)) &&
              SUCCEEDED(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), &g_hud.ps)) &&
              SUCCEEDED(device->CreatePixelShader(strip->GetBufferPointer(), strip->GetBufferSize(), &g_hud.stripPs));
    SafeRelease(vs);
    SafeRelease(ps);
    SafeRelease(strip);
    if (!ok) return false;
    D3D10_BUFFER_DESC bd = { 64, D3D10_USAGE_DEFAULT, D3D10_BIND_CONSTANT_BUFFER, 0, 0 };
    D3D10_SAMPLER_DESC sd = {};
    sd.Filter = D3D10_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D10_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D10_FLOAT32_MAX;
    D3D10_BLEND_DESC blend = {};
    blend.RenderTargetWriteMask[0] = D3D10_COLOR_WRITE_ENABLE_ALL;
    D3D10_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D10_FILL_SOLID;
    rd.CullMode = D3D10_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    D3D10_DEPTH_STENCIL_DESC dd = {};
    D3D10_STATE_BLOCK_MASK mask;
    D3D10StateBlockMaskEnableAll(&mask);
    D3D10_TEXTURE2D_DESC td = {};
    td.Width = cornerWidth;
    td.Height = cornerHeight;
    td.MipLevels = td.ArraySize = 1;
    td.Format = g_format;
    td.SampleDesc.Count = 1;
    td.Usage = D3D10_USAGE_DEFAULT;
    td.BindFlags = D3D10_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateBuffer(&bd, nullptr, &g_hud.params)) || FAILED(device->CreateSamplerState(&sd, &g_hud.sampler)) ||
        FAILED(device->CreateBlendState(&blend, &g_hud.blend)) || FAILED(device->CreateRasterizerState(&rd, &g_hud.raster)) ||
        FAILED(device->CreateDepthStencilState(&dd, &g_hud.depth)) || FAILED(D3D10CreateStateBlock(device, &mask, &g_hud.stateBlock)) ||
        FAILED(device->CreateTexture2D(&td, nullptr, &g_hud.corner)) ||
        FAILED(device->CreateShaderResourceView(g_hud.corner, nullptr, &g_hud.cornerSrv)))
        return false;
    for (int i = 0; i < 2; i++)
        if (FAILED(device->CreateRenderTargetView(g_cache[i], nullptr, &g_hud.cacheRtv[i]))) return false;
    D3D10_BLEND_DESC alpha = blend;
    alpha.BlendEnable[0] = TRUE;
    alpha.SrcBlend = D3D10_BLEND_SRC_ALPHA;
    alpha.DestBlend = D3D10_BLEND_INV_SRC_ALPHA;
    alpha.BlendOp = D3D10_BLEND_OP_ADD;
    alpha.SrcBlendAlpha = D3D10_BLEND_ONE;
    alpha.DestBlendAlpha = D3D10_BLEND_ZERO;
    alpha.BlendOpAlpha = D3D10_BLEND_OP_ADD;
    D3D10_TEXTURE2D_DESC whole = td;
    whole.Width = g_width;
    whole.Height = g_height;
    if (FAILED(device->CreateBlendState(&alpha, &g_hud.stripBlend)) || FAILED(device->CreateTexture2D(&whole, nullptr, &g_hud.before)) ||
        FAILED(device->CreateTexture2D(&whole, nullptr, &g_hud.after)) ||
        FAILED(device->CreateShaderResourceView(g_hud.before, nullptr, &g_hud.beforeSrv)) ||
        FAILED(device->CreateShaderResourceView(g_hud.after, nullptr, &g_hud.afterSrv)) ||
        FAILED(device->CreateTexture2D(&whole, nullptr, &g_hud.preOverlay)) ||
        FAILED(device->CreateTexture2D(&whole, nullptr, &g_hud.postOverlay)) ||
        FAILED(device->CreateShaderResourceView(g_hud.preOverlay, nullptr, &g_hud.preOverlaySrv)) ||
        FAILED(device->CreateShaderResourceView(g_hud.postOverlay, nullptr, &g_hud.postOverlaySrv)))
        return false;
    g_hud.failed = false;
    Log("HUD corner: masked copy ready (%ux%u)", cornerWidth, cornerHeight);
    return true;
}

// Draws the frame's top-left corner (already in g_hud.corner) into the view's band, masked.
void DrawHudCorner(ID3D10Device* device, int view, UINT cornerWidth, UINT cornerHeight)
{
    float bandW = (float)g_width, bandH = (float)(g_height / 2);
    float h = (float)g_height;
    struct { float rect[4]; float circle[4]; float icon[4]; float uv[4]; } p = {
        { -1.0f, 1.0f, cornerWidth / bandW * 2.0f - 1.0f, 1.0f - cornerHeight / bandH * 2.0f },
        { kMinimapCenterX * h, kMinimapCenterY * h, kMinimapRadius * h, g_hudMask ? 1.0f : 0.0f },
        { (kMinimapCenterX + kMinimapIconX) * h, (kMinimapCenterY + kMinimapIconY) * h, kMinimapIconRadius * h, 0.0f },
        { 0.0f, 0.0f, 1.0f, 1.0f },
    };
    g_hud.stateBlock->Capture();
    D3D10_VIEWPORT vp = { 0, 0, g_width, g_height / 2, 0.0f, 1.0f };
    float blendFactor[4] = {};
    device->OMSetRenderTargets(1, &g_hud.cacheRtv[view], nullptr);
    device->OMSetBlendState(g_hud.blend, blendFactor, 0xFFFFFFFF);
    device->OMSetDepthStencilState(g_hud.depth, 0);
    device->RSSetViewports(1, &vp);
    device->RSSetState(g_hud.raster);
    device->IASetInputLayout(nullptr);
    device->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    device->SOSetTargets(0, nullptr, nullptr);
    device->VSSetShader(g_hud.vs);
    device->GSSetShader(nullptr);
    device->PSSetShader(g_hud.ps);
    device->UpdateSubresource(g_hud.params, 0, nullptr, &p, 0, 0);
    device->VSSetConstantBuffers(0, 1, &g_hud.params);
    device->PSSetConstantBuffers(0, 1, &g_hud.params);
    device->PSSetShaderResources(0, 1, &g_hud.cornerSrv);
    device->PSSetSamplers(0, 1, &g_hud.sampler);
    device->Draw(4, 0);
    g_hud.stateBlock->Apply();
}

// The HUD (not the scenery) of the frame's rows above and below the band: the top quarter goes to
// the top of the view's band, the bottom quarter (weapons, weapon switch, subtitles) to its bottom.
void DrawHudStrips(ID3D10Device* device, int view)
{
    struct { float rect[4]; float circle[4]; float icon[4]; float uv[4]; } p = {};
    p.circle[0] = kHudStripGain;
    p.circle[1] = g_hud.haveOverlay ? 1.0f : 0.0f;
    g_hud.stateBlock->Capture();
    D3D10_VIEWPORT vp = { 0, 0, g_width, g_height / 2, 0.0f, 1.0f };
    float blendFactor[4] = {};
    device->OMSetRenderTargets(1, &g_hud.cacheRtv[view], nullptr);
    device->OMSetBlendState(g_hud.stripBlend, blendFactor, 0xFFFFFFFF);
    device->OMSetDepthStencilState(g_hud.depth, 0);
    device->RSSetViewports(1, &vp);
    device->RSSetState(g_hud.raster);
    device->IASetInputLayout(nullptr);
    device->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    device->SOSetTargets(0, nullptr, nullptr);
    device->VSSetShader(g_hud.vs);
    device->GSSetShader(nullptr);
    device->PSSetShader(g_hud.stripPs);
    ID3D10ShaderResourceView* views[4] = { g_hud.afterSrv, g_hud.beforeSrv, g_hud.preOverlaySrv, g_hud.postOverlaySrv };
    device->VSSetConstantBuffers(0, 1, &g_hud.params);
    device->PSSetConstantBuffers(0, 1, &g_hud.params);
    device->PSSetShaderResources(0, 4, views);
    device->PSSetSamplers(0, 1, &g_hud.sampler);
    // The top quarter's messages (mission started, settlement boxes) would sit right at the half's
    // top edge (user: move it down, fully visible); right of the minimap corner it goes a bit lower.
    float shift = g_hudTopStripShift * 2.0f; // of the band, in NDC
    float split = (float)kHudCornerWidth;   // of the width
    const float strips[3][8] = {
        // rect x0, y0, x1, y1 (band NDC), uv u0, v0, u1, v1 (full frame)
        { -1.0f, 1.0f, split * 2.0f - 1.0f, 0.0f, 0.0f, 0.0f, split, 0.25f },
        { split * 2.0f - 1.0f, 1.0f - shift, 1.0f, 0.0f - shift, split, 0.0f, 1.0f, 0.25f },
        { -1.0f, 0.0f, 1.0f, -1.0f, 0.0f, 0.75f, 1.0f, 1.0f },
    };
    for (const float* s : strips) {
        memcpy(p.rect, s, 4 * sizeof(float));
        memcpy(p.uv, s + 4, 4 * sizeof(float));
        device->UpdateSubresource(g_hud.params, 0, nullptr, &p, 0, 0);
        device->Draw(4, 0);
    }
    // The other player's popup and prompt panel: back to the frame without the HUD there.
    const float* hide[3];
    int hideCount = 0;
    if (g_hudHidePopup == view) {
        hide[hideCount++] = kHudPopupRects[0];
        hide[hideCount++] = kHudPopupRects[1];
    }
    if (g_hudHideFacePanel == view) hide[hideCount++] = g_hudFacePanelRect;
    if (hideCount) {
        device->OMSetBlendState(g_hud.blend, blendFactor, 0xFFFFFFFF);
        device->PSSetShader(g_hud.ps);
        device->PSSetShaderResources(0, 1, &g_hud.beforeSrv);
        p.circle[3] = 0.0f; // no mask
        for (int i = 0; i < hideCount; i++) {
            const float* r = hide[i];
            float rect[4] = { r[0] * 2.0f - 1.0f, 1.0f - r[2] * 2.0f, r[1] * 2.0f - 1.0f, 1.0f - r[3] * 2.0f };
            float uv[4] = { r[0], 0.25f + r[2] * 0.5f, r[1], 0.25f + r[3] * 0.5f };
            memcpy(p.rect, rect, sizeof(rect));
            memcpy(p.uv, uv, sizeof(uv));
            device->UpdateSubresource(g_hud.params, 0, nullptr, &p, 0, 0);
            device->Draw(4, 0);
        }
    }
    g_hud.stateBlock->Apply();
}

bool EnsureCache(ID3D10Device* device, const D3D10_TEXTURE2D_DESC& bb)
{
    if (g_device == device && g_width == bb.Width && g_height == bb.Height && g_format == bb.Format) return true;
    ReleaseCache();

    D3D10_TEXTURE2D_DESC desc = {};
    desc.Width = bb.Width;
    desc.Height = bb.Height / 2;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = bb.Format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D10_USAGE_DEFAULT;
    desc.BindFlags = D3D10_BIND_RENDER_TARGET; // the masked HUD corner is drawn into it
    for (int i = 0; i < 2; i++) {
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &g_cache[i]))) {
            Log("Split cache texture creation failed");
            ReleaseCache();
            return false;
        }
    }
    g_device = device;
    g_device->AddRef();
    g_width = bb.Width;
    g_height = bb.Height;
    g_format = bb.Format;
    return true;
}

// The physics floating origin (FUN_009A1940 in the world update) follows whatever the main camera
// holds: with Player 2's world in it on his frames it would jump between the players, so while
// that is the case it never shifts there ([0x01092AA0], the distance it shifts at) and world2
// shifts it for Player 1 instead.
const uintptr_t kOriginShiftDistance = 0x01092AA0;
float g_originShiftDistance;

void ApplyOriginShiftDistance()
{
    float* shift = (float*)kOriginShiftDistance;
    DWORD old;
    if (!VirtualProtect(shift, 4, PAGE_READWRITE, &old)) return;
    if (!g_originShiftDistance) g_originShiftDistance = *shift;
    *shift = g_enabled && g_keepPlayer2World ? 1.0e9f : g_originShiftDistance;
    VirtualProtect(shift, 4, old, &old);
}

void SetEnabled(bool on)
{
    g_enabled = on;
    ApplyOriginShiftDistance();
    if (!on) {
        SplitRenderRestoreLogicWorld();
        // Only when Player 2 is gone; a pause keeps his spawn points held.
        if (SplitscreenPlayer2Status() != Player2Status::Joined) {
            World2ReleaseSpawnPoints();
            g_havePosition[0] = g_havePosition[1] = false;
        }
    }
    g_haveView[0] = g_haveView[1] = false;
    g_havePrevious[0] = g_havePrevious[1] = false;
    memset(g_history, 0, sizeof(g_history));
    g_haveLogicWorld = false;
    g_haveBlurPrevious[0] = g_haveBlurPrevious[1] = false;
    g_lastBlurView = -1;
    SetCameraOverride(on ? OnGameCamera : nullptr);
    SetMotionBlurCallback(on ? OnMotionBlur : nullptr);
    Log("Split render %s", on ? "on" : "off");
}

} // namespace

bool SplitViewGroundAxes(int view, float forward[2], float right[2])
{
    if (!g_enabled || (view != 0 && view != 1)) return false;
    // The camera looks along -forward; keep the ground (x, z) part of each axis.
    float f[2] = { -g_forward[view][0], -g_forward[view][2] }, r[2] = { g_right[view][0], g_right[view][2] };
    float fl = sqrtf(f[0] * f[0] + f[1] * f[1]), rl = sqrtf(r[0] * r[0] + r[1] * r[1]);
    if (fl < 1e-4f || rl < 1e-4f) return false;
    for (int i = 0; i < 2; i++) {
        forward[i] = f[i] / fl;
        right[i] = r[i] / rl;
    }
    return true;
}

bool SplitViewMatrix(int view, CameraMatrix* out)
{
    if (!g_enabled || (view != 0 && view != 1) || !g_havePrevious[view]) return false;
    *out = g_world[view];
    return true;
}

// Player markers (user: each player sees where the other one is): the other player's head
// projected with this view's camera and FOV (the half shows the full frame's middle band, NDC y
// -0.5..0.5). Off the half or behind the camera, the marker sits on the half's edge toward him.
const float kMarkerHeight = 2.3f, kMarkerEdge = 0.04f;

bool SplitRenderMarker(int view, float out[2], float* distance, bool* onScreen)
{
    if (!g_enabled || (view != 0 && view != 1) || !g_havePosition[view] || g_viewFov[view] <= 0.0f) return false;
    float target[3];
    if (view == 1) {
        if (!Player1CharacterPosition(target)) return false;
    } else {
        float body[16];
        if (!Player2BodyMatrix(body)) return false;
        memcpy(target, &body[12], sizeof(target));
    }
    target[1] += kMarkerHeight;
    const CameraMatrix& m = g_world[view];
    float d[3] = { target[0] - m.position[0], target[1] - m.position[1], target[2] - m.position[2] };
    *distance = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    float cx = d[0] * m.right[0] + d[1] * m.right[1] + d[2] * m.right[2];
    float cy = d[0] * m.up[0] + d[1] * m.up[1] + d[2] * m.up[2];
    float cz = -(d[0] * m.forward[0] + d[1] * m.forward[1] + d[2] * m.forward[2]);
    float t = tanf(g_viewFov[view] * 0.5f);
    bool behind = cz < 0.1f;
    float x = 0.5f, y = 0.5f;
    if (!behind) {
        x = (cx / (cz * t * g_frameAspect) + 1.0f) * 0.5f;
        y = 0.5f - cy / (cz * t);
    }
    *onScreen = !behind && x >= 0.0f && x <= 1.0f && y >= 0.0f && y <= 1.0f;
    if (!*onScreen) {
        // From the half's centre toward him (behind: by his side, kept at the bottom edge's side).
        float dx = behind ? cx : x - 0.5f, dy = behind ? fabsf(cy) + 0.001f : y - 0.5f;
        float ax = fabsf(dx), ay = fabsf(dy);
        if (ax < 1e-6f && ay < 1e-6f) dy = ay = 1.0f;
        float k = 1e9f;
        if (ax > 1e-6f) k = (0.5f - kMarkerEdge) / ax;
        if (ay > 1e-6f && (0.5f - kMarkerEdge) / ay < k) k = (0.5f - kMarkerEdge) / ay;
        x = 0.5f + dx * k;
        y = 0.5f + dy * k;
    }
    out[0] = x;
    out[1] = y;
    return true;
}

bool SplitViewCamera(int view, float position[3], float direction[3])
{
    if (view != 0 && view != 1) return false;
    // While the split is off for a menu or the map (the pause menu) with Player 2 still in, each
    // view is where it was last: despawning by the main camera alone then removed the helicopter
    // Player 2 sat in, 5 km from Player 1 (user, 2026-10-29).
    if (!g_enabled && !(SplitscreenPlayer2Status() == Player2Status::Joined && g_havePosition[view])) return false;
    for (int i = 0; i < 3; i++) {
        position[i] = g_position[view][i];
        direction[i] = -g_forward[view][i]; // the camera looks along -forward
    }
    Normalize(direction);
    return true;
}

void SplitRenderRestoreLogicWorld()
{
    if (!g_logicWorldPending) return;
    g_logicWorldPending = false;
    SetMainCameraWorld(g_logicWorld);
}

bool SplitRenderPlayer1Position(float out[3])
{
    if (!g_enabled || !g_haveLogicWorld) return false;
    memcpy(out, g_logicWorld.position, 3 * sizeof(float));
    return true;
}

void SplitRenderPushPlayer1Back(bool on)
{
    g_pushPlayer1Back = on;
}

void SplitRenderCastCameraRays()
{
    if (!g_cameraCollision) return;
    for (WallRay& ray : g_wallRays) {
        if (!ray.wanted) continue;
        ray.wanted = false;
        ray.clear = ClearDistance(ray.from, ray.dir, ray.length, ray.skip);
        ray.answered = true;
    }
}

std::string SplitRenderCameraWalls(const char* args)
{
    while (*args == ' ') args++;
    int layers;
    if (!strcmp(args, "on") || !strcmp(args, "off")) g_cameraCollision = !strcmp(args, "on");
    else if (sscanf_s(args, "layers %d", &layers) == 1) g_wallLayers = layers;
    float ray[7];
    if (sscanf_s(args, "test %f %f %f %f %f %f %f", &ray[0], &ray[1], &ray[2], &ray[3], &ray[4], &ray[5], &ray[6]) == 7) {
        Normalize(ray + 3);
        WallRay& test = g_wallRays[2];
        memcpy(test.from, ray, sizeof(test.from));
        memcpy(test.dir, ray + 3, sizeof(test.dir));
        test.length = ray[6];
        test.skip = 0.0f;
        test.answered = false;
        test.wanted = true; // cast by the next brain think: "split camwalls" reports it
        return "ok test ray queued";
    }
    char text[500];
    const WallRay& test = g_wallRays[2];
    if (test.answered && !strcmp(args, "result")) {
        sprintf_s(text, "ok test ray clear for %.2f of %.2f m", test.clear, test.length);
        return text;
    }
    const uint32_t* r = g_lastRayResult;
    sprintf_s(text, "ok camera wall collision %s (layers %d), allowed distance P1 %.2f P2 %.2f, pulled in frames P1 %ld P2 %ld, "
              "last ray %08X %.2f %.2f %.2f frac %.3f %08X %08X keys %08X %08X %08X %08X",
              g_cameraCollision ? "on" : "off", g_wallLayers, g_wallDistance[0], g_wallDistance[1], g_wallHits[0], g_wallHits[1],
              r[0], *(float*)&r[1], *(float*)&r[2], *(float*)&r[3], *(float*)&r[4], r[5], r[6], r[7], r[8], r[9], r[10]);
    return text;
}

bool SplitRenderPlayer1World(CameraMatrix* out)
{
    if (!g_enabled || !g_haveLogicWorld || !g_keepPlayer2World) return false;
    *out = g_logicWorld;
    return true;
}

void SplitRenderKeepPlayer2World(bool on)
{
    g_keepPlayer2World = on;
    ApplyOriginShiftDistance();
}

void SplitRenderTurnPlayer2Camera(float radians)
{
    g_p2Yaw += radians;
}

void SplitRenderPitchPlayer2Camera(float radians)
{
    g_p2Pitch = radians;
}

void SplitRenderFacePlayer2Camera(const float forward[3])
{
    // Same convention as the camera matrix ("cam"): the camera sits along +forward from its target.
    g_p2Yaw = atan2f(forward[0], forward[2]);
    g_p2Pitch = asinf(forward[1] < -1 ? -1 : forward[1] > 1 ? 1 : forward[1]);
    g_p2YawSet = true;
}

void SplitRenderBeforeHud()
{
    static bool logged;
    if (!g_enabled || !g_hudStrips || !g_device || !g_hud.before || g_hud.haveBefore) return;
    // A GUI pass that draws before the 3D scene is done (user saw whole strips of scenery over
    // both halves: the frame was kept before the scene, so everything counted as HUD). While the
    // motion blur pass runs (the scene's last step) the frame is kept only after it; with motion
    // blur off in the settings there is no such marker.
    long present = SplitRenderPresents();
    g_hudBeforeLastPresent = present;
    g_hudBeforeLastBlur = g_blurPresent;
    if (g_hudBlurGate && present - g_blurPresent < 30 && g_blurPresent != present) {
        g_hudBeforeSkipped++;
        return;
    }
    g_hudBeforeTaken++;
    ID3D10RenderTargetView* rtv = nullptr;
    g_device->OMGetRenderTargets(1, &rtv, nullptr);
    if (!rtv) return;
    ID3D10Resource* target = nullptr;
    rtv->GetResource(&target);
    ID3D10Texture2D* texture = nullptr;
    if (target && SUCCEEDED(target->QueryInterface(__uuidof(ID3D10Texture2D), (void**)&texture))) {
        D3D10_TEXTURE2D_DESC d = {};
        texture->GetDesc(&d);
        if (!logged) {
            logged = true;
            Log("HUD strips: first HUD pass, target %ux%u format %d (back buffer %ux%u format %d), thread %lu", d.Width, d.Height,
                (int)d.Format, g_width, g_height, (int)g_format, GetCurrentThreadId());
        }
        if (d.Width == g_width && d.Height == g_height && d.Format == g_format && d.SampleDesc.Count == 1) {
            g_device->CopyResource(g_hud.before, texture);
            g_hud.haveBefore = true;
        }
        texture->Release();
    }
    if (target) target->Release();
    rtv->Release();
}

void SplitRenderHudPassBegin(uintptr_t pass, int queued)
{
    SplitRenderBeforeHud();
    if (!g_enabled || !g_hudStrips || !g_device || !g_hud.preOverlay) return;
    HudPassQuery* q = HudPassQueryFor(pass);
    if (!q) return;
    if (q->pending) {
        UINT64 samples = 0;
        if (g_device && q->query->GetData(&samples, sizeof(samples), D3D10_ASYNC_GETDATA_DONOTFLUSH) == S_OK) {
            q->pending = false;
            q->coverage = (float)samples / ((float)g_width * g_height);
            q->queued = queued;
            q->overlay = queued <= kOverlayMaxQueued && q->coverage > kOverlayCoverage;
        }
    }
    if (!q->query) {
        D3D10_QUERY_DESC qd = { D3D10_QUERY_OCCLUSION, 0 };
        if (FAILED(g_device->CreateQuery(&qd, &q->query))) return;
    }
    if (q->overlay && g_hud.haveBefore) {
        if (ID3D10Texture2D* t = HudTarget()) {
            g_device->CopyResource(g_hud.preOverlay, t);
            t->Release();
        }
    }
    if (!q->pending) {
        q->query->Begin();
        q->running = true;
    }
}

void SplitRenderHudPassEnd(uintptr_t pass)
{
    if (!g_enabled || !g_device || !g_hud.preOverlay) return;
    HudPassQuery* q = HudPassQueryFor(pass);
    if (!q) return;
    if (q->running) {
        q->query->End();
        q->running = false;
        q->pending = true;
    }
    if (q->overlay && g_hud.haveBefore) {
        if (ID3D10Texture2D* t = HudTarget()) {
            g_device->CopyResource(g_hud.postOverlay, t);
            t->Release();
            if (!g_hud.haveOverlay) g_overlayFrames++;
            g_hud.haveOverlay = true;
        }
    }
}

void SplitRenderHidePopup(int view)
{
    g_hudHidePopup = view;
}

void SplitRenderHideFacePanel(int view)
{
    g_hudHideFacePanel = view;
}

void SplitRenderSetFacePanelRect(float left, float right, float top, float bottom)
{
    g_hudFacePanelRect[0] = left;
    g_hudFacePanelRect[1] = right;
    g_hudFacePanelRect[2] = top;
    g_hudFacePanelRect[3] = bottom;
}

void SplitRenderOwnPlayer2Fov(bool on)
{
    g_p2OwnFov = on;
}

void SplitRenderZoomPlayer2(bool on)
{
    g_p2Zoom = on;
}

bool SplitRenderPlayer2Zoomed()
{
    return g_p2Zoom;
}

void SplitRenderSetHudStrips(bool on)
{
    g_hudStrips = on;
}

void SplitRenderHudTopShift(float fraction)
{
    g_hudTopStripShift = fraction;
}

std::string SplitRenderHudBlurGate(const char* mode)
{
    if (!strcmp(mode, "on") || !strcmp(mode, "off")) g_hudBlurGate = !strcmp(mode, "on");
    char text[256];
    int overlays = 0;
    for (const HudPassQuery& q : g_hudPassQueries) overlays += q.overlay;
    sprintf_s(text, "HUD blur gate %s: last kept at present %ld, blur at %ld; kept %ld, skipped %ld; overlay passes %d, frames with one %ld",
              g_hudBlurGate ? "on" : "off", g_hudBeforeLastPresent, g_hudBeforeLastBlur, g_hudBeforeTaken,
              g_hudBeforeSkipped, overlays, g_overlayFrames);
    std::string out = text;
    for (const HudPassQuery& q : g_hudPassQueries) {
        if (!q.pass) continue;
        sprintf_s(text, "\n  pass %08X queued %d covers %.3f%s", (unsigned)q.pass, q.queued, q.coverage, q.overlay ? " overlay" : "");
        out += text;
    }
    return out;
}

volatile LONG g_presents;

long SplitRenderPresents()
{
    return g_presents;
}

void SplitRenderOnPresent(IDXGISwapChain* swapChain)
{
    InterlockedIncrement(&g_presents);
    bool want = SplitscreenActive();
    if (want != g_enabled) SetEnabled(want);
    if (!g_enabled) return;

    // Which player's camera was this frame drawn with?
    int view = CurrentRenderView();
    if (view < 0) return;
    if ((LONG)(GetTickCount() - g_nextMatchLog) >= 0) {
        g_nextMatchLog = GetTickCount() + 10000;
        if (g_fallbackMatches) Log("Split render: %u frames matched exactly, %u by closeness", g_exactMatches, g_fallbackMatches);
        g_exactMatches = g_fallbackMatches = 0;
    }

    ID3D10Device* device = nullptr;
    ID3D10Texture2D* backBuffer = nullptr;
    if (FAILED(swapChain->GetDevice(__uuidof(ID3D10Device), (void**)&device))) return;
    if (SUCCEEDED(swapChain->GetBuffer(0, __uuidof(ID3D10Texture2D), (void**)&backBuffer))) {
        D3D10_TEXTURE2D_DESC desc;
        backBuffer->GetDesc(&desc);
        if (desc.SampleDesc.Count == 1 && EnsureCache(device, desc)) {
            UINT half = desc.Height / 2;
            // Middle band of the full frame keeps the aspect ratio (crops top and bottom).
            g_frameAspect = (float)desc.Width / (float)desc.Height;
            D3D10_BOX band = { 0, desc.Height / 4, 0, desc.Width, desc.Height / 4 + half, 1 };
            device->CopySubresourceRegion(g_cache[view], 0, 0, 0, 0, backBuffer, 0, &band);
            // The HUD's minimap and heat indicator sit in the full frame's top-left corner, above
            // the band: that corner of this view's own frame goes into the band's top-left.
            if (g_hudCorner) {
                UINT w = (UINT)(desc.Width * kHudCornerWidth), h = (UINT)(desc.Height * kHudCornerHeight);
                if (h > half) h = half;
                D3D10_BOX corner = { 0, 0, 0, w, h, 1 };
                if (EnsureHud(device, w, h)) {
                    device->CopySubresourceRegion(g_hud.corner, 0, 0, 0, 0, backBuffer, 0, &corner);
                    DrawHudCorner(device, view, w, h);
                } else {
                    device->CopySubresourceRegion(g_cache[view], 0, 0, 0, 0, backBuffer, 0, &corner);
                }
                if (g_hud.haveBefore && g_hudStrips && g_hud.after) {
                    device->CopyResource(g_hud.after, backBuffer);
                    DrawHudStrips(device, view);
                }
                g_hud.haveBefore = g_hud.haveOverlay = false;
            }
            g_haveView[view] = true;
            for (int v = 0; v < 2; v++)
                if (g_haveView[v])
                    device->CopySubresourceRegion(backBuffer, 0, 0, v * half, 0, g_cache[v], 0, nullptr);
        }
        backBuffer->Release();
    }
    device->Release();
}
