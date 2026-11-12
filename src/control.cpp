#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string>
#include "control.h"
#include "screenshot.h"
#include "watch.h"
#include "game.h"
#include "splitscreen.h"
#include "player2.h"
#include "audio.h"
#include "splitrender.h"
#include "terrain2.h"
#include "world2.h"
#include "trace.h"
#include "hooks.h"
#include "log.h"

namespace {

std::string g_dir;
DWORD g_nextPoll;
std::string g_watchId; // pending watch result
DWORD g_watchEnd;

const std::string& GameDir()
{
    if (g_dir.empty()) {
        char path[MAX_PATH];
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        g_dir = path;
        g_dir = g_dir.substr(0, g_dir.find_last_of('\\') + 1);
    }
    return g_dir;
}

void WriteResult(const std::string& id, const std::string& body)
{
    std::string tmp = GameDir() + "jc2ss_result.tmp";
    std::string dst = GameDir() + "jc2ss_result.txt";
    FILE* f = nullptr;
    if (fopen_s(&f, tmp.c_str(), "wb") || !f) return;
    fprintf(f, "id %s\n%s", id.c_str(), body.c_str());
    if (!body.empty() && body.back() != '\n') fputc('\n', f);
    fclose(f);
    MoveFileExA(tmp.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING);
}

bool ReadCommand(std::string* id, std::string* cmd)
{
    std::string path = GameDir() + "jc2ss_cmd.txt";
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) return false;

    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") || !f) return false;
    char buf[2048] = {};
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    DeleteFileA(path.c_str());
    buf[n] = '\0';

    char* line2 = strchr(buf, '\n');
    if (strncmp(buf, "id ", 3) != 0 || !line2) return false;
    *id = std::string(buf + 3, line2 - (buf + 3));
    *cmd = line2 + 1;
    while (!id->empty() && (id->back() == '\r' || id->back() == ' ')) id->pop_back();
    while (!cmd->empty() && (cmd->back() == '\r' || cmd->back() == '\n' || cmd->back() == ' ')) cmd->pop_back();
    return true;
}

float g_offset[3];
float g_yaw; // radians

void RotateY(float v[4], float c, float s)
{
    float x = v[0], z = v[2];
    v[0] = x * c + z * s;
    v[2] = -x * s + z * c;
}

// Test override: moves the camera by g_offset and turns it by g_yaw around world Y.
void OffsetCamera(const CameraUpdate& update, bool after)
{
    if (after) return;
    CameraMatrix* m = update.world;
    float c = cosf(g_yaw), s = sinf(g_yaw);
    RotateY(m->right, c, s);
    RotateY(m->up, c, s);
    RotateY(m->forward, c, s);
    for (int i = 0; i < 3; i++) m->position[i] += g_offset[i];
}

std::string CameraText()
{
    CameraMatrix m;
    if (!ReadMainCamera(&m)) return "camera not available";
    char buf[256];
    sprintf_s(buf, "pos %.3f %.3f %.3f\nright %.3f %.3f %.3f\nup %.3f %.3f %.3f\nforward %.3f %.3f %.3f",
              m.position[0], m.position[1], m.position[2], m.right[0], m.right[1], m.right[2],
              m.up[0], m.up[1], m.up[2], m.forward[0], m.forward[1], m.forward[2]);
    return buf;
}

void Execute(IDXGISwapChain* swapChain, const std::string& id, const std::string& cmd)
{
    Log("Command %s: %s", id.c_str(), cmd.c_str());
    char verb[32] = {};
    sscanf_s(cmd.c_str(), "%31s", verb, (unsigned)sizeof(verb));
    const char* args = cmd.c_str() + strlen(verb);
    while (*args == ' ') args++;

    if (!strcmp(verb, "ping")) {
        WriteResult(id, "ok");
    } else if (!strcmp(verb, "cam")) {
        WriteResult(id, CameraText());
    } else if (!strcmp(verb, "uimode")) {
        // Read only: the game's UI mode, and whether the mod takes it as the main menu.
        WriteResult(id, "ui mode " + std::to_string(SplitscreenUiMode()) + (SplitscreenInMainMenu() ? " (main menu)" : ""));
    } else if (!strcmp(verb, "kbp1")) {
        // "kbp1 on|off": keyboard mode, as F7 / the main menu button; no argument just reports it.
        if (!strcmp(args, "on") || !strcmp(args, "off")) SplitscreenSetKeyboardP1(!strcmp(args, "on"));
        WriteResult(id, std::string("ok keyboard mode ") + (SplitscreenKeyboardP1() ? "on" : "off") + ", player 2 on pad " +
                            std::to_string(SplitscreenPlayer2Slot() + 1));
    } else if (!strcmp(verb, "screenshot")) {
        const char* error;
        WriteResult(id, SaveScreenshot(swapChain, args, &error) ? std::string("ok") : std::string("error ") + error);
    } else if (!strcmp(verb, "p2")) {
        if (!strcmp(args, "spawn")) WriteResult(id, Player2RequestSpawn());
        else if (!strncmp(args, "possess", 7)) {
            unsigned npc = 0;
            sscanf_s(args + 7, "%x", &npc);
            WriteResult(id, Player2RequestPossess(npc));
        } else if (!strncmp(args, "drive", 5)) {
            WriteResult(id, Player2RequestDrive());
        } else if (!strncmp(args, "enable", 6)) {
            unsigned c = 0;
            sscanf_s(args + 6, "%x", &c);
            WriteResult(id, Player2RequestEnable(c));
        } else if (!strncmp(args, "press", 5)) {
            // p2 press <hexAction> [ms] | p2 press <hexAction>+<hexAction2> [ms]
            unsigned action = 0, action2 = 0, ms = 150;
            int n = sscanf_s(args + 5, "%x+%x %u", &action, &action2, &ms);
            if (n < 2) {
                action2 = 0xFFFFFFFF;
                n = sscanf_s(args + 5, "%x %u", &action, &ms);
            }
            if (n < 1) {
                WriteResult(id, "error usage: p2 press <hexAction>[+<hexAction2>] [ms]");
            } else {
                Player2PressAction((int)action, ms, (int)action2);
                WriteResult(id, "ok pressing");
            }
        } else if (!strncmp(args, "padpress", 8)) {
            unsigned buttons = 0, ms = 150;
            sscanf_s(args + 8, "%x %u", &buttons, &ms);
            WriteResult(id, Player2PadPress(buttons, ms));
        } else if (!strncmp(args, "padlog", 6)) {
            WriteResult(id, Player2PadLog(strstr(args, "off") == nullptr));
        } else if (!strcmp(args, "roll") || !strcmp(args, "rollcheck")) {
            WriteResult(id, Player2RollTest(!strcmp(args, "roll")));
        } else if (!strcmp(args, "firecheck")) {
            WriteResult(id, Player2RequestFireCheck());
        } else if (!strcmp(args, "kill") || !strcmp(args, "kill raw")) {
            WriteResult(id, Player2RequestKill(!strcmp(args, "kill raw")));
        } else if (!strcmp(args, "swap")) {
            WriteResult(id, PlayersRequestSwap());
        } else if (!strcmp(args, "gear")) {
            WriteResult(id, Player2RequestGear());
        } else if (!strcmp(args, "full on") || !strcmp(args, "full off")) {
            Player2FullUpdate(!strcmp(args, "full on"));
            WriteResult(id, "ok");
        } else if (!strncmp(args, "turn", 4)) {
            float deg = 0;
            sscanf_s(args + 4, "%f", &deg);
            SplitRenderTurnPlayer2Camera(deg * 3.14159265f / 180.0f);
            WriteResult(id, "ok turned player 2's camera");
        } else if (!strncmp(args, "pitch", 5)) {
            float deg = 0;
            sscanf_s(args + 5, "%f", &deg);
            SplitRenderPitchPlayer2Camera(deg * 3.14159265f / 180.0f);
            WriteResult(id, "ok pitched player 2's camera");
        } else if (!strcmp(args, "join") || !strcmp(args, "leave")) {
            SplitscreenSimulateJoin(!strcmp(args, "join"));
            WriteResult(id, !strcmp(args, "join") ? "ok joined (as if Start on pad 2)" : "ok left");
        } else if (!strcmp(args, "rico")) {
            WriteResult(id, Player2RequestRicoBody());
        } else if (!strcmp(args, "cam")) {
            // Read only: Player 2's view as "cam" prints Player 1's ("p2 face" takes its forward).
            CameraMatrix m;
            char buf[160];
            if (!SplitViewMatrix(1, &m)) WriteResult(id, "error no player 2 view");
            else {
                sprintf_s(buf, "pos %.3f %.3f %.3f\nforward %.3f %.3f %.3f", m.position[0], m.position[1], m.position[2],
                          m.forward[0], m.forward[1], m.forward[2]);
                WriteResult(id, buf);
            }
        } else if (!strcmp(args, "look")) {
            WriteResult(id, Player2RequestRicoLook());
        } else if (!strncmp(args, "move", 4)) {
            float r = 0, f = 0;
            sscanf_s(args + 4, "%f %f", &r, &f);
            Player2SetMoveInput(r, f);
            WriteResult(id, "ok");
        } else if (!strncmp(args, "sticks", 6)) {
            float v[6] = {};
            unsigned ms = 1000;
            sscanf_s(args + 6, "%f %f %f %f %f %f %u", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &ms);
            Player2SetTestSticks(v[0], v[1], v[2], v[3], v[4], v[5], ms);
            WriteResult(id, "ok sticks held");
        } else if (!strncmp(args, "face", 4)) {
            float f[3] = {};
            if (sscanf_s(args + 4, "%f %f %f", &f[0], &f[1], &f[2]) < 3) {
                WriteResult(id, "error usage: p2 face <fx> <fy> <fz> (a camera forward as cam prints it)");
            } else {
                SplitRenderFacePlayer2Camera(f);
                WriteResult(id, "ok player 2's camera faced");
            }
        } else if (!strncmp(args, "at", 2)) {
            float p[3] = {};
            if (sscanf_s(args + 2, "%f %f %f", &p[0], &p[1], &p[2]) < 3) WriteResult(id, "error usage: p2 at <x> <y> <z>");
            else WriteResult(id, Player2RequestTeleportTo(p[0], p[1], p[2]));
        } else if (!strncmp(args, "tp", 2)) {
            float o[3] = { 2.0f, 0.2f, 0.0f };
            sscanf_s(args + 2, "%f %f %f", &o[0], &o[1], &o[2]);
            WriteResult(id, Player2RequestTeleport(o[0], o[1], o[2]));
        }
        else WriteResult(id, Player2Describe());
    } else if (!strcmp(verb, "trace")) {
        unsigned addr = 0, ms = 10000;
        unsigned long thread = 0;
        if (sscanf_s(args, "%x %u %lu", &addr, &ms, &thread) < 1) WriteResult(id, "error usage: trace <hexaddr> [ms] [thread]");
        else WriteResult(id, TraceStart(addr, ms, thread));
    } else if (!strcmp(verb, "chutecap")) {
        SetParachuteCap(strcmp(args, "off") != 0);
        WriteResult(id, std::string("ok parachute cap ") + (strcmp(args, "off") ? "on" : "off"));
    } else if (!strcmp(verb, "terrain2")) {
        WriteResult(id, Terrain2Set(args));
    } else if (!strcmp(verb, "world2")) {
        WriteResult(id, World2Set(args));
    } else if (!strcmp(verb, "chutedrag")) {
        float scale = 1;
        sscanf_s(args, "%f", &scale);
        SetReelChuteDragScale(scale);
        WriteResult(id, "ok reel parachute drag scale " + std::to_string(scale));
    } else if (!strcmp(verb, "fpscap")) {
        int fps = 0;
        sscanf_s(args, "%d", &fps);
        SetFpsCap(fps);
        WriteResult(id, "ok frame cap " + std::to_string(fps));
    } else if (!strcmp(verb, "p1") && !strncmp(args, "at", 2)) {
        float p[3] = {};
        if (sscanf_s(args + 2, "%f %f %f", &p[0], &p[1], &p[2]) < 3) WriteResult(id, "error usage: p1 at <x> <y> <z>");
        else WriteResult(id, Player1RequestTeleportTo(p[0], p[1], p[2]));
    } else if (!strcmp(verb, "p1jobcam")) {
        WriteResult(id, Player1JobCamera(strcmp(args, "off") != 0));
    } else if (!strcmp(verb, "p2minimap")) {
        WriteResult(id, Player2MinimapPerPlayer(strcmp(args, "off") != 0));
    } else if (!strcmp(verb, "p2markers")) {
        WriteResult(id, Player2MarkersPerPlayer(strcmp(args, "off") != 0));
    } else if (!strcmp(verb, "p2sharestunt")) {
        WriteResult(id, Player2ShareStuntPositions(strcmp(args, "off") != 0));
    } else if (!strcmp(verb, "p2skylog")) {
        unsigned ms = 300;
        sscanf_s(args, "%u", &ms);
        WriteResult(id, Player2SkyDiveLog(ms));
    } else if (!strcmp(verb, "p2skyremap")) {
        WriteResult(id, Player2SkyDiveRemap(strcmp(args, "off") != 0));
    } else if (!strcmp(verb, "p2aimlend")) {
        WriteResult(id, Player2AimLend(strcmp(args, "off") != 0));
    } else if (!strcmp(verb, "god")) {
        WriteResult(id, PlayersGodMode(strcmp(args, "off") != 0));
    } else if (!strcmp(verb, "groundat")) {
        // Testing: terrain height at x z from the height map game logic uses (loaded cells only).
        float p[3] = {}, height = 0;
        if (sscanf_s(args, "%f %f", &p[0], &p[2]) < 2) WriteResult(id, "error usage: groundat <x> <z>");
        else if (!Terrain2GroundHeight(p, &height)) WriteResult(id, "error that cell isn't loaded");
        else WriteResult(id, "ok ground " + std::to_string(height));
    } else if (!strcmp(verb, "p1top2")) {
        WriteResult(id, Player1RequestMoveToPlayer2());
    } else if (!strcmp(verb, "p1lift")) {
        float m = 30;
        sscanf_s(args, "%f", &m);
        WriteResult(id, Player1RequestLift(m));
    } else if (!strcmp(verb, "money")) {
        int dollars = 50000;
        sscanf_s(args, "%d", &dollars);
        RequestMoney(dollars);
        WriteResult(id, "ok money requested");
    } else if (!strcmp(verb, "ammo")) {
        RequestAmmoRefill();
        WriteResult(id, "ok ammo refill requested");
    } else if (!strcmp(verb, "split") && (!strcmp(args, "p2world on") || !strcmp(args, "p2world off"))) {
        bool on = !strcmp(args, "p2world on");
        SplitRenderKeepPlayer2World(on);
        WriteResult(id, std::string("ok player 2's world matrix kept through his frames ") + (on ? "on" : "off"));
    } else if (!strcmp(verb, "split") && !strncmp(args, "camwalls", 8)) {
        WriteResult(id, SplitRenderCameraWalls(args + 8));
    } else if (!strcmp(verb, "split") && (!strcmp(args, "p1near") || !strcmp(args, "p1far"))) {
        SplitRenderPushPlayer1Back(!strcmp(args, "p1far"));
        WriteResult(id, std::string("ok player 1's camera ") + (strcmp(args, "p1far") ? "where the game puts it" : "as far back as player 2's"));
    } else if (!strcmp(verb, "split") && (!strcmp(args, "hudmask on") || !strcmp(args, "hudmask off"))) {
        SplitRenderSetHudMask(!strcmp(args, "hudmask on"));
        WriteResult(id, std::string("ok HUD corner mask ") + (strcmp(args, "hudmask on") ? "off" : "on"));
    } else if (!strcmp(verb, "split") && !strncmp(args, "p2fov", 5)) {
        SplitRenderOwnPlayer2Fov(strstr(args, "off") == nullptr);
        WriteResult(id, "ok Player 2 FOV set");
    } else if (!strcmp(verb, "split") && (!strcmp(args, "hudstrips on") || !strcmp(args, "hudstrips off"))) {
        SplitRenderSetHudStrips(!strcmp(args, "hudstrips on"));
        WriteResult(id, std::string("ok HUD strips ") + (strcmp(args, "hudstrips on") ? "off" : "on"));
    } else if (!strcmp(verb, "guievent")) {
        WriteResult(id, Player2FireGuiEvent(args));
    } else if (!strcmp(verb, "split") && !strncmp(args, "hudtop ", 7)) {
        SplitRenderHudTopShift((float)atof(args + 7));
        WriteResult(id, "ok top HUD shift set");
    } else if (!strcmp(verb, "split") && !strncmp(args, "hudblurgate", 11)) {
        WriteResult(id, SplitRenderHudBlurGate(args[11] == ' ' ? args + 12 : ""));
    } else if (!strcmp(verb, "p2audio")) {
        int mode;
        if (sscanf_s(args, "mode %d", &mode) == 1) AudioListenerMode(mode);
        WriteResult(id, !strcmp(args, "dump") ? AudioDump() : AudioTwoListeners(strcmp(args, "off") != 0));
    } else if (!strcmp(verb, "p2hud")) {
        unsigned weapons, ammo;
        float x, y;
        if (!strcmp(args, "sway on") || !strcmp(args, "sway off")) Player2HudSway(!strcmp(args, "sway on"));
        if (!strcmp(args, "proximity on") || !strcmp(args, "proximity off"))
            Player2HudProximity(!strcmp(args, "proximity on"));
        if (!strcmp(args, "passes")) {
            WriteResult(id, Player2HudPasses());
            return;
        }
        if (!strncmp(args, "prompts", 7)) {
            WriteResult(id, Player2HudPrompts(args + 7));
            return;
        }
        if (!strcmp(args, "pickups on") || !strcmp(args, "pickups off")) {
            WriteResult(id, Player2Pickups(!strcmp(args, "pickups on")));
            return;
        }
        if (!strcmp(args, "popup off")) Player2HudPopup(false, 0.0f, 0.0f);
        else if (sscanf_s(args, "popup %f %f", &x, &y) == 2) Player2HudPopup(true, x, y);
        if (!strcmp(args, "log") || !strcmp(args, "nolog")) Player2HudLog(!strcmp(args, "log"));
        if (sscanf_s(args, "refresh %x %x", &weapons, &ammo) == 2) WriteResult(id, Player2HudRefresh(weapons, ammo));
        else WriteResult(id, Player2HudPerPlayer(strcmp(args, "off") != 0));
    } else if (!strcmp(verb, "split") && (!strcmp(args, "hudcorner on") || !strcmp(args, "hudcorner off"))) {
        SplitRenderSetHudCorner(!strcmp(args, "hudcorner on"));
        WriteResult(id, std::string("ok HUD corner in each half ") + (strcmp(args, "hudcorner on") ? "off" : "on"));
    } else if (!strcmp(verb, "split") && !strncmp(args, "fallshake", 9)) {
        float deg = 2.0f;
        sscanf_s(args + 9, "%f", &deg);
        SplitRenderSetFallShake(deg);
        WriteResult(id, "ok player 2's free fall camera shake " + std::to_string(deg) + " deg per strength");
    } else if (!strcmp(verb, "p2rumble")) {
        float low = 6.0f, high = 3.0f;
        sscanf_s(args, "%f %f", &low, &high);
        SplitscreenSetRumbleScale(low, high);
        WriteResult(id, "ok pad 2 free fall rumble " + std::to_string(low) + " / " + std::to_string(high) + " per strength");
    } else if (!strcmp(verb, "split") && !strncmp(args, "falllead", 8)) {
        float lead = 0.3f;
        sscanf_s(args + 8, "%f", &lead);
        SplitRenderSetFallLead(lead);
        WriteResult(id, "ok player 2's free fall camera lead " + std::to_string(lead) + " rad per lean");
    } else if (!strcmp(verb, "split") && !strncmp(args, "helicam", 7)) {
        float distance = 40.0f, height = 3.0f;
        sscanf_s(args + 7, "%f %f", &distance, &height);
        SplitRenderSetHeliCamera(distance, height);
        WriteResult(id, "ok player 2's helicopter camera " + std::to_string(distance) + " m back, " +
                            std::to_string(height) + " m up");
    } else if (!strcmp(verb, "split") && !strncmp(args, "planecam", 8)) {
        float distance = 30.0f, height = 2.0f;
        sscanf_s(args + 8, "%f %f", &distance, &height);
        SplitRenderSetPlaneCamera(distance, height);
        WriteResult(id, "ok player 2's plane camera " + std::to_string(distance) + " m back, " +
                            std::to_string(height) + " m up");
    } else if (!strcmp(verb, "split") && !strncmp(args, "bigplanecam", 11)) {
        float distance = 90.0f, height = 10.0f;
        sscanf_s(args + 11, "%f %f", &distance, &height);
        SplitRenderSetBigPlaneCamera(distance, height);
        WriteResult(id, "ok player 2's Aeroliner camera " + std::to_string(distance) + " m back, " +
                            std::to_string(height) + " m up");
    } else if (!strcmp(verb, "split") && !strncmp(args, "bikecam", 7)) {
        float back = 2.5f, down = 0.6f;
        sscanf_s(args + 7, "%f %f", &back, &down);
        SplitRenderSetBikeCamera(back, down);
        WriteResult(id, "ok player 2's motorbike camera " + std::to_string(back) + " m farther back, " +
                            std::to_string(down) + " m lower than on foot");
    } else if (!strcmp(verb, "split") && !strcmp(args, "flip")) {
        SplitRenderFlipViews();
        WriteResult(id, "ok views flipped");
    } else if (!strcmp(verb, "split")) {
        SplitscreenForce(!strcmp(args, "on"));
        WriteResult(id, !strcmp(args, "on") ? "ok split forced on" : "ok split force off");
    } else if (!strcmp(verb, "camoffset")) {
        float yawDeg = 0;
        if (!strcmp(args, "off")) {
            SetCameraOverride(nullptr);
            WriteResult(id, "ok override off");
        } else if (sscanf_s(args, "%f %f %f %f", &g_offset[0], &g_offset[1], &g_offset[2], &yawDeg) >= 3) {
            g_yaw = yawDeg * 3.14159265f / 180.0f;
            SetCameraOverride(OffsetCamera);
            WriteResult(id, "ok override on");
        } else {
            WriteResult(id, "error usage: camoffset <dx> <dy> <dz> [yaw_deg] | camoffset off");
        }
    } else if (!strcmp(verb, "watch")) {
        char mode[8] = {};
        unsigned ms = 0;
        int used = 0;
        if (sscanf_s(args, "%7s %u %n", mode, (unsigned)sizeof(mode), &ms, &used) < 2) {
            WriteResult(id, "error usage: watch <w|rw|x>[e] <ms> <addr> [addr...]  (e: a row per ecx)");
            return;
        }
        uintptr_t addrs[4];
        int count = 0;
        for (const char* p = args + used; *p && count < 4;) {
            char* end;
            addrs[count++] = (uintptr_t)strtoul(p, &end, 16);
            if (end == p) break;
            p = end;
            while (*p == ' ') p++;
        }
        std::string error;
        size_t modeLength = strlen(mode);
        bool byEcx = modeLength > 1 && mode[modeLength - 1] == 'e';
        if (byEcx) mode[modeLength - 1] = 0;
        WatchMode wm = !strcmp(mode, "rw") ? WatchMode::ReadWrite : !strcmp(mode, "x") ? WatchMode::Execute : WatchMode::Write;
        if (!WatchStart(addrs, count, wm, &error, byEcx)) {
            WriteResult(id, "error " + error);
            return;
        }
        g_watchId = id;
        g_watchEnd = GetTickCount() + ms;
    } else {
        WriteResult(id, "error unknown command");
    }
}

} // namespace

void ControlOnPresent(IDXGISwapChain* swapChain)
{
    TraceTick();
    DWORD now = GetTickCount();
    if (!g_watchId.empty() && (LONG)(now - g_watchEnd) >= 0) {
        WriteResult(g_watchId, WatchStop());
        g_watchId.clear();
    }

    if ((LONG)(now - g_nextPoll) < 0) return;
    g_nextPoll = now + 100;

    std::string id, cmd;
    if (ReadCommand(&id, &cmd)) Execute(swapChain, id, cmd);
}
