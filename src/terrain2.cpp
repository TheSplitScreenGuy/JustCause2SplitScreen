#include <windows.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <intrin.h>
#include "terrain2.h"
#include "game.h"
#include "player2.h"
#include "splitrender.h"
#include "splitscreen.h"
#include "patch.h"
#include "log.h"

// Player 2's own terrain detail. The game builds the landscape around one camera; its vegetation
// (CVegetationManager) and ground mesh (CPatchManager) keep their state in a singleton and in
// globals, and the render job reads those (and a global table of terrain blocks the vegetation
// refers to) while the game prepares the next frame. Player 2 gets second copies of both, built
// with the game's own constructors and initialisers, and the landscape no longer runs in the
// world update: at the frame hand-off, after the last frame was drawn and before the next one
// starts, the state of the player whose view comes next is switched in and the game's landscape
// update and draw-list preparation run for that player's camera.

namespace {

struct Range {
    uintptr_t addr;
    size_t size;
};

template <size_t N>
void SaveState(const Range (&ranges)[N], uint8_t* out)
{
    for (const Range& r : ranges) {
        memcpy(out, (void*)r.addr, r.size);
        out += r.size;
    }
}

template <size_t N>
void LoadState(const Range (&ranges)[N], const uint8_t* in)
{
    for (const Range& r : ranges) {
        memcpy((void*)r.addr, in, r.size);
        in += r.size;
    }
}

// A global's value in a saved state.
template <size_t N>
uint32_t ReadState(const Range (&ranges)[N], const uint8_t* state, uintptr_t addr)
{
    for (const Range& r : ranges) {
        if (addr >= r.addr && addr + 4 <= r.addr + r.size) return *(const uint32_t*)(state + (addr - r.addr));
        state += r.size;
    }
    return *(uint32_t*)addr;
}

template <size_t N>
void WriteState(const Range (&ranges)[N], uint8_t* state, uintptr_t addr, uint32_t value)
{
    for (const Range& r : ranges) {
        if (addr >= r.addr && addr + 4 <= r.addr + r.size) {
            *(uint32_t*)(state + (addr - r.addr)) = value;
            return;
        }
        state += r.size;
    }
}

// --- Vegetation -------------------------------------------------------------------------------

// Globals the vegetation code keeps per manager (tools/ghidra/TreeRefs.java over its call tree).
const Range kVegetationState[] = {
    { 0x011902F0, 0x04 },   // CVegetationManager singleton
    { 0x01191378, 0x04 },   // vegetation sets per climate zone (shared, see below)
    { 0x0108B8F8, 0x38 },   // job handles +0x0C..+0x28, detail distances
    { 0x0108BCC8, 0x28 },   // cell each detail level was last requested for
    { 0x0108C454, 0x04 },
    { 0x01381D40, 0x08 },   // render states (FUN_00a98f10)
    { 0x01381F78, 0x04 },
    { 0x01381F88, 0x04 },
    { 0x01381F94, 0x04 },   // +2 reset request, +3 set up
    { 0x01381FB0, 0x40 },   // renderers (FUN_00a98f10), leaves type; impostor types +8/+0x24 with
                            // their staging write pointers and counts after each
    { 0x01381FF0, 0x68 },   // render states, lock, job types, camera, block pools
    { 0x013820F0, 0x0C },   // vector
    { 0x0138AAAC, 0x04 },
    { 0x0138AAB0, 0x1690 }, // 19x19 cells: list of tree indices (16 bytes each, vector at +4)
    { 0x0138C140, 0x0C },   // cell queue (vector)
    { 0x0138C1A8, 0x28 },   // two bit vectors (0x0138C190/A0 between are shared textures)
    // Climate zone whose vegetation sets the camera uses (FUN_00a55680): first-call flag, current
    // zone; and the zone FUN_00a557a0 skips (with the zone object's fields, see SwitchZone).
    { 0x012B4478, 0x08 },
    { 0x012B448C, 0x04 },
};
// Shared by both copies: renderers, render states, geometry and block types FUN_00a98f10 makes
// (the graphics engine keeps one type per name, so Player 2's duplicates are dead; his copy only
// needs the cell and block arrays at 0x01382034..0x01382040). Player 2's copy starts from
// Player 1's values; the staging write pointers/counts after the impostor types then stay per copy.
const uintptr_t kVegetationShared[] = {
    0x01381D40, 0x01381D44, 0x01381F78, 0x01381F88, 0x01381FB0, 0x01381FB4, 0x01381FB8, 0x01381FD4,
    0x01381FBC, 0x01381FC0, 0x01381FC4, 0x01381FC8, 0x01381FCC, 0x01381FD0,
    0x01381FD8, 0x01381FDC, 0x01381FE0, 0x01381FE4, 0x01381FE8, 0x01381FEC,
};
// Containers the constructor (re)allocates in place, freeing what was there: cleared first so
// Player 2's copy gets new ones and Player 1's stay intact.
const Range kVegetationContainers[] = {
    { 0x0138AAB0, 0x1690 },
    { 0x0138C140, 0x0C },
    { 0x0138C1A8, 0x28 },
    { 0x013820F0, 0x0C },
};
const uintptr_t kVegetationManager = 0x011902F0;
const uintptr_t kVegetationSets = 0x01191378;
const uintptr_t kVegetationTreeHandler = 0xC;     // CBreakableTreePatchHandler
const size_t kTreeHandlerSize = 0x30;
const uintptr_t kTreeHandlerCtor = 0x00529EC0;    // __thiscall(this)
const uintptr_t kVegetationJobTypes = 0x0138200C; // the 8 job type name hashes
const size_t kVegetationManagerSize = 0x414C;
const uintptr_t kVegetationCtor = 0x00A94250;     // __thiscall(this)
const uintptr_t kVegetationSetup = 0x00A98F10;    // reads the singleton

// The job type table the vegetation constructor appends its 8 job types to: count, then {name
// hash, handler} pairs from +4. It is full (whatever follows it are other globals), so the count
// is set back to where Player 1's vegetation entries start for the constructor: it rewrites them
// with the same names and equivalent handlers.
const uintptr_t kJobTypeCount = 0x0118FEA8;
const uintptr_t kJobTypeEntries = 0x0118FEAC;

// --- Ground -----------------------------------------------------------------------------------

const uintptr_t kWaterBlocks = 0x01382100;   // 704 x 0x30
const uintptr_t kWaterFreeList = 0x0138A500; // 704 uint16 indices
const uintptr_t kWaterFreeTop = 0x0138AA80;  // uint16: index of the top free entry
const int kWaterBlockCount = 704;

// CPatchManager: terrain mesh patches at 11 detail levels around a camera, drawn from its own two
// model pools (+0x8D8/+0x8DC), with blocks from a table of 704 that one copy fills completely.
const Range kGroundState[] = {
    { 0x01191834, 0x04 },    // CPatchManager singleton
    // Job handles (update, visibility, render) and settings after them. The 4 bytes before are
    // global feature flags (+1 = high-end water, kHighEndWater), not per player.
    { 0x0108AB08, 0x14 },
    { 0x012AB570, 0x8F08 },  // per-frame load budget, then 11x64 patch cells (0x34 each) from +8
    { 0x01365498, 0x340 },   // per-cell bits
    { 0x013657F4, 0x20 },    // flags; the copy's two pools at +0x18/+0x1C (FUN_00a69ed0)
    { 0x01368150, 0x13984 }, // 704 CTerrainRenderBlocks (0x70 each), free list, its top index
    // The stream patch handlers the render job reads too (see kStreamState): the same for both
    // players unless Player 2 has his own stream patches.
    { 0x01191D98, 0x04 },    // CTerrainTexture
    { 0x01191224, 0x04 },    // CMapTexture
    // Water render blocks (non-GPU water): each loaded cell takes one (cell +4, FUN_00A88C10 from
    // the cell load FUN_00A67360) from a pool of 704 CWaterHighEndRenderBlocks (0x30 each, built
    // in place on taking, destroyed on giving back FUN_00A80B50) with a free list of indices and its
    // 16-bit top. Shared, Player 2's cells ran the free list below zero: his cells got junk indices
    // (no sea for him without GPU water simulation, e.g. on the Steam Deck; user, 2026-10-29) and
    // the game built render blocks at table + index * 0x30 up to 3 MB past the table, with or
    // without GPU water. Each player has his own pool (CreateGround2 makes his full).
    { kWaterBlocks, kWaterBlockCount * 0x30 },
    { kWaterFreeList, kWaterBlockCount * 2 },
    { kWaterFreeTop, 0x02 },
};
const uintptr_t kHighEndWater = 0x0108AB05;      // byte: NVIDIA high-end water available
// Cleared before Player 2's copy initialises its cells (FUN_00a69ed0 starts from empty cells).
const Range kPatchContainers[] = {
    { 0x012AB578, 0x8F00 },
    { 0x01365498, 0x340 },
};
const uintptr_t kPatchManager = 0x01191834;
const uintptr_t kPatchPools = 0x8D8;
const size_t kPatchManagerSize = 0x8E0;
const uintptr_t kTerrainBlocks = 0x01368150;
const uintptr_t kPatchCtor = 0x00A69FB0;         // __thiscall(this)
const uintptr_t kPatchInit = 0x00A69ED0;         // __cdecl(pool, pool)
const uintptr_t kTerrainBlocksInit = 0x00A58150; // __thiscall(table): blocks + free list
const uintptr_t kPatchUpdate = 0x00A6A970;       // __thiscall(this, camera): cells around the camera
const uintptr_t kPatchVisibility = 0x00A638C0;   // __thiscall(this, camera (unused))
const uintptr_t kVegetationUpdate = 0x00A92CA0;  // __thiscall(this, camera)
// [0x0136582C] FUN_00a61220 (__thiscall(this, camera)): boxes around the camera per frame parity.
const uintptr_t kTerrainRegions = 0x0136582C;
const uintptr_t kTerrainRegionsUpdate = 0x00A61220;

// --- Landscape --------------------------------------------------------------------------------

const uintptr_t kLandscape = 0x01191C48;         // CLandscapeManager
const uintptr_t kLandscapeEnabled = 0xD;         // gates its update and draw-list preparation
const uintptr_t kLandscapeUpdate = 0x00A751B0;   // __thiscall(this), camera from [0x011919F4]+0x10
const uintptr_t kLandscapeDrawLists = 0x00A67100;// __thiscall(this)
const uintptr_t kWaitLandscapeJobs = 0x00A5E360; // water, vegetation update, patch update/visibility
const uintptr_t kWaitVegetationDraw = 0x00A7B650;
const uintptr_t kPatchDrawJob = 0x0108AB10;
// Stream patch residency: 64x64 bits (one per 512 m cell, rows of 2 dwords), set when a cell's
// file is loaded (FUN_00a3f7b0). The patch update stops loading at the first detail-level cell
// that isn't resident (FUN_00a67610: budget [0x012AB570] = 0).
const uintptr_t kStreamResidency = 0x012AB250;
const size_t kStreamResidencySize = 0x200;
const uintptr_t kCameraManager = 0x011919F4;
const uintptr_t kCameraManagerLogic = 0x10;      // CCamera* the landscape (and game logic) reads

// --- Stream patches ---------------------------------------------------------------------------

// CStreamPatchManager: an 8x8 ring of 512 m cells around a position, each loaded from
// <base>_XX_YY.dat by the stream queue (worker threads) into a CStreamPatch slot of the manager,
// which hands the data to the terrain patch reader [0x012AB24C] (one shared object). The reader
// passes it on to five handlers, each a singleton with 64 slots (cell & 7): CTerrainMapGame
// (heights, materials), CTerrainTexture (assigns the textures to registered terrain blocks),
// CMapTexture, CTerrainData (mesh data) and CGameZoneMap. Player 2 gets his own manager and
// handlers. The two texture handlers belong to the ground state (the render job reads them while
// it draws a player's frame); the rest is Player 1's outside Player 2's turn, as game logic and
// physics read the heights and zones.
const Range kStreamState[] = {
    { 0x01190868, 0x04 },   // CStreamPatchManager singleton
    { 0x012AB248, 0x04 },
    { 0x012AB250, 0x200 },  // residency bits
    { 0x01190290, 0x04 },   // CTerrainMapGame
    { 0x011F9C68, 0x04 },   // CTerrainData
    { 0x01381AF8, 0x04 },   // CGameZoneMap
};
const uintptr_t kStreamManager = 0x01190868;
const size_t kStreamManagerSize = 0x881C;
const uintptr_t kStreamManagerCtor = 0x00A42F30; // __thiscall(this, reader)
const uintptr_t kStreamReader = 0x012AB24C;
const uintptr_t kStreamGlobalLoad = 0x00A43120;  // <base>.dat into the handlers (reader slot 2)
const uintptr_t kStreamFlag = 0x012AB248;
const uintptr_t kTerrainMap = 0x01190290;
const uintptr_t kTerrainTexture = 0x01191D98;
const uintptr_t kMapTexture = 0x01191224;
const uintptr_t kTerrainData = 0x011F9C68;
const uintptr_t kTerrainDataGlobal = 0x478;       // FUN_00A643C0: header fields +0x478..+0x490
const size_t kTerrainDataGlobalSize = 0x1C;
const uintptr_t kTerrainDataLevelTables = 0x49C;  // per-level tables, levels 14..10
const uintptr_t kTerrainDataGlobalLoaded = 0x4A0;
const uintptr_t kZoneMap = 0x01381AF8;
// The CTerrainMapGame constructor frees and reloads this (a constant displacement table that
// height queries read); Player 1's stays.
const uintptr_t kTerrainDisplacement = 0x01365824;
// CStreamPatch: +0x10/+0x14 the cell requested, +0x18 set while its file loads.
const uintptr_t kPatchSlotRequested = 0x10;
const uintptr_t kPatchSlotLoading = 0x18;

// Graphics engine [0x0118FBDC] slot 3 (FUN_00942f60): the frame hand-off. +0x28 is the handle of
// the render job in flight, which it waits for before kicking the next.
const uintptr_t kHandOffSlot = 0x00F22938;
const uintptr_t kEngineRenderJob = 0x28;
const uintptr_t kGraphicsEngine = 0x0118FBDC;

const uintptr_t kGameNew = 0x00444AE0;           // __cdecl(size)
const uintptr_t kWaitJob = 0x004ED210;           // __cdecl(handle)

typedef void*(__cdecl* GameNew_t)(size_t size);
typedef uintptr_t(__fastcall* Ctor_t)(void* self);
typedef void(__cdecl* Void_t)();
typedef void(__cdecl* Arg_t)(uintptr_t arg);
typedef void(__cdecl* Arg2_t)(uintptr_t a, uintptr_t b);
typedef void(__fastcall* This_t)(uintptr_t self);
typedef void(__fastcall* ThisArg_t)(uintptr_t self, void* edx, uintptr_t arg);
typedef void(__fastcall* HandOff_t)(void* engine);

HandOff_t g_origHandOff;
volatile bool g_on = true;       // runs whenever Player 2 is in ("terrain2 off" to compare)
volatile bool g_player1Only;     // testing: "terrain2 p1only" runs only Player 1's at the hand-off
volatile bool g_player1Alternate;// testing: "terrain2 p1alt" ... and only on his frames
// P2's turn runs the whole FUN_00a751b0 (without the climate zone and sky his sky is black and
// his ground uses Player 1's zone); "terrain2 partial" only the per-player parts. The climate
// zone's selection is per player (SwitchZone), so players in different zones keep both loaded.
volatile bool g_fullUpdate = true;
// During P2's turn every stream cell counts as loaded (the stream window is P1's), so his patch
// manager builds all detail levels instead of stalling at the first cell outside P1's window
// ("terrain2 noresident" turns it off). Cells outside the window read another cell's data (the
// stream slots are a ring, cell & 7): fine up to ~2 km from P1, broken beyond (no ground at 3 km).
volatile bool g_spoofResidency = true;
// Player 2 streams his own patches ("terrain2 nostream2" before the first "terrain2 on" turns it
// off: the copies are made together, so his terrain blocks only ever register with his own
// texture handler).
volatile bool g_ownStream = true;
int g_activeStream = 0;          // whose stream state is in the globals
uintptr_t g_streamManager[2];
uint8_t g_streamState[2][0x400];
float g_player1StreamPosition[3];// what the world update asked Player 1's manager for
bool g_havePlayer1StreamPosition;
bool g_groundEverOn;             // the ground copy has been used (its blocks registered somewhere)
// Testing ("terrain2 stream"): the stream patches load around Player 2 instead of the camera.
volatile bool g_streamAtPlayer2;
// Player 2's frames are drawn with his camera's world position ("terrain2 norenderworld" to compare).
volatile bool g_renderWorld2 = true;
int g_active = -1;               // whose ground state is in the globals (-1: copies not set up)
int g_activeVegetation = 0;      // whose vegetation state is
uintptr_t g_vegetationManager[2];
uint8_t g_vegetationState[2][0x2000];
uintptr_t g_patchManager[2];
uint8_t g_groundState[2][0x26000];

const size_t kCameraCopySize = 0x700;
uint8_t g_camera2[kCameraCopySize];   // Player 2's camera after its update (splitrender)
uint8_t g_landCamera[kCameraCopySize];// what the landscape reads during Player 2's turn
bool g_haveCamera2;

LARGE_INTEGER g_freq;
double g_ms;
int g_frames;

void WaitLandscapeJobs()
{
    ((Void_t)kWaitLandscapeJobs)();
    ((Void_t)kWaitVegetationDraw)();
    if (*(int*)kPatchDrawJob != -1) {
        ((Arg_t)kWaitJob)(*(uintptr_t*)kPatchDrawJob);
        *(int*)kPatchDrawJob = -1;
    }
}

bool CreateVegetation2()
{
    unsigned jobTypes = *(unsigned*)kJobTypeCount, firstVegetationJob = jobTypes;
    for (unsigned i = 0; i < jobTypes; i++)
        if (*(uint32_t*)(kJobTypeEntries + i * 8) == *(uint32_t*)kVegetationJobTypes) firstVegetationJob = i;
    if (firstVegetationJob + 8 > jobTypes) {
        Log("Terrain 2: player 1's vegetation job types not found");
        return false;
    }
    uint8_t* state1 = g_vegetationState[0];
    g_vegetationManager[0] = *(uintptr_t*)kVegetationManager;
    SaveState(kVegetationState, state1);
    for (const Range& r : kVegetationContainers) memset((void*)r.addr, 0, r.size);
    *(unsigned*)kJobTypeCount = firstVegetationJob;

    // Vegetation definitions per climate zone (CVegetationSetCollection): loaded once at startup,
    // shared with Player 1 (the constructor clears the pointer).
    uintptr_t sets = *(uintptr_t*)kVegetationSets;
    void* memory = ((GameNew_t)kGameNew)(kVegetationManagerSize);
    if (!memory) return false;
    uintptr_t vegetation = ((Ctor_t)kVegetationCtor)(memory);
    *(uintptr_t*)kVegetationManager = vegetation;
    *(uintptr_t*)kVegetationSets = sets;
    // Its own breakable vegetation handler (+0xC), made like the game does (FUN_00812c50).
    void* handler = ((GameNew_t)kGameNew)(kTreeHandlerSize);
    if (!handler) return false;
    *(uintptr_t*)(vegetation + kVegetationTreeHandler) = ((Ctor_t)kTreeHandlerCtor)(handler);
    ((Void_t)kVegetationSetup)();

    unsigned written = *(unsigned*)kJobTypeCount - firstVegetationJob;
    *(unsigned*)kJobTypeCount = jobTypes;
    if (written != 8) Log("Terrain 2: vegetation constructor registered %u job types", written);
    for (uintptr_t a : kVegetationShared) *(uint32_t*)a = ReadState(kVegetationState, state1, a);

    SaveState(kVegetationState, g_vegetationState[1]);
    LoadState(kVegetationState, state1);
    g_vegetationManager[1] = vegetation;
    return true;
}

bool CreateGround2()
{
    g_patchManager[0] = *(uintptr_t*)kPatchManager;
    SaveState(kGroundState, g_groundState[0]);
    void* memory = ((GameNew_t)kGameNew)(kPatchManagerSize);
    if (!memory) return false;
    uintptr_t patches = ((Ctor_t)kPatchCtor)(memory);
    *(uintptr_t*)kPatchManager = patches;
    for (const Range& r : kPatchContainers) memset((void*)r.addr, 0, r.size);
    // His own water render block pool, all free (like the game's at startup).
    memset((void*)kWaterBlocks, 0, kWaterBlockCount * 0x30);
    for (int i = 0; i < kWaterBlockCount; i++) ((uint16_t*)kWaterFreeList)[i] = (uint16_t)i;
    *(uint16_t*)kWaterFreeTop = kWaterBlockCount - 1;
    ((This_t)kTerrainBlocksInit)(kTerrainBlocks);
    ((Arg2_t)kPatchInit)(*(uintptr_t*)(patches + kPatchPools), *(uintptr_t*)(patches + kPatchPools + 4));
    SaveState(kGroundState, g_groundState[1]);
    LoadState(kGroundState, g_groundState[0]);
    g_patchManager[1] = patches;
    return true;
}

typedef void(__fastcall* StreamPatchUpdate_t)(void* self, void* edx, float* position);
typedef uint32_t(__fastcall* StreamPatchLoaded_t)(uintptr_t self, void* edx, void* name, const void* data, uint32_t size);
StreamPatchUpdate_t g_origStreamPatchUpdate;
StreamPatchLoaded_t g_origStreamPatchLoaded;

bool CreateStream2()
{
    uint8_t* state1 = g_streamState[0];
    g_streamManager[0] = *(uintptr_t*)kStreamManager;
    SaveState(kStreamState, state1);
    uintptr_t displacement = *(uintptr_t*)kTerrainDisplacement;
    *(uintptr_t*)kTerrainDisplacement = 0;
    bool made = false;
    // Made like the landscape init does (FUN_00a74f80).
    void* texture = ((GameNew_t)kGameNew)(0xD40);
    void* map = ((GameNew_t)kGameNew)(0x80808);
    void* data = ((GameNew_t)kGameNew)(0x4A4);
    void* mapTexture = ((GameNew_t)kGameNew)(0x510);
    uint8_t* zones = (uint8_t*)((GameNew_t)kGameNew)(0x40308);
    void* manager = ((GameNew_t)kGameNew)(kStreamManagerSize);
    if (texture && map && data && mapTexture && zones && manager) {
        *(uintptr_t*)kTerrainTexture = ((Ctor_t)0x00A8F600)(texture);
        *(uintptr_t*)kTerrainMap = ((Ctor_t)0x00A74E40)(map);
        *(uintptr_t*)kTerrainData = ((Ctor_t)0x00A64140)(data);
        *(uintptr_t*)kMapTexture = ((Ctor_t)0x00A67BF0)(mapTexture);
        ((Ctor_t)0x00A581A0)(zones + 4);
        *(uint32_t*)(zones + 4) = 0x00F2007C;
        *(uint32_t*)zones = 0x00F20088;
        *(uintptr_t*)kZoneMap = (uintptr_t)zones;
        ((ThisArg_t)kStreamManagerCtor)((uintptr_t)manager, nullptr, *(uintptr_t*)kStreamReader);
        *(uintptr_t*)kStreamManager = (uintptr_t)manager;
        *(uint32_t*)kStreamFlag = 0;
        memset((void*)kStreamResidency, 0, kStreamResidencySize);
        ((This_t)kStreamGlobalLoad)((uintptr_t)manager);
        // The whole-map part of <base>.dat (CTerrainData +0x478..+0x49C: header, then the per-level
        // tables of the 5 coarsest levels, 14..10, that FUN_00A5EE40 reads) came out zeros and junk
        // in his copy, so his distant terrain (patch levels 6-10) was flat at sea level: no
        // mountains across the sea. It is static data, the same for both: his copy uses Player 1's.
        uintptr_t data1 = ReadState(kStreamState, state1, kTerrainData);
        uintptr_t data2 = *(uintptr_t*)kTerrainData;
        if (data1 && data2 && *(uint8_t*)(data1 + kTerrainDataGlobalLoaded)) {
            memcpy((void*)(data2 + kTerrainDataGlobal), (void*)(data1 + kTerrainDataGlobal), kTerrainDataGlobalSize);
            *(uintptr_t*)(data2 + kTerrainDataLevelTables) = *(uintptr_t*)(data1 + kTerrainDataLevelTables);
        }
        SaveState(kStreamState, g_streamState[1]);
        // His texture handlers go with his ground state; Player 1's are put back.
        WriteState(kGroundState, g_groundState[1], kTerrainTexture, *(uint32_t*)kTerrainTexture);
        WriteState(kGroundState, g_groundState[1], kMapTexture, *(uint32_t*)kMapTexture);
        *(uint32_t*)kTerrainTexture = ReadState(kGroundState, g_groundState[0], kTerrainTexture);
        *(uint32_t*)kMapTexture = ReadState(kGroundState, g_groundState[0], kMapTexture);
        g_streamManager[1] = (uintptr_t)manager;
        made = true;
    }
    // Player 2's displacement table (the same data) is left unused.
    *(uintptr_t*)kTerrainDisplacement = displacement;
    LoadState(kStreamState, state1);
    return made;
}

void SwitchStream(int view)
{
    if (view == g_activeStream) return;
    SaveState(kStreamState, g_streamState[g_activeStream]);
    LoadState(kStreamState, g_streamState[view]);
    g_activeStream = view;
}

bool OwnStreamActive()
{
    return g_streamManager[1] && g_active >= 0;
}

// Loaded stream patch files arrive on the stream queue's worker threads at any time. While each
// player has his own stream state they are kept and handed to the slot during its owner's turn
// at the hand-off (the texture handler writes into the terrain block table of the ground state).
struct PendingLoad {
    uintptr_t slot;
    int owner;
    char name[16];
    uint8_t* data;
    uint32_t size;
};
CRITICAL_SECTION g_pendingLock;
PendingLoad g_pending[256];
int g_pendingCount;
volatile LONG g_pendingDropped;

int SlotOwner(uintptr_t slot)
{
    uintptr_t p2 = g_streamManager[1];
    return p2 && slot >= p2 && slot < p2 + kStreamManagerSize ? 1 : 0;
}

// Called by the stream queue (FUN_004e0780) with the queue item (its name string first); AL
// nonzero takes the item off the queue, zero has it delivered again.
uint32_t __fastcall StreamPatchLoadedHook(uintptr_t self, void* edx, void* name, const void* data, uint32_t size)
{
    if (!OwnStreamActive() || !name || !data) {
        // Player 2's manager while his copies are off: the data would land in Player 1's handlers.
        if (SlotOwner(self) != 1) return g_origStreamPatchLoaded(self, edx, name, data, size);
        *(uint8_t*)(self + kPatchSlotLoading) = 0;
        return 1;
    }
    PendingLoad load = { self, SlotOwner(self) };
    uint32_t capacity = *(uint32_t*)((uintptr_t)name + 0x18);
    const char* text = capacity < 16 ? (const char*)name + 4 : *(const char**)((uintptr_t)name + 4);
    strncpy_s(load.name, text, _TRUNCATE);
    load.data = (uint8_t*)malloc(size ? size : 1);
    load.size = size;
    if (load.data) memcpy(load.data, data, size);
    bool kept = false;
    EnterCriticalSection(&g_pendingLock);
    if (load.data && g_pendingCount < (int)(sizeof(g_pending) / sizeof(g_pending[0]))) {
        g_pending[g_pendingCount++] = load;
        kept = true;
    }
    LeaveCriticalSection(&g_pendingLock);
    // The queue delivers the file again until the slot's loading flag is down (FUN_00a3f710 clears
    // it); the manager doesn't request the cell again before the owner's turn hands it over.
    if (kept) {
        *(uint8_t*)(self + kPatchSlotLoading) = 0;
        return 1;
    }
    if (InterlockedIncrement(&g_pendingDropped) <= 3)
        Log("Terrain 2: patch %s (slot %08X owner %d, size %u) not kept (%d waiting, copy %s)", load.name,
            (unsigned)self, load.owner, size, g_pendingCount, load.data ? "made" : "failed");
    free(load.data);
    // Player 1's is handed over now; Player 2's slot loads it again.
    if (load.owner == 0) return g_origStreamPatchLoaded(self, edx, name, data, size);
    *(uint8_t*)(self + kPatchSlotLoading) = 0;
    return 1;
}

// An MSVC 8 std::string as FUN_00a3f710 reads it: buffer at +4 (inline below 16), capacity +0x18.
struct GameString {
    uint32_t proxy;
    char text[16];
    uint32_t size;
    uint32_t capacity;
};

// Hands the owner's kept files to their slots (game thread, that player's state switched in).
// owner -1: drop Player 2's, hand over Player 1's (whatever state is in).
void ReplayLoads(int owner)
{
    PendingLoad mine[256];
    int n = 0;
    EnterCriticalSection(&g_pendingLock);
    int kept = 0;
    for (int i = 0; i < g_pendingCount; i++) {
        PendingLoad& p = g_pending[i];
        if (p.owner == owner || owner < 0) mine[n++] = p;
        else g_pending[kept++] = p;
    }
    g_pendingCount = kept;
    LeaveCriticalSection(&g_pendingLock);
    for (int i = 0; i < n; i++) {
        PendingLoad& p = mine[i];
        int x = -1, y = -1;
        sscanf_s(p.name, "%02d%02d", &x, &y);
        // Still the cell the slot asked for (a newer request may have replaced it meanwhile).
        bool wanted = *(uint8_t*)(p.slot + kPatchSlotLoading) == 0 &&
                      *(int*)(p.slot + kPatchSlotRequested) == x && *(int*)(p.slot + kPatchSlotRequested + 4) == y;
        if (wanted && (owner >= 0 || p.owner == 0)) {
            GameString name = {};
            strncpy_s(name.text, p.name, _TRUNCATE);
            name.size = (uint32_t)strlen(name.text);
            name.capacity = 15;
            g_origStreamPatchLoaded(p.slot, nullptr, &name, p.data, p.size);
        }
        free(p.data);
    }
}

const uintptr_t kAfterPlayerDeviceUpdate = 0x00782432; // return address in FUN_00781D60

// The world update moves Player 1's stream window; while the players have their own stream
// states that happens in his turn at the hand-off instead (with his ground state in).
void __fastcall StreamPatchUpdateHook(void* self, void* edx, float* position)
{
    // The frame's object update (FUN_00781D60) calls this right after Player 1's grappling
    // device update (FUN_0068BAF0, manager's player only): Player 2's runs here, at the same point.
    if (_ReturnAddress() == (void*)kAfterPlayerDeviceUpdate) Player2DeviceUpdate();
    float p2[3];
    if (OwnStreamActive() && (uintptr_t)self == g_streamManager[0]) {
        // Player 1's camera even when the main camera holds Player 2's world ("split p2world").
        if (!SplitRenderPlayer1Position(g_player1StreamPosition))
            memcpy(g_player1StreamPosition, position, sizeof(g_player1StreamPosition));
        g_havePlayer1StreamPosition = true;
        return;
    }
    if (g_streamAtPlayer2 && Player2Position(p2)) position = p2;
    g_origStreamPatchUpdate(self, edx, position);
}

// --- Collision near Player 2 ------------------------------------------------------------------

// Physics and game logic read heights from Player 1's CTerrainMapGame at any time (the custom
// heightfield shape, FUN_00991900, and ray casts, FUN_00a64a90, both through FUN_00a56400 on the
// slot of cell & 7, whatever cell it holds). So outside Player 2's turn the slots of the cells
// around Player 2 point at Player 2's height and material buffers, unless Player 1 needs that slot
// himself (his own 3x3 cells) or it already holds the cell. Player 1's turn (the only time his
// stream patches load and unload) sees his own buffers.
const uintptr_t kMapSlots = 4;                   // CTerrainMapStreamPatchGame[64], 0x20 each
const uintptr_t kMapSlotCell = 4;                // cell x, y
// +0x18 heights (0x8820 bytes), +0x1C materials (0x4840 bytes).
const uintptr_t kStreamCellOrigin = 0x01089EE8;  // FUN_00a49320: cell = (p + [0x00F0EA50] - origin) * [0x00F0EAC4]
const uintptr_t kStreamCellOffset = 0x00F0EA50;
const uintptr_t kStreamCellScale = 0x00F0EAC4;

// The whole slot record after its vtable is swapped: ray casts (FUN_00A64A90) also check the
// slot's loaded flag (+0x14) and that it holds the ray's cell (+4/+8); with only the buffers
// swapped they found no ground under Player 2 and he went into a skydive on every jump.
const size_t kMapSlotRecord = 0x1C;
struct MapRedirect {
    uintptr_t slot;
    uint8_t saved[kMapSlotRecord];
};
// Player 2's cell and the two rings around it (at least 1 km each way): his roads (gathered 800 m
// around his camera) are draped over these heights when the world update builds them, and with
// only the 2x2 nearest cells a road went up into a wall where it reached a cell whose slot held
// one of Player 1's cells.
const int kRedirectRadius = 2;
MapRedirect g_mapRedirects[(2 * kRedirectRadius + 1) * (2 * kRedirectRadius + 1)];
int g_mapRedirectCount;
volatile bool g_player2Collision = true;   // "terrain2 nocollision" turns it off

int StreamCell(float p)
{
    float c = (p + *(float*)kStreamCellOffset - *(float*)kStreamCellOrigin) * *(float*)kStreamCellScale;
    return (int)floorf(c);
}

// The cell whose height map slot FUN_00a5f600 reads at p: the stream cell rounds to the nearest
// (+256 m, it centres the stream window), this doesn't. With StreamCell the west/south half of
// each cell was checked against the wrong slot (P2 stood on Player 1's hill of the slot next door).
const uintptr_t kHeightCellOrigin = 0x0108AB34;
const uintptr_t kHeightCellScale = 0x00F0F854;
int HeightCell(float p)
{
    return (int)floorf((p - *(float*)kHeightCellOrigin) * *(float*)kHeightCellScale);
}

// The physics ground (CCustomSampledHeightFieldShape, landscape manager +0x1485C) samples heights in
// its slot 9 (FUN_00991900) through FUN_00a5f600 on [kTerrainMap], whose slot of cell & 7 may hold
// another cell (an invisible wall where Player 2's cell shares a slot with one of Player 1's, or
// isn't redirected yet). On a private copy of its vtable each point is sampled on the map whose slot
// holds that point's cell: the one in, else either player's.
const uintptr_t kHeightFieldShape = 0x1485C;           // CLandscapeManager: FUN_00a5e730 on its +4 part's +0x14858
const uintptr_t kHeightFieldShapeVtable = 0x00F27970;
const int kHeightFieldShapeSlots = 19;                  // the next vtable starts at 0x00F279BC
const int kHeightFieldSampleSlot = 9;
const uintptr_t kSampleTerrainMap = 0x00A5F600;         // __thiscall(map, float point[4], float out[12]): height, normal, ...
const uintptr_t kMapSlotHeights = 0x18;
const uintptr_t kMapSlotLoaded = 0x14;
const float kNoGround = -10000.0f;                      // where no map has the point's cell in
typedef void(__thiscall* SampleTerrainMap_t)(uintptr_t map, const float* point, float* out);
volatile uintptr_t g_collisionMaps[2];                  // Player 1's and Player 2's CTerrainMapGame while both stream
void* g_heightFieldVtable[kHeightFieldShapeSlots];
uintptr_t g_heightFieldShape;
// Diagnostics for the hand-off log line: points sampled on the map in, on the other player's, on
// neither; and the last point's cell with what the map in holds in its slot.
volatile long g_heightFieldSamples[3];
int g_lastSampleCell[4];

bool MapHoldsCell(uintptr_t map, int x, int y)
{
    if (!map) return false;
    uintptr_t slot = map + kMapSlots + ((x & 7) + (y & 7) * 8) * 0x20;
    // A slot takes the cell when it's requested; until the patch is in (+0x14, which ray casts check
    // too) its buffers still hold the cell it had before (P2 stood on a 537 m hill over the sea).
    return *(int*)(slot + kMapSlotCell) == x && *(int*)(slot + kMapSlotCell + 4) == y && *(uint8_t*)(slot + kMapSlotLoaded) &&
           *(uintptr_t*)(slot + kMapSlotHeights);
}

// in: { float (*points)[4], int count }, out: per point normal and depth below the ground.
void __fastcall HeightFieldSampleHook(void* self, void* edx, const uintptr_t* in, float* out)
{
    const float* p = (const float*)in[0];
    for (int i = 0; i < (int)in[1]; i++, p += 4, out += 4) {
        uintptr_t map = *(uintptr_t*)kTerrainMap;
        int x = HeightCell(p[0]), y = HeightCell(p[2]);
        int found = 0;
        if (!MapHoldsCell(map, x, y)) {
            found = 2;
            for (int m = 0; m < 2; m++) {
                uintptr_t other = g_collisionMaps[m];
                if (other != map && MapHoldsCell(other, x, y)) { map = other; found = 1; break; }
            }
        }
        g_heightFieldSamples[found]++;
        // While both players stream, a cell neither has in has no ground (alone, as the game does).
        if (found == 2 && g_collisionMaps[0]) {
            out[0] = 0.0f;
            out[1] = 1.0f;
            out[2] = 0.0f;
            out[3] = (p[1] - kNoGround) - p[3];
            continue;
        }
        if (map) {
            uintptr_t slot = map + kMapSlots + ((x & 7) + (y & 7) * 8) * 0x20;
            int last[4] = { x, y, *(int*)(slot + kMapSlotCell), *(int*)(slot + kMapSlotCell + 4) };
            memcpy(g_lastSampleCell, last, sizeof(last));
        }
        // As FUN_00991900 has them: 16-byte aligned, and the sample fills 0x30 bytes.
        __declspec(align(16)) float point[4] = { p[0], 0.0f, p[2], p[3] };
        __declspec(align(16)) float sample[12];
        ((SampleTerrainMap_t)kSampleTerrainMap)(map, point, sample);
        out[0] = sample[1];
        out[1] = sample[2];
        out[2] = sample[3];
        out[3] = (p[1] - sample[0]) - p[3];
    }
}

void KeepHeightFieldHooked()
{
    uintptr_t landscape = *(uintptr_t*)kLandscape;
    uintptr_t shape = landscape ? *(uintptr_t*)(landscape + kHeightFieldShape) : 0;
    if (shape == g_heightFieldShape && shape && *(void***)shape == g_heightFieldVtable) return;
    if (!shape || *(uintptr_t*)shape != kHeightFieldShapeVtable) {
        static bool logged;
        if (!logged) Log("Terrain 2: landscape %08X +0x1485C (%08X) is not the height field shape (vtable %08X)",
                         (unsigned)landscape, (unsigned)shape, shape ? (unsigned)*(uintptr_t*)shape : 0);
        logged = true;
        return;
    }
    memcpy(g_heightFieldVtable, (void*)kHeightFieldShapeVtable, sizeof(g_heightFieldVtable));
    g_heightFieldVtable[kHeightFieldSampleSlot] = (void*)&HeightFieldSampleHook;
    *(void***)shape = g_heightFieldVtable;
    g_heightFieldShape = shape;
    Log("Terrain 2: height field shape %08X samples per cell", (unsigned)shape);
}

void RestorePlayer2Collision()
{
    for (int i = 0; i < g_mapRedirectCount; i++)
        memcpy((void*)(g_mapRedirects[i].slot + 4), g_mapRedirects[i].saved, kMapSlotRecord);
    g_mapRedirectCount = 0;
}

// Player 1's stream state is in.
void ApplyPlayer2Collision()
{
    RestorePlayer2Collision();
    g_collisionMaps[0] = g_collisionMaps[1] = 0;
    float p2[3];
    if (!g_player2Collision || !OwnStreamActive() || !g_havePlayer1StreamPosition || !Player2Position(p2)) return;
    uintptr_t map1 = *(uintptr_t*)kTerrainMap;
    uintptr_t map2 = ReadState(kStreamState, g_streamState[1], kTerrainMap);
    if (!map1 || !map2 || map1 == map2) return;
    KeepHeightFieldHooked();
    g_collisionMaps[0] = map1;
    g_collisionMaps[1] = map2;
    int p1x = StreamCell(g_player1StreamPosition[0]), p1y = StreamCell(g_player1StreamPosition[2]);
    int cx = StreamCell(p2[0]), cy = StreamCell(p2[2]);
    for (int i = 0; i < (int)_countof(g_mapRedirects); i++) {
        int c[2] = { cx + i % (2 * kRedirectRadius + 1) - kRedirectRadius,
                     cy + i / (2 * kRedirectRadius + 1) - kRedirectRadius };
        int slotIndex = (c[0] & 7) + (c[1] & 7) * 8;
        bool player1Needs = false;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
                if ((((p1x + dx) & 7) + ((p1y + dy) & 7) * 8) == slotIndex) player1Needs = true;
        uintptr_t slot1 = map1 + kMapSlots + slotIndex * 0x20;
        uintptr_t slot2 = map2 + kMapSlots + slotIndex * 0x20;
        if (player1Needs) continue;
        if (*(int*)(slot1 + kMapSlotCell) == c[0] && *(int*)(slot1 + kMapSlotCell + 4) == c[1]) continue;
        if (!MapHoldsCell(map2, c[0], c[1])) continue;
        MapRedirect& r = g_mapRedirects[g_mapRedirectCount++];
        r.slot = slot1;
        memcpy(r.saved, (void*)(slot1 + 4), kMapSlotRecord);
        memcpy((void*)(slot1 + 4), (void*)(slot2 + 4), kMapSlotRecord);
    }
}

// Vegetation and ground are switched separately: the frame being drawn needs its player's ground
// state (the render job reads the terrain block table and patch cells), but of the vegetation
// globals it reads only the manager's flags (the same in both copies), so between hand-offs the
// vegetation is always Player 1's. Game code in the world update that adds or removes vegetation
// (locations' placed vegetation, characters bending grass) then always reaches Player 1's copy.
// Climate zones: CClimateZone ([0x011907D0], FUN_00a69370 in the landscape update) picks the zone
// around the camera and has its CVegetationZoneCallback (zone +8) load that zone's vegetation sets
// into the set collection (8 slots, one per zone) and release the previous zone's. With one zone
// object for both players in different zones, it released and loaded the two zones every frame
// (Player 1's plants changed whenever Player 2's camera was in another zone, e.g. turning near a
// border). The zone selection is per player (the globals in kVegetationState and these fields of
// the zone object), and a zone the other player is in isn't released.
const uintptr_t kClimateZone = 0x011907D0;
const uintptr_t kZoneFields[] = { 0x78, 0xB4, 0xB8, 0xBC, 0xC0 };
const uintptr_t kCurrentVegetationZone = 0x012B447C;
const uintptr_t kZoneCallbackLoadSlot = 0x00E4B41C;    // __thiscall(callback, zone)
const uintptr_t kZoneCallbackReleaseSlot = 0x00E4B420; // __thiscall(callback, zone)
const uintptr_t kZoneCallbackStates = 0x364;           // per zone: 0 none, 1 loading, 2 in
const int kZoneSlots = 8;
uint32_t g_zoneFields[2][_countof(kZoneFields)];
bool g_haveZoneFields[2];
typedef void(__fastcall* ZoneCallback_t)(uintptr_t callback, void* edx, int zone);
ZoneCallback_t g_origZoneLoad, g_origZoneRelease;

volatile bool g_testWholeZone; // testing ("terrain2 wholezone on|off"): all of the zone object per player
uint8_t g_zoneObject[2][0xDC];
bool g_haveZoneObject[2];

void SwitchZone(int from, int to)
{
    uintptr_t zone = *(uintptr_t*)kClimateZone;
    if (!zone) return;
    if (g_testWholeZone) {
        memcpy(g_zoneObject[from], (void*)(zone + 4), sizeof(g_zoneObject[from]) - 4);
        g_haveZoneObject[from] = true;
        if (g_haveZoneObject[to]) memcpy((void*)(zone + 4), g_zoneObject[to], sizeof(g_zoneObject[to]) - 4);
        return;
    }
    g_haveZoneObject[0] = g_haveZoneObject[1] = false;
    for (size_t i = 0; i < _countof(kZoneFields); i++) g_zoneFields[from][i] = *(uint32_t*)(zone + kZoneFields[i]);
    g_haveZoneFields[from] = true;
    if (g_haveZoneFields[to])
        for (size_t i = 0; i < _countof(kZoneFields); i++) *(uint32_t*)(zone + kZoneFields[i]) = g_zoneFields[to][i];
}

// The other player's current vegetation zone, -1 when there is no other player's state.
int OtherPlayersZone()
{
    if (!g_vegetationManager[1]) return -1;
    int other = 1 - g_activeVegetation;
    return (int)ReadState(kVegetationState, g_vegetationState[other], kCurrentVegetationZone);
}

void __fastcall ZoneLoadHook(uintptr_t callback, void* edx, int zone)
{
    // Already in (or loading) for the other player: loading again would add its sets twice.
    if (zone >= 0 && zone < kZoneSlots && *(int*)(callback + kZoneCallbackStates + zone * 4) != 0) return;
    g_origZoneLoad(callback, edx, zone);
}

void __fastcall ZoneReleaseHook(uintptr_t callback, void* edx, int zone)
{
    if (zone == OtherPlayersZone()) return;
    g_origZoneRelease(callback, edx, zone);
}

volatile uint32_t g_testVegetationKeepMask; // testing ("terrain2 vegmask <hex>"): kVegetationState ranges P2's turn takes from P1
volatile uintptr_t g_testVegetationField, g_testVegetationFieldLength; // testing ("terrain2 vegfield"), see SwitchVegetation

size_t VegetationRangeOffset(int i)
{
    size_t offset = 0;
    for (int j = 0; j < i; j++) offset += kVegetationState[j].size;
    return offset;
}

void SwitchVegetation(int view)
{
    if (view == g_activeVegetation) return;
    static std::vector<uint8_t> ownRanges;
    static uint32_t maskApplied;
    SaveState(kVegetationState, g_vegetationState[g_activeVegetation]);
    if (maskApplied && g_activeVegetation == 1) {
        for (int i = 0; i < (int)_countof(kVegetationState); i++) {
            if (!(maskApplied & (1u << i))) continue;
            size_t o = VegetationRangeOffset(i);
            memcpy(g_vegetationState[0] + o, g_vegetationState[1] + o, kVegetationState[i].size);
            memcpy(g_vegetationState[1] + o, ownRanges.data() + o, kVegetationState[i].size);
        }
        maskApplied = 0;
    }
    LoadState(kVegetationState, g_vegetationState[view]);
    if (view == 1 && g_testVegetationKeepMask) {
        ownRanges.assign(g_vegetationState[1], g_vegetationState[1] + VegetationRangeOffset(_countof(kVegetationState)));
        for (int i = 0; i < (int)_countof(kVegetationState); i++) {
            if (!(g_testVegetationKeepMask & (1u << i))) continue;
            memcpy((void*)kVegetationState[i].addr, g_vegetationState[0] + VegetationRangeOffset(i), kVegetationState[i].size);
        }
        maskApplied = g_testVegetationKeepMask;
    }
    // Testing ("terrain2 vegfield <hex offset> [hex length]", 0 off): during P2's turn those
    // bytes of his CVegetationManager hold Player 1's.
    static uint8_t fieldOwn[kVegetationManagerSize];
    static uintptr_t fieldSwapped, fieldSwappedLength;
    if (fieldSwappedLength && g_vegetationManager[1]) {
        memcpy((void*)(g_vegetationManager[1] + fieldSwapped), fieldOwn, fieldSwappedLength);
        fieldSwappedLength = 0;
    }
    uintptr_t field = g_testVegetationField, length = g_testVegetationFieldLength;
    if (view == 1 && field && length && field + length <= kVegetationManagerSize && g_vegetationManager[0] && g_vegetationManager[1]) {
        memcpy(fieldOwn, (void*)(g_vegetationManager[1] + field), length);
        memcpy((void*)(g_vegetationManager[1] + field), (void*)(g_vegetationManager[0] + field), length);
        fieldSwapped = field;
        fieldSwappedLength = length;
    }
    SwitchZone(g_activeVegetation, view);
    g_activeVegetation = view;
}

volatile bool g_landscapeStream2 = true; // testing ("terrain2 landstream1"): P2's landscape turn keeps P1's stream state
// P2's frames use P1's texture handlers: bit 0 CTerrainTexture, bit 1 CMapTexture ("terrain2
// sharedtex" both, "sharedterraintex" / "sharedmaptex" one, "ownsharedtex" none).
volatile uint32_t g_sharedGroundTextures;

volatile uint32_t g_testGroundKeepMask; // testing ("terrain2 groundmask <hex>"): kGroundState ranges P2's turn takes from P1

// Byte offset of kGroundState range i in a saved state.
size_t GroundRangeOffset(int i)
{
    size_t offset = 0;
    for (int j = 0; j < i; j++) offset += kGroundState[j].size;
    return offset;
}

void SwitchGround(int view)
{
    if (view == g_active) return;
    static uint32_t own[2];
    static bool replaced;
    static uint32_t maskApplied;
    SaveState(kGroundState, g_groundState[g_active]);
    if (replaced && g_active == 1) {
        WriteState(kGroundState, g_groundState[1], kTerrainTexture, own[0]);
        WriteState(kGroundState, g_groundState[1], kMapTexture, own[1]);
        replaced = false;
    }
    static std::vector<uint8_t> ownRanges;
    if (maskApplied && g_active == 1) {
        // Player 1's bytes were in for the masked ranges: put Player 2's back into his state, and
        // what Player 2's turn did to Player 1's into Player 1's.
        for (int i = 0; i < (int)_countof(kGroundState); i++) {
            if (!(maskApplied & (1u << i))) continue;
            size_t o = GroundRangeOffset(i);
            memcpy(g_groundState[0] + o, g_groundState[1] + o, kGroundState[i].size);
            memcpy(g_groundState[1] + o, ownRanges.data() + o, kGroundState[i].size);
        }
        maskApplied = 0;
    }
    LoadState(kGroundState, g_groundState[view]);
    if (view == 1 && g_testGroundKeepMask) {
        ownRanges.assign(g_groundState[1], g_groundState[1] + GroundRangeOffset(_countof(kGroundState)));
        for (int i = 0; i < (int)_countof(kGroundState); i++) {
            if (!(g_testGroundKeepMask & (1u << i))) continue;
            memcpy((void*)kGroundState[i].addr, g_groundState[0] + GroundRangeOffset(i), kGroundState[i].size);
        }
        maskApplied = g_testGroundKeepMask;
    }
    if (view == 1 && g_sharedGroundTextures) {
        own[0] = *(uint32_t*)kTerrainTexture;
        own[1] = *(uint32_t*)kMapTexture;
        if (g_sharedGroundTextures & 1)
            *(uint32_t*)kTerrainTexture = ReadState(kGroundState, g_groundState[0], kTerrainTexture);
        if (g_sharedGroundTextures & 2)
            *(uint32_t*)kMapTexture = ReadState(kGroundState, g_groundState[0], kMapTexture);
        replaced = true;
    }
    g_active = view;
}

volatile bool g_testKeepVegetation1, g_testKeepGround1, g_testKeepCamera1; // testing ("terrain2 keep veg|ground|camera|none")
volatile bool g_testVegetationStays; // testing ("terrain2 vegstay on|off"): P2's vegetation state stays in until the next hand-off
// Testing ("terrain2 vegregen <mask>", bit 0 P1's turns, bit 1 P2's): the vegetation update
// (FUN_00A92CA0) starts a level's generation job only when its camera's cell differs from the cell
// last requested (0x0108BCCC.., 4 levels x {x, z}; -0x40 = none): forgetting them regenerates
// all levels on every turn.
volatile uint32_t g_testVegetationRegen;
const uintptr_t kVegetationRequestedCells = 0x0108BCCC; // 8 ints

void Switch(int view)
{
    SwitchVegetation(g_testKeepVegetation1 ? 0 : view);
    SwitchGround(g_testKeepGround1 ? 0 : view);
}

// Patch cells: 11 levels x 64 ring slots of 0x34 bytes from 0x012AB578 (array index 0 the finest,
// 32 m; 10 the coarsest); +3 bit 2 built, +0xC/+0x10 its cell. The patch update (FUN_00A67610)
// builds a cell only when the cell its slot should hold changes (per level the last cell at
// CPatchManager +0x6D0 - 8 * (10 - level)), so the coarse cells, whose cell practically never changes,
// keep whatever heights they got when built.
const uintptr_t kPatchCells = 0x012AB578;
const size_t kPatchCellSize = 0x34;
const int kPatchLevels = 11;
const uintptr_t kPatchLastCells = 0x6D0;
volatile uint32_t g_rebuildPlayer2Cells; // levels (bits) Player 2's next turn rebuilds ("terrain2 rebuild2 [hex]")

// Makes the patch update build the cells of these levels again (current ground state).
void ForgetPatchCells(uint32_t levels)
{
    uintptr_t patches = *(uintptr_t*)kPatchManager;
    for (int level = 0; level < kPatchLevels; level++) {
        if (!(levels & (1u << level))) continue;
        for (int slot = 0; slot < 64; slot++) {
            uint8_t* cell = (uint8_t*)(kPatchCells + (level * 64 + slot) * kPatchCellSize);
            *(int*)(cell + 0xC) = 0x7FFFFFFF; // no cell: the update unregisters it and builds the wanted one
            *(int*)(cell + 0x10) = 0x7FFFFFFF;
        }
        int* last = (int*)(patches + kPatchLastCells - 8 * (kPatchLevels - 1 - level));
        last[0] = (int)0x80000000;
        last[1] = (int)0x80000000;
    }
}

// Placed vegetation (user: Player 1's plants changed the plants around Player 2). Locations place
// their own plants (the palms and lawn around Karl's hideout): a CModelCollection's load
// (FUN_008A40F0) adds each with FUN_00A93450 -> FUN_00A91480 into the instance vector 0x013820F0
// (16-byte entries: position, +0xC flag, +0xD type, +0xE in use, +0xF climate zone) and the 19x19
// cell lists 0x0138AAB0, and marks the cell for a rebuild; its unload (FUN_00909AF0) removes them
// with FUN_00A935C0. That happens in the world update, with Player 1's vegetation state in, so
// Player 2's copy had none of them. On each of Player 2's turns his copy is brought in line with
// Player 1's entries: new or changed ones added the same way, gone ones removed.
const uintptr_t kVegetationInstances = 0x013820F0; // vector: begin, end, capacity
const size_t kVegetationInstanceSize = 0x10;
const uintptr_t kVegetationCellOrigin = 0x0108B948, kVegetationCellScale = 0x00F209E4; // floats
typedef int(__stdcall* VegetationAdd_t)(short* cell, uint8_t type, void* unused, const void* position, uint32_t flag,
                                       uint8_t zone, uint16_t* index); // FUN_00A91480, ret 0x1C
typedef void(__stdcall* VegetationRemove_t)(int index);               // FUN_00A935C0, ret 4 (a short)
const VegetationAdd_t VegetationAdd = (VegetationAdd_t)0x00A91480;
const VegetationRemove_t VegetationRemove = (VegetationRemove_t)0x00A935C0;
struct PlacedMirror { uint8_t entry[kVegetationInstanceSize]; uint16_t index; bool used; };
std::vector<PlacedMirror> g_placedMirror; // by Player 1's index
uintptr_t g_placedMirrorManager;          // Player 2's manager the mirror belongs to
volatile bool g_mirrorPlacedVegetation = true; // "terrain2 placed on|off" (testing)
long g_placedAdded, g_placedRemoved;           // testing

// Player 2's vegetation state is in; Player 1's saved.
void MirrorPlacedVegetation()
{
    if (g_placedMirrorManager != g_vegetationManager[1]) {
        g_placedMirror.clear(); // a new copy has none
        g_placedMirrorManager = g_vegetationManager[1];
    }
    bool on = g_mirrorPlacedVegetation;
    uint8_t* begin = on ? (uint8_t*)ReadState(kVegetationState, g_vegetationState[0], kVegetationInstances) : nullptr;
    uint8_t* end = on ? (uint8_t*)ReadState(kVegetationState, g_vegetationState[0], kVegetationInstances + 4) : nullptr;
    size_t count = begin && end > begin ? (end - begin) / kVegetationInstanceSize : 0;
    __try {
        for (size_t i = count; i < g_placedMirror.size(); i++)
            if (g_placedMirror[i].used) {
                VegetationRemove(g_placedMirror[i].index);
                g_placedRemoved++;
            }
        if (g_placedMirror.size() > count) g_placedMirror.resize(count);
        else if (g_placedMirror.size() < count) g_placedMirror.resize(count, PlacedMirror{});
        float origin = *(float*)kVegetationCellOrigin, scale = *(float*)kVegetationCellScale;
        for (size_t i = 0; i < count; i++) {
            const uint8_t* e = begin + i * kVegetationInstanceSize;
            PlacedMirror& m = g_placedMirror[i];
            bool inUse = e[0xE] != 0;
            if (m.used && (!inUse || memcmp(m.entry, e, kVegetationInstanceSize) != 0)) {
                VegetationRemove(m.index);
                m.used = false;
                g_placedRemoved++;
            }
            if (!inUse || m.used) continue;
            const float* p = (const float*)e;
            short cell[2] = { (short)floorf((p[0] - origin) * scale), (short)floorf((p[2] - origin) * scale) };
            uint32_t unused = 0xFFFFFFFF;
            uint16_t index = 0xFFFF;
            VegetationAdd(cell, e[0xD], &unused, e, e[0xC], e[0xF], &index);
            memcpy(m.entry, e, kVegetationInstanceSize);
            m.index = index;
            m.used = true;
            g_placedAdded++;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static bool logged;
        if (!logged) Log("Terrain 2: fault mirroring placed vegetation");
        logged = true;
        g_mirrorPlacedVegetation = false;
    }
}

// The landscape of the view about to be drawn, with its camera (render idle, game thread).
void RunLandscape(int view)
{
    uintptr_t landscape = *(uintptr_t*)kLandscape;
    uintptr_t cameras = *(uintptr_t*)kCameraManager;
    Switch(view);
    if (view == 1 && g_activeVegetation == 1 && g_vegetationManager[1]) MirrorPlacedVegetation();
    if (g_testVegetationRegen >> view & 1)
        for (int i = 0; i < 8; i++) ((int*)kVegetationRequestedCells)[i] = -0x40;
    uintptr_t logic = *(uintptr_t*)(cameras + kCameraManagerLogic);
    if (view == 1 && !g_testKeepCamera1) {
        // A copy: the camera controller writes the world matrix through this pointer.
        memcpy(g_landCamera, g_camera2, sizeof(g_landCamera));
        *(uintptr_t*)(cameras + kCameraManagerLogic) = (uintptr_t)g_landCamera;
    }
    *(uint8_t*)(landscape + kLandscapeEnabled) = 1;
    if (view == 1 && g_rebuildPlayer2Cells) {
        ForgetPatchCells(g_rebuildPlayer2Cells);
        g_rebuildPlayer2Cells = 0;
    }
    RestorePlayer2Collision();
    if (OwnStreamActive()) {
        SwitchStream(g_landscapeStream2 ? view : 0);
        // Loaded files first: until then their slots look idle and the update would ask again.
        ReplayLoads(view);
        float p2[3];
        if (view == 1 && Player2Position(p2)) g_origStreamPatchUpdate((void*)g_streamManager[1], nullptr, p2);
        if (view == 0 && g_havePlayer1StreamPosition)
            g_origStreamPatchUpdate((void*)g_streamManager[0], nullptr, g_player1StreamPosition);
    }
    // Testing ("terrain2 resident"): every stream cell counts as loaded during P2's turn.
    static uint8_t residency[kStreamResidencySize];
    bool spoof = view == 1 && g_spoofResidency && !OwnStreamActive();
    if (spoof) {
        memcpy(residency, (void*)kStreamResidency, sizeof(residency));
        memset((void*)kStreamResidency, 0xFF, sizeof(residency));
    }
    if (view == 0 || g_fullUpdate) {
        ((This_t)kLandscapeUpdate)(landscape);
    } else {
        // Only the parts of FUN_00a751b0 that have a copy per player ("terrain2 partial"); water
        // FUN_00a88d40, climate zone FUN_00a69370 (the zone's textures and vegetation sets) and
        // sky FUN_00a66c30 stay Player 1's.
        WaitLandscapeJobs();
        uintptr_t camera = *(uintptr_t*)(cameras + kCameraManagerLogic);
        if (uintptr_t regions = *(uintptr_t*)kTerrainRegions) ((ThisArg_t)kTerrainRegionsUpdate)(regions, nullptr, camera);
        ((ThisArg_t)kPatchUpdate)(*(uintptr_t*)kPatchManager, nullptr, camera);
        ((ThisArg_t)kVegetationUpdate)(*(uintptr_t*)kVegetationManager, nullptr, camera);
        ((ThisArg_t)kPatchVisibility)(*(uintptr_t*)kPatchManager, nullptr, camera);
    }
    ((This_t)kLandscapeDrawLists)(landscape);
    WaitLandscapeJobs();
    {
        static int lastZone[2] = { -2, -2 };
        int zone = *(int*)kCurrentVegetationZone;
        if (zone != lastZone[view]) {
            const float* at = (const float*)(*(uintptr_t*)(cameras + kCameraManagerLogic) + 0x54);
            Log("Terrain 2: P%d's vegetation zone %d -> %d (camera %.0f %.0f, other's %d)", view + 1, lastZone[view], zone,
                at[0], at[2], OtherPlayersZone());
            lastZone[view] = zone;
        }
    }
    if (spoof) memcpy((void*)kStreamResidency, residency, sizeof(residency));
    *(uint8_t*)(landscape + kLandscapeEnabled) = 0;
    *(uintptr_t*)(cameras + kCameraManagerLogic) = logic;
    if (!g_testVegetationStays) SwitchVegetation(0);
    // Game logic between hand-offs (height queries, physics) reads Player 1's (keeping Player 2's
    // in until the next turn kills Player 1 at once).
    SwitchStream(0);
    ApplyPlayer2Collision();
}

// Back to Player 1's landscape, run by the game in the world update again.
void Stop()
{
    WaitLandscapeJobs();
    RestorePlayer2Collision();
    Switch(0);
    SwitchStream(0);
    *(uint8_t*)(*(uintptr_t*)kLandscape + kLandscapeEnabled) = 1;
    // Player 2's zone's vegetation sets go unless Player 1 is in it, and his zone selection starts
    // again from Player 1's (when he comes back his zone is picked and loaded afresh).
    uintptr_t zone = *(uintptr_t*)kClimateZone;
    if (g_vegetationManager[1] && zone) {
        int zone2 = (int)ReadState(kVegetationState, g_vegetationState[1], kCurrentVegetationZone);
        uint32_t zone1 = *(uint32_t*)kCurrentVegetationZone;
        if (zone2 >= 0 && (uint32_t)zone2 != zone1 && *(uintptr_t*)(zone + 8))
            g_origZoneRelease(*(uintptr_t*)(zone + 8), nullptr, zone2);
        WriteState(kVegetationState, g_vegetationState[1], kCurrentVegetationZone, zone1);
        g_haveZoneFields[1] = false;
    }
}

// Logs the first few access violations in the game's code or ours (any thread).
volatile LONG g_faultsLogged;

bool InOurCode(uintptr_t eip)
{
    HMODULE ours = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&InOurCode, &ours);
    HMODULE module = nullptr;
    return GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              (LPCSTR)eip, &module) && module == ours;
}

LONG CALLBACK LogFault(EXCEPTION_POINTERS* info)
{
    uintptr_t eip = info->ContextRecord->Eip;
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
        !((eip >= 0x00401000 && eip < 0x00D92000) || InOurCode(eip)) || InterlockedIncrement(&g_faultsLogged) > 8)
        return EXCEPTION_CONTINUE_SEARCH;
    const CONTEXT* c = info->ContextRecord;
    Log("Terrain 2: fault at %08X (%s %08X) thread %lu eax %08X ecx %08X edx %08X esi %08X edi %08X", c->Eip,
        info->ExceptionRecord->ExceptionInformation[0] ? "write" : "read",
        (unsigned)info->ExceptionRecord->ExceptionInformation[1], GetCurrentThreadId(), c->Eax, c->Ecx, c->Edx,
        c->Esi, c->Edi);
    char line[256] = "";
    int len = 0;
    DWORD ebp = c->Ebp;
    for (int i = 0; i < 12 && ebp; i++) {
        DWORD frame[2] = {};
        SIZE_T got = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(uintptr_t)ebp, frame, sizeof(frame), &got)) break;
        len += sprintf_s(line + len, sizeof(line) - len, " %08X", frame[1]);
        if (frame[0] <= ebp) break;
        ebp = frame[0];
    }
    HMODULE module = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)(uintptr_t)c->Eip, &module))
        GetModuleFileNameA(module, name, sizeof(name));
    Log("Terrain 2: frames%s, eip in %s +%X", line, name, (unsigned)(c->Eip - (uintptr_t)module));
    // Vtables of the objects the registers point at (class names: tools/rtti_map.json).
    DWORD regs[] = { c->Eax, c->Ecx, c->Edx, c->Ebx, c->Esi, c->Edi };
    len = 0;
    line[0] = 0;
    for (DWORD r : regs) {
        DWORD vt = 0;
        SIZE_T got = 0; // a read that can't fault (a fault here would re-enter this handler)
        ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(uintptr_t)r, &vt, sizeof(vt), &got);
        len += sprintf_s(line + len, sizeof(line) - len, " %08X", vt);
    }
    Log("Terrain 2: [eax ecx edx ebx esi edi]%s", line);
    // Frame pointers are often missing in the game's code: also every game-code address on the stack.
    len = 0;
    line[0] = 0;
    DWORD stack[400] = {};
    SIZE_T got = 0;
    ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(uintptr_t)c->Esp, stack, sizeof(stack), &got);
    for (size_t i = 0, n = 0; i < got / 4 && n < 20; i++) {
        if (stack[i] >= 0x00401000 && stack[i] < 0x00D92000) {
            len += sprintf_s(line + len, sizeof(line) - len, " %08X", stack[i]);
            n++;
        }
    }
    Log("Terrain 2: stack%s", line);
    return EXCEPTION_CONTINUE_SEARCH;
}

// Testing: the game's container checks (_invalid_parameter_noinfo in MSVCR80) end the process;
// this logs where they fail first.
volatile LONG g_invalidLogged;

void __cdecl LogInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t)
{
    if (InterlockedIncrement(&g_invalidLogged) > 3) return;
    char line[256] = "";
    int n = 0, len = 0;
    const DWORD* stack = (const DWORD*)&n; // a local: the stack from here up
    __try {
        for (const DWORD* p = stack; n < 14 && p < stack + 600; p++) {
            if (*p >= 0x00401000 && *p < 0x00D92000) {
                len += sprintf_s(line + len, sizeof(line) - len, " %08X", *p);
                n++;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    Log("Terrain 2: invalid parameter (thread %lu), stack%s", GetCurrentThreadId(), line);
}

void EnsureDiagnostics()
{
    static PVOID handler;
    if (handler) return;
    // Under Wine/Proton the game's anti-tamper check quit after Player 2 joined (when he grappled
    // into a parachute): no extra exception handler or CRT handler there.
    if (RunningUnderWine()) {
        handler = (PVOID)1;
        Log("Terrain 2: running under Wine, fault diagnostics left out");
        return;
    }
    handler = AddVectoredExceptionHandler(0, LogFault);
    typedef void*(__cdecl* SetHandler_t)(void*);
    HMODULE crt = GetModuleHandleA("MSVCR80.dll");
    SetHandler_t set = crt ? (SetHandler_t)GetProcAddress(crt, "_set_invalid_parameter_handler") : nullptr;
    if (set) set((void*)LogInvalidParameter);
}

// CStreamPatchManager slot 1 (FUN_00a49320, update) and CStreamPatch slot 1 (FUN_00a3f710, file loaded).
const uintptr_t kStreamPatchUpdateSlot = 0x00F0EF60;
const uintptr_t kStreamPatchLoadedSlot = 0x00F0EF3C;

void __fastcall HandOffHook(void* engine)
{
    // The water flag also went 0 outside Player 2's turns, around Player 2 leaving and rejoining
    // (his patch cells registering with the water again). Once it was seen on this session and his
    // copies exist, it stays on (also while he is out: Player 1 lost the sea too).
    static bool highEndWaterSeen;
    static int restoredOutside;
    if (*(uint8_t*)kHighEndWater) {
        highEndWaterSeen = true;
    } else if (highEndWaterSeen && g_patchManager[1]) {
        *(uint8_t*)kHighEndWater = 1;
        if (restoredOutside++ < 8 || (restoredOutside & 1023) == 0)
            Log("Terrain 2: high-end water flag was 0 at the hand-off, put back (%d times)", restoredOutside);
    }
    // Files kept for the stream copies while they were on (and any that came in as they stopped).
    if (g_active < 0 && g_pendingCount) {
        __try {
            ReplayLoads(-1);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("Terrain 2: exception %08X handing over loaded patches", GetExceptionCode());
        }
    }
    float p2[3];
    bool on = g_on && SplitscreenPlayer2Status() == Player2Status::Joined && g_haveCamera2 && Player2Position(p2) &&
              *(uintptr_t*)kLandscape;
    if (on || g_active >= 0) {
        __try {
            LARGE_INTEGER t0, t1;
            QueryPerformanceCounter(&t0);
            // The frame being drawn is done with the landscape state.
            int* job = (int*)((uintptr_t)engine + kEngineRenderJob);
            if (*job) {
                ((Arg_t)kWaitJob)(*job);
                *job = 0;
            }
            WaitLandscapeJobs();
            if (!on) {
                Stop();
                g_active = -1;
                ReplayLoads(-1);
            } else {
                if (g_active < 0) {
                    bool made = (g_vegetationManager[1] || CreateVegetation2()) && (g_patchManager[1] || CreateGround2());
                    // His own stream patches only together with a new ground copy (see g_ownStream).
                    if (made && g_ownStream && !g_streamManager[1] && g_patchManager[1] && !g_groundEverOn) {
                        if (CreateStream2())
                            Log("Terrain 2: own stream patches (manager %08X)", (unsigned)g_streamManager[1]);
                        else
                            Log("Terrain 2: could not create the stream patch copies");
                    }
                    g_groundEverOn = true;
                    if (made) {
                        // This frame's landscape draw lists were made in the world update with
                        // Player 1's state; from the next frame on the landscape runs here.
                        g_active = 0;
                        *(uint8_t*)(*(uintptr_t*)kLandscape + kLandscapeEnabled) = 0;
                        Log("Terrain 2: on (vegetation %08X, patches %08X)", (unsigned)g_vegetationManager[1],
                            (unsigned)g_patchManager[1]);
                    } else {
                        Log("Terrain 2: could not create the copies");
                        g_on = false;
                    }
                } else if (g_player1Alternate) {
                    if (SplitRenderGameView() == 0) RunLandscape(0);
                } else {
                    int view = g_player1Only ? 0 : SplitRenderGameView();
                    // Any failed NVIDIA water call clears the high-end water flag for good, and the
                    // fallback water path draws no sea (and a black band toward the horizon). On
                    // Player 2's turn his patch manager registers its terrain cells with the water
                    // (FUN_00A67610 -> FUN_00A79F00 -> FUN_00C3E410), which fails for cells Player
                    // 1's already registered. The water itself is fine: keep the flag.
                    uint8_t before = *(uint8_t*)kHighEndWater;
                    RunLandscape(view);
                    static int changes;
                    if (*(uint8_t*)kHighEndWater != before) {
                        if (changes++ < 8 || (changes & 1023) == 0)
                            Log("Terrain 2: high-end water flag %d -> %d in view %d's landscape turn (%d times), put back",
                                before, *(uint8_t*)kHighEndWater, view, changes);
                        *(uint8_t*)kHighEndWater = before;
                    }
                }
            }
            QueryPerformanceCounter(&t1);
            g_ms += double(t1.QuadPart - t0.QuadPart) * 1000.0 / double(g_freq.QuadPart);
            if (++g_frames >= 600) {
                Log("Terrain 2: %.2f ms per hand-off, %d loaded patches waiting, %ld handed over at once, high-end water %d",
                    g_ms / g_frames, g_pendingCount, g_pendingDropped, *(uint8_t*)kHighEndWater);
                float p2[3] = {}, ground = -9999.0f;
                if (Player2Position(p2)) Terrain2GroundHeight(p2, &ground);
                if (g_heightFieldShape)
                    Log("Terrain 2: ground samples %ld on the map in, %ld on the other, %ld on neither; last cell %d %d "
                        "(slot holds %d %d), P2 cell %d %d at %.1f %.1f %.1f (ground %.1f), P1 cell %d %d",
                        g_heightFieldSamples[0], g_heightFieldSamples[1], g_heightFieldSamples[2], g_lastSampleCell[0],
                        g_lastSampleCell[1], g_lastSampleCell[2], g_lastSampleCell[3], StreamCell(p2[0]), StreamCell(p2[2]),
                        p2[0], p2[1], p2[2], ground, StreamCell(g_player1StreamPosition[0]),
                        StreamCell(g_player1StreamPosition[2]));
                g_heightFieldSamples[0] = g_heightFieldSamples[1] = g_heightFieldSamples[2] = 0;
                g_ms = 0;
                g_frames = 0;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("Terrain 2: exception %08X, turned off", GetExceptionCode());
            g_on = false;
        }
    }
    // The hand-off copies the main camera to the render camera, world matrix (+0x24) included,
    // and the render side measures detail and draw distances from that world position. On Player
    // 2's frames the main camera's world is Player 1's again (game logic reads it), so for the copy
    // it is Player 2's.
    CameraMatrix world2, previous;
    bool swapped = g_renderWorld2 && SplitRenderGameView() == 1 && SplitViewMatrix(1, &world2) &&
                   SwapMainCameraWorld(world2, &previous);
    g_origHandOff(engine);
    if (swapped) RestoreMainCameraWorld(previous, world2);
    SplitRenderRestoreLogicWorld();
}

} // namespace

bool Terrain2GroundLoaded(int player, const float position[3])
{
    __try {
        // Outside the hand-off the globals hold Player 1's stream state (and Player 2's when he
        // has no own one).
        uintptr_t map = *(uintptr_t*)kTerrainMap;
        if (player == 1 && OwnStreamActive()) map = ReadState(kStreamState, g_streamState[1], kTerrainMap);
        if (!map) return true;
        int cx = StreamCell(position[0]), cy = StreamCell(position[2]);
        uintptr_t slot = map + kMapSlots + ((cx & 7) + (cy & 7) * 8) * 0x20;
        return *(int*)(slot + kMapSlotCell) == cx && *(int*)(slot + kMapSlotCell + 4) == cy;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return true;
    }
}

void Terrain2WaitFrameDrawn()
{
    uintptr_t engine = *(uintptr_t*)kGraphicsEngine;
    if (!engine) return;
    int* job = (int*)(engine + kEngineRenderJob);
    if (*job) {
        ((Arg_t)kWaitJob)(*job);
        *job = 0; // as the hand-off does after waiting (it then doesn't wait again)
    }
}

unsigned Terrain2UseMapTexture(int player)
{
    if (g_active < 0 || player == g_active || player < 0 || player > 1) return 0;
    uint32_t mine = ReadState(kGroundState, g_groundState[player], kMapTexture);
    uint32_t in = *(uint32_t*)kMapTexture;
    if (!mine || mine == in) return 0;
    *(uint32_t*)kMapTexture = mine;
    return in;
}

void Terrain2RestoreMapTexture(unsigned saved)
{
    if (saved) *(uint32_t*)kMapTexture = saved;
}

bool Terrain2GroundHeight(const float position[3], float* height)
{
    __try {
        // The height map game logic and physics read (Player 1's, with Player 2's cells around
        // him redirected); only when its slot holds the cell (FUN_00A5F600 doesn't check).
        uintptr_t map = *(uintptr_t*)kTerrainMap;
        if (!map) return false;
        if (!MapHoldsCell(map, HeightCell(position[0]), HeightCell(position[2]))) return false;
        typedef void(__fastcall * HeightAt_t)(uintptr_t map, void* edx, const float* position, float* out);
        float out[16] = {};
        ((HeightAt_t)0x00A5F600)(map, nullptr, position, out); // out[0] height, then the normal
        *height = out[0];
        return out[0] == out[0];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool Terrain2InstallHooks()
{
    QueryPerformanceFrequency(&g_freq);
    InitializeCriticalSection(&g_pendingLock);
    EnsureDiagnostics();
    size_t vegetation = 0, ground = 0;
    for (const Range& r : kVegetationState) vegetation += r.size;
    for (const Range& r : kGroundState) ground += r.size;
    if (vegetation > sizeof(g_vegetationState[0]) || ground > sizeof(g_groundState[0])) {
        Log("Terrain 2: state buffers too small");
        return false;
    }
    DWORD old;
    if (!VirtualProtect((void*)kHandOffSlot, 4, PAGE_READWRITE, &old)) return false;
    g_origHandOff = *(HandOff_t*)kHandOffSlot;
    *(void**)kHandOffSlot = (void*)HandOffHook;
    VirtualProtect((void*)kHandOffSlot, 4, old, &old);
    if (!VirtualProtect((void*)kStreamPatchUpdateSlot, 4, PAGE_READWRITE, &old)) return false;
    g_origStreamPatchUpdate = *(StreamPatchUpdate_t*)kStreamPatchUpdateSlot;
    *(void**)kStreamPatchUpdateSlot = (void*)StreamPatchUpdateHook;
    VirtualProtect((void*)kStreamPatchUpdateSlot, 4, old, &old);
    if (!VirtualProtect((void*)kStreamPatchLoadedSlot, 4, PAGE_READWRITE, &old)) return false;
    g_origStreamPatchLoaded = *(StreamPatchLoaded_t*)kStreamPatchLoadedSlot;
    *(void**)kStreamPatchLoadedSlot = (void*)StreamPatchLoadedHook;
    VirtualProtect((void*)kStreamPatchLoadedSlot, 4, old, &old);
    if (!VirtualProtect((void*)kZoneCallbackLoadSlot, 8, PAGE_READWRITE, &old)) return false;
    g_origZoneLoad = *(ZoneCallback_t*)kZoneCallbackLoadSlot;
    g_origZoneRelease = *(ZoneCallback_t*)kZoneCallbackReleaseSlot;
    *(void**)kZoneCallbackLoadSlot = (void*)ZoneLoadHook;
    *(void**)kZoneCallbackReleaseSlot = (void*)ZoneReleaseHook;
    VirtualProtect((void*)kZoneCallbackLoadSlot, 8, old, &old);
    return true;
}

bool Terrain2UsePlayer2(bool in, bool ground)
{
    static int groundBefore;
    __try {
        if (!g_on || !OwnStreamActive()) return false;
        if (in) {
            RestorePlayer2Collision();
            SwitchStream(1);
            groundBefore = g_active;
            if (ground) SwitchGround(1);
        } else {
            SwitchGround(groundBefore);
            SwitchStream(0);
            ApplyPlayer2Collision();
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Terrain2CapturePlayer2Camera(const void* camera)
{
    __try {
        memcpy(g_camera2, camera, kCameraCopySize);
        g_haveCamera2 = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

std::string Terrain2Set(const std::string& mode)
{
    EnsureDiagnostics();
    if (mode.compare(0, 8, "rebuild2") == 0) {
        uint32_t levels = mode.size() > 9 ? strtoul(mode.c_str() + 9, nullptr, 16) : 0x7FF;
        g_rebuildPlayer2Cells = levels;
        char text[96];
        sprintf_s(text, "ok player 2's next turn rebuilds patch levels %X", (unsigned)levels);
        return text;
    }
    if (mode == "celldiff") {
        // Testing: P1's and P2's patch cells (kGroundState range 2: 11 levels x 64 cells of 0x34 from
        // +8, slot (level * 8 + (y & 7)) * 8 + (x & 7), x +0xC, y +0x10) for the same slot and
        // coordinates: which dwords differ, and some values (non-GPU water missing for P2).
        const uint8_t* live = (const uint8_t*)kGroundState[2].addr;
        size_t off = GroundRangeOffset(2);
        int active = g_active;
        const uint8_t* cells1 = active == 0 || active < 0 ? live : g_groundState[0] + off;
        const uint8_t* cells2 = active == 1 ? live : g_groundState[1] + off;
        int same = 0, diffCount[13] = {};
        char sample[600] = "";
        int sampleLen = 0;
        for (int i = 0; i < 11 * 64; i++) {
            const uint8_t* a = cells1 + 8 + i * 0x34;
            const uint8_t* b = cells2 + 8 + i * 0x34;
            if (*(const int*)(a + 0xC) != *(const int*)(b + 0xC) || *(const int*)(a + 0x10) != *(const int*)(b + 0x10)) continue;
            same++;
            for (int d = 0; d < 13; d++) {
                if (*(const uint32_t*)(a + d * 4) == *(const uint32_t*)(b + d * 4)) continue;
                diffCount[d]++;
                if (sampleLen < 500 && diffCount[d] <= 2)
                    sampleLen += sprintf_s(sample + sampleLen, sizeof(sample) - sampleLen, " [slot %d +%02X: %08X vs %08X]", i, d * 4,
                                           *(const uint32_t*)(a + d * 4), *(const uint32_t*)(b + d * 4));
            }
        }
        char text[1000];
        int len = sprintf_s(text, "ok %d cells at the same place (active %d); differing dwords:", same, active);
        for (int d = 0; d < 13; d++)
            if (diffCount[d]) len += sprintf_s(text + len, sizeof(text) - len, " +%02X x%d", d * 4, diffCount[d]);
        Log("Terrain 2: celldiff%s", sample);
        return text;
    }
    if (mode.compare(0, 9, "vegfield ") == 0) {
        char* end;
        uintptr_t field = strtoul(mode.c_str() + 9, &end, 16) & ~3u;
        uintptr_t length = strtoul(end, nullptr, 16);
        if (!length) length = 4;
        if (field + length > kVegetationManagerSize) length = kVegetationManagerSize - field;
        g_testVegetationFieldLength = 0;
        g_testVegetationField = field;
        g_testVegetationFieldLength = field ? length : 0;
        char text[80];
        sprintf_s(text, "ok P2's turn uses P1's vegetation manager +%X..+%X", (unsigned)field, (unsigned)(field + length));
        return text;
    }
    if (mode == "vegfields") {
        // Read only: the fields of the two CVegetationManagers that point at objects with a vtable
        // (class from RTTI), shared or each its own; the full list goes to the log.
        uintptr_t m[2] = { g_vegetationManager[0], g_vegetationManager[1] };
        if (!m[0] || !m[1]) return "error no second vegetation manager";
        auto className = [](uintptr_t object, char* out, size_t size) -> bool {
            __try {
                uintptr_t vt = *(uintptr_t*)object;
                if (vt < 0x00D90000 || vt >= 0x01100000) return false;
                uintptr_t col = *(uintptr_t*)(vt - 4);
                if (col < 0x00D90000 || col >= 0x01500000) return false;
                const char* name = (const char*)(*(uintptr_t*)(col + 0xC) + 8);
                strncpy_s(out, size, name, _TRUNCATE);
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        };
        int shared = 0, own = 0;
        for (size_t o = 0; o + 4 <= kVegetationManagerSize; o += 4) {
            uintptr_t a = *(uintptr_t*)(m[0] + o), b = *(uintptr_t*)(m[1] + o);
            char na[96] = "", nb[96] = "";
            bool ca = a >= 0x10000 && className(a, na, sizeof(na)), cb = b >= 0x10000 && className(b, nb, sizeof(nb));
            if (!ca && !cb) continue;
            if (a == b) shared++;
            else own++;
            Log("Terrain 2: vegfield +%04X %s P1 %08X %s P2 %08X %s", (unsigned)o, a == b ? "SHARED" : "own", (unsigned)a, na,
                (unsigned)b, nb);
        }
        char text[120];
        sprintf_s(text, "ok %d shared object fields, %d own (see log)", shared, own);
        return text;
    }
    if (mode.compare(0, 6, "leaves") == 0) {
        // Testing ("terrain2 leaves <player 1|2> <chance>"): a player's falling leaves spawn chance
        // (CLeafManager [0x01382030] +0x2584; +0x2588 leaves alive).
        int player = 2;
        float chance = -1;
        sscanf_s(mode.c_str() + 6, "%d %f", &player, &chance);
        int p = player == 1 ? 0 : 1;
        uintptr_t leaves = p == g_activeVegetation ? *(uintptr_t*)0x01382030 : ReadState(kVegetationState, g_vegetationState[p], 0x01382030);
        if (!leaves) return "error no leaf manager";
        if (chance >= 0) *(float*)(leaves + 0x2584) = chance;
        char text[160];
        sprintf_s(text, "ok P%d leaf manager %08X: chance %.3f, %d alive", p + 1, (unsigned)leaves, *(float*)(leaves + 0x2584),
                  *(int*)(leaves + 0x2588));
        return text;
    }
    if (mode.compare(0, 5, "slots") == 0) {
        // "terrain2 slots <x> <z>": the height map slot of that point's cell in both players' maps
        // (cell held, loaded flag, a hash of its heights, the height there). Read only.
        float p[3] = {};
        if (sscanf_s(mode.c_str() + 5, "%f %f", &p[0], &p[2]) < 2) return "error usage: terrain2 slots <x> <z>";
        int x = HeightCell(p[0]), y = HeightCell(p[2]);
        uintptr_t maps[2] = { g_activeStream == 0 ? *(uintptr_t*)kTerrainMap : ReadState(kStreamState, g_streamState[0], kTerrainMap),
                              g_activeStream == 1 ? *(uintptr_t*)kTerrainMap : ReadState(kStreamState, g_streamState[1], kTerrainMap) };
        char text[600];
        int len = sprintf_s(text, "ok cell %d %d (slot %d), redirects %d:", x, y, (x & 7) + (y & 7) * 8, g_mapRedirectCount);
        for (int m = 0; m < 2; m++) {
            if (!maps[m]) continue;
            uintptr_t slot = maps[m] + kMapSlots + ((x & 7) + (y & 7) * 8) * 0x20;
            const uint16_t* heights = *(const uint16_t**)(slot + kMapSlotHeights);
            uint32_t hash = 2166136261u;
            if (heights)
                for (int i = 0; i < 0x4410; i++) hash = (hash ^ heights[i]) * 16777619u;
            __declspec(align(16)) float point[4] = { p[0], 0, p[2], 0 };
            __declspec(align(16)) float sample[12] = {};
            if (heights) ((SampleTerrainMap_t)kSampleTerrainMap)(maps[m], point, sample);
            len += sprintf_s(text + len, sizeof(text) - len, " P%d map %08X holds %d %d loaded %d heights %08X hash %08X first %u %u height %.1f;",
                             m + 1, (unsigned)maps[m], *(int*)(slot + kMapSlotCell), *(int*)(slot + kMapSlotCell + 4),
                             *(uint8_t*)(slot + kMapSlotLoaded), (unsigned)(uintptr_t)heights, hash,
                             heights ? heights[0] : 0, heights ? heights[0x2200] : 0, sample[0]);
        }
        return text;
    }
    if (mode == "wholezone on" || mode == "wholezone off") {
        g_testWholeZone = mode == "wholezone on";
        return g_testWholeZone ? "ok whole climate zone object per player" : "ok zone fields per player";
    }
    if (mode.compare(0, 9, "vegregen ") == 0) {
        g_testVegetationRegen = strtoul(mode.c_str() + 9, nullptr, 16);
        char text[80];
        sprintf_s(text, "ok vegetation regenerated every turn for mask %X (1 P1, 2 P2)", (unsigned)g_testVegetationRegen);
        return text;
    }
    if (mode == "vegstay on" || mode == "vegstay off") {
        g_testVegetationStays = mode == "vegstay on";
        return g_testVegetationStays ? "ok player 2's vegetation stays in through his frame" : "ok player 1's vegetation back after player 2's turn";
    }
    if (mode == "placed" || mode == "placed on" || mode == "placed off") {
        if (mode != "placed") g_mirrorPlacedVegetation = mode == "placed on";
        size_t used = 0;
        for (const PlacedMirror& m : g_placedMirror) used += m.used;
        char text[160];
        sprintf_s(text, "ok placed vegetation mirrored to player 2 %s: %u of %u entries, %ld added, %ld removed",
                  g_mirrorPlacedVegetation ? "on" : "off", (unsigned)used, (unsigned)g_placedMirror.size(), g_placedAdded, g_placedRemoved);
        return text;
    }
    if (mode == "vegdump") {
        // Both copies' values of 0x01381FF0..0x01382058 (render states, job types, camera, block arrays).
        for (int p = 0; p < 2; p++) {
            char line[512];
            int len = sprintf_s(line, "Terrain 2: vegetation P%d (active %d):", p + 1, g_activeVegetation);
            for (uintptr_t a = 0x01381FF0; a < 0x01382058; a += 4)
                len += sprintf_s(line + len, sizeof(line) - len, " %08X",
                                 p == g_activeVegetation ? *(uint32_t*)a : ReadState(kVegetationState, g_vegetationState[p], a));
            Log("%s", line);
        }
        return "ok see the log";
    }
    if (mode.compare(0, 8, "vegmask ") == 0) {
        g_testVegetationKeepMask = strtoul(mode.c_str() + 8, nullptr, 16);
        char text[96];
        sprintf_s(text, "ok player 2's turn takes vegetation ranges %X from player 1", (unsigned)g_testVegetationKeepMask);
        return text;
    }
    if (mode.compare(0, 11, "groundmask ") == 0) {
        g_testGroundKeepMask = strtoul(mode.c_str() + 11, nullptr, 16);
        char text[96];
        sprintf_s(text, "ok player 2's turn takes ground ranges %X from player 1", (unsigned)g_testGroundKeepMask);
        return text;
    }
    if (mode.compare(0, 5, "keep ") == 0) {
        std::string what = mode.substr(5);
        g_testKeepVegetation1 = what == "veg";
        g_testKeepGround1 = what == "ground";
        g_testKeepCamera1 = what == "camera";
        return "ok player 2's landscape turn keeps player 1's " + what;
    }
    if (mode == "landstream1" || mode == "landstream2") {
        g_landscapeStream2 = mode == "landstream2";
        return "ok player 2's landscape turn with player " + std::string(g_landscapeStream2 ? "2" : "1") + "'s stream state";
    }
    if (mode == "sharedtex" || mode == "ownsharedtex" || mode == "sharedterraintex" || mode == "sharedmaptex") {
        g_sharedGroundTextures = mode == "sharedtex" ? 3 : mode == "sharedterraintex" ? 1 : mode == "sharedmaptex" ? 2 : 0;
        return "ok player 2's frames use player 1's texture handlers: terrain " +
               std::string(g_sharedGroundTextures & 1 ? "yes" : "no") + ", map " + (g_sharedGroundTextures & 2 ? "yes" : "no");
    }
    if (mode == "resident" || mode == "noresident") {
        g_spoofResidency = mode == "resident";
        return "ok residency spoof " + std::string(g_spoofResidency ? "on" : "off");
    }
    if (mode == "renderworld" || mode == "norenderworld") {
        g_renderWorld2 = mode == "renderworld";
        return "ok player 2's frames drawn from his camera position " + std::string(g_renderWorld2 ? "on" : "off");
    }
    if (mode == "collision" || mode == "nocollision") {
        g_player2Collision = mode == "collision";
        return "ok player 2 ground collision from his own height map " + std::string(g_player2Collision ? "on" : "off");
    }
    if (mode == "stream2" || mode == "nostream2") {
        g_ownStream = mode == "stream2";
        if (g_ownStream && g_groundEverOn && !g_streamManager[1])
            return "error own stream patches can only start with the ground copy (before the first terrain2 on)";
        return "ok player 2 own stream patches " + std::string(g_ownStream ? "on" : "off") +
               (g_streamManager[1] ? " (made; it stays in use)" : "");
    }
    if (mode == "stream" || mode == "nostream") {
        g_streamAtPlayer2 = mode == "stream";
        return "ok stream patches around " + std::string(g_streamAtPlayer2 ? "player 2" : "the camera");
    }
    g_fullUpdate = mode != "partial";
    g_on = mode == "on" || mode == "p1only" || mode == "p1alt" || mode == "full" || mode == "partial";
    g_player1Only = mode == "p1only";
    g_player1Alternate = mode == "p1alt";
    return "ok player 2 terrain " + mode;
}
