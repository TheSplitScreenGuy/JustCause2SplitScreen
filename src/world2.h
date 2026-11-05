#pragma once

#include <string>

// Player 2's own world streaming: locations (buildings: the 512 m grid tiles' near/far blocks,
// landmarks, settlements) load around both players' cameras (see docs/HANDOFF.md, world streaming).

// Hooks CGameWorld's update. Call once from DllMain.
bool World2InstallHooks();

// At load, before the game creates its physics world: makes the world's box (where bodies have
// collision, around the floating origin) bigger, jc2ss.ini [Physics] WorldSize (default 16100 m,
// the game's is 8100), so both players keep collision farther apart.
void World2EnlargePhysicsWorld();

// Game thread, after the main camera's update (before the world update's road update): keeps the
// road builder's cell equal to the camera's, so the game never rebuilds the roads for one player.
void World2SyncRoadCell();

// The split turned off (Player 2 left, or the game left gameplay: menus, game over, loading), or
// the world is about to be respawned (Player 1 died): spawn points held for Player 2 get their own
// despawn distance back before any teardown, and with pauseMs none are held again for that long.
void World2ReleaseSpawnPoints(unsigned pauseMs = 0);

// Testing: "on" / "off".
std::string World2Set(const std::string& mode);
