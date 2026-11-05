#include <windows.h>
#include <intrin.h>
#include <stdint.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <algorithm>
#include "world2.h"
#include "splitrender.h"
#include "splitscreen.h"
#include "terrain2.h"
#include "log.h"

void Player2UpdatePrompts(); // player2.cpp: Player 2's button prompts, after Player 1's HUD update

// Locations (CGameWorld [0x01191720] + 0x58, the spatial location manager) load around one point:
// the world update passes the main camera's position to CGameWorld slot 0x94 (FUN_007439F0),
// which unloads one loaded location whose box doesn't contain it (FUN_007F7440) or else loads
// what is wanted (FUN_00584D50; the 512 m grid tiles "Locations/XXxYY.nl/.fl", landmarks and
// settlements are picked by FUN_007ED240: an entry whose box, shrunk by a margin, contains the
// position). With Player 2 in, that part runs here instead, with both cameras: a location stays
// while either camera is inside its box and loads for either.
//
// Entry (0x28 bytes): +0 name, +4 box min (x, y, z), +0x10 flags (bits 2-3: kind), +0x14 flags
// (bit 1: unload), +0x18 box max, +0x24 the loaded location (vtable slot 2: unload).
// Manager: +0x34 loaded entries (vector of pointers), +0x54 entries loading (its capacity is how
// many may load at once), +0x60 the entry being unloaded, +0x64 flags.

namespace {

const uintptr_t kGameWorldUpdateSlot = 0x00EB92B8; // CGameWorld vtable slot 0x94, FUN_007439F0
const uintptr_t kGameWorldFlags = 0xBC;            // bit 0 skips the location part
const uintptr_t kLocations = 0x58;

const uintptr_t kLocationsBusy = 0x00545490;       // __fastcall(this) -> AL
const uintptr_t kLocationsStep = 0x00520070;       // __fastcall(this): finish loads and unloads
const uintptr_t kLocationsWanted = 0x00763470;     // __fastcall(this) -> entry asked for by name
const uintptr_t kLocationsWanted2 = 0x0060AC80;    // __fastcall(this) -> entry asked for by name
const uintptr_t kLocationsNear = 0x007ED240;       // __thiscall(this, position) -> entry to load
const uintptr_t kLocationLoad = 0x00830560;        // __thiscall(this, entry)

typedef void(__fastcall* Update_t)(uintptr_t self, void* edx, float* position);
typedef bool(__fastcall* This_t)(uintptr_t self);
typedef uintptr_t(__fastcall* Pick_t)(uintptr_t self);
typedef uintptr_t(__fastcall* PickAt_t)(uintptr_t self, void* edx, const float* position);
typedef void(__fastcall* Load_t)(uintptr_t self, void* edx, uintptr_t entry);

Update_t g_origUpdate;
volatile bool g_on = true;
volatile bool g_updateAtPlayer2;
DWORD g_nextLog;

bool Inside(uintptr_t entry, const float* p)
{
    const float* mn = (const float*)(entry + 4);
    const float* mx = (const float*)(entry + 0x18);
    return mn[0] < p[0] && mn[2] < p[2] && p[0] < mx[0] && p[2] < mx[2];
}

bool Keep(uintptr_t entry, const float* const* points, int n)
{
    if (*(uint8_t*)(entry + 0x14) & 2) return false;
    for (int i = 0; i < n; i++)
        if (Inside(entry, points[i])) return true;
    return false;
}

void UnloadEntry(uintptr_t manager, uintptr_t entry)
{
    *(uintptr_t*)(manager + 0x60) = entry;
    if (uintptr_t location = *(uintptr_t*)(entry + 0x24)) {
        typedef void(__fastcall * Unload_t)(uintptr_t self);
        ((Unload_t)(*(uintptr_t**)location)[2])(location);
    }
}

// FUN_007F7440 with several points: true when it started an unload (or one is going on).
bool UnloadOne(uintptr_t manager, const float* const* points, int n)
{
    if (*(uintptr_t*)(manager + 0x60)) return true;
    uintptr_t* begin = *(uintptr_t**)(manager + 0x54);
    uintptr_t* end = *(uintptr_t**)(manager + 0x58);
    for (uintptr_t* it = begin; begin && it < end; it++) {
        uintptr_t entry = *it;
        if ((*(uint8_t*)(entry + 0x10) & 0xC) && !Keep(entry, points, n)) {
            memmove(it, it + 1, (end - it - 1) * sizeof(*it));
            *(uintptr_t**)(manager + 0x58) = end - 1;
            if (*(uintptr_t*)(entry + 0x24)) UnloadEntry(manager, entry);
            return true;
        }
    }
    uintptr_t* loaded = *(uintptr_t**)(manager + 0x34);
    uintptr_t* loadedEnd = *(uintptr_t**)(manager + 0x38);
    if (!loaded) return false;
    for (intptr_t i = loadedEnd - loaded - 1; i >= 0; i--) {
        uintptr_t entry = loaded[i];
        if (!Keep(entry, points, n)) {
            UnloadEntry(manager, entry);
            return true;
        }
    }
    return false;
}

// FUN_00584D50 with several points.
void LoadWanted(uintptr_t manager, const float* const* points, int n)
{
    uintptr_t* begin = *(uintptr_t**)(manager + 0x54);
    size_t loading = begin ? *(uintptr_t**)(manager + 0x58) - begin : 0;
    size_t capacity = begin ? *(uintptr_t**)(manager + 0x5C) - begin : 0;
    if (loading == capacity) return;
    while (uintptr_t entry = ((Pick_t)kLocationsWanted)(manager)) ((Load_t)kLocationLoad)(manager, nullptr, entry);
    while (uintptr_t entry = ((Pick_t)kLocationsWanted2)(manager)) ((Load_t)kLocationLoad)(manager, nullptr, entry);
    uint8_t& flags = *(uint8_t*)(manager + 0x64);
    begin = *(uintptr_t**)(manager + 0x54);
    bool idle = !begin || *(uintptr_t**)(manager + 0x58) == begin;
    if ((flags & 0x10) && idle) {
        for (int i = 0; i < n; i++) {
            if (uintptr_t entry = ((PickAt_t)kLocationsNear)(manager, nullptr, points[i])) {
                ((Load_t)kLocationLoad)(manager, nullptr, entry);
                break;
            }
        }
    }
    if (flags & 0x40) flags = (flags & ~0x40) | 0x10;
}

// --- Decorations ------------------------------------------------------------------------------

// World decorations (street lamps, signs, barrels, fences and other props with their physics and
// lights; [0x011913E0], updated by FUN_007C1A40 in the world update) exist only within a radius
// of the main camera: on even frames FUN_007B9890 releases the active groups farther than it
// (FUN_00698B80), on odd frames FUN_0078D510 creates the ones within it (each group's +7 bit 7
// marks it created). With Player 2 in, that update runs here instead: groups near either camera
// stay, and creation runs once per camera (Player 2's with the camera pointer on a copy at his
// position). +0x42 set makes the game's own call skip; it is put back on the next frame.
//
// Manager: +0x10 vector of 0x30-byte cells, each with a vector of active groups at +0x20
// (begin +0x24, end +0x28); +0x3C pass (0 release, 1 create), +0x42 off.
const uintptr_t kDecorations = 0x011913E0;
const uintptr_t kDecorationsCreate = 0x0078D510;  // __fastcall(this), reads the main camera
const uintptr_t kDecorationsRelease = 0x00698B80; // __thiscall(this, group)
const uintptr_t kDecorationsAfter = 0x005EA110;   // __fastcall(this) when [0x011FB010] is set
const uintptr_t kDecorationsAfterFlag = 0x011FB010;
const uintptr_t kDecorationRadiusSq = 0x00E4E4EC;
const uintptr_t kCameraManager = 0x011919F4;
const uintptr_t kCameraManagerLogic = 0x10;
const size_t kCameraCopySize = 0x700;
const uintptr_t kCameraPosition = 0x54;

typedef void(__fastcall* Decorations_t)(uintptr_t self);
typedef void(__fastcall* Release_t)(uintptr_t self, void* edx, uintptr_t group);

volatile bool g_decorations = true;  // "world2 nodecorations" to compare
bool g_decorationsOffByUs;
uint8_t g_decorationCamera[kCameraCopySize];

float DistanceSq(const float* a, const float* b)
{
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return dx * dx + dy * dy + dz * dz;
}

void ReleaseFarDecorations(uintptr_t manager, const float* const* points, int n)
{
    float radiusSq = *(float*)kDecorationRadiusSq;
    uint8_t* cells = *(uint8_t**)(manager + 0x10);
    uint8_t* cellsEnd = *(uint8_t**)(manager + 0x14);
    for (uint8_t* cell = cells; cells && cell < cellsEnd; cell += 0x30) {
        uintptr_t* it = *(uintptr_t**)(cell + 0x24);
        while (it && it < *(uintptr_t**)(cell + 0x28)) {
            uintptr_t group = *it;
            const float* at = (const float*)(group + 8);
            bool close = false;
            for (int i = 0; i < n; i++) close |= DistanceSq(points[i], at) <= radiusSq;
            if (close) {
                it++;
                continue;
            }
            ((Release_t)kDecorationsRelease)(manager, nullptr, group);
            uintptr_t* end = *(uintptr_t**)(cell + 0x28);
            memmove(it, it + 1, (end - it - 1) * sizeof(*it));
            *(uintptr_t**)(cell + 0x28) = end - 1;
        }
    }
}

void UpdateDecorations(const float* camera1, const float* camera2)
{
    uintptr_t manager = *(uintptr_t*)kDecorations;
    if (!manager) return;
    uint8_t& off = *(uint8_t*)(manager + 0x42);
    if (g_decorationsOffByUs) {
        off = 0;
        g_decorationsOffByUs = false;
    }
    if (off || !g_decorations) return;
    int& pass = *(int*)(manager + 0x3C);
    if (pass == 0) {
        const float* points[2] = { camera1, camera2 };
        ReleaseFarDecorations(manager, points, 2);
    } else if (pass == 1) {
        ((Decorations_t)kDecorationsCreate)(manager);
        uintptr_t cameras = *(uintptr_t*)kCameraManager;
        uintptr_t logic = *(uintptr_t*)(cameras + kCameraManagerLogic);
        memcpy(g_decorationCamera, (void*)logic, sizeof(g_decorationCamera));
        memcpy(g_decorationCamera + kCameraPosition, camera2, 12);
        *(uintptr_t*)(cameras + kCameraManagerLogic) = (uintptr_t)g_decorationCamera;
        __try {
            ((Decorations_t)kDecorationsCreate)(manager);
        } __finally {
            *(uintptr_t*)(cameras + kCameraManagerLogic) = logic;
        }
    }
    pass = (pass + 1) & 1;
    if (*(uint8_t*)kDecorationsAfterFlag) {
        ((Decorations_t)kDecorationsAfter)(manager);
        *(uint8_t*)kDecorationsAfterFlag = 0;
    }
    // The game's own call (later in this world update) skips.
    off = 1;
    g_decorationsOffByUs = true;
}

// --- Roads ------------------------------------------------------------------------------------

// Road meshes and their collision (the streets) exist only within a radius of the main camera:
// the road builder ([[0x01191194]+0xA8], updated by FUN_008C2A20 in the world update) rebuilds
// its wanted set when the camera moves into another 50 m cell (FUN_008C4470: FUN_008C56B0 gathers
// the roads within +0x290 of the camera into +0x1F8, built roads not in it are removed), then
// builds the wanted ones it hasn't built yet over the next frames. With Player 2 in, the rebuild
// runs here with both cameras (Player 2's roads merged into the wanted set), and the builder's cell
// is kept at Player 1's so the game's own update never rebuilds for him alone.
//
// Builder: +0x1C4 built roads (vector of pointers: +0x20 CRoad, +0x24 its shared count), +0x1F8
// wanted roads (vector of shared pointers, 8 bytes), +0x21C cell x, y, z, +0x229 on, +0x290 radius.
const uintptr_t kRoadManager = 0x01191194;
const uintptr_t kRoadBuilder = 0xA8;
const uintptr_t kRoadGather = 0x008C56B0;  // __thiscall(manager, vector, position, radius)
const uintptr_t kRoadPush = 0x008F7AD0;    // __thiscall(vector, const shared_ptr<CRoad>*): push_back
const uintptr_t kRoadRemove = 0x0089BAF0;  // __thiscall(builder, shared_ptr<CRoad> by value)
const uintptr_t kRoadCellScale = 0x00F09268;
const uintptr_t kTerrainMap = 0x01190290;
const uintptr_t kRenderer = 0x01191AB4;    // slot 5: remove a render object
const uintptr_t kGameDelete = 0x00440350;  // __cdecl(pointer)

typedef int(__fastcall* RoadGather_t)(uintptr_t manager, void* edx, void* vector, const float* position, float radius);
typedef void(__fastcall* RoadPush_t)(void* vector, void* edx, const uint32_t* road);
typedef void(__fastcall* RoadRemove_t)(uintptr_t builder, void* edx, uint32_t road, uint32_t count);
typedef void(__fastcall* RendererRemove_t)(uintptr_t renderer, void* edx, uint32_t object);
typedef void(__fastcall* CountRelease_t)(uintptr_t count);
typedef void(__cdecl* GameDelete_t)(void* p);

struct GameVector {
    uint32_t proxy;
    uint32_t* begin;
    uint32_t* end;
    uint32_t* capacity;
};

volatile bool g_roads = true;  // "world2 noroads" to compare
GameVector g_roadsAround[2];   // each player's roads at his last gather (the gather clears it)
int g_roadCell[2][3];
bool g_haveRoads[2];
DWORD g_nextRoadRefresh[2];
bool g_roadsOwned;             // the builder's set is the merged one

void RoadCell(const float* p, int out[3])
{
    float s = *(float*)kRoadCellScale;
    for (int i = 0; i < 3; i++) out[i] = (int)floorf(p[i] * s);
}

void ReleaseCount(uint32_t count)
{
    if (!count) return;
    if (InterlockedDecrement((LONG*)(count + 4)) == 0) {
        ((CountRelease_t)(*(uintptr_t**)count)[1])(count);
        if (InterlockedDecrement((LONG*)(count + 8)) == 0) ((CountRelease_t)(*(uintptr_t**)count)[2])(count);
    }
}

void RemoveBuiltRoad(uintptr_t builder, uintptr_t built)
{
    uint32_t road = *(uint32_t*)(built + 0x20), count = *(uint32_t*)(built + 0x24);
    if (count) InterlockedIncrement((LONG*)(count + 4)); // the copy passed by value
    ((RoadRemove_t)kRoadRemove)(builder, nullptr, road, count);
    uintptr_t renderer = *(uintptr_t*)kRenderer;
    ((RendererRemove_t)(*(uintptr_t**)renderer)[5])(renderer, nullptr, *(uint32_t*)built);
    ReleaseCount(count);
    ((GameDelete_t)kGameDelete)((void*)built);
}

bool Wanted(const GameVector& wanted, uint32_t road)
{
    for (uint32_t* it = wanted.begin; it && it < wanted.end; it += 2)
        if (*it == road) return true;
    return false;
}

// The builder makes a road's mesh around the camera of the frame it builds on, and works through
// the wanted set on a time budget: roads left over were built on the other player's frame, kilometres
// from the player who wanted them, without a mesh near him (a 480 m road under Player 1 stayed
// invisible). Each built road is tagged with the player whose frame built it; a player's road built
// for the other one is built again for him.
struct BuiltTag {
    uint32_t entry, road;
    int player;
};
std::vector<BuiltTag> g_builtFor;
int g_lastRoadPlayer;          // whose road update ran last frame (built roads since then are his)

void TagBuiltRoads(const GameVector& built, int newPlayer)
{
    std::vector<BuiltTag> tags;
    for (uint32_t* it = built.begin; it && it < built.end; it++) {
        BuiltTag tag = { *it, *(uint32_t*)(*it + 0x20), newPlayer };
        for (const BuiltTag& old : g_builtFor)
            if (old.entry == tag.entry && old.road == tag.road) tag.player = old.player;
        tags.push_back(tag);
    }
    g_builtFor.swap(tags);
}

// The player whose frame built the road, -1 when it isn't built.
int BuiltFor(uint32_t road)
{
    for (const BuiltTag& tag : g_builtFor)
        if (tag.road == road) return tag.player;
    return -1;
}

// Drops wanted roads that aren't this player's (queued on the other player's frame, not built yet):
// they'd be built around this camera. The other player queues them again on his frame.
void KeepOwnWanted(GameVector& wanted, int player)
{
    uint32_t* out = wanted.begin;
    for (uint32_t* it = wanted.begin; it && it < wanted.end; it += 2) {
        if (Wanted(g_roadsAround[player], *it)) {
            out[0] = it[0];
            out[1] = it[1];
            out += 2;
        } else {
            ReleaseCount(it[1]);
        }
    }
    if (wanted.begin) wanted.end = out;
}

// Few at a time, so the builder's time budget finishes them on this player's frame (built on
// the other player's frame, far from that camera, a road gets no mesh).
const int kRoadsQueuedPerFrame = 4;

// On a player's own frame (the builder makes road meshes for the main camera, which is then his):
// gathers his roads, queues the ones not built yet, and removes built roads neither player has.
// Queueing a built road again would make the builder replace it (the road flickers and can come
// back without a mesh when it's rebuilt on the other player's frame).
void RebuildRoads(uintptr_t builder, int player, const float* camera)
{
    uintptr_t manager = *(uintptr_t*)kRoadManager;
    float radius = *(float*)(builder + 0x290);
    GameVector& wanted = *(GameVector*)(builder + 0x1F8);
    GameVector& built = *(GameVector*)(builder + 0x1C4);
    GameVector& mine = g_roadsAround[player];
    ((RoadGather_t)kRoadGather)(manager, nullptr, &mine, camera, radius);
    g_haveRoads[player] = true;
    int queued = 0;
    int other = 1 - player;
    for (uint32_t* it = mine.begin; it && it < mine.end && queued < kRoadsQueuedPerFrame; it += 2) {
        if (Wanted(wanted, *it)) continue;
        // Built for him, or for the other player who has it too (both cameras near it): keep it.
        int builtFor = BuiltFor(*it);
        if (builtFor == player || (builtFor == other && g_haveRoads[other] && Wanted(g_roadsAround[other], *it)))
            continue;
        ((RoadPush_t)kRoadPush)(&wanted, nullptr, it);
        queued++;
    }
    if (!g_haveRoads[0] || !g_haveRoads[1]) return;
    uint32_t* it = built.begin;
    uint32_t* end = built.end;
    while (it && it < end) {
        uint32_t road = *(uint32_t*)(*it + 0x20);
        if (Wanted(g_roadsAround[0], road) || Wanted(g_roadsAround[1], road) || Wanted(wanted, road)) {
            it++;
            continue;
        }
        RemoveBuiltRoad(builder, *it);
        *it = *--end;
    }
    if (built.begin) built.end = end;
}

void UpdateRoads(bool two, const float* camera1, const float* camera2)
{
    uintptr_t manager = *(uintptr_t*)kRoadManager;
    uintptr_t builder = manager ? *(uintptr_t*)(manager + kRoadBuilder) : 0;
    if (!builder) return;
    int* gameCell = (int*)(builder + 0x21C);
    if (!two || !g_roads) {
        // Back to Player 1 alone: the game rebuilds on its next update.
        if (g_roadsOwned) gameCell[0] = 0x7FFFFFFF;
        g_roadsOwned = false;
        g_haveRoads[0] = g_haveRoads[1] = false;
        g_builtFor.clear(); // the game builds them all for Player 1 again
        g_lastRoadPlayer = 0;
        return;
    }
    if (!*(uint8_t*)(builder + 0x229) || !*(uintptr_t*)kTerrainMap) return;
    g_roadsOwned = true;
    // This runs before the camera update of this frame (camera control, later in the world update):
    // the view the game last updated is the previous frame's, the road update below is for the
    // other player.
    int player = 1 - SplitRenderGameView();
    const float* camera = player ? camera2 : camera1;
    TagBuiltRoads(*(GameVector*)(builder + 0x1C4), g_lastRoadPlayer);
    g_lastRoadPlayer = player;
    if (g_haveRoads[player]) KeepOwnWanted(*(GameVector*)(builder + 0x1F8), player);
    int cell[3];
    RoadCell(camera, cell);
    // Also every 0.2 s: there may be more roads to queue, and the game rebuilds for Player 1 alone
    // outside its update too (FUN_008FCC10, e.g. when a road file finishes loading).
    bool refresh = (LONG)(GetTickCount() - g_nextRoadRefresh[player]) >= 0;
    if (refresh || !g_haveRoads[player] || memcmp(cell, g_roadCell[player], sizeof(cell))) {
        g_nextRoadRefresh[player] = GetTickCount() + 200;
        RebuildRoads(builder, player, camera);
        memcpy(g_roadCell[player], cell, sizeof(cell));
    }
    // The game's road update compares the cell of this frame's camera; keep them equal so it never
    // rebuilds for one player alone (again after the camera update: World2SyncRoadCell).
    memcpy(gameCell, cell, sizeof(cell));
}

// --- AI navigation tiles -----------------------------------------------------------------------
//
// The AI streams navigation tiles (AI\patches\XX\N.aitile) around the main camera through the
// tile cache CAITileCache (one object, vtable 0x00F0A068): slot 1 FUN_004763C0 requests a tile
// (loads it or adds a reference), slot 2 FUN_0045B410 releases one. With the main camera
// alternating between the players, tiles near each of them were requested and released every
// frame, and the reloads kept the game's file queue ([0x012A1448], FUN_004D63E0) from ever going
// empty; the vehicle type loader (spawn manager stage 0, FUN_005A5D50) waits for an empty queue,
// so no new vehicle type loaded and spawns that needed one (a base's jets, P1's parked cars) hung.
// With Player 2 in, a release waits a few seconds; a request of the same tile in that time
// cancels both.
const uintptr_t kTileRequestSlot = 0x00F0A06C;
const uintptr_t kTileReleaseSlot = 0x00F0A070;
const DWORD kTileReleaseDelay = 3000;
typedef void(__fastcall* Tile_t)(uintptr_t cache, void* edx, uint32_t tile);
Tile_t g_origTileRequest, g_origTileRelease;
struct PendingRelease {
    uintptr_t cache;
    uint32_t tile;
    DWORD at;
};
std::vector<PendingRelease> g_pendingReleases;
CRITICAL_SECTION g_tileLock;
volatile bool g_tilesShared;                // set by the world update while Player 2 is in
volatile bool g_tileDelay = true;           // "world2 tiles1" releases at once (the game's way)
volatile LONG g_tileRequests, g_tileReleases, g_tileCancelled;

// Hands over the releases that waited long enough (all of them once Player 2 is out). Called from
// the hooks, i.e. where the game itself uses the cache.
void FlushTileReleases()
{
    PendingRelease due[16];
    int n = 0;
    EnterCriticalSection(&g_tileLock);
    DWORD now = GetTickCount();
    bool all = !g_tilesShared || !g_tileDelay;
    size_t kept = 0;
    for (const PendingRelease& r : g_pendingReleases) {
        if (n < (int)_countof(due) && (all || now - r.at >= kTileReleaseDelay)) due[n++] = r;
        else g_pendingReleases[kept++] = r;
    }
    g_pendingReleases.resize(kept);
    LeaveCriticalSection(&g_tileLock);
    for (int i = 0; i < n; i++) g_origTileRelease(due[i].cache, nullptr, due[i].tile);
}

void __fastcall TileRequestHook(uintptr_t cache, void* edx, uint32_t tile)
{
    InterlockedIncrement(&g_tileRequests);
    bool cancelled = false;
    EnterCriticalSection(&g_tileLock);
    for (size_t i = 0; i < g_pendingReleases.size(); i++) {
        if (g_pendingReleases[i].cache == cache && g_pendingReleases[i].tile == tile) {
            g_pendingReleases.erase(g_pendingReleases.begin() + i);
            cancelled = true; // its reference was never given back
            break;
        }
    }
    LeaveCriticalSection(&g_tileLock);
    if (cancelled) InterlockedIncrement(&g_tileCancelled);
    else g_origTileRequest(cache, edx, tile);
    FlushTileReleases();
}

void __fastcall TileReleaseHook(uintptr_t cache, void* edx, uint32_t tile)
{
    InterlockedIncrement(&g_tileReleases);
    if (g_tilesShared && g_tileDelay) {
        EnterCriticalSection(&g_tileLock);
        g_pendingReleases.push_back({ cache, tile, GetTickCount() });
        LeaveCriticalSection(&g_tileLock);
    } else {
        g_origTileRelease(cache, edx, tile);
    }
    FlushTileReleases();
}

// --- Vehicle detail --------------------------------------------------------------------------
//
// Vehicles pick their detail level (steering wheel, wheel models, then the blocky body; the
// level goes into each part's render flags, bits 29-31, FUN_00804770 / FUN_006E0550) from vtable
// slot +0x12C, FUN_0061BB00 in every vehicle class: distance from the main camera (+0x54),
// times [0x0107214C] and a per-part scale, over the camera's zoom (+0x620, at least 1), squared,
// then a piecewise ramp. It runs in the vehicle update, when the main camera holds Player 1's
// view, so cars in front of a far Player 2 drew at Player 1's distance. Now the level is the
// more detailed of the two players' cameras (the game's own result is kept for its debug
// override [0x011FC2A0]).
const uintptr_t kVehicleLodSlots[] = { 0x00EBF648, 0x00EBF7A8, 0x00EBF930, 0x00EBFAB8, 0x00EBFC30,
                                       0x00EBFDA8, 0x00EBFF08, 0x00EC0090, 0x00EC0208 };
const uintptr_t kVehicleLodScale = 0x0107214C;
const uintptr_t kVehicleLodSteps = 0x00EBEDE4;   // 4 floats: thresholds of the ramp
const uintptr_t kVehicleLodForced = 0x011FC2A0;  // byte: debug override on
const uintptr_t kCameraZoom = 0x620;
typedef float(__fastcall* VehicleLod_t)(void* vehicle, void* edx, float scale, const float* position);
VehicleLod_t g_origVehicleLod[_countof(kVehicleLodSlots)];
volatile bool g_vehicleLodBoth = true; // "world2 lod1" leaves it to the main camera

// FUN_0061BB00's ramp for one camera position.
float VehicleLodFrom(const float* camera, float zoom, float scale, const float* position)
{
    float dx = camera[0] - position[0], dy = camera[1] - position[1], dz = camera[2] - position[2];
    float k = *(float*)kVehicleLodScale * scale;
    if (zoom <= 1.0f) zoom = 1.0f;
    float v = (dx * dx + dy * dy + dz * dz) * k * k / (zoom * zoom);
    const float* t = (const float*)kVehicleLodSteps;
    if (v < t[0]) return v * *(float*)0x00E4EE38;
    if (v < t[1]) return (v - t[0]) * *(float*)0x00E4C8B0 + *(float*)0x00E20668;
    if (v < t[2]) return (v - t[1]) * *(float*)0x00E32A44 + *(float*)0x00E2D510;
    if (v < t[3]) return (v - t[2]) * *(float*)0x00E47610 + *(float*)0x00E23AD4;
    return *(float*)0x00E251B4;
}

// Vehicles go to sleep (frozen, no physics, not drawn) by distance from the main camera: FUN_00842CE0
// (from the vehicle update FUN_008219F0) compares the camera's x/z ([[0x011919F4]+0x10]+0x54/+0x5C)
// with the vehicle's (+0x318/+0x320) squared: asleep (+0x378 = 1, +0x368 = 0, vfunc +0x84) past
// +0x380 (4000 m for the helicopter), awake again inside +0x37C. With Player 1's camera that froze
// Player 2's helicopter 4 km from Player 1 in mid-air (user, 2026-10-29). Each frame (from the
// detail hook, which runs for every vehicle) both distances get Player 1's minus the nearer
// player's distance added, so the check gives the nearer player's answer. The game's values are
// kept per vehicle (a value that isn't what we wrote means a new vehicle) and put back without
// Player 2.
const uintptr_t kVehiclePositionX = 0x318, kVehiclePositionZ = 0x320;
const uintptr_t kVehicleWakeDistance = 0x37C, kVehicleSleepDistance = 0x380;
struct VehicleSleep {
    float wake, sleep;       // the game's
    float wroteWake, wroteSleep;
};
std::vector<std::pair<uintptr_t, VehicleSleep>> g_vehicleSleep;
volatile bool g_vehicleSleepBoth = true; // "world2 sleep1" leaves it to the main camera
CRITICAL_SECTION g_vehicleSleepLock;

void AdjustVehicleSleep(uintptr_t vehicle, float shift, bool restore)
{
    float& wake = *(float*)(vehicle + kVehicleWakeDistance);
    float& sleep = *(float*)(vehicle + kVehicleSleepDistance);
    EnterCriticalSection(&g_vehicleSleepLock);
    VehicleSleep* s = nullptr;
    size_t index = 0;
    for (; index < g_vehicleSleep.size(); index++)
        if (g_vehicleSleep[index].first == vehicle) {
            s = &g_vehicleSleep[index].second;
            break;
        }
    if (s && (wake != s->wroteWake || sleep != s->wroteSleep)) { // changed since: not ours
        g_vehicleSleep.erase(g_vehicleSleep.begin() + index);
        s = nullptr;
    }
    if (restore) {
        if (s) {
            wake = s->wake;
            sleep = s->sleep;
            g_vehicleSleep.erase(g_vehicleSleep.begin() + index);
        }
    } else if (s || (sleep > 1.0e4f && sleep < 1.0e9f && wake <= sleep)) {
        if (!s) {
            g_vehicleSleep.push_back({ vehicle, { wake, sleep, 0, 0 } });
            s = &g_vehicleSleep.back().second;
        }
        wake = s->wake + shift;
        sleep = s->sleep + shift;
        s->wroteWake = wake;
        s->wroteSleep = sleep;
    }
    LeaveCriticalSection(&g_vehicleSleepLock);
}

void KeepVehicleAwake(uintptr_t vehicle)
{
    bool restore = !g_vehicleSleepBoth || !g_on || SplitscreenPlayer2Status() != Player2Status::Joined;
    float c1[3], c2[3], d[3];
    float shift = 0;
    if (!restore) {
        if (!SplitViewCamera(0, c1, d) || !SplitViewCamera(1, c2, d)) return;
        float x = *(float*)(vehicle + kVehiclePositionX), z = *(float*)(vehicle + kVehiclePositionZ);
        float d1 = (c1[0] - x) * (c1[0] - x) + (c1[2] - z) * (c1[2] - z);
        float d2 = (c2[0] - x) * (c2[0] - x) + (c2[2] - z) * (c2[2] - z);
        shift = d1 > d2 ? d1 - d2 : 0.0f;
    }
    AdjustVehicleSleep(vehicle, shift, restore);
}

bool KeepVehicleAwakeSafe(uintptr_t vehicle)
{
    __try {
        KeepVehicleAwake(vehicle);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The vehicle update that does the sleep check, also while asleep (it is what wakes a vehicle):
// slot 7 of the vtable at vehicle +4 (CHelicopter's 0x00E56F7C: FUN_006B31A0 -> FUN_007D0DE0 ->
// FUN_008219F0 -> FUN_00842CE0; __thiscall(vehicle +4, dword), ret 4). The vehicle classes' +4
// vtables (47 slots): the 8 whose slot 3 is FUN_0084C2A0, and CCar's 0x00E48D24 (slot 3
// FUN_00707440 calls it; slot 7 FUN_005F12B0 -> the land vehicle update FUN_005674B0). (Slot 27 of
// the main vtables, FUN_00782C90, never ran.) Hooking those slots in place made the game's
// integrity check quit on the Steam Deck (ExitProcess(0) "from 8000DEAD" while a pickup loaded
// as Player 2 drove into a town), so each vehicle's +4 pointer goes to a private copy instead:
// ScanForVehicles (background, every few seconds while Player 2 is in) finds them by their main
// vtable (the 9 classes of the detail slots) followed by one of these.
const uintptr_t kVehicleIfaceVtables[] = { 0x00E3C5DC, 0x00E57B54, 0x00E56F7C, 0x00E464EC, 0x00E5696C,
                                           0x00E501B4, 0x00E5499C, 0x00E4EBA4, 0x00E48D24 };
const uintptr_t kVehicleMainVtables[] = { 0x00EBF51C, 0x00EBF67C, 0x00EBF804, 0x00EBF98C, 0x00EBFB04,
                                          0x00EBFC7C, 0x00EBFDDC, 0x00EBFF64, 0x00EC00DC };
const int kVehicleIfaceSlots = 47;
const int kVehicleIfaceUpdateIndex = 7;
const int kVehicleClassCount = _countof(kVehicleIfaceVtables);
uintptr_t g_vehicleIfaceVtables[kVehicleClassCount][1 + kVehicleIfaceSlots];
typedef uint32_t(__fastcall* VehicleUpdate_t)(void* iface, void* edx, uint32_t a);
VehicleUpdate_t g_origVehicleUpdate[kVehicleClassCount];

volatile LONG g_vehicleUpdates;
DWORD g_nextVehicleLog;

template <int I>
uint32_t __fastcall VehicleUpdateHook(void* iface, void* edx, uint32_t a)
{
    uintptr_t vehicle = (uintptr_t)iface - 4;
    KeepVehicleAwakeSafe(vehicle);
    InterlockedIncrement(&g_vehicleUpdates);
    if ((LONG)(GetTickCount() - g_nextVehicleLog) >= 0) {
        g_nextVehicleLog = GetTickCount() + 10000;
        Log("World 2: vehicle updates in 10 s: %ld, %u vehicles with their sleep distance shifted",
            InterlockedExchange(&g_vehicleUpdates, 0), (unsigned)g_vehicleSleep.size());
    }
    return g_origVehicleUpdate[I](iface, edx, a);
}

const void* kVehicleUpdateHooks[] = { (void*)VehicleUpdateHook<0>, (void*)VehicleUpdateHook<1>, (void*)VehicleUpdateHook<2>,
                                      (void*)VehicleUpdateHook<3>, (void*)VehicleUpdateHook<4>, (void*)VehicleUpdateHook<5>,
                                      (void*)VehicleUpdateHook<6>, (void*)VehicleUpdateHook<7>, (void*)VehicleUpdateHook<8> };
static_assert(_countof(kVehicleUpdateHooks) == kVehicleClassCount, "one hook per vehicle class");

void MakeVehicleVtables()
{
    for (int i = 0; i < kVehicleClassCount; i++) {
        memcpy(g_vehicleIfaceVtables[i], (const void*)(kVehicleIfaceVtables[i] - 4), sizeof(g_vehicleIfaceVtables[i]));
        g_origVehicleUpdate[i] = (VehicleUpdate_t)g_vehicleIfaceVtables[i][1 + kVehicleIfaceUpdateIndex];
        g_vehicleIfaceVtables[i][1 + kVehicleIfaceUpdateIndex] = (uintptr_t)kVehicleUpdateHooks[i];
    }
}

volatile LONG g_vehicleScanning;
volatile LONG g_vehiclesSwitched;

bool IsVehicleMainVtable(uintptr_t v)
{
    for (uintptr_t m : kVehicleMainVtables)
        if (v == m) return true;
    return false;
}

// Background: puts every vehicle's +4 pointer on the private copy of its class's vtable.
DWORD WINAPI ScanForVehicles(void*)
{
    MEMORY_BASIC_INFORMATION mbi;
    LONG switched = 0;
    for (uintptr_t a = 0x00010000; a < 0x7FFF0000 && VirtualQuery((void*)a, &mbi, sizeof(mbi)); a += mbi.RegionSize) {
        if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE || !(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) ||
            (mbi.Protect & PAGE_GUARD))
            continue;
        __try {
            uintptr_t* p = (uintptr_t*)mbi.BaseAddress;
            uintptr_t* end = (uintptr_t*)((uintptr_t)mbi.BaseAddress + mbi.RegionSize) - 1;
            for (; p < end; p++) {
                if (!IsVehicleMainVtable(p[0])) continue;
                for (int i = 0; i < kVehicleClassCount; i++) {
                    if (p[1] != kVehicleIfaceVtables[i]) continue;
                    if (InterlockedCompareExchange((volatile LONG*)&p[1], (LONG)(uintptr_t)&g_vehicleIfaceVtables[i][1],
                                                   (LONG)kVehicleIfaceVtables[i]) == (LONG)kVehicleIfaceVtables[i])
                        switched++;
                    break;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    if (switched) {
        InterlockedExchangeAdd(&g_vehiclesSwitched, switched);
        Log("World 2: %ld vehicles on private update vtables (%ld in all)", switched, g_vehiclesSwitched);
    }
    InterlockedExchange(&g_vehicleScanning, 0);
    return 0;
}

// World update: a scan every few seconds while Player 2 is in (new vehicles spawn all the time).
void KeepVehiclesOnPrivateVtables()
{
    static DWORD next;
    if (!g_vehicleIfaceVtables[0][1] || (LONG)(GetTickCount() - next) < 0) return;
    if (InterlockedCompareExchange(&g_vehicleScanning, 1, 0) != 0) return;
    next = GetTickCount() + 3000;
    CloseHandle(CreateThread(nullptr, 0, ScanForVehicles, nullptr, 0, nullptr));
}

template <int I>
float __fastcall VehicleLodHook(void* vehicle, void* edx, float scale, const float* position)
{
    float lod = g_origVehicleLod[I](vehicle, edx, scale, position);
    if (!g_vehicleLodBoth || !g_on || *(uint8_t*)kVehicleLodForced ||
        SplitscreenPlayer2Status() != Player2Status::Joined)
        return lod;
    __try {
        uintptr_t cameras = *(uintptr_t*)kCameraManager;
        uintptr_t camera = cameras ? *(uintptr_t*)(cameras + kCameraManagerLogic) : 0;
        float zoom = camera ? *(float*)(camera + kCameraZoom) : 1.0f;
        float best = FLT_MAX;
        for (int view = 0; view < 2; view++) {
            float p[3], d[3];
            if (SplitViewCamera(view, p, d)) {
                float v = VehicleLodFrom(p, zoom, scale, position);
                if (v < best) best = v;
            }
        }
        if (best != FLT_MAX) return best;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return lod;
}

// --- Despawning ------------------------------------------------------------------------------
//
// Spawned objects (parked and traffic vehicles, AI agents: CSpawnedObject, owner +8) go when the
// spawn manager ([0x01191918], FUN_00779D20 in the world update, one stage per frame) asks each
// one's despawn functor (stage 3 FUN_006A29F0 -> FUN_005C0EC0 -> functor slot 0) with the main
// camera's position and look direction. That camera is Player 1's or Player 2's depending on the
// frame, so whatever ran on Player 1's frames removed the car in front of a far Player 2. Now an
// object goes only when the functor would remove it seen from both players' cameras.
// Functor slot 0: __thiscall(functor, object, float, object position, camera position, camera
// direction, flag, int* result) -> despawn, ret 0x1C.
const uintptr_t kDespawnFunctorSlots[] = {
    0x00E3748C, // TSpecializedDespawnFunctor<CSpawnManager> (FUN_007A17B0)
    0x00E330A8, // TSpecializedDespawnFunctor<CHeatSpawnManager> (FUN_007A17B0)
    0x00E57C28, // TSpecializedDespawnFunctor<CWorldSim> (FUN_006A4340)
};
typedef bool(__fastcall* Despawn_t)(void* functor, void* edx, uint32_t object, float value, const float* objectPosition,
                                    const float* cameraPosition, const float* cameraDirection, uint32_t flag, int* result);
// 0..2: the functor vtable slots above (0 unused, see HookSpawnManagerDespawn); 3..7: the
// CSpawnManager functors' member functions.
Despawn_t g_origDespawn[_countof(kDespawnFunctorSlots) + 5];
volatile bool g_despawnBoth = true; // "world2 despawn1" to compare

volatile LONG g_despawnLogLeft; // testing: "world2 despawnlog <n>" logs the next n calls

template <int I>
bool __fastcall DespawnHook(void* functor, void* edx, uint32_t object, float value, const float* objectPosition,
                            const float* cameraPosition, const float* cameraDirection, uint32_t flag, int* result)
{
    if (g_despawnLogLeft > 0 && InterlockedDecrement(&g_despawnLogLeft) >= 0)
        Log("World 2: despawn functor %d called from %08X (thread %lu), functor %08X object %08X", I,
            (unsigned)(uintptr_t)_ReturnAddress(), GetCurrentThreadId(), (unsigned)(uintptr_t)functor, object);
    int before = result ? *result : 0;
    bool despawn = g_origDespawn[I](functor, edx, object, value, objectPosition, cameraPosition, cameraDirection, flag, result);
    if (!despawn || !g_despawnBoth || !g_on || SplitscreenPlayer2Status() != Player2Status::Joined) return despawn;
    for (int view = 0; view < 2; view++) {
        float position[3], direction[3];
        if (!SplitViewCamera(view, position, direction)) return despawn;
        if (result) *result = before;
        if (!g_origDespawn[I](functor, edx, object, value, objectPosition, position, direction, flag, result)) return false;
    }
    return true;
}

// The CSpawnManager functors' vtable (0x00E3748C) is checked by the game: with its slot hooked,
// setting a map waypoint made it quit (ExitProcess(0) with the fake return address 0x8000DEAD, on
// another thread, without the hook running; 2026-10-29, also without Player 2). The manager has
// five functors (+0x17F8..+0x1808, one per kind of spawned object; vtable +0, member function +8,
// this = [+0x18] + [+0xC]) whose slot 0 (FUN_007A17B0, a thunk) calls the member function with
// its own arguments, so each heap member pointer is redirected to a DespawnHook instead (again
// whenever a new manager or functor shows up, e.g. after a load).
const uintptr_t kSpawnManagerDespawnFunctors = 0x17F8;
const int kSpawnManagerDespawnFunctorCount = 5;
const uintptr_t kDespawnFunctorMember = 8;
const int kFirstMemberHook = 3; // g_origDespawn index of the first manager functor
bool g_despawnPerFunctor;

void HookSpawnManagerDespawn()
{
    if (!g_despawnPerFunctor) return;
    static const uintptr_t hooks[kSpawnManagerDespawnFunctorCount] = {
        (uintptr_t)DespawnHook<3>, (uintptr_t)DespawnHook<4>, (uintptr_t)DespawnHook<5>, (uintptr_t)DespawnHook<6>,
        (uintptr_t)DespawnHook<7>,
    };
    __try {
        uintptr_t manager = *(uintptr_t*)0x01191918; // CSpawnManager (kSpawnManager below)
        if (!manager) return;
        for (int j = 0; j < kSpawnManagerDespawnFunctorCount; j++) {
            uintptr_t functor = *(uintptr_t*)(manager + kSpawnManagerDespawnFunctors + 4 * j);
            if (!functor || *(uintptr_t*)functor != kDespawnFunctorSlots[0]) continue;
            uintptr_t& member = *(uintptr_t*)(functor + kDespawnFunctorMember);
            if (member == hooks[j]) continue;
            g_origDespawn[kFirstMemberHook + j] = (Despawn_t)member;
            member = hooks[j];
            Log("World 2: spawn manager despawn functor %d (%08X) now calls the hook (member %08X)", j,
                (unsigned)functor, (unsigned)(uintptr_t)g_origDespawn[kFirstMemberHook + j]);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// Spawn points (parked vehicles and the like; stage 2, FUN_005923B0): a 4x4 grid of 8 km cells
// (vectors of points at manager +0x148, 0x10 each); each point +0x68 position, +0x80 / +0x84 /
// +0x88 spawn / despawn / minimum distance squared, +0xA0 bit 0 enabled, +0xA4 state (0 none),
// +0xE0 busy. Every frame the stage queues points the camera is within reach of (FUN_0079DF40) and
// despawns the others beyond the despawn distance (FUN_00515070 + FUN_007999F0). The camera it
// reads is Player 1's on every frame (watched: the point at the hideout took the despawn branch
// 144 times a second with Player 2 next to it), so nothing spawned around a far Player 2, and
// what was queued for him was dropped the next frame. So points within despawn reach of Player 2's
// camera get their despawn distance raised out of Player 1's reach (put back when he leaves),
// and his points are queued here by the stage's rule (FUN_0079DF40). The queue (stage 10,
// FUN_00800840, current at manager +0xB8, worker FUN_007B70D0) loads a vehicle type first if
// needed (point +0xC0 components; stage 0 FUN_005A5D50, which needs an idle file queue: see the
// AI tiles above).
const uintptr_t kSpawnManager = 0x01191918;
const uintptr_t kSpawnOn = 0xD1;
const uintptr_t kSpawnHeld = 0x2E5;
const uintptr_t kSpawnCells = 0x148;
const uintptr_t kSpawnQueue = 0x0079DF40;       // __thiscall(manager, point)
typedef void(__fastcall* SpawnPoint_t)(uintptr_t manager, void* edx, uintptr_t point);
volatile bool g_spawnBoth = true; // "world2 spawn1" leaves the points to the game
struct HeldPoint {
    uintptr_t point;
    float despawn;
    uint16_t homeCells; // the cells it was listed in when the hold began (bit = cell index)
};
std::vector<HeldPoint> g_heldPoints; // points near Player 2 with their own despawn distance
// A held point's despawn distance (squared). The game finds a point's cells from its position +-
// sqrt(max(spawn, despawn)) (FUN_00672000, cell index FUN_00851860 clamped to 0..3) whenever it
// unregisters it (FUN_006615f0, e.g. in the CSpawnSet destructor). FLT_MAX made that index
// INT_MIN -> cell 0 only: a held point destroyed with its location stayed listed in its real cells,
// and stage 2 crashed on it (FUN_007999F0 -> FUN_0053AC60), and despawned what was left (P2's
// helicopter froze in the air; user, 2026-10-29). 32 km covers the whole map and clamps to all 16
// cells, so an unregister removes it everywhere; a point registered again while held went into
// every cell, so when the hold ends it is taken out of the cells it wasn't in before.
const float kHeldDespawn = 32768.0f * 32768.0f;
DWORD g_spawnHoldsPausedUntil;       // World2ReleaseSpawnPoints(pauseMs): no holding until then
// Diagnostics (2026-10-28: reloading a save with Player 2 joined crashed in stage 2, FUN_007999F0,
// on a listed point whose +0xC0 vector was garbage): the last points we queued, and a one-time log
// of the first listed point that looks broken.
uintptr_t g_queuedRing[64];
DWORD g_queuedRingTime[64];
unsigned g_queuedRingNext;
bool g_badPointLogged;
HeldPoint* FindHeld(uintptr_t point);

bool PointLooksBroken(uintptr_t point)
{
    uintptr_t begin = *(uintptr_t*)(point + 0xC0), end = *(uintptr_t*)(point + 0xC4);
    if (!begin) return false;
    return begin < 0x10000 || begin >= 0x7FFF0000 || end < begin || end - begin > 0x10000 || (begin & 3);
}

void CheckSpawnPoint(uintptr_t point, bool two)
{
    if (g_badPointLogged || !PointLooksBroken(point)) return;
    g_badPointLogged = true;
    int queuedAgo = -1;
    for (unsigned i = 0; i < 64; i++)
        if (g_queuedRing[i] == point) queuedAgo = (int)(GetTickCount() - g_queuedRingTime[i]);
    Log("World 2: broken spawn point %08X (+C0 %08X +C4 %08X, vtable %08X), held %d, queued by us %d ms ago, split %d",
        (unsigned)point, *(unsigned*)(point + 0xC0), *(unsigned*)(point + 0xC4), *(unsigned*)point,
        FindHeld(point) != nullptr, queuedAgo, two);
}

const uintptr_t kPointObject = 0xB4; // CPlacementComponent (FUN_007999F0 -> FUN_0053AC60 on it)

HeldPoint* FindHeld(uintptr_t point)
{
    for (HeldPoint& h : g_heldPoints)
        if (h.point == point) return &h;
    return nullptr;
}

// A listed point that is no longer a CSpawnSet (freed while still listed), or whose placement
// component (+0xB4, CPlacementComponent) or component list (+0xC0) is gone: stage 2's despawn
// (FUN_007999F0 -> FUN_0053AC60) crashed on such a point with Player 2 far from Player 1 (user,
// 2026-10-29). It is logged and taken out of the manager's list before the stage runs.
const uintptr_t kSpawnSetVtable = 0x00EBB5BC;
const uintptr_t kPlacementComponentVtable = 0x00EBB814;

bool IsCode(uintptr_t p) { return p >= 0x00401000 && p < 0x00D90000; }

int PointBroken(uintptr_t point)
{
    __try {
        if (*(uintptr_t*)point != kSpawnSetVtable) return 1;
        uintptr_t placement = *(uintptr_t*)(point + kPointObject);
        if (placement && *(uintptr_t*)placement != kPlacementComponentVtable) return 2;
        if (PointLooksBroken(point)) return 3;
        uintptr_t begin = *(uintptr_t*)(point + 0xC0), end = *(uintptr_t*)(point + 0xC4);
        for (uintptr_t it = begin; begin && it < end; it += 4) {
            uintptr_t component = *(uintptr_t*)it;
            uintptr_t vtable = component ? *(uintptr_t*)component : 0;
            if (vtable < 0x00D90000 || vtable >= 0x01100000 || !IsCode(*(uintptr_t*)vtable)) return 4;
        }
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 5;
    }
}

// A held point never despawns, so it was left out when leaving gameplay (game over, loading)
// tore down the vehicles: after the load the restored distance let stage 2 despawn it
// (FUN_007999F0) through its list of freed objects (+0xC0) and the game crashed. So the distances
// go back as soon as the split turns off (World2ReleaseSpawnPoints), before any teardown.
SRWLOCK g_spawnLock = SRWLOCK_INIT;
void UpdateSpawnPointsLocked(bool two, const float* camera2);

// Game thread, world update (before the spawn manager's). Only points found in the manager's
// lists are touched (a held point that is gone, e.g. after a load, is forgotten).
void UpdateSpawnPoints(bool two, const float* camera2)
{
    AcquireSRWLockExclusive(&g_spawnLock);
    __try {
        UpdateSpawnPointsLocked(two, camera2);
    } __finally {
        ReleaseSRWLockExclusive(&g_spawnLock);
    }
}

// The cells a point is listed in (bit = cell index).
uint16_t CellsOf(uintptr_t manager, uintptr_t point)
{
    uint16_t mask = 0;
    for (int cell = 0; cell < 16; cell++) {
        uintptr_t* list = (uintptr_t*)(manager + kSpawnCells + cell * 0x10);
        for (uintptr_t* it = (uintptr_t*)list[1]; it && it < (uintptr_t*)list[2]; it++)
            if (*it == point) mask |= (uint16_t)(1 << cell);
    }
    return mask;
}

// Takes a point out of the cells outside its home cells (the game registered it again while held,
// into every cell; it unregisters it later only from the cells its own distances reach).
void KeepOnlyHomeCells(uintptr_t manager, uintptr_t point, uint16_t home)
{
    int removed = 0;
    for (int cell = 0; cell < 16; cell++) {
        if (home & (1 << cell)) continue;
        uintptr_t* list = (uintptr_t*)(manager + kSpawnCells + cell * 0x10);
        for (uintptr_t* it = (uintptr_t*)list[1]; it && it < (uintptr_t*)list[2]; it++) {
            if (*it != point) continue;
            uintptr_t* end = (uintptr_t*)list[2];
            *it = *(end - 1);
            list[2] = (uintptr_t)(end - 1);
            it--;
            removed++;
        }
    }
    if (removed) Log("World 2: released spawn point %08X taken out of %d extra cells", (unsigned)point, removed);
}

void UpdateSpawnPointsLocked(bool two, const float* camera2)
{
    uintptr_t manager = *(uintptr_t*)kSpawnManager;
    if (!manager) {
        g_heldPoints.clear();
        return;
    }
    bool mine = two && g_spawnBoth && (LONG)(GetTickCount() - g_spawnHoldsPausedUntil) >= 0;
    bool queue = mine && *(uint8_t*)(manager + kSpawnOn) && !*(uint8_t*)(manager + kSpawnHeld);
    std::vector<HeldPoint> held;
    std::vector<uintptr_t> seen;     // a point listed in several cells is handled once
    std::vector<HeldPoint> released; // cleaned up after the walk
    for (int cell = 0; cell < 16; cell++) {
        uintptr_t* list = (uintptr_t*)(manager + kSpawnCells + cell * 0x10);
        for (uintptr_t* it = (uintptr_t*)list[1]; it && it < (uintptr_t*)list[2]; it++) {
            uintptr_t point = *it;
            if (int why = PointBroken(point)) {
                int queuedAgo = -1;
                for (unsigned i = 0; i < 64; i++)
                    if (g_queuedRing[i] == point) queuedAgo = (int)(GetTickCount() - g_queuedRingTime[i]);
                Log("World 2: dead spawn point %08X (reason %d) taken out of cell %d; held %d, queued by us %d ms ago, "
                    "split %d", (unsigned)point, why, cell, FindHeld(point) != nullptr, queuedAgo, two);
                // Erase it (the order of the list doesn't matter to the stage).
                uintptr_t* end = (uintptr_t*)list[2];
                *it = *(end - 1);
                list[2] = (uintptr_t)(end - 1);
                it--;
                continue;
            }
            if (std::find(seen.begin(), seen.end(), point) != seen.end()) continue;
            seen.push_back(point);
            CheckSpawnPoint(point, two);
            float& despawn = *(float*)(point + 0x84);
            HeldPoint* h = FindHeld(point);
            float own = h ? h->despawn : despawn;
            float d = mine ? DistanceSq(camera2, (const float*)(point + 0x68)) : FLT_MAX;
            if (d <= own) {
                held.push_back({ point, own, h ? h->homeCells : CellsOf(manager, point) });
                despawn = kHeldDespawn;
                if (queue && (*(uint8_t*)(point + 0xA0) & 1) && !*(uint32_t*)(point + 0xA4) &&
                    !*(uint32_t*)(point + 0xE0) && d < *(float*)(point + 0x80) && d > *(float*)(point + 0x88)) {
                    ((SpawnPoint_t)kSpawnQueue)(manager, nullptr, point);
                    g_queuedRing[g_queuedRingNext % 64] = point;
                    g_queuedRingTime[g_queuedRingNext++ % 64] = GetTickCount();
                }
            } else if (h) {
                despawn = own;
                released.push_back(*h);
            }
        }
    }
    g_heldPoints.swap(held);
    for (const HeldPoint& r : released) KeepOnlyHomeCells(manager, r.point, r.homeCells);
}

// The builder's update (FUN_008C2A20, called by the world update right after it waits for the
// render job: graphics engine slot 0x88, FUN_0092DF00) drapes the road meshes over the terrain
// height map and takes their look from it (through FUN_00A646A0 / FUN_00A56400): with Player 1's
// stream state in, Player 2's roads came out white whenever Player 1 was far away. On Player 2's
// road frames the update runs here first with his terrain in; the game's own call that follows
// finds nothing left to build (what the time budget didn't reach is dropped and queued again).
const uintptr_t kRenderWaitSlot = 0x00F229B4;
const uintptr_t kRenderWaitBeforeRoads = 0x0078230D;
const uintptr_t kRoadBuilderUpdate = 0x008C2A20; // __fastcall(builder)
typedef void(__fastcall* Void_t)(uintptr_t self);
Void_t g_origRenderWait;
volatile bool g_roadsOwnTerrain = true;  // "world2 roadterrain1" builds Player 2's with Player 1's
volatile bool g_roadsOwnGround = false;  // "world2 roadground" also swaps his ground state in

void BuildPlayer2Roads()
{
    if (!g_roadsOwned || g_lastRoadPlayer != 1 || !g_roadsOwnTerrain) return;
    uintptr_t manager = *(uintptr_t*)kRoadManager;
    uintptr_t builder = manager ? *(uintptr_t*)(manager + kRoadBuilder) : 0;
    if (!builder || !*(uint8_t*)(builder + 0x229)) return;
    if (!Terrain2UsePlayer2(true, g_roadsOwnGround)) return;
    __try {
        if (*(uintptr_t*)kTerrainMap) ((Void_t)kRoadBuilderUpdate)(builder);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    Terrain2UsePlayer2(false, g_roadsOwnGround);
    GameVector& wanted = *(GameVector*)(builder + 0x1F8);
    for (uint32_t* it = wanted.begin; it && it < wanted.end; it += 2) ReleaseCount(it[1]);
    if (wanted.begin) wanted.end = wanted.begin;
}

void __fastcall RenderWaitHook(uintptr_t self)
{
    g_origRenderWait(self);
    if ((uintptr_t)_ReturnAddress() != kRenderWaitBeforeRoads) return;
    __try {
        BuildPlayer2Roads();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    // Player 1's HUD update (FUN_00792510, 0x0078225B) has just written his button prompts.
    Player2UpdatePrompts();
}

} // namespace

// The camera update after this hook moves the camera, e.g. Player 2's 10 m around him as he turns
// it, often into another cell: the game's road update then rebuilt the wanted set for that camera
// alone and removed the other player's roads (they flickered until rebuilt here).
void World2SyncRoadCell()
{
    if (!g_roadsOwned) return;
    __try {
        uintptr_t manager = *(uintptr_t*)kRoadManager;
        uintptr_t builder = manager ? *(uintptr_t*)(manager + kRoadBuilder) : 0;
        uintptr_t cameras = *(uintptr_t*)kCameraManager;
        uintptr_t camera = cameras ? *(uintptr_t*)(cameras + 0x10) : 0;
        if (!builder || !camera) return;
        RoadCell((const float*)(camera + 0x54), (int*)(builder + 0x21C));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

namespace {

// --- Player 1-centred systems -----------------------------------------------------------------

// Systems that must follow Player 1 alone get his camera even when the main camera holds Player
// 2's world on his frames ("split p2world"): the area sets (two regional asset sets around one
// point, CAreaSetsManager [0x01191660] slot 0x38) and the physics floating origin (FUN_009A1940
// shifts it when the camera is more than [0x01092AA0] m away; "split p2world" makes that never
// happen, so it is done here: FUN_009B3950 on the physics system [0x0118FFF4], between the players,
// see ShiftOriginForPlayers).
const uintptr_t kAreaSetsUpdateSlot = 0x00F0C24C;
const uintptr_t kPhysicsSystem = 0x0118FFF4;
const uintptr_t kPhysicsShiftOrigin = 0x009B3950; // __thiscall(this, position)
const float kOriginShiftDistance = 1000.0f;
typedef void(__fastcall* PositionUpdate_t)(uintptr_t self, void* edx, float* position);
PositionUpdate_t g_origAreaSets;

volatile bool g_areaSetsAtPlayer2;   // testing: "world2 areasets2" ("areasets1" back)

// The update (FUN_008B37B0, only when its snapped position changed, +0x74) asks slot 10
// (FUN_008A5CC0: the sets of the 4 corners +-1000 m around the position, into a std::set) what is
// wanted, unloads the slots not wanted and loads wanted ones into free slots (2). Regional
// buildings and vehicle models come from these: with Player 1 at a base 5 km away, the town
// around Player 2 had no buildings. AreaSetsQueryHook adds Player 2's wanted sets; the update
// runs again when Player 2's 1024 m cell changes.
const uintptr_t kAreaSetsQuerySlot = 0x00F0C23C;
const uintptr_t kAreaSetsLastPosition = 0x74;
const float kAreaSetsCell = 1024.0f;
typedef void(__fastcall* AreaSetsQuery_t)(uintptr_t self, void* edx, const float* position, void* wanted, uint8_t* flag);
AreaSetsQuery_t g_origAreaSetsQuery;
volatile bool g_areaSetsBoth = true;  // "world2 areasets1" leaves them to Player 1
int g_areaSetsCell2[2] = { 0x7FFFFFFF, 0x7FFFFFFF };

void __fastcall AreaSetsQueryHook(uintptr_t self, void* edx, const float* position, void* wanted, uint8_t* flag)
{
    g_origAreaSetsQuery(self, edx, position, wanted, flag);
    float camera[3], direction[3];
    if (g_areaSetsBoth && !g_areaSetsAtPlayer2 && SplitscreenPlayer2Status() == Player2Status::Joined &&
        SplitViewCamera(1, camera, direction))
        g_origAreaSetsQuery(self, edx, camera, wanted, flag);
}

void __fastcall AreaSetsHook(uintptr_t self, void* edx, float* position)
{
    float camera[3], direction[3];
    if (g_areaSetsAtPlayer2 ? SplitViewCamera(1, camera, direction) : SplitRenderPlayer1Position(camera))
        position = camera;
    float camera2[3];
    if (g_areaSetsBoth && SplitscreenPlayer2Status() == Player2Status::Joined && SplitViewCamera(1, camera2, direction)) {
        int cell[2] = { (int)floorf(camera2[0] / kAreaSetsCell), (int)floorf(camera2[2] / kAreaSetsCell) };
        if (cell[0] != g_areaSetsCell2[0] || cell[1] != g_areaSetsCell2[1]) {
            g_areaSetsCell2[0] = cell[0];
            g_areaSetsCell2[1] = cell[1];
            *(float*)(self + kAreaSetsLastPosition) = NAN; // never equal: the update runs
        }
    }
    g_origAreaSets(self, edx, position);
}

// The physics world only takes in bodies within its broadphase box, +-4050 m on x and z around the
// origin (broadphase [[[0x0118FFF4]+0x58]+0x54] +0x20/+0x30); a shift takes out the static bodies
// that end up outside, and they come back only when their location loads again. Following Player
// 1 alone, Player 2 more than ~4 km away had no buildings or objects to stand on or grapple onto
// (user, 2026-10-29), and a base he kept loaded stayed without collision when Player 1 teleported
// back to it (both fell through its helipad). The origin goes between the two players, so both
// have collision while they are within ~8 km of each other on each axis; farther apart, Player 1
// keeps his.
// The box is a cube whose size the world creation (FUN_0098CF70) takes as [0x01092AAC] -
// [0x01092AA8] (y max 7100 - y min -1000 = 8100 m: x/z +-4050); World2EnlargePhysicsWorld raises y
// max before the world is created.
const uintptr_t kPhysicsWorldYMin = 0x01092AA8, kPhysicsWorldYMax = 0x01092AAC;
const float kOriginPlayer1Clearance = 550.0f; // Player 1 at least this far inside the box per axis

void ShiftOriginForPlayers(const float* camera1, const float* camera2)
{
    uintptr_t physics = *(uintptr_t*)kPhysicsSystem;
    if (!physics) return;
    float half = (*(float*)kPhysicsWorldYMax - *(float*)kPhysicsWorldYMin) * 0.5f;
    float margin = half > 2.0f * kOriginPlayer1Clearance ? half - kOriginPlayer1Clearance : half * 0.5f;
    float target[3] = { (camera1[0] + camera2[0]) * 0.5f, camera1[1], (camera1[2] + camera2[2]) * 0.5f };
    for (int i = 0; i < 3; i += 2) {
        if (target[i] > camera1[i] + margin) target[i] = camera1[i] + margin;
        if (target[i] < camera1[i] - margin) target[i] = camera1[i] - margin;
    }
    float dx = target[0] - *(float*)(physics + 0x28), dz = target[2] - *(float*)(physics + 0x30);
    if (dx * dx + dz * dz > kOriginShiftDistance * kOriginShiftDistance)
        ((PositionUpdate_t)kPhysicsShiftOrigin)(physics, nullptr, target);
}

void __fastcall UpdateHook(uintptr_t world, void* edx, float* position)
{
    HookSpawnManagerDespawn();
    float camera2[3], direction[3];
    bool two = g_on && SplitscreenPlayer2Status() == Player2Status::Joined && SplitViewCamera(1, camera2, direction);
    if (!two) {
        // Undo our +0x42 from the last frame.
        uintptr_t manager = *(uintptr_t*)kDecorations;
        if (g_decorationsOffByUs && manager) *(uint8_t*)(manager + 0x42) = 0;
        g_decorationsOffByUs = false;
        g_tilesShared = false;
        __try {
            UpdateRoads(false, position, position);
            UpdateSpawnPoints(false, position);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        g_origUpdate(world, edx, position);
        return;
    }
    // Player 1's camera even when the main camera holds Player 2's world ("split p2world").
    float camera1[3];
    if (SplitRenderPlayer1Position(camera1)) {
        position = camera1;
        __try {
            ShiftOriginForPlayers(camera1, camera2);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    uint8_t& flags = *(uint8_t*)(world + kGameWorldFlags);
    bool skip = flags & 1;
    flags |= 1;
    // Testing ("world2 updatepos2"): the rest of the update gets Player 2's camera on his frames
    // (this runs before this frame's camera update: the view is 1 - the previous one).
    g_origUpdate(world, edx, g_updateAtPlayer2 && SplitRenderGameView() == 0 ? camera2 : position);
    if (!skip) flags &= ~1;
    __try {
        uintptr_t manager = world + kLocations;
        if (!skip && !((This_t)kLocationsBusy)(manager)) {
            ((Pick_t)kLocationsStep)(manager);
            // Player 1's camera first: his locations load before Player 2's.
            const float* points[2] = { position, camera2 };
            if (!UnloadOne(manager, points, 2)) LoadWanted(manager, points, 2);
        }
        UpdateDecorations(position, camera2);
        UpdateRoads(true, position, camera2);
        UpdateSpawnPoints(true, camera2);
        KeepVehiclesOnPrivateVtables();
        g_tilesShared = true;
        if ((LONG)(GetTickCount() - g_nextLog) >= 0) {
            g_nextLog = GetTickCount() + 10000;
            uintptr_t* loaded = *(uintptr_t**)(manager + 0x34);
            EnterCriticalSection(&g_tileLock);
            size_t pending = g_pendingReleases.size();
            LeaveCriticalSection(&g_tileLock);
            Log("World 2: %d locations loaded (cameras %.0f %.0f, %.0f %.0f); AI tiles in 10 s: %ld requests, "
                "%ld releases, %ld kept, %u waiting",
                loaded ? (int)(*(uintptr_t**)(manager + 0x38) - loaded) : 0, position[0], position[2], camera2[0],
                camera2[2], InterlockedExchange(&g_tileRequests, 0), InterlockedExchange(&g_tileReleases, 0),
                InterlockedExchange(&g_tileCancelled, 0), (unsigned)pending);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("World 2: exception %08X, turned off", GetExceptionCode());
        g_on = false;
    }
}

// Testing ("world2 trace"): logs the distinct call stacks that add rigid bodies to the physics
// world and take them out (CPfxRigidBody slots 9 and 10).
const uintptr_t kPfxAddSlot = 0x00F268D0;
const uintptr_t kPfxRemoveSlot = 0x00F268D4;
typedef void(__fastcall* PfxWorld_t)(void* self);
PfxWorld_t g_origPfxAdd, g_origPfxRemove;
volatile bool g_trace;
CRITICAL_SECTION g_traceLock;
uint32_t g_traceSeen[64];
int g_traceCount;

volatile uintptr_t g_traceTarget; // "world2 trace <hex>": only that CPfxRigidBody, every call

void TraceCaller(const char* what, void* self)
{
    if (!g_trace) return;
    if (g_traceTarget && (uintptr_t)self != g_traceTarget) return;
    if (g_traceTarget) g_traceCount = 0;
    void* frames[10] = {};
    USHORT n = RtlCaptureStackBackTrace(1, 10, frames, nullptr);
    uint32_t key = 0;
    for (USHORT i = 0; i < n; i++) key = key * 31 + (uint32_t)(uintptr_t)frames[i];
    EnterCriticalSection(&g_traceLock);
    bool seen = false;
    for (int i = 0; i < g_traceCount; i++) seen |= g_traceSeen[i] == key;
    if (!seen && g_traceCount < 64) g_traceSeen[g_traceCount++] = key;
    LeaveCriticalSection(&g_traceLock);
    if (seen) return;
    char line[200] = "";
    int len = 0;
    for (USHORT i = 0; i < n; i++) len += sprintf_s(line + len, sizeof(line) - len, " %08X", (unsigned)(uintptr_t)frames[i]);
    Log("World 2: %s from%s (thread %lu)", what, line, GetCurrentThreadId());
}

void __fastcall PfxAddHook(void* self)
{
    TraceCaller("add body", self);
    g_origPfxAdd(self);
}

void __fastcall PfxRemoveHook(void* self)
{
    TraceCaller("remove body", self);
    g_origPfxRemove(self);
}

bool HookSlot(uintptr_t slot, void* hook, void** orig)
{
    DWORD old;
    if (!VirtualProtect((void*)slot, 4, PAGE_READWRITE, &old)) return false;
    *orig = *(void**)slot;
    *(void**)slot = hook;
    VirtualProtect((void*)slot, 4, old, &old);
    return true;
}

} // namespace

void World2EnlargePhysicsWorld()
{
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) *(slash + 1) = '\0';
    strcat_s(path, "jc2ss.ini");
    // jc2ss.ini [Physics] WorldSize: the cube's edge in metres (the game's 8100: players ~8 km apart).
    float size = (float)GetPrivateProfileIntA("Physics", "WorldSize", 16100, path);
    float* yMin = (float*)kPhysicsWorldYMin;
    float* yMax = (float*)kPhysicsWorldYMax;
    float before = *yMax - *yMin;
    if (size < before) {
        Log("World 2: physics world size %.0f m kept (asked for %.0f)", before, size);
        return;
    }
    DWORD old;
    if (!VirtualProtect(yMax, 4, PAGE_READWRITE, &old)) return;
    *yMax = *yMin + size;
    VirtualProtect(yMax, 4, old, &old);
    Log("World 2: physics world %.0f -> %.0f m (x/z +-%.0f, y %.0f..%.0f)", before, size, size * 0.5f, *yMin, *yMax);
}

void World2ReleaseSpawnPoints(unsigned pauseMs)
{
    if (pauseMs) g_spawnHoldsPausedUntil = GetTickCount() + pauseMs;
    size_t held = g_heldPoints.size();
    __try {
        UpdateSpawnPoints(false, nullptr); // not "mine": every held point gets its distance back
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    if (held) Log("World 2: %u spawn points held for player 2 got their despawn distance back", (unsigned)held);
}

bool DebugSkip(const char* key); // dllmain.cpp: jc2ss.ini [Debug] switches (testing)

bool World2InstallHooks()
{
    InitializeCriticalSection(&g_traceLock);
    InitializeCriticalSection(&g_vehicleSleepLock);
    InitializeCriticalSection(&g_tileLock);
    if (!DebugSkip("SkipW2Lod")) {
        HookSlot(kVehicleLodSlots[0], (void*)VehicleLodHook<0>, (void**)&g_origVehicleLod[0]);
        HookSlot(kVehicleLodSlots[1], (void*)VehicleLodHook<1>, (void**)&g_origVehicleLod[1]);
        HookSlot(kVehicleLodSlots[2], (void*)VehicleLodHook<2>, (void**)&g_origVehicleLod[2]);
        HookSlot(kVehicleLodSlots[3], (void*)VehicleLodHook<3>, (void**)&g_origVehicleLod[3]);
        HookSlot(kVehicleLodSlots[4], (void*)VehicleLodHook<4>, (void**)&g_origVehicleLod[4]);
        HookSlot(kVehicleLodSlots[5], (void*)VehicleLodHook<5>, (void**)&g_origVehicleLod[5]);
        HookSlot(kVehicleLodSlots[6], (void*)VehicleLodHook<6>, (void**)&g_origVehicleLod[6]);
        HookSlot(kVehicleLodSlots[7], (void*)VehicleLodHook<7>, (void**)&g_origVehicleLod[7]);
        HookSlot(kVehicleLodSlots[8], (void*)VehicleLodHook<8>, (void**)&g_origVehicleLod[8]);
    }
    if (!DebugSkip("SkipW2Sleep")) MakeVehicleVtables();
    if (!DebugSkip("SkipW2Tiles")) {
        HookSlot(kTileRequestSlot, (void*)TileRequestHook, (void**)&g_origTileRequest);
        HookSlot(kTileReleaseSlot, (void*)TileReleaseHook, (void**)&g_origTileRelease);
    }
    if (!DebugSkip("SkipW2AreaSets")) {
        HookSlot(kAreaSetsUpdateSlot, (void*)AreaSetsHook, (void**)&g_origAreaSets);
        HookSlot(kAreaSetsQuerySlot, (void*)AreaSetsQueryHook, (void**)&g_origAreaSetsQuery);
    }
    if (!DebugSkip("SkipW2Pfx")) {
        HookSlot(kPfxAddSlot, (void*)PfxAddHook, (void**)&g_origPfxAdd);
        HookSlot(kPfxRemoveSlot, (void*)PfxRemoveHook, (void**)&g_origPfxRemove);
    }
    if (!DebugSkip("SkipW2RenderWait")) HookSlot(kRenderWaitSlot, (void*)RenderWaitHook, (void**)&g_origRenderWait);
    if (!DebugSkip("SkipW2Despawn")) {
        // Slot 0 (the CSpawnManager functor) is not hooked in its vtable: the game checks that
        // vtable's contents and quits (see HookSpawnManagerDespawn).
        if (DebugSkip("DespawnVtable0")) HookSlot(kDespawnFunctorSlots[0], (void*)DespawnHook<0>, (void**)&g_origDespawn[0]);
        else g_despawnPerFunctor = !DebugSkip("SkipW2Despawn0");
        if (!DebugSkip("SkipW2Despawn1")) HookSlot(kDespawnFunctorSlots[1], (void*)DespawnHook<1>, (void**)&g_origDespawn[1]);
        if (!DebugSkip("SkipW2Despawn2")) HookSlot(kDespawnFunctorSlots[2], (void*)DespawnHook<2>, (void**)&g_origDespawn[2]);
    }
    if (DebugSkip("SkipW2Update")) return true;
    DWORD old;
    if (!VirtualProtect((void*)kGameWorldUpdateSlot, 4, PAGE_READWRITE, &old)) return false;
    g_origUpdate = *(Update_t*)kGameWorldUpdateSlot;
    *(void**)kGameWorldUpdateSlot = (void*)UpdateHook;
    VirtualProtect((void*)kGameWorldUpdateSlot, 4, old, &old);
    return true;
}

std::string World2Set(const std::string& mode)
{
    if (mode == "roads" || mode == "noroads") {
        g_roads = mode == "roads";
        return std::string("ok roads around both players ") + (g_roads ? "on" : "off");
    }
    if (mode == "roadterrain2" || mode == "roadterrain1" || mode == "roadground" || mode == "noroadground") {
        if (mode == "roadterrain2" || mode == "roadterrain1") g_roadsOwnTerrain = mode == "roadterrain2";
        else g_roadsOwnGround = mode == "roadground";
        return std::string("ok player 2's roads built with his terrain ") + (g_roadsOwnTerrain ? "on" : "off") +
               (g_roadsOwnGround ? " (and his ground state)" : "");
    }
    if (mode == "sleep1" || mode == "sleep2") {
        g_vehicleSleepBoth = mode == "sleep2";
        return std::string("ok vehicles sleep by ") + (g_vehicleSleepBoth ? "the nearer player" : "the main camera");
    }
    if (mode == "lod1" || mode == "lod2") {
        g_vehicleLodBoth = mode == "lod2";
        return std::string("ok vehicle detail from ") + (g_vehicleLodBoth ? "the nearer player's camera" : "the main camera");
    }
    if (mode == "tiles1" || mode == "tiles2") {
        g_tileDelay = mode == "tiles2";
        return std::string("ok AI tile releases ") + (g_tileDelay ? "wait while player 2 is in" : "at once");
    }
    if (mode == "spawn1" || mode == "spawn2") {
        g_spawnBoth = mode == "spawn2";
        return std::string("ok spawn points near player 2 ") + (g_spawnBoth ? "kept" : "left to the game");
    }
    if (mode.compare(0, 10, "despawnlog") == 0) {
        g_despawnLogLeft = mode.size() > 11 ? atol(mode.c_str() + 11) : 50;
        return "ok logging the next " + std::to_string(g_despawnLogLeft) + " despawn functor calls";
    }
    if (mode == "despawn1" || mode == "despawn2") {
        g_despawnBoth = mode == "despawn2";
        return std::string("ok spawned objects despawn by ") + (g_despawnBoth ? "both cameras" : "the main camera only");
    }
    if (mode == "updatepos1" || mode == "updatepos2") {
        g_updateAtPlayer2 = mode == "updatepos2";
        return std::string("ok the world update gets player ") + (g_updateAtPlayer2 ? "2's camera on his frames" : "1's camera");
    }
    if (mode == "areasets1" || mode == "areasets2" || mode == "areasets") {
        g_areaSetsAtPlayer2 = mode == "areasets2";
        g_areaSetsBoth = mode == "areasets";
        return std::string("ok area sets around ") + (g_areaSetsBoth ? "both players" : g_areaSetsAtPlayer2 ? "player 2" : "player 1");
    }
    if (mode == "decorations" || mode == "nodecorations") {
        g_decorations = mode == "decorations";
        return std::string("ok decorations around both players ") + (g_decorations ? "on" : "off");
    }
    if (mode.compare(0, 5, "trace") == 0 || mode == "notrace") {
        g_trace = mode != "notrace";
        g_traceTarget = mode.size() > 6 ? strtoul(mode.c_str() + 6, nullptr, 16) : 0;
        g_traceCount = 0;
        return std::string("ok body add/remove trace ") + (g_trace ? "on" : "off");
    }
    g_on = mode != "off";
    return std::string("ok locations around both players ") + (g_on ? "on" : "off");
}
