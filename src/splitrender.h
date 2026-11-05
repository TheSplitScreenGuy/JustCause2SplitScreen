#pragma once

#include <stdint.h>
#include <string>

struct IDXGISwapChain;

// Prototype splitscreen by alternating frames: even game frames use Player 1's camera,
// odd frames Player 2's. Each presented frame is identified by the render thread's camera,
// its middle band is cached for that player, and both cached bands are composed into the
// top and bottom halves of the back buffer.
//
// Call from the Present hook, before the overlay. Turns itself on and off with
// SplitscreenActive().
void SplitRenderOnPresent(IDXGISwapChain* swapChain);

// Ground-plane (x, z) unit vectors a view's camera looks along and to its right
// (view 0 = Player 1, 1 = Player 2). False while the split is off.
bool SplitViewGroundAxes(int view, float forward[2], float right[2]);

// A view's whole camera world matrix as last given to the game. False while the split is off.
struct CameraMatrix;
bool SplitViewMatrix(int view, CameraMatrix* out);

// Where the other player's marker goes in this view's half (0..1 across, 0..1 down), his distance
// from the camera, and whether he is in view (else the marker is on the half's edge toward him).
// False while the split is off.
bool SplitRenderMarker(int view, float out[2], float* distance, bool* onScreen);

// A view's camera position and the unit direction it looks in. False while the split is off.
bool SplitViewCamera(int view, float position[3], float direction[3]);

// Testing: turns Player 2's camera around their body.
void SplitRenderTurnPlayer2Camera(float radians);

// Game thread: the view (0 or 1) the main camera was last updated for (0 when not split).
int SplitRenderGameView();
// Game thread: main camera updates (one per game frame) since the split turned on.
long SplitRenderGameFrames();
// Presents so far (testing: tells which frame a render-thread event belongs to).
long SplitRenderPresents();

// Testing ("split falllead <r>"): how far Player 2's free fall camera leads his turn.
void SplitRenderSetFallLead(float radiansPerLean);
// Testing ("split helicam <distance> [height]"): Player 2's helicopter camera, metres behind and
// above it.
void SplitRenderSetHeliCamera(float distance, float height);

// Testing ("split planecam <distance> [height]"): the same for planes.
void SplitRenderSetPlaneCamera(float distance, float height);
// Testing ("split bigplanecam <distance> [height]"): the Aeroliner 474's camera.
void SplitRenderSetBigPlaneCamera(float distance, float height);
// Testing ("split bikecam <back> <down>"): Player 2's motorbike camera, relative to on foot.
void SplitRenderSetBikeCamera(float back, float down);
// Testing ("split hudcorner on|off"): the HUD's top-left corner (minimap, heat) in each half.
void SplitRenderSetHudCorner(bool on);
// Testing ("split hudmask on|off"): off copies the whole corner rectangle instead of the circle.
void SplitRenderSetHudMask(bool on);
// Render thread, right before the HUD pass: keeps the frame so far, to tell the HUD from the scenery.
void SplitRenderBeforeHud();
// The weapon switch popup is left out of this view's half (-1: shown in both).
void SplitRenderHidePopup(int view);
// The button prompt panel ("Stunt jump B", right side) is left out of this view's half (-1: none).
void SplitRenderHideFacePanel(int view);
void SplitRenderSetFacePanelRect(float left, float right, float top, float bottom); // band fractions (testing)
// Testing ("split hudstrips on|off"): the HUD outside the band (bottom weapons, top) in each half.
void SplitRenderSetHudStrips(bool on);
// Render thread, around each GUI pass that has something to draw: keeps the HUD-less frame (the
// first), and frames around full-screen overlay passes, which the HUD strips leave out.
void SplitRenderHudPassBegin(uintptr_t pass, int queued);
void SplitRenderHudPassEnd(uintptr_t pass);
// How far down the top quarter's HUD (right of the minimap) goes in each half, as a fraction of
// the half's height ("split hudtop <fraction>").
void SplitRenderHudTopShift(float fraction);
// Testing ("split hudblurgate on|off|stats"): keep the HUD-less frame only after the motion blur pass.
std::string SplitRenderHudBlurGate(const char* mode);
// Testing ("split p2fov on|off"): Player 2's own FOV; off shows the game's (Player 1's) in his half.
void SplitRenderOwnPlayer2Fov(bool on);
// Player 2's aim zoom.
void SplitRenderZoomPlayer2(bool on);
bool SplitRenderPlayer2Zoomed();
// Testing ("split fallshake <deg>"): Player 2's free fall camera jitter per unit of strength.
void SplitRenderSetFallShake(float degreesPerStrength);

// Testing: shifts which frames draw which view by one frame.
void SplitRenderFlipViews();
void SplitRenderPitchPlayer2Camera(float radians); // testing: absolute, above horizontal
// Player 2's world matrix stays in the main camera through his frame's game update (default on,
// "split p2world off"); SplitRenderRestoreLogicWorld (after the frame hand-off) puts Player 1's back.
void SplitRenderKeepPlayer2World(bool on);
void SplitRenderRestoreLogicWorld();
// Player 1's camera position / world matrix after his last camera update (false when split is
// off; the world only while Player 2's world can be in the main camera, "split p2world").
bool SplitRenderPlayer1Position(float out[3]);
bool SplitRenderPlayer1World(CameraMatrix* out);
// Player 1's camera drawn as far behind him as Player 2's ("split p1near" turns it off).
void SplitRenderPushPlayer1Back(bool on);
// Testing ("split camwalls [on|off]"): both cameras stay out of buildings and objects; reports the
// distances allowed now and how often each camera was pulled in.
std::string SplitRenderCameraWalls(const char* args);
// Main thread, from a player's brain think: casts the rays the cameras asked for.
void SplitRenderCastCameraRays();
// Testing: Player 2's camera axis as "cam" prints it (forward; the camera looks along -forward).
void SplitRenderFacePlayer2Camera(const float forward[3]);
