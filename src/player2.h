#pragma once

#include <stdint.h>
#include <string>

// Player 2: a second CPlayer (with its own Rico) created with the game's own
// CCharacterManager::CreatePlayer, kept outside the manager's player slot so the rest of
// the game still treats Player 1 as "the player".

// Installs Player 2's hooks into game code. Call once from DllMain.
bool Player2InstallHooks();

// Game thread, once per frame (called from the camera update hook).
void Player2GameTick();

// Requests creation on the next game tick. Returns a status line.
std::string Player2RequestSpawn();

// Requests Player 2 to take over an NPC character as its body (player rules, controller, brain).
std::string Player2RequestPossess(uintptr_t npc);

// Creates a new NPC from Player 1's character info (Rico's model) with the game's character
// factory, possesses it as Player 2's body and places it next to Player 1.
std::string Player2RequestRicoBody();

// Updates Player 2's own CPlayer (and its Rico) every frame right after Player 1's update, with
// Player 2's input.
std::string Player2RequestDrive();

// Enables a character in the world like the spawn system does (0 = Player 2's body).
std::string Player2RequestEnable(uintptr_t character);

// Experiment: give the possessed body Rico's look.
std::string Player2RequestRicoLook();

// Refills clips and reserve ammo (up to the game's caps) for Player 1, and Player 2 when driven,
// on the next game tick.
void RequestAmmoRefill();

// Testing (F9): adds dollars to the money (shared, Player 1's) on the next game tick.
void RequestMoney(int dollars);

// Testing: logs both characters' fire conditions on the next game tick.
std::string Player2RequestFireCheck();

// Testing: runs Player 2's create-gear (parachute, grapple, grapple actions) again.
std::string Player2RequestGear();

// Testing: Player 2 dies (raw: the game's own death, with the death screen).
std::string Player2RequestKill(bool raw);

// Frame cap the game needs right now (0 = none): with "chutecap on", 60 while a player is under a
// parachute, whose flight depends on the frame rate. Off by default (see ParachuteDragTick).
// Render thread.
int Player2FrameCapNeeded();
void SetParachuteCap(bool on); // testing

// Lowers the parachute's drag for a moment after it is opened from a reel, above 60 FPS, so the
// reel's momentum carries like it does at 60. Game thread, every frame.
void ParachuteDragTick();
void SetReelChuteDragScale(float scale); // testing (1 = off)
// Testing: places Player 1 this many meters higher on the next game tick.
std::string Player1RequestLift(float meters);
// Testing ("god on|off"): keeps both players' health far above any single hit.
std::string PlayersGodMode(bool on);
// Testing ("p1jobcam on|off"): Player 1's update job swaps his camera world into the main camera.
std::string Player1JobCamera(bool on);
// Testing ("p2aimlend on|off"): Player 2's fire hook action attaches at his own aim target.
std::string Player2AimLend(bool on);
// Testing ("p2skyremap on|off"): Player 2's free fall stick remapped to Player 1's camera (off:
// as it is, like the parachute's).
std::string Player2SkyDiveRemap(bool on);
// Whether Player 2 is in free fall (movement controller CSkyDiveController), and his sideways
// lean then (controller state +0x14C, -1..1).
bool Player2InFreeFall(float* lean = nullptr);
// Whether Player 2 drives a car (CDriverSeatController): his camera sits farther back.
bool Player2DrivingCar();
// The model name of the vehicle Player 2 sits in ("V114_JumboJet"); false when none.
bool Player2VehicleModel(char* out, size_t size);
// Player 2 mans a mounted gun (flak cannon; CMountedGunController).
bool Player2OnMountedGun();
// Player 2 rides a motorbike (CMCController, vehicle controller class 3).
bool Player2RidingMotorbike();
// Whether Player 2 flies a helicopter (movement controller CHelicopterController): his camera then
// stays behind it and his right stick turns it, like Player 1's mouse.
bool Player2PilotingHelicopter();

// Whether Player 2 flies a plane (CAirPlaneController, any plane): his camera follows behind it.
bool Player2PilotingPlane();
// Player 2's free fall speed effect strength (his speed * 0.002, the game's own scale; 0 when not
// falling): shakes his camera and rumbles pad 2, like Player 1's own free fall does to his.
float Player2FallShake();
// Player 2's own camera shake (the game's, e.g. from sprinting), advanced by dt: the offset (yaw,
// pitch in radians, also added to the position in metres); false while he has none.
bool Player2CameraShake(float dt, float offset[3]);
// Game thread, frame object update: Player 2's grappling device update (FUN_0068BAF0), which the
// game runs for the manager's player only; it also hands his queued grapple ropes to the renderer.
void Player2DeviceUpdate();
// Testing ("p2skylog <ms>"): logs every free fall controller update for a while.
std::string Player2SkyDiveLog(unsigned ms);
// Testing ("p2minimap on|off"): Player 2's frames update the HUD minimap with his position.
std::string Player2MinimapPerPlayer(bool on);
// Testing ("p2markers on|off"): each half's mission marker distances are from its own player.
std::string Player2MarkersPerPlayer(bool on);
// Testing ("p2 sharestunt on|off"): both players may take one vehicle's stunt position.
std::string Player2ShareStuntPositions(bool on);
// Testing ("p2hud on|off"): the HUD script reads health and weapons per player.
std::string Player2HudPerPlayer(bool on);
std::string Player2HudRefresh(unsigned weapons, unsigned ammo);
void Player2HudLog(bool on);
void Player2HudSway(bool on);
// Testing: proximity icons (pickups) in Player 2's half by his own distance ("p2hud proximity on|off").
void Player2HudProximity(bool on);
// Testing: button prompts per player ("p2hud prompts on|off|dump"): floating buttons on objects and
// the side panel ("Stunt jump B") come from each half's own player.
std::string Player2HudPrompts(const char* args);
// World update (main thread), after Player 1's HUD update: Player 2's button prompts.
void Player2UpdatePrompts();
// Testing: Player 2 collects pickups ("p2hud pickups on|off"); reports the triggers found.
std::string Player2Pickups(bool on);
// Testing ("guievent <name>"): fires a GUI event (e.g. gui_trigger_completion) on the next game tick.
std::string Player2FireGuiEvent(const char* name);
// Testing: the HUD passes that drew in the last frame ("p2hud passes").
std::string Player2HudPasses();
std::string Player2RollTest(bool roll);
void Player2HudPopup(bool fixed, float x, float y);
// Testing ("p2 padlog on|off"): logs pad 2's button changes and the parachute/jump action states.
std::string Player2PadLog(bool on);
// Testing ("p2 padpress <hex buttons> [ms]"): XInput buttons (0x10000 / 0x20000 = left / right
// trigger) held on pad 2 for a moment, through the same bindings as the real pad.
std::string Player2PadPress(unsigned buttons, unsigned ms);

// Testing (F6 / "p2 swap"): Player 1 and Player 2 trade places on the next game tick.
std::string PlayersRequestSwap();

// Testing: places Player 1 next to Player 2 on the next game tick.
std::string Player1RequestMoveToPlayer2();

// Testing: Player 2 gets the game's whole CPlayer::Update instead of only its character driver.
void Player2FullUpdate(bool on);

// Testing: holds one of Player 2's input actions (id from the game's action table) for ms.
void Player2PressAction(int action, unsigned ms, int action2 = -1);

// Test movement input for Player 2 when controller 2 isn't connected (-1..1: right, forward).
void Player2SetMoveInput(float right, float forward);

// Testing ("p2 sticks lx ly rx ry lt rt ms"): pad 2's sticks (-1..1) and triggers (0..1) replaced
// for ms milliseconds (vehicles).
void Player2SetTestSticks(float lx, float ly, float rx, float ry, float lt, float rt, unsigned ms);

// Player 2's body (the possessed character); 0 if none.
uintptr_t Player2Body();

// World matrix (rows: right, up, back, position) of Player 2's body; false if none.
bool Player2BodyMatrix(float out[16]);

// Requests teleporting Player 2 next to Player 1, offset along Player 1's right/up/forward axes.
std::string Player2RequestTeleport(float right, float up, float forward);
// Both players' ammo refilled (as F5) a second from now: on joining and rejoining (user request).
void Player2RefillAmmoSoon();
// Testing: teleports Player 1 to a world position.
std::string Player1RequestTeleportTo(float x, float y, float z);
// Testing: teleports Player 2 to a world position.
std::string Player2RequestTeleportTo(float x, float y, float z);

// Human-readable state for the control channel.
std::string Player2Describe();

// World position of Player 2's character; false if there is none.
bool Player2Position(float out[3]);
bool Player1CharacterPosition(float out[3]);
