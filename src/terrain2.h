#pragma once

#include <string>

// Player 2's own terrain detail: second copies of the game's vegetation and ground-mesh systems,
// and the landscape run per view at the frame hand-off (see docs/HANDOFF.md, per-player terrain).

// Hooks the frame hand-off. Call once from DllMain.
bool Terrain2InstallHooks();

// Game thread, right after the main camera was updated for Player 2's view: keeps a copy of the
// camera object (Player 2's matrices) for his landscape updates.
void Terrain2CapturePlayer2Camera(const void* camera);

// Game thread, outside the hand-off with the render job idle: Player 2's stream state (and his
// ground state with ground) in, or (in false) back to what was in before. False when he has none.
bool Terrain2UsePlayer2(bool in, bool ground);

// Testing: Player 2's terrain "on", "off", "p1only" (the landscape runs at the hand-off but only
// Player 1's). The copies are created on first use.
std::string Terrain2Set(const std::string& mode);
// Whether that player's terrain height map holds the stream cell at a position (game thread,
// outside the hand-off). After a long teleport the ground isn't there for a few seconds.
bool Terrain2GroundLoaded(int player, const float position[3]);
// Terrain height under a position from the height map game logic uses (game thread, outside the
// hand-off); false when that cell isn't loaded.
bool Terrain2GroundHeight(const float position[3], float* height);

// The minimap's tiles come from the map texture handler [0x01191224] (slot of cell & 7, whatever
// cell it holds); outside the hand-off it is Player 1's. Puts the player's in (game thread) and
// returns what to give Terrain2RestoreMapTexture; 0 when there is nothing to swap.
unsigned Terrain2UseMapTexture(int player);
void Terrain2RestoreMapTexture(unsigned saved);

// Waits for the frame being drawn (the render job the hand-off kicked). The minimap's tile sprites
// keep their texture, which the render reads while it draws: the next update (the other view's)
// re-textured them under the frame in flight (game thread).
void Terrain2WaitFrameDrawn();
