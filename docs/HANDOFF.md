# Handoff: JC2 native splitscreen (state as of 2026-11-04)

**Start with "Current state" just below.** The older sections are a running log, newest entries
mostly near the middle of the file, and some early details are out of date (e.g. P2's grapple is
now LB). Where they disagree, trust the dated entries and the source.

Read this first, then `README.md` and `docs/re-notes.md`. All game addresses are for the Steam
`JustCause2.exe` 1.0.0.2 (no ASLR, base 0x00400000).

## Current state (2026-11-04)

The installed PC build and `dist/JC2Splitscreen.zip` are the same DLL and match the source
(2026-11-04, ~21:35: race screen split-off, P2 race rings, decal-job guard; F8 overlay hidden at
start, thin black divider instead of the yellow one, "P2: press Start to join" hint for 8 s once
per launch after 2 s of gameplay, `JoinHintVisible` in overlay.cpp). Version renamed to v1.0
(dllmain.cpp log line, overlay.cpp status line, README.md, dist/package/README.txt) for the first
GitHub release (2026-11-05); README.md is now the player-facing page, the old developer notes are
in docs/DEVELOPMENT.md. That one zip is for Windows and the Deck (same DLL; it detects Wine at run
time); its files are in `dist/package` (dinput8.dll, jc2ss.ini, README.txt with both install
steps). To refresh: copy build\dinput8.dll into dist\package and re-zip it (README's first line
has the build date). Nothing since 2026-10-29 has been run on the Deck.

Done 2026-11-03/04, each with a dated entry further down (search the titles), all confirmed by the
user in play unless noted:
- Button prompts per player (floating B/Y, the "Stunt jump" panel).
- Camera collision with buildings for both cameras (game ray cast from the brain think).
- Grapple crosshair, crosshair colour and spread per half (colour with enemies untested).
- Ammo refill (F5) on P2's join/rejoin; F5 also refills grenades and C4 to the upgrade maximum.
  P2's grenade/C4 counts are copied from P1's every tick (user: keep it).
- Foliage: placed vegetation (palms) mirrored into P2's copy. OPEN: P2's small ground plants take
  the types of P1's surroundings ("Foliage per player" entry lists what is ruled out). Paused by
  the user until later.
- P2's motorbike camera (12.5 m, 0.6 m lower) and Aeroliner 474 camera (90 m, 10 m up).
- Flak cannons: P2's left stick turns them (look actions), P1's camera no longer jumps to the
  cannon (CCameraObject messages dropped for P2). Not done: P2's view behind the barrel; a "Flak
  Cannon" weapon panel shows in both halves for a while after getting off.
- Health: P2's base maximum, teleports keep health, "god off" restores health, P2's dead flag
  cleared on respawn (hits after a real death only drained his meter), his ring shows his real
  meter and empties when dead. LogPlayer2Health logs every P2 health change; TakeHitInPlaceHook
  logs hits the team check drops (none seen: P1's grenades already hurt P2; melee from P1 still
  doesn't).

Races (2026-11-04), entry "Races (2026-11-04": the split turns off while a script holds P1's
control (the race challenge screen; checked by screenshots), and P2 passes race rings himself
(user: P2 started and completed a race alone). Tools: tools/datadiff.py, objdiff.py, rtti.py
(vtables from RTTI names), ringtrig.py, racetrig.py, p1pos.py, ghidra/RefsTo.java.

Open problems:
- Crash 2026-11-04 14:35 on a physics thread in HeightFieldSampleHook (terrain2.cpp), jump to
  0x3F400012, around P2 dying and respawning far away; suspected use of P2's height buffers while
  his stream cells unload. Not solved (dump in %LOCALAPPDATA%\CrashDumps).
- Crash 2026-11-04 16:50:55, the same as the 2026-11-03 15:22 one (entry "Crash (2026-11-03"):
  access violation at 0x009332F1 in FUN_00933260 (reads a triangle's vertices past the vertex
  buffer) on a worker, chain 0096CF48 (FUN_0096CEC0, collects triangles near a point: decals /
  impact marks) -> 0092C457 -> 00930C41 -> 00954CF9 -> 009565A5; no mod code on the stack (dump
  JustCause2.exe.18124.dmp). Both players at 9324 214 1149 (far east); just before it: a strong
  rumble burst on P1's pad (an explosion), the split off 16:50:45-16:50:51 (menu or cutscene).
  Both times right after an explosion near the players: user thinks a car blew up this time (the
  first time a jet). So likely impact marks on a vehicle wreck's mesh (the wreck swaps models when
  it blows up; the vehicle LOD / sleep hooks in world2 touch vehicles: VehicleLodHook picks the more
  detailed of both cameras' levels, so a mark may index a mesh LOD that isn't the one loaded).
  Repro idea: blow up a car near both players with "world2 lod1" (game's own LOD) vs default.
  Third one 2026-11-04 17:38:56 (user: P1 driving away from exploded stuff; dump
  JustCause2.exe.10336.dmp): 0x00946728 in FUN_009466E0 (vertex read at buffer + 0x151998) <-
  FUN_00940A80 <- FUN_0063DBE0 <- FUN_00841AD0. All three are the game's decal jobs, registered
  by FUN_00763280: GenerateDecalsTask FUN_006F95D0 (the first two crashes),
  GenerateDeformDecalsTask FUN_00841AD0 (this one), GenerateSkinnedDecalsTask FUN_00828890.
  GUARDED (not a root-cause fix): EnsureDecalJobGuards (player2.cpp, every second, any time)
  points those job handlers (+0x20, heap data like the player job) at DecalJobGuard, which catches
  an access violation, skips that decal and logs "Decal job ... faulted". Checked: guards attach
  at load ("Decal job ... guarded"), P1's rocket still leaves a scorch mark, no faults logged.
  jc2ss.ini [Debug] SkipDecalGuard=1 turns it off. Root cause still open (LOD hook suspect).
- Ground-plant types for P2 (above). Traffic/NPCs near P2, two-monitor mode: not started.

Next work, in the user's order of mention (details in the "NEXT" entries, search for them):
1. Traffic and NPCs near P2. Nothing written yet. First run `tools/tests/popcount.py` with the
   players more than 1 km apart in a city to measure what is missing.
2. Two-monitor mode (a viewer's idea): an optional setting, split stays the default.
3. Older open items: vehicle weapon panels aren't refreshed per player, P2's zoom keeps its stance
   mode through a switch to a two-handed weapon, rumble routing, P1's melee on P2.

Test helpers added 2026-11-04: places "cam_wall", "airport", "flak_p2" in tools/tests/places.txt;
tools/tests/grenadep2.py <n> (P1 drops a grenade beside P2; real damage); "split camwalls",
"split bikecam", "split bigplanecam", "p2hud prompts", "terrain2 placed|vegstay|vegregen|wholezone".
After tests, turn "god on" off: the user plays in the same session.

Done on 2026-10-29/30, each with a dated entry below: per-player hijack and code-entry prompts, P2's
pad matching P1's layout (`tools/tests/padbinds.py`), HUD per player (health ring, panels, switch
popup), own FOV and aim zoom with stance (right stick click, also on stunt positions), holstered
weapon sway, 15 m car camera, audio (two FMOD listeners plus the game's own listener).

Confirmed by the user: prompts, HUD and popup, ammo counters (after the swap fix), roll, zoom and
stance, weapon sway, car camera. The user moved on after the audio fix with "great job", but never
said outright that P2's sounds are right. Not tried: zoom on a plane, firing accuracy from the
stance, ambient sound near P2 (other listener reads get P1's view, so it may follow P1).

The Deck zip has not been run on the Deck: everything since the last zip is untested under Proton.
`jc2ss.ini` `[Debug] SkipAudio=1` turns the audio hooks off.

Working with this user:
- They test by ear and feel; ask them what they hear or feel, and give short test steps. They may
  have the game open: closing it for a build is allowed, but say so when it happens.
- Check claims against a solo reading. The swapped ammo counters came from assuming what a number
  meant; `p2audio dump`, `p2hud` and `p2 rollcheck` are there for measuring instead.
- `Load Game` in the menu wipes autosave data (see memory): don't accept it. A mission save with
  a countdown: `tools/tests/pausefast.py`, then Abort Mission.
- Build and test flow: `tools/tests/hudboot.ps1` closes the game, builds, installs, loads and
  joins P2. Run it in the background; a load takes over a minute, and watch for it stopping at the
  title screen. The test diagnostics ("p2 roll", "p2 rollcheck", "p2audio mode", "p2audio dump")
  are still in the source.
- `near` is a macro in the Windows headers; don't use it as a name.

## Goal

Local splitscreen for Just Cause 2: Player 2 on controller 2 gets their own Rico, their own camera
in the bottom half of the screen, and independent controls. The split only turns on after Player 2
presses Start on controller 2. Cutscene handling is deferred until it becomes a problem.

## What works

| Piece | Where | Status |
| --- | --- | --- |
| Mod loading (dinput8.dll proxy), Present hook, overlay | `src/dllmain.cpp`, `src/hooks.cpp`, `src/overlay.cpp` | solid |
| Join: Start on pad 2 spawns P2's Rico next to Rico 1, drives him, split on (Back+Start leaves) | `AutoJoinTick()` in `src/player2.cpp`, `src/splitscreen.cpp` | works |
| Two-view split by alternating frames + per-view motion blur | `src/splitrender.cpp` | solid (each half is a cropped middle band) |
| P2 orbit camera (right stick, up = look up) | `MakePlayer2Camera` in `src/splitrender.cpp` | works |
| **P2's own Rico**: drawn, animated, character driver every frame (~145/s) | `StartDriving()`, `PlayerJobHook` | works |
| P2's own input context (P1's devices can't reach P2) | `GivePrivateInput`, `WritePlayer2Input` | works |
| P2 movement relative to P2's camera; jump; weapon equip (D-pad) | `WritePlayer2Input` | works |
| P2 weapons on his back + pistol in hand on join/teleport/respawn (auto equip) | `EquipPlayer2` | works |
| **P2 shoots along his own camera** | `BrainThinkHook`, `LineOfFireHook`, `AimFilterHook` | works (`tools\tests\p2shoot.py`) |
| P2 weapons, ammo, health (copied from P1), parachute | `StartDriving`, `GiveWeapons` | works |
| **P2 grapple** (LT): hook fires from his hand, reels him in; P1 and P2 can grapple at once | `GiveGrappleActions`, `ShareGrappleParams`, `SetupPlayer2Launcher`, grapple runner in `Player2Update` | works (`tools\tests\p2hook.py`) |
| P2's shots don't shake P1's camera or rumble P1's pad | `WeaponUpdateHook` | works (`p2shake.py`) |
| P2 jumps/falls with Rico's gravity (-21, not -9.82) | `MatchPlayer1Gravity` | works (`jumparc.py`) |
| P2's death doesn't trigger the game-over screen / save reload | `CharacterDeathHook` | works (`p2 kill`; `p2 kill raw` shows the game over) |
| P2 respawns next to Rico 1 five seconds after dying | `RespawnTick` | works |
| P2 brought along when P1 jumps > 50 m (respawn after death, load, fast travel) | `FollowPlayer1Jumps` | untested |
| Map / pause menu: full screen (no split), P2 paused | `IsInGameplay` in `src/splitscreen.cpp`, `InGameplay` | works |
| F5 refills ammo for both players | `RefillAmmo` | works |
| Automation (launch, load save, keys, mouse clicks, screenshots, watchpoints, traces) | `tools/jc2ctl.py`, `src/control.cpp`, `src/watch.cpp`, `src/trace.cpp` | solid |

**Open issues:** see "Where it stopped".

## How to test

```
powershell -ExecutionPolicy Bypass -File tools\cycle.ps1   # build, restart game, load autosave
python -I tools\tests\jointest.py      # join (as if Start on pad 2): spawn + drive + split
python -I tools\tests\p2controls.py    # movement vs P2 camera at 0/90 degrees, jump
python -I tools\tests\inputsplit.py    # P1 keyboard doesn't move P2, P2 input doesn't move P1
python -I tools\tests\p2rate.py        # P2 updates per second (should match the frame rate)
python -I tools\tests\respawn.py       # zero P2's health, expect respawn next to Rico 1
python -I tools\tests\maptest.py       # pause menu + map open/close with P2 in the game
python -I tools\tests\p2shoot.py       # P2 fires at two camera headings, gun follows P2's aim
python -I tools\tests\p2holster.py     # P2's holstered weapons sit on his body, not his feet
python -I tools\tests\shared.py        # objects both characters point to
python -I tools\tests\p2hook.py        # P2 fires the grapple: P2 pulled ~24 m, P1 stays (run from a fresh start)
python -I tools\tests\p2grapcam.py     # P1 view turn while P2 grapples from 4 spots (expect <0.1; reset P2's camera first)
python -I tools\tests\p1aimsp2.py      # P2 in / beside P1's crosshair, grapples each time (expect P1 aim never on P2)
python -I tools\tests\p2grapdir.py     # P2 grapples in several camera setups; movement toward the aim point
python -I tools\tests\p2grapshake.py   # camera shake amplitude while P2 grapples (expect 0)
python -I tools\tests\p1grapchute.py join 1.1   # P1 lifted, grapples, parachute: camera setups over time
python -I tools\tests\p2shake.py       # P1 camera jitter while P2 shoots (expect 0)
python -I tools\tests\jumparc.py       # P1 vs P2 jump arcs (expect both ~1.6 m peak at ~0.6 s)
python -I tools\tests\p2kill.py        # P1 shoots P2 (P1's bullets don't hurt P2; use `p2 kill`)
python -I tools\tests\p1grapple.py f   # P1 grapples ahead; P1 key F = FIRE_GRAPPLE
python -I tools\tests\p2grapple.py / p2aim.py   # older grapple/aim experiments
```

P1 keyboard: F = FIRE_GRAPPLE (0x51), E = use/exit vehicle/grappling action, right mouse =
FIRE_LEFT/throw grenade, space = jump, ctrl = crouch, shift = dash/zoom.

Mod commands (`python -I tools\jc2ctl.py mod <cmd>`): `p2 join|leave` (as if Start / Back+Start on
pad 2), `p2` (status, incl. both cameras' ground axes), `p2 move <right> <fwd>` (test stick),
`p2 press <hexAction> [ms]` (hold one action), `p2 turn <deg>` (turn P2's camera), `p2 tp <r> <u> <f>`,
`p2 press <a>+<b> [ms]` (hold two actions, e.g. `B+D` = right trigger),
`p2 firecheck` (logs both characters' fire conditions), `p2 gear` (re-run create-gear),
`p2 pitch <deg>` (set P2's camera pitch), `p2 kill` (P2 dies via the character death vfunc) / `p2 kill raw` (same without our hook: game over), `p2 full on|off`
(full CPlayer::Update vs driver only, default off), `ammo`, `split on|off`, `trace <hex> [ms] [thread]`,
`watch w|rw|x <ms> <addr...>`, `screenshot <path>`. Older: `p2 spawn`, `p2 drive`, `p2 possess <hex>`,
`p2 rico`, `p2 enable`, `p2 look`. `jc2ctl.py` also has `click()` for mouse buttons.

**Gotcha:** the game pauses when its window loses focus; `jc2ctl.py focus` before acting.

## Hard rules learned (don't relearn these)

- **Never inline-patch game code** (except the motion blur hook at 0x0096A420). Patching makes the
  game exit (Steam anti-tamper). Data hooks are fine: vtable slots (CCamera::Update, brain think)
  and heap function pointers (player job handler).
- **Hardware breakpoints must stay short-lived and few.** Steam's anti-tamper quits the game with
  status **0x8000DEAD** (via ExitProcess, no crash dump) when its check runs on a thread that has a
  debug register set. Breakpoints on every thread killed the game within seconds or when opening the
  map. A breakpoint on one thread for a few frames (spawn, `watch`, `trace`) is OK.
- **The player update is a job** run by whichever worker thread is free (a different thread each
  frame). Dispatcher FUN_004D3500 looks up `{type, handler*}` in `[0x0118FEA8]` entries from
  0x0118FEAC; generic handlers call `handler+0x20` (__cdecl, 1 arg). The player job's target is
  FUN_006174F0 (calls CPlayer::Update at 0x0061751A). `PlayerJobHook` replaces that pointer and runs
  P2's update right after P1's (`EnsurePlayerJobHook` re-hooks if the game rebuilds the handler).
- Any hook must pass the original EAX return value through.
- Never create a dummy D3D device (raced the Steam overlay).
- Never share `CCharacterRules` (character +0x48C) between characters.
- Game UI mode: `[0x011958C4]` 3 = gameplay, 4 = pause menu, 5 = map; `[0x0105F754]` pending switch
  (6 = none). P2 only updates in gameplay (`InGameplay`).
- F1 is the game's map key (ammo refill is on F5).
- **Never move the physics origin away and back.** Creating a player's character (FUN_00664440 at
  0x006657A9) shifts the Havok floating origin (FUN_009B3950, physics `[0x0118FFF4]`, origin +0x28)
  to the new character's start point (0, 0). The shift drops loaded static bodies (the military
  base's helipad at 903 318 -4250: both players fell through it after P2 joined there) and
  shifting back never re-adds them. Re-adding the 218 dropped bodies by hand flung both players
  4 km. Spawn() now re-arms the constructor's breakpoint slot at FUN_009B3950 (`OnOriginShift`) and
  hands it the current origin: no shift at all.

## Key findings (Player 2)

Spawning / setup:
- `CCharacterManager*` = `[0x0118FB24]`; player slot = manager+0x3570. `CreatePlayer` = 0x00673C40.
  During it: zero 0x01190AEC, fill the CCharacter ctor descriptor (0x007EBD20) with P1's info
  strings, fix the rule count at the body builder (0x007E6C60) (breakpoints, main thread). `Spawn()`.
- Add to world: CCharacter+0xF0 interface (vtable 0x00E5602C) **slot 3** (FUN_006C8370; registers
  the world handle at +0x600, physics, flags 0x20|1). Slot 4 removes from world.
- Place in world: CCharacter vtable slot 30 (FUN_005AE3A0); it also runs the character reset
  FUN_006E0100 (used for respawn).
- Weapons: manager `[0x011917F4]`, create FUN_0072FB20(mgr, sp*, id), set slot FUN_007D60C0(inv,
  sp*, slot); weapon id = `*[weapon+0x224]`. Inventory hand mode +0x64 (1 = slot 0, 2 = slot 1,
  3 = slot 2), reserve ammo +0x78 per type, clip rounds weapon+0x1E8.
- Health +0xE8, max +0xEC (P2 gets P1's; a runtime player gets 50).
- Gear (FUN_005BEED0): parameter file paths CPlayer+0x2A0 (ParachuteParams) / +0x2C0
  (GrapplingParams) get the game folder prefixed and stored, so copied absolute paths got prefixed
  again; P2 gets relative paths. **The files still don't load mid-game** (they're in archives), so
  P2 shares P1's parsed parachute params (CPlayer+0x674 +0xC..+0x30). Grapple, see below.

Grapple (all needed for P2's hook to work):
- Action set char+0x628 (CGrapplingActionSet): 6 x {shared_ptr<CContextAction>, action id}, enable
  bytes at set+0x48 (set by FUN_00732C90(char, index, on)). FUN_007D5320(char) (in CPlayer::Update)
  queues enabled actions on the character; the controller update (FUN_00882070 -> FUN_008DBF90
  "can start" slot 4 -> FUN_008D2BA0 "pressed" slot 5 / start slot 6) starts one. `Player2Update`
  now calls FUN_007D5320 for P2 after the driver.
- Actions keep per-use state, so sharing P1's let only one player grapple at a time. Each class has
  a creator (CContextActionCreator<T> vtable slot 1 = create(sp* out), slot 2 = size; `this`
  unused). `GiveGrappleActions` creates new ones and copies the file-loaded base fields +0x18,
  +0x24 (input action), +0x28, +0x30.
- CGrapplingDevice (inventory +0xD0) params: range +0xD8 (P1 120 m, default 10 m), limits
  +0xD4..+0x114, effect ids +0x218.., CEffect shared_ptrs +0x238..+0x26C (`ShareGrappleParams`).
  Its rope textures (+0x140..+0x164, +0x1A4..+0x1B8) are still missing on P2 (visual only?).
- Launcher = inventory slot 3 weapon (id 52). Rico's loadout (FUN_00540C40) calls FUN_0071FF60
  (weapon, 0x18) after storing it: activates it (+0x3E bit 0 on the +4 interface; inactive weapons
  ignore trigger pulls) and attaches it to the left hand; then weapon vtable slot 24 (false).
  Without it P2's hook spawned at his feet. `SetupPlayer2Launcher` (per tick, checks +0x254).
- The launcher only fires for a player-driven owner (char +0x728), so `WeaponUpdateHook` leaves it
  player-driven and zeroes its shake/rumble (desc +0x2F4/+0x2F8) during its update instead.
- Hook projectile CGrapplingHook vtable 0x00EBA84C (update slot 5 FUN_005F28E0, owner +0xE4);
  attach handler FUN_00538470 (device), which enables reel-in via FUN_007F3DB0.

Death / physics:
- Character death = CCharacter vtable slot 24 (0x00EB887C, FUN_007DD0A0); posts "ply.killed"
  (mission manager: death screen + reload) only when +0x728 is set. `CharacterDeathHook` clears it
  for P2. P1's bullets don't damage P2 (and fall damage doesn't exist for Rico).
- Rico's physics body (char+0x230 CPfxCharacterInstance) gravity +0xBC = -21 on P1, default -9.82
  on P2 (`MatchPlayer1Gravity` copies it whenever P2's is the default).

Update / input:
- CPlayer::Update FUN_00672CE0 = driver FUN_00853AE0 + gear/grapple-actions (FUN_007D5320 runs the
  grapple action set; it read a null action until the set was filled) + HUD bits. P2 gets only the
  driver by default: with the full update P1's camera misbehaved while P1 grappled (P2 evaluates the
  grapple actions it shares with P1, and the update posts player-wide HUD events).
- Objects P1 and P2 share by design: the COnFootController (controllers come from a registry by
  type hash, `CCombatantComponent` at char+0xA70; per-character state is char+0xA34) and the
  CAnimationSets.
- Inventory: don't copy P1's hand state (+0x64..+0x74, +0xD4..+0xE0); it left P2's upper body stuck
  in a non-ready aim state (0x15A). New weapons lie at the feet until an equip (EQUIP_RIGHT_SLOT
  0x72) attaches them; placing in world resets the hand state, so P2 re-equips after teleports.
- Input context = `*(*(CPlayer+0x188)+4)`; per action a list of per-device entries, highest wins
  (FUN_009828A0). P2's CSteeringPlayer points at a private copy (`GivePrivateInput`).
- **Action ids** = index in the action name table at `0x01071DE8` (MOVE_FORWARD 0x1E, BACKWARD 0x1F,
  MOVE_LEFT 0x20, MOVE_RIGHT 0x21, JUMP 0x23, FIRE 0x0B, FIRE_LEFT 0x0C, FIRE_RIGHT 0x0D, RELOAD 0x10,
  PARACHUTE_OPEN_CLOSE 0x47, FIRE_GRAPPLE 0x51, EQUIP_* 0x71..0x75, ...). Entry state byte: 1 idle,
  2 pressed this update, 3 held, 4 released. The on-foot fire handler reads FIRE_RIGHT (0x0D).
- Movement conversion uses P1's camera orientation (not the main camera matrix), so P2's stick is
  rotated from P2's camera frame into P1's (`SplitViewGroundAxes`).
- Aim / shooting (all hooks are vtable slots = data):
  - Brain (CPlayer+8, vtable 0x00E4CD64) slot 1 = think FUN_005DF870 (main thread): if byte
    [0x0106EC81] it has the camera system re-apply the camera controller (follows P1) to the main
    camera, then aims the aim control (CPlayer+0x184) with FUN_007047A0 (ray from camera
    `[0x011919F4]+0x10`), copies the point to the controller state (char+0xA34 +0x44) and runs
    FUN_0075FA20. `BrainThinkHook`: for P2 the refresh byte is cleared and P2's view is swapped into
    the main camera for the whole think. (Fixing the aim after the think made it flip every frame,
    restarting the aim animation, so the gun never got "aim ready".)
  - Brain slot 30 = FUN_00590140 line-of-fire: short ray from the gun hand bone toward the target;
    a hit sets brain+0x6D "blocked" (no shots). The rays use CPlayerAimRaycastFilter (vtable
    0x00EB4D74, slot 2 FUN_0050B410), which only skips the manager player's body, so P2's own body
    blocked him. `AimFilterHook` skips P2's body (hit +0xC == P2's CCharacter) for **every** ray:
    P1's aim also stopped on P2, and then P2's grapple hook hit P2 himself.
  - P1's lock-on (aim mode 2, FUN_007A0B90 in the think) snaps the aim (aim control CPlayer+0x184,
    target entry at +0x3C, point +0x44) to a world object entry near the crosshair even when the ray
    misses it; FUN_00619860 skips entries of type 3 (entry +8 & 0x7F). P2's entry = his world handle
    (char+0x600); `BrainThinkHook` gives it type 3 during P1's think only. Side effect of both: P1
    can't target or grapple P2 (his bullets never hurt P2 anyway).
  - Firing chain: FUN_0075EAD0 (worker, controller update) sets the fire request (state +0x258) on a
    fresh press of FIRE_RIGHT 0x0D; FUN_006DFA90 (main) waits for "aim ready" (FUN_0067D1E0: upper
    body state 0x15B/0x15C fully blended) then FUN_00770670 -> FUN_005DBBD0 -> weapon pull trigger
    (CWeapon(+0xA0) slot 1 FUN_0052F880) -> weapon update FUN_0083F860 -> FUN_006EC6E0 (rounds -1).
  - A character the player aims at gets state +0x120 set every frame (FUN_00770A10, uses the
    manager player's aim target), and +0x120 > 0 blocks its trigger. With P1 no longer targeting
    P2 this shouldn't hit P2 any more (P2 shot fine with P1's crosshair on him in `p1aimsp2.py`).
  - P2's camera starts nearly level (pitch 0.08): pitched down, his crosshair is the ground in front.
- World streaming (nothing changed yet): everything streams around ONE point, the active camera
  CCamera = [[0x011919F4]+0x10], position +0x54. The world update FUN_00781d60 (called once per
  frame from FUN_00745310) passes it to: CGameWorld [0x01191720] slot 0x94 (FUN_007439F0),
  FUN_009A1940 (Havok floating origin: shifts when the camera is > 1000 m from it, FUN_009B3950),
  CAreaSetsManager [0x01191660] slot 0x38 (FUN_00859200), CStreamPatchManager [0x01190868] slot 1
  (FUN_00A49320: 512 m cells, 8x8 ring of slots indexed cell&7 around the camera's cell, loads
  via FUN_00A466C0), and CLandscapeManager [0x01191C48] (FUN_00A751B0 reads the camera itself ->
  FUN_00A92CA0: terrain clipmap levels at 512 m (16x16 grid), 128 m, 64 m and 8 m cells, each
  re-requested (FUN_00A88810) when the camera's cell changes; a camera jump > 512 m in one update
  resets all levels). Measured with P2 teleported ahead of P1: at 150 m P2's ground texture is
  already blurry (objects, cars, grass fine); 300-600 m trees and buildings still there, ground
  blurry; 1.5 km trees gone, only the coarse terrain. Feeding two centers into these
  single-center structures would evict each other (ring slots, clipmap resets), so real
  per-player streaming means a second copy of the landscape state (incl. its GPU textures).
  The terrain detail levels are one image each, rebuilt by a job (FUN_00A88810) for the new
  center, not tile lists, so P2's tiles can't be added next to P1's.
  CGameWorld slot 0x94 only picks the region the camera is in (FUN_007F7440, for spawning).
  Area sets: 15 regional sets + General (table at manager +0x84, 0x58 each, keyed by a color
  from an area-set map, slot 0x2C), picked from the 4 corners +-1000 m around the camera snapped
  to 1024 m (slot 0x28 FUN_008A5CC0), at most 2 loaded (slots at +0x68; FUN_008B37B0 unloads /
  loads via CGameWorld slots 0x44 / 0x3C). Tested by passing P2's position to slot 0x38: P2 at
  (2197, -1263) got AreaSet05 loaded in place of P1's AreaSet11, and neither view changed (P2
  still only coarse terrain, P1 unchanged). So area sets aren't what P2 is missing; the hook
  was removed.
- Independent terrain per player (user wants this; midpoint rejected). CLandscapeManager
  (singleton [0x01191C48], 0x14888 bytes, created by FUN_006E7830, ctor FUN_00A5E1D0, dtor
  FUN_00A63500) creates in its init (vtable 0xF2005C slot 2, FUN_00A74F80) the singletons
  CClimateZone [0x011907D0], CTerrainTexture [0x01191D98], CPatchManager [0x01191834],
  CVegetationManager [0x011902F0], CTerrainMapGame [0x01190290], CTerrainData [0x011F9C68],
  CMapTexture [0x01191224], CGameZoneMap [0x01381AF8], CWaterPatchManager [0x011919C0],
  CStreamPatchManager [0x01190868] (made in the ctor), plus an object at manager +0x28.
  Stream patch data lands in 8x8 slot rings (cell&7) inside CTerrainMapGame (0x20/slot),
  CTerrainData (0x10/slot), CGameZoneMap (0x100C/slot). Terrain code 0xA3F000-0xAA0000 touches
  ~750 globals in .data (tools/ghidra/DataRefs.java), clustered at 0x01089EB0-0x0108CD40,
  0x012AB208-0x012B9490, 0x01365490-0x013F4574 and others.
  1 s rw watch on the singleton pointers: read by the game thread (3488: world update,
  gameplay terrain queries e.g. 0x005226BB 2562 hits/s), the render thread (28428: vegetation
  0x00A7D948 8241 hits, water 0x00A7F2E1, landscape 0x00A824BC), loader workers (12852, 24432,
  26408, 3752) and another thread reading water patches (22216, 0x0059FF64, likely physics).
  So a per-view state swap has to stop the render thread and workers while swapping, and P2's
  own terrain queries (collision, feet) need his state while P1's is active.
  (An execute watch on 4 render-block slots hung the game once right after a restart; a retry
  worked with no hits. The game stops rendering when it loses focus and pauses: jc2ctl focus,
  then esc.)
- Raising P1's detail distances, tested live with P2 at 380 m and 1.5 km (screenshots compared),
  changed nothing visible for P2: FUN_00A92CA0 is CVegetationManager's update ([0x011902F0]);
  its quality-3 distances 183/188 (0x00F21C04/00) -> 600/610, the shader constants
  450/350/480/120 (0x00F20A20..2C, copied to [0x01191AB4]+0x14..) x3, and CLandscapeManager
  +0x4B0..+0x4BC (0.32, 0.95, 0.6, 10) x4. The loaded detail window comes from fixed-size
  slot grids, not from a distance setting. tools/procmem.py write() now unprotects .rdata.
  The exe has a `/lodfactor=n` command-line option ("Geometric detail 0-3, default 0", from its
  /h help text) - not tried yet.
- Frame pipeline (for per-player terrain): main loop FUN_00421B80 -> FUN_004512A0 -> game update
  FUN_00745310 (world update FUN_00781D60 in state 3) -> FUN_005D9D50 -> graphics engine
  ([0x0118FBDC], vtable 0xF2292C) slot 0xC = FUN_00942F60, the frame hand-off: advances the
  frame counter [0x0108C594] (render data rings are %3), waits for the previous render job
  (handle at engine +0x28, FUN_004ED210), copies the main camera to the render camera, then kicks
  the render (engine +0x1480 == 1: SetEvent(engine +0x1484) for the render thread; else a job).
  So render N runs while the game updates N+1. Plan: run the landscape update for a view inside
  a hook on slot 0xC (0x00F22938), after the previous render finished and before the next is
  kicked, with that view's terrain state swapped in.
- **Player 2's terrain works (src/terrain2.cpp, test command `terrain2 on`, off by default).**
  P2 gets his own vegetation (trees, plants) and ground mesh at any distance from P1 (tested
  400 m, 800 m, 1.5 km: full detail, matches the reference of P1 standing there). ~1.3 ms per
  frame, FPS unchanged (144), stable for minutes.
  - Design: the landscape no longer runs in the world update (CLandscapeManager [0x01191C48]
    +0xD = 0 gates FUN_00A751B0 update and FUN_00A67100 draw lists). It runs at the frame
    hand-off (hook on graphics engine slot 3, 0x00F22938 -> FUN_00942F60), after waiting for
    the render job in flight (engine +0x28), for the view about to be drawn: that player's
    vegetation + ground state switched in, the landscape camera pointer ([0x011919F4]+0x10)
    pointed at a snapshot of P2's camera for his turn, then the game's own update and draw
    lists, then all landscape jobs waited (FUN_00A5E360, FUN_00A7B650, [0x0108AB10]). On P2's
    turn only the per-player parts of FUN_00A751B0 run (detail regions FUN_00A61220 on
    [0x0136582C], patches FUN_00A6A970/FUN_00A638C0, vegetation FUN_00A92CA0); water
    (FUN_00A88D40), climate zone (FUN_00A69370: loads the zone's atlas and vegetation sets) and
    sky stay P1's. After P2's turn the vegetation goes back to P1's state at once (the render
    job reads only the manager's +4/+5 flags, same in both), so world-update code that touches
    vegetation always hits P1's; the ground stays P2's until the next hand-off (the render job
    reads the terrain block table and the patch cells; forest blocks refer to cells).
  - Vegetation copy (kVegetationState): created as before (job type count trick: the job type
    table at 0x0118FEAC is full; containers cleared first; FUN_00A98F10), sharing P1's
    CVegetationSetCollection [0x01191378] (only the climate zone loader fills it) and the draw
    types the engine keeps one of per name (kVegetationShared: renderers, impostor/leaves/fin
    types, render states; P2's duplicates are dead objects: zero-size CreateBuffer, null
    vtable in CRenderBlockLeaves' type). Own: cell lists 0x0138AAB0 (0x1690), 0x0138C140 (vector)
    and 0x0138C1A8 (bit vectors; 0x0138C190..A4 between are shared textures!), the impostor
    staging pointers 0x01381FBC.., the 3 per-frame objects 0x01382044..4C, and its own
    CBreakableTreePatchHandler (new(0x30) + FUN_00529EC0, like FUN_00812C50).
  - Ground copy (kGroundState): CPatchManager (new(0x8E0) + FUN_00A69FB0, cells cleared,
    FUN_00A58150 on 0x01368150 for a fresh 704-block table + free list, FUN_00A69ED0 with its
    two pools). Per copy: [0x01191834], 0x0108AB04 (handles), 0x012AB570 (budget + 704 cells),
    0x01365498 (bits), 0x013657F4 (flags, pools), 0x01368150..0x0137BAD4 (block table, free
    list, top). P1's table is full (top = -1), so the copies can't share it.
  - Calling conventions that bit: FUN_00A638C0 / FUN_00A63910 are `ret 4` (camera arg).
  - Probably fixed by the per-player climate zones (2026-10-27, "Other open issues"); re-check.
    Open: with P2 more than ~600 m away, P1's surroundings get P2's area's plants (red
    heliconias, big leaves) near P1; not at 400 m. Ruled out: P2's job kinds, pools (each copy
    has its own; pools are fixed size, 195x195 etc.), climate zone, impostor staging, frame
    objects, breakable handler, world-update vegetation calls. Remaining shared thing: the set
    collection (P2's own isn't loaded: crash in FUN_00A85EB0). Also P2's water/sky/climate are
    P1's.
  - Sky: with only the per-player parts on P2's turn his sky was black (the user saw it; zone/sky
    state was P1's). Now P2's turn runs the whole FUN_00A751B0 (default; `terrain2 partial` for
    the old way). Caveat: FUN_00A69370 -> FUN_00A557A0 returns a new zone whenever the camera's
    zone differs from the current (+0x78) and then loads that zone's atlases, so with the players
    in different climate zones it reloads every frame. Fix: a second CClimateZone (ctor
    FUN_00A706E0, 0xDC bytes, init FUN_00A74F20; singleton [0x011907D0], also used by
    FUN_00A85EB0/FUN_00A92210/FUN_00A92CA0/FUN_00A93450) switched with the rest.
  - Ground detail, cause found: P2's patch manager only had levels 9-14 (blocks scan:
    no 16 m blocks near P2; cells.py dumps the cells per level). FUN_00A67610 stops loading
    (budget [0x012AB570] = 0) at the first detail-level cell whose 512 m stream cell isn't
    resident (bitmap 0x012AB250, 64x64 bits, set by CStreamPatch FUN_00A3F7B0 on load); level 9
    spans +-2 km around the camera, the stream window +-2 km around P1, so P2's level 9 always
    hit missing cells and the finer levels were never built. Interim (default): the bitmap is
    all ones during P2's turn -> all 11 levels around P2, full detail (screenshots res_*). Cells
    outside P1's window read another cell's ring slot (cell & 7): fine to ~1.5-2 km, broken at
    3 km (black ground, floating grass; P2 fell to sea level: no ground collision there either).
    Real fix: P2's own stream state. Load completion path: CStreamQueueItem (async file) ->
    CStreamPatch slot (vtable 0xF0EF38) slot 1 FUN_00A3F710 -> slot 4 FUN_00A3F7B0 (bitmap bit,
    then [0x012AB24C] provider ->vt0(cell, data) -> the data handlers, which keep 8x8 rings:
    CTerrainMapGame [0x01190290], CTerrainData [0x011F9C68], CTerrainTexture [0x01191D98],
    CMapTexture [0x01191224], CGameZoneMap [0x01381AF8]). A second CStreamPatchManager has its
    own slot objects, so hooking CStreamPatch slot 1 can route its completions to P2's handlers.
  - Ground detail: the user saw P2's ground still blurrier than P1's (compared with
    sg_off_level: P2's view with P1 standing next to him). Fixed only when the stream patches
    (512 m cells; `terrain2 stream`, set BEFORE `terrain2 on`) are centred on P2 too; the user
    confirmed it looks right that way. So the stream patch data is per player as well (all 64
    cells of the 8x8 window load, but something in them depends on the distance to the centre;
    CTerrainMapGame +0x804 is a 512x512 map around it?). Next: a per-player stream patch state
    (async loads: completions must reach the right copy). Moving the stream centre while the
    copies run unloads cells P1's blocks use (crash in d3d11 binding a terrain texture).
  - terrain2 is on by default: it runs while P2 is joined (SplitscreenPlayer2Status), stops when
    he leaves and resumes with the same copies when he rejoins (`terrain2 off` to compare).
  - DONE: P2's own stream patches (default; `terrain2 nostream2` before the first `terrain2 on`
    for the old residency spoof). Verified 3.5 km from P1 on a plateau (4320 1241 -2912, a snowy military base: P1 swapped
    there gets attacked, pick another spot for long tests): his
    terrain renders, he walks on it at the right height; `p2 swap` both ways fine; ~1.2 ms per
    hand-off. How (terrain2.cpp):
    - CreateStream2 (with the ground copy, first hand-off): new CTerrainTexture (0xD40,
      FUN_00A8F600), CTerrainMapGame (0x80808, FUN_00A74E40; it frees and reloads the shared
      displacement table [0x01365824] -> P1's pointer is put back), CTerrainData (0x4A4,
      FUN_00A64140), CMapTexture (0x510, FUN_00A67BF0), CGameZoneMap (0x40308: FUN_00A581A0 on
      +4, vtables +4 0xF2007C / +0 0xF20088), CStreamPatchManager (0x881C, FUN_00A42F30(this,
      [0x012AB24C] reader)), then FUN_00A43120 (<base>.dat global load: sets CTerrainData +0x4A0).
    - kStreamState (manager, 0x012AB248, residency bits, map, data, zones) is P2's only during his
      turn: game logic and physics read heights/zones at any time (keeping P2's in kills P1 at
      once). The two texture handlers are in kGroundState instead (the render job draws with
      them; with them reverting, P2's terrain outside P1's window was black).
    - P1's stream update (manager slot 0xF0EF60) is deferred from the world update to P1's turn.
    - Loads: the stream queue (FUN_004E0780, worker threads, ~one file per 50 ms) calls
      CStreamPatch slot 1 (0xF0EF3C) with (queue item = name string first, data, size); AL
      nonzero takes the item, zero redelivers it at once (the first hook returned void -> flood).
      The hook copies the file, clears the slot's loading flag (+0x18) and returns 1; the owner's
      turn replays it (owner = slot inside P2's manager) before that manager's update, if the
      slot still wants that cell (+0x10/+0x14).
    - Collision: physics (CCustomSampledHeightFieldShape 0xF27970, FUN_00991900) and rays
      (FUN_00A64A90) read [0x01190290] slots by cell & 7 without checking the cell
      (FUN_00A5F600 -> FUN_00A56400, slot +0x18 heights 0x8820 B, +0x1C materials 0x4840 B).
      Between turns, the slots of P2's cell and its 3 nearest neighbours in P1's map point at
      P2's buffers unless P1's 3x3 cells use the slot or it already holds the cell
      (ApplyPlayer2Collision; restored at the start of every turn and in Stop). `terrain2
      nocollision` to compare: P2 falls through the plateau / stands on false ground.
    - Caveats: P2's cell 4 km (8 cells) from P1 shares slots with P1's own -> no collision for P2
      there. Queue items are named by cell ("XXYY"); a cancel by name (FUN_00A42E90) from one
      manager could cancel the other's request for the same cell (not seen). Climate zone still
      P1's (P2 on the snowy plateau looked blue-lit). Mesh lookup FUN_00A64370: levels < 7 always
      use the flat default grid (heights from the patch height texture), not a bug.
    - Tools: `p2 at x y z` (absolute teleport), tools/flatland.py (flat land from the coarse
      island height map, CTerrainMapGame +0x804, 512x512 u16 * [0x0108AB40]).
  - Test modes: `terrain2 p1only` (only P1's landscape, at the hand-off), `p1alt` (only on P1's
    frames: P2's half then draws no landscape at all). Crash logger (frames, module, register
    vtables) and MSVCR80 invalid-parameter logger in terrain2.cpp; tools/threadstacks.py (hang
    stacks), tools/focusscan.py (memory that follows P1's position), `p1top2` (P1 next to P2).
- Buildings, props, NPCs, vehicles for P2 (in progress). Test spot: a city with traffic, see
  docs/positions.md; tools/citycmp.py takes P2's view there with P1 next to him and with P1 3 km
  away (flat jungle 1632 240 1248). Commands: `p1 at x y z`, `p2 at x y z`, `p2 face fx fy fz`
  (P2's camera axis as `cam` prints it), tools/spot.py (P1 there + his camera turned with the
  mouse). Long teleports hold the player in place until his terrain cell is loaded
  (Terrain2GroundLoaded; he fell through the ground otherwise) and `p1 at` no longer drags P2.
  - Locations (world2.cpp, DONE): CGameWorld [0x01191720] + 0x58 is the spatial location
    manager. Entries 0x28 bytes (+0 name, +4 box min, +0x10 kind bits 2-3, +0x14 flags, +0x18 box
    max, +0x24 loaded CLocation, vtable 0xEB5804, near flag +0xC0). Vectors: +0x4 named (global,
    landmarks, area sets at +0x24), +0x14 spatial: every 512 m tile "wc\XX\XXxYY" twice, kind -2
    (far blocks .fl, ~3 km box) and kind -1 (near blocks .nl, ~1.3 km box), plus settlements,
    roads, mission areas. +0x34 loaded, +0x54 loading (capacity = loads at once), +0x60 being
    unloaded, +0x64 flags. CGameWorld slot 0x94 (FUN_007439F0, vtable slot 0xEB92B8) gets the
    camera position: FUN_00520070 (step), FUN_007F7440 (unload one loaded entry whose box doesn't
    contain the point), FUN_00584D50 (load named ones; FUN_007ED240 picks one spatial entry whose
    box minus a margin contains the point). The hook sets CGameWorld +0xBC bit 0 (skips that
    part), calls the original and runs a copy with both cameras. P2 now gets the city's buildings
    3 km from P1 (146 locations loaded).
  - Still missing for P2 with P1 far (but present with P1 next to him): signs, street lamps,
    small props, lights, NPCs, traffic, and the street collision (P2 sinks to the terrain under
    the street). The objects themselves exist (tools/objpos.py: ~900 CSimpleRigidObject within
    500 m of the city), so it's activation/visibility measured from P1. Ruled out: the render
    camera's world position (the hand-off now gives P2's frames his world matrix, `terrain2
    norenderworld`; no change), Havok broadphase (hkp3AxisSweep box +-4050 x/z, -1000..7100 y
    around the floating origin: the city is inside). Leads: each CLocation's +0xBC
    CPfxInstanceComposer (static collision; +0xE4 0x20 near P1 -> 0 far; nothing reads its
    centre +0x44 while P1 is far), FUN_00729AD0 (light on/off by distance/angle to the camera,
    ~30000 calls/s), spawners following P1 (focusscan: CRoadSpawner@NSpawning +0x538,
    CHeatSpawnManager, CCreatureManager, CPlayerCenteredCover). Most game-update readers of the
    camera position read the main camera's world (+0x54), P1's.
  - Decorations (world2.cpp, DONE): CWorldDecorationCache [0x011913E0] (street lamps, signs,
    barrels, small props with physics, lights; FUN_007C1A40 in the world update) keeps groups
    within 250 m ([0x00E4E4EC] = r^2) of the main camera: even frames FUN_007B9890 releases far
    groups (FUN_00698B80 -> object physics removed: that was the "street" P2 sank through), odd
    frames FUN_0078D510 creates near ones. With P2 in, world2 runs that update itself (release
    only when far from both cameras, create once per camera, P2's with the camera pointer on a
    copy at his position) and sets +0x42 so the game's call skips (cleared next frame).
  - Roads (world2.cpp, partly): road builder [[0x01191194]+0xA8]: FUN_008C2A20 rebuilds the
    wanted set when the camera's 50 m cell changes (FUN_008C4470 -> gather FUN_008C56B0(manager,
    builder+0x1F8, pos, radius +0x290 = 800 m), removes built roads (+0x1C4) not wanted), then
    builds wanted roads over the next frames. world2 rebuilds with both cameras (P2's merged via
    push_back FUN_008F7AD0, removal like the game: FUN_0089BAF0, renderer slot 5, delete
    FUN_00440350), keeps the builder cell at P1's, and refreshes once a second (FUN_008FCC10
    rebuilds for P1 alone too). P2's roads are built (36 near the city with P1 3 km away) but not
    drawn: the road draw list (FUN_008B9940 -> FUN_008B6F30) keeps roads passing the main
    camera's frustum (FUN_00935C20 on [[0x011919F4]+0x10] planes +0x50C..+0x5E0).
  - Result (tools/viewdist.py, P2 at the city spot, P1 walking away in <= 1 km steps): P2
    stands on the street at every distance; signs and lamps to ~1.5 km (gone at 2 km); road
    markings gone from 1 km; NPCs/traffic not there. A 3 km jump in one frame (3 km physics
    origin shift) still drops a standing P2 through the city ground (1 km jumps are fine).
  - DONE, culling from P2's camera ("split p2world", default on): the camera update runs before
    the world update and the main camera's frustum planes are already P2's on his frames (tested
    with FUN_00935C20 on a point in front of each camera), but splitrender put P1's world matrix
    (+0x24, position +0x54) back right after P2's camera update, and the update measures detail /
    draw / mesh distances from that position (P2's roads were built but had no mesh, render box
    +0x50 empty; signs gone at 2 km). Now P2's world stays in through his frame and P1's goes back
    after the hand-off (SplitRenderRestoreLogicWorld, also at the next camera update). What must
    stay P1-centred takes his camera from SplitRenderPlayer1Position: P1's terrain stream update,
    world2 (locations, decorations, roads; the road builder's cell is set to the main camera's
    so the game never rebuilds), area sets (CAreaSetsManager slot 0x38 hook, 0xF0C24C), and the
    physics floating origin: its shift distance [0x01092AA0] is 1e9 while split + p2world are on
    and world2 shifts it for P1 (FUN_009B3950 on [0x0118FFF4] when P1 > 1000 m away). Result
    (viewdist.py): P2 at the city with P1 2-3 km away sees roads, markings, signs, lamps, props
    like P1 does next to him. P1 checks: tests/p2grapcam.py same as before (facing turn 0), P1's
    aim point (CPlayerAimControl +0x44) stays at P1. Cost ~144 -> ~115 fps with both areas full.
  - Frame order (matters for anything per view): in the world update, CGameWorld slot 0x94 (the
    world2 hook) runs BEFORE this frame's camera update (camera control manager [0x011900F8]
    slot 0x18 at 0x00782139 -> CCamera::Update); the road update etc. after it. So in the world2
    hook SplitRenderGameView() is the previous frame's view; this frame's is 1 - that.
  - Roads fix (the user saw P2's roads flash and then disappear when P1 went far): queuing a road
    that's already built makes the builder replace it, and a road built on the other player's
    frame gets no mesh. Now each player's roads are gathered on his own frame (1 - view), only
    roads not built are queued (4 per frame so the builder finishes them that frame, every 0.2 s),
    built roads are removed only when in neither player's set, and the builder's cell is set to
    this frame's player's so the game's own rebuild (it ran every other frame) never runs.
    Checked: 36 city roads built, all with a mesh, set unchanged over 3 s, FUN_008C4470 0 calls.
  - Skydive fix (P2 went into a skydive on every jump far from P1): ray casts (FUN_00A64A90)
    check the height-map slot's loaded flag (+0x14) and cell (+4/+8), so the collision redirect
    now swaps the whole slot record (+4..+0x1F), not just the two buffers.
  - P2's camera sits 10 m behind him (was 4.5; user asked). It has no collision, so it goes into
    walls and trees more often now. P1's camera is drawn as far back too ("split p1near" to turn
    off): on P1's frames the camera's pending world is moved back along its own view axis to 10 m
    from his head (the crosshair line, so aim is unchanged) and after the update the main camera
    gets the unmoved world (g_logicWorld), so the camera controller doesn't keep pushing it.
  - Camera ground collision (user asked): KeepCameraAboveGround in splitrender moves a camera that
    would be under the terrain (+0.6 m) in along the line to its target (P2: his head; P1's pushed
    camera: back toward where the game had it). Height from Terrain2GroundHeight (FUN_00A5F600 on
    the map game logic uses, only when its slot holds the cell). Terrain only, not buildings or
    plants. P2's pitch range is now +-1.45 rad (was -0.3..1.1), close to P1's.
  - P1 flung when grappling far from P2 (user report): with P2's world in the main camera on his
    frames, P1's brain think (aiming, grapple targets) could aim from P2's camera kilometres away.
    BrainThinkHook now gives P1's brain P1's world (SplitRenderPlayer1World) like P2's gets his.
    The user then saw it was P1's camera that got flung, not his body: his grapple/reel camera
    started from the main camera during his player update, so PlayerJobHook now runs P1's player
    update job with P1's world in the main camera too. tests/p1grapcamfling.py samples the render
    camera during 8 grapples: farthest from both players ~67 m in every mode (also with
    p2world/p1far off: the game's reel camera trailing), nothing beyond that.
    tests/p1grapfling.py: 8 grapples with P2 3 km away, 3-33 m each, no fling. P1's grapple key is
    F. tests/p2grapfar.py: P2's grapple 3 km from P1 goes to his aim point.
  - Still missing for P2: NPCs and traffic (P1 has traffic at F, P2's city has none). Spawners
    follow P1 (focusscan: CRoadSpawner@NSpawning +0x538, CHeatSpawnManager, CCreatureManager) and
    seemingly not the camera.
  - Hardware watches during a long teleport crash the game (twice).
  - Tools: classcount.py (live objects per class, diff), objsnap.py / locsnap.py (object and
    location field diffs), memstrings.py (heap strings), locations.py (the location entries),
    focusglobals.py (exe globals following P1), objpos.py (where a class's objects are).

## Current work (2026-10-28)

Working style the user wants: work to milestones on your own, and drive the game yourself through
`tools/jc2ctl.py` (no asking the user to do in-game steps). No git commits unless asked. No
DRM removal or inline code patches; only data and vtable hooks.

Build: `cmd /c .\build.bat`, then close the game (`jc2ctl.py close`), copy `build\dinput8.dll` to
the game folder (`install.ps1` finds it through Steam), `jc2ctl.py loadsave`, `jc2ctl.py mod "p2 join"`.
The log is `jc2ss.log` in the game folder. The deployed DLL matches the source, including
everything below.

**Fixed: P2's road white / overbright (user report).** Spot: P2 at 1542.3 233.5 1170.7
looking north (`p2 face 0.06 0.2 0.998`), P1 far in the city (1077.6 210.9 -1456.8). P2 saw a
white road with faint lane markings while P1 was far; with P1 there it was normal grey asphalt.
Repro / A-B: `tools/tests/roadwhite.py <tag>`, `tools/tests/roadblocks.py near` (lists the
CRenderBlockSplineRoad blocks near P2).
- Cause: build time, not render time. The road blocks (vtable 0x00F22C64, 0x80 bytes) of a white
  and a normal build have the same textures (+0x14, +0x20..+0x40) and box; the road comes out
  white when the builder runs with P1's stream state in. The builder update FUN_008C2A20 (world
  update, 0x0078232B) drapes and shades the mesh from the height map through FUN_00A646A0 ->
  FUN_00A56400 (CTerrainMapGame slots: heights +0x18, materials +0x1C, material table 0x0137BAF0).
  The 5x5 slot redirect (below) wasn't enough.
- Fix (world2.cpp `RenderWaitHook` / `BuildPlayer2Roads`): the world update calls graphics engine
  slot 0x88 (0x00F229B4, FUN_0092DF00 = wait for the render job) right before the road update
  (return address 0x0078230D). On P2's road frames (`g_lastRoadPlayer == 1`) the hook runs
  FUN_008C2A20 itself with P2's stream state in (`Terrain2UsePlayer2`, terrain2.cpp: collision
  redirect off, SwitchStream(1), then back and redirect on again). It then empties the wanted
  vector (releasing the counts), so the game's own call that follows builds nothing with P1's
  state; what the time budget didn't reach is queued again by RebuildRoads. A-B with P1 far:
  `world2 roadterrain1` -> white again after a rebuild (P2 away 2 km and back), `world2
  roadterrain2` (default) -> normal. `world2 roadground` also swaps P2's ground state in
  (render job is idle there); not needed, off by default.
- Seen during the test, not from this change: a caught fault at 00A289AB (write to 0) in the
  FUN_005910E0 update ([0x011919FC], world update 0x007822D3) right as P2 died after a test
  teleport to 3542 300 1170. The game kept running.
- Fixed on the way: the road also rose into a vertical wall. Road meshes are draped over the
  height map at build time (world update), and only P2's nearest 2x2 stream cells were redirected
  to his height data. ApplyPlayer2Collision now redirects his 5x5 cells (`kRedirectRadius` 2),
  skipping P1's own 3x3 slots. The wall is gone.
- Ruled out: area sets. Both positions want `AreaSet01` + `AreaSet05`. CAreaSetsManager
  [0x01191660] has only 2 slots (vector at +0x68, 0x58 each, name std::string at +0). The query is
  vtable slot 10 (FUN_008A5CC0: 4 corners ±1000 around the 1024-snapped position into a std::set),
  and the update is FUN_008B37B0 (world vtable +0x3C load, +0x44 unload). The test switch
  `world2 areasets2` / `areasets1` makes them follow P2 / P1 (default P1). It changed nothing.

Other things from this session (details in "Other open issues"): crash logger always on, P2 aim
start, per-player road building, road cell sync, per-player climate zones, `p1 at` long-jump
slide (it also killed P1 when dropped at y 260; `p1top2` slid into the sea).

## Where it stopped

- Fixed: P1's camera turned (up to ~100 degrees) while P2 reeled in. The player driver
  (FUN_00853AE0, end) asks the camera control manager ([0x011900F8], vtable 0x00EB8774 slot 3
  FUN_00600850 = set camera setup) for the setup matching its own character (reeling = 0x1D);
  P2's driver switched the one game camera. `SetCameraSetupHook` ignores requests while
  `t_inPlayer2Update` (set in `PlayerJobHook`). Residual P1 view movement â‰¤0.05 rad
  (`p2grapcam.py`). Not verified: P1's own long reel still gets the reel camera (P1's requests
  pass through; a 6 m reel stayed on setup 0).
- Fixed: P2's grapple shook P1's camera, from two sources. (1) The end of a reel-in plays an
  effect with a camera-shake element (vtable slot 0x00EBA3E8, FUN_0062B300 -> FUN_0070D460,
  strength 5, no position): `ShakeElementHook` skips shake elements while P2's current action
  (char+0x324) is a grapple action and P1's isn't. (2) Every frame of a reel, the reel-in action
  (FUN_006B8890 -> FUN_00515CB0 -> FUN_00648F70) shakes the camera with strength [0x0106D174] and
  rumbles the pad ([0x0106D160..6C]); `PlayerJobHook` zeroes those during P2's update. Shake state
  = camera control manager +0x24 amplitude / +0x28 strength (`p2grapshake.py`: peak 0 for P2,
  P1's own grapple still 0.036).
- Fixed: P1's camera never switched setups in splitscreen (no skydive/parachute camera, stuck in
  the reel camera after grapple + parachute until another grapple or melee). CreatePlayer for P2
  runs FUN_006F77A0, which loads camera setup ids from the player's camera parameters (empty
  mid-game) into globals (0x011FCCD0..DC, 0x011FCCEC, 0x01076ACC) and +4 of the shared character
  controllers (registry = character manager +0x35A0, lookup FUN_0084EC30 by type hash): all
  became -1. `Spawn` saves those before CreatePlayer and restores them after
  (`p1grapchute.py join 1.1`: setups 2 -> 29 -> 1 -> 0 like solo). Test command `p1lift <m>`.
- Parachute momentum depends on the frame rate (game logic runs per frame): opening the parachute
  out of a reel, P1 keeps ~8.6 m/s at 30 FPS, ~5.6 at 60, stops (0.1) at ~144 uncapped. Same in
  vanilla (mod DLL removed). Frame cap in the Present hook: mod command `fpscap <n>` (0 = off,
  default off). In splitscreen the views alternate frames, so each half gets cap/2.
  (Measured 0.1 at 2.5 s = the momentum is gone.)
  Test: `p1chutespeed.py solo 90 40` (lifted, grapple, parachute); `p1chutephases.py [fps]` logs
  controller/action/speed per 50 ms. Steady gliding is the same at all frame rates; the loss is
  after the reel -> parachute transition (speed turns into a climb at high fps). The parachute
  update (CParachuteController 0x00EC24AC slot 6, FUN_005A9950) integrates velocity with the
  frame time ([0x0118FD24]+4 +0x14); the frame-dependent term wasn't found. Running that update
  at a fixed 60 Hz by skipping frames froze the parachute (it must run every frame).
  Parachute constants at 0x010776B8..FC (CC force scale 0.0111, E8 lift 1.7, EC normal force 10,
  F0 drag along the canopy 3.0, F8 glide conversion 0.4). Uncapped tests: F0 1.5 / 0.75 keep the
  reel momentum (3.3 / 10.1 m/s at 2.5 s) but also speed up normal gliding (16 / 19 vs 11);
  EC 5 / 2.5 change nothing; E8 0.85 doesn't help and slows gliding to 5-8.
  Current fix: `ParachuteDragTick` (game tick) multiplies F0 by 0.4 while a player's current
  action is CDeployParachuteWhileReelingAction (0x00EC2FDC) and for 1 s after, only when a frame
  is shorter than 1/61 s. Result at ~144 fps: 2.5 s 5.5-7.2 m/s, ~2.5-4 after (60 FPS reference:
  ~6.9, ~4), solo and splitscreen; plain gliding unchanged (~11). Scale 0.25 carries more
  (9.5 / ~6). Test command `chutedrag <scale>` (1 = off). F0 is global, so an NPC parachuting in
  that second gets less drag too. Opening the chute after a released grapple (not mid-reel) isn't
  covered or tested.
  Alternative kept off: `chutecap on` (`Player2FrameCapNeeded` caps the frame rate at 60 while
  either player has the parachute controller, `WaitForFrameSlot` in hooks.cpp).
- User saw P2 pulled backwards once, flying until cancelled. Not reproduced: `p2grapdir.py`
  (normal, looking down 45/60, looking up) always pulled toward the aim point. P2's aim ray starts
  ~4.6 m out (aim control +0x64 = near, +0x60 = far), i.e. at P2, so obstacles behind P2 aren't it.
- Fixed (2026-10-28): P2's view drew no sea (the seabed showed) and a black band toward the
  horizon, with both players at the sea (spot: P2 at 890.4 223.4 -2027.7, `p2 face 0.99 0.1
  0.137`) and with P1 8 km away. Cause: byte 0x0108AB05 (`kHighEndWater`) = NVIDIA high-end water
  available; any failed NV water call clears it for good (FUN_00A78B90, FUN_00A7EAB0, FUN_00A79F00
  ...), and the fallback path (FUN_00A88D60 -> FUN_00A81190) draws no sea. On P2's landscape turn
  his patch manager registers its terrain cells with the water (FUN_00A67610 -> FUN_00A79F00 ->
  FUN_00C3E410), which fails for cells P1's already registered, on every P2 turn. Also the flag
  sat in kGroundState (range 0x0108AB04), so only P2's copy went 0 at first. Fix: the range now
  starts at 0x0108AB08 (the 4 flag bytes are global), and the hand-off puts the flag back after a
  P2 turn cleared it (logged the first 8 times, then every 1024th). The log's per-hand-off line
  shows `high-end water 1`. Found by bisecting with test switches (terrain2.cpp, kept):
  `terrain2 keep veg|ground|camera|none` (P2's turn keeps P1's part), `terrain2 groundmask <hex>`
  (kGroundState ranges by bit taken from P1), `terrain2 sharedtex`, `terrain2 landstream1`.
  Not verified after the fix: P1's own view of the sea (P1 was facing inland in the shots).
  Came back (2026-10-29, user): after P2's controller disconnected (P2 left), P1 jumped 4.6 km and
  P2 rejoined, the flag stayed 0 from the rejoin on, cleared outside P2's turns. Now the
  hand-off also keeps it on whenever it was seen on this session and P2's copies exist (P2 in or
  out), log "high-end water flag was 0 at the hand-off, put back". Checked with a simulated
  leave / P1 jump / rejoin: sea in both views (that run didn't clear it outside the turns).
- Fixed (2026-10-28): vehicles near P2 went away when P1 left (user: the parked car at Karl's
  hideout, P2 at the deck spot). Spawn manager [0x01191918], update FUN_00779D20 (world update
  0x00782229) = 14 stages, one per frame or more. Two removals, both measured from P1:
  - Stage 3 (FUN_006A29F0 -> FUN_005C0EC0) asks each spawned object's despawn functor (slot 0,
    __thiscall(functor, object, float, object pos, camera pos, camera dir, flag, int* result),
    ret 0x1C; vtables 0x00E3748C CSpawnManager, 0x00E330A8 CHeatSpawnManager, 0x00E57C28
    CWorldSim) with the main camera. world2 `DespawnHook`: despawn only if the functor says so
    for both players' cameras (SplitViewCamera). `world2 despawn1` = old way.
  - Stage 2 (FUN_005923B0) checks the spawn points (4x4 grid of 8 km cells at manager +0x148;
    point +0x68 pos, +0x80/+0x84/+0x88 spawn/despawn/min distance squared, +0xA0 bit 0 on, +0xA4
    state 0 none .. 4 spawned / 5 failed) and resets those beyond the despawn distance
    (FUN_00515070 + FUN_007999F0). The camera it reads is P1's on every frame, even P2's (watch:
    the hideout point took that branch every frame with P2 next to it). world2
    `UpdateSpawnPoints`: points within despawn reach of P2's camera get +0x84 = FLT_MAX, put back
    when he leaves. `world2 spawn1` = old way.
  - Test: tests/p2cars.py (both at the deck, P1 to 3000 889 -6000, counts CCar = vtable
    0x00EBFF64, position +0x118, near P2): car stays 35+ s (was gone in 6 s); with P1 back there is
    one car.
  - Spawning around P2 (2026-10-29, done): UpdateSpawnPoints also queues P2's points by the
    stage's rule (FUN_0079DF40). tests/p2cars.py: with P1 8 km away the hideout car, removed with
    the old rules, comes back for P2 within 6 s.
  - Fixed (2026-10-29, user report): enemies and vehicles (jets) at a military base didn't spawn
    for P1 until P2 came near. tests/p1base.py (P2 at the deck, P1 to the mountain base at 3678
    1347 -5430, 21 points): 28 queued, the current one stuck at state 1 for 40+ s; instantly fine
    with P2 out or `split p2world off`. Chain: the queue's current point (manager +0xB8) waits for
    its vehicle type (resource slots: manager +0x1720 count, +0x1740 entries of 0x58, +0 state -1
    free / 0 requested / 1 loading / 2,4 loaded, +0x2C priority bits; FUN_0084E9D0, FUN_0058D0D0);
    stage 0 FUN_005E1B20 / FUN_005A5D50 starts a type's load only when the file queue
    [0x012A1448] is idle (FUN_004D63E0: 4 priority deques at +0x4C, 0x14 apart). Queue 3 was never
    empty: AI navigation tiles (CStreamQueueItem of AI\patches\XX\N.aitile, CAITile). The AI tile
    cache (CAITileCache vtable 0x00F0A068: slot 1 FUN_004763C0 request, slot 2 FUN_0045B410
    release) got ~1300 request/release pairs a second from the alternating main camera, i.e.
    constant reloads. Fix (world2 "AI navigation tiles"): while P2 is in, a release waits 3 s and
    a request of the same tile cancels it (the hooks hand over due releases themselves, on the
    game's thread). Log line "AI tiles in 10 s: N requests, N releases, N kept, N waiting".
    `world2 tiles1` = old way. Result: base 15-16 points spawned within 5 s, file queue idle, and
    P2's spawning above works at the same time. Tools: tools/spawnqueue.py, spawnslots.py,
    ioqueue.py.
- Parachute checked (2026-10-29): tests/p2chute.py (both 100 m up, P1 Space, P2 action 0x47
  PARACHUTE_OPEN_CLOSE): both open, settle at -1.7 m/s; tests/p2chutesteer.py: forward glide
  ~10 m/s for both (P1 W, P2 stick), closing drops both back into free fall (-10..-20 m/s, ~18
  m/s forward). Canopy drawn in both halves. P2's pad: A sends jump and parachute (the game uses
  whichever applies). The HUD prompt ("Close parachute SPACE") is shared, so P2 sees P1's hint.
- Fixed (2026-10-29, user report): P2 couldn't open the parachute out of a grapple reel-in
  (pad A); falling/skydiving chutes worked. Two causes: (1) the deploy action
  (CDeployParachuteWhileReelingAction, vtable 0x00EC2FDC) waits for its own input action 0x92
  DEPLOY_PARACHUTE_WHILE_REELING_ACTION (action id at +0x24; 0x90 REELED_IN_JUMP_ACTION, 0x91
  REELED_IN_RELEASE_ACTION), which P2's pad never sent; (2) context actions check "just pressed"
  through vtable slot 5 = FUN_008B7B80 -> FUN_00984140 (the game's input, P1's). Now A also
  sends 0x92 (B sends 0x91), the press check is hooked in the 31 vtables that use it
  (ContextActionPressedHook: during P2's update it reads his private input), and during a
  reel-in (current action CReelInAction 0x00EC7834) A drops the jumps bound to it (the reeled-in
  jump started instead). P2's pad grapple moved to LB (like P1's pad), left trigger is the
  left-hand weapon. Test helpers: `p2 padpress <hex buttons> [ms]` (through the pad bindings),
  `p2 padlog on|off`; tests/p2reelchute.py pad 1.0 "-0.83 -0.35 -0.55" (deck spot, lifting reel,
  A at 1 s: deploy, rises to y 247), tests/p1reelchute.py, tools/actionrec.py (both players'
  current actions while the user plays).
- Fixed (2026-10-28, user report: jump and grapple "delayed", presses needed repeating, for both
  players after the parachute fix): FUN_008B7B80 is __thiscall(action, a, input context), ret 8
  (the lookup runs on the second stack argument); ContextActionPressedHook took only the action,
  so every press check read garbage and unbalanced the stack. Now it takes and passes both.
  Before: P2's A taps jumped 6 of 12 (tests/p2tap.py a 60,100,150 4); after: 12 of 12. P2's update
  runs exactly once per game frame (`p2` prints "updates" and "frames", 650/650 in 5 s).
- Fixed (2026-10-28, user report: P2's grapple went where P1 was pointing): CFireHookAction's start
  (slot 6 0x00EC3AEC, FUN_0079DCB0) and update (slot 7 0x00EC3AF0, FUN_0061D6B0) attach the hook
  with FUN_0076F1D0 (ecx = [char+0x484]+0xD0, link at +0x214), which takes the target from
  [[0x0118FB24]+0x3570]+0x184, Player 1's aim control (hit object +0x3C, point +0x44). Hooks on
  both slots put P2's aim control into P1's CPlayer+0x184 for the call when the action's character
  (shared_ptr arg - 0xF0) is P2's. "p2aimlend off" restores the old behaviour for testing. User
  confirmed. tests/p2grapaim.py: aim point and pull vs both views ("cam f" is the look direction).
- Fixed (2026-10-28, user report: P2's parachute steering inverted left/right and up/down, and he
  "lost all momentum" out of a reel): WritePlayer2Input remaps P2's stick from his camera to P1's
  (on foot the game reads directions with P1's camera), but the parachute (CParachuteController
  at char+0x2F0) takes the stick as is, so with the cameras facing apart the controls flipped and
  "forward" stalled him. No remap while P2 is under the parachute. User confirmed both;
  tests/chuterec.py (both players' actions/controllers and speeds while the user plays) showed
  P2 keeping ~30 m/s through reel -> parachute. Free fall / skydive steering with the remap is
  unchecked.
- Fixed (2026-10-28, user report: reloading after P1's death with P2 joined crashed). Three parts:
  (1) P2's CreatePlayer put his character into P1's entry of the object registry ([0x012AAAFC],
  std::map name hash -> weak_ptr; the player's key 0x4E41494C): `Spawn` puts P1's weak_ptr back.
  (2) The respawn (CGameWorld slot 0xC8 at 0x00EB92EC, FUN_00536660) walks that registry and
  resets each object (FUN_008FD210, ecx [0x0118FC34]); it crashed in the iterator (FUN_008AC3C0,
  null node; later the checked iterator's silent exit). `RespawnWalkHook` walks a snapshot while
  P2 exists (logs "Respawn: ... walked on a snapshot" and any reset that changes the registry;
  none did after (1)). (3) Spawn points held for P2 (despawn distance FLT_MAX) were never despawned,
  so the load's teardown freed their vehicles while the points still listed them (+0xC0); after
  the load the restored distance made stage 2 despawn them (FUN_007999F0) through freed memory.
  `World2ReleaseSpawnPoints` (from splitrender's SetEnabled(false): menus, game over, loading,
  P2 leaving) gives them their distance back first; g_spawnLock guards g_heldPoints. Repro: P2
  joined, `god off`, `p1 at 905 900 -2040`, P1 health 1 (char +0xE8), die, reload from the
  game-over screen. User confirmed. A diagnostic logs the first listed spawn point with a
  broken +0xC0 vector ("World 2: broken spawn point").
- Fixed (2026-10-28): free fall (CSkyDiveController update slot 6 0x00EC2C38, FUN_00619510)
  smooths the stick in globals shared by both players ([0x01077C3C] forward/back, [0x011FD0AC]
  left/right; read with the passed input context): `SkyDiveUpdateHook` gives P2's update his own
  copies. The stick is dive/lean relative to the body, so P2's isn't remapped to P1's camera
  in free fall ("p2skyremap on" for the old way). Steering: the body turns toward the camera's
  heading. The player driver's FUN_0077FC80 (__fastcall(CPlayer*)) puts the heading error between
  the body's right axis and the main camera ([[0x011919F4]+0x10]+0x24/+0x2C) into controller
  state +4 (after copying the old one to +8), and FUN_006B9290 adds it to the lean
  (+0x14C; write at 0x006B9E95). Half the frames the main camera holds the other player's world,
  so each body was pulled toward the other player's camera (user: "when P1 stopped and righted
  himself, P2 did too", and P2 bled into P1). `SkyDiveUpdateHook` re-runs FUN_0077FC80 for that
  player with his own camera world swapped in right before his update (`RecomputeFallTurn`, +8
  kept). P1's game camera leads his turn (+4 ~ +0.14 at full lean, same sign), so P2's camera in
  free fall now follows his body with a lead (splitrender MakePlayer2Camera, 0.3 rad per unit of
  lean, rate 3/s, while the right stick is idle; "split falllead <r>"; 0.6 kept spinning after
  release). Measured: P2 turns ~60 deg/s like P1's ~70, stops on release, the idle player stays
  put either way. Not yet felt by the user. Tests: tests/skydivecross.py, tests/skydiveright.py
  [key|p2] (both bodies' tilt, heading and lean; P2's raw move inputs), "p2skylog <ms>" (each
  free fall update and turn recompute: character, input context, thread).
- Partly done, on hold (2026-10-28, user: P1's camera shakes a little and his pad rumbles while
  P2 is in free fall; wanted on P2 instead). FUN_00619510 (free fall update) starts with speed
  effects when byte [0x0106EC80] is set (made writable at hook install): FUN_00698DE0 on the
  camera system [0x011900F8] (shake source: strength = speed * [0x0107986C] 0.002 / distance from
  the main camera, keeps the max; params 1.5 * strength, -0.5) and FUN_008CFF40 on [0x0118FB98]
  (random wobble added at +0x14). `SkyDiveUpdateHook` clears the byte during P2's update and
  records his strength (`UpdatePlayer2FallShake`, speed from his position, `Player2FallShake()`).
  P2 gets: camera jitter in MakePlayer2Camera (strength * 2 deg random yaw/pitch, not kept;
  "split fallshake <deg>"; P1's own view jitters ~0.05-0.1 deg a frame at 30 m/s) and pad 2
  rumble through XInputSetState (splitscreen UpdateRumble, low/high = strength * 3 / 1.5,
  "p2rumble <low> <high>"; 6 / 3 felt a little high). Open: the user still felt P1's pad rumble
  while P2 fell, so another game rumble path remains (not found). Diagnostics added: the game's
  XInput imports are hooked (IAT 0x00D93738 XInputGetState = xinput1_3 #2, 0x00D9373C
  XInputSetState = #3): "Game rumble: slot N motors a b (from <return address>)" on each change
  and "Game XInput reads in 10 s" per slot. First run (P2 falling, P1 still): the game reads
  slots 0 AND 1 (~1400 each per 10 s, so both pads feed P1's input) and made no rumble call at
  all, so the rumble felt on "P1's pad" was most likely the mod's slot-1 rumble landing on the
  other physical pad (Steam Input mapping), not a game path. Also: Steam Input orders the XInput slots randomly per
  session (the user's P1 pad changes; outside the game only slot 0 exists), so P2 = "slot 1"
  (jc2ss.ini ControllerSlot) can be either physical pad; better would be P2 = the pad that
  pressed Start. Don't use the `trace` command or execute watches (`watch x`) on game code: both
  made the game quit (data watches are fine).
- Done (2026-10-28, user): P2 melee. Melee is input action KICK 0x7C (Rico swings his grapple;
  not a context action); P2's pad B now sends KICK (+ reel release), crouch moved to left stick
  click. User confirmed the swing works. (Action names: tools-side dump of the table at
  0x01071DE8; 0x7D is the other melee side's key binding.)
- Done (2026-10-28, user confirmed: rope, hooks and the melee swing show for P2; details below).
  (was: P2's grapple rope invisible, while grappling and in the melee
  swing; the launcher on his arm is fine). P2's CGrapplingDevice (inventory +0xD0) is built by
  create-gear FUN_005BEED0 -> FUN_00779370 (ctor FUN_005FDBC0, 0x31C bytes) -> FUN_00802EC0
  (applies the parsed grapple file: params, "hook_texture_%d" textures at +0x148.. and +0x1A4..,
  and two hook model instances at +0x140/+0x144 via FUN_008701B0 with scene [0x011911F0], then
  hidden: +0xC &= ~0x11). The file doesn't load for P2, so all of that was missing.
  `ShareGrappleParams` now shares the textures (shared_ptr pairs +0x148..+0x168, +0x1A4..+0x1BC)
  and `MakeGrappleHookModels` builds P2's two instances from P1's model (instance +0xC4
  shared_ptr<CModel>; game alloc FUN_00444AE0(0xCC), ctor FUN_00A4F4B0(inst, &model, 1), place
  FUN_008909B0(scene, inst, identity, 1, 1, 0)). Result (user): the hooks now show (seen with the
  two-point tether) but flicker, and the rope itself is still invisible. Cause of the rope (and a
  crash): each frame the hook update (CGrapplingHook slot 5 FUN_005F28E0 -> FUN_00515A30(hook,
  device)) builds the rope from the owner's launcher muzzle (launcher +0x194) to the hook with
  FUN_007DE2E0 (device: resources +0x1A4 x8 shared_ptr, segment list +0x1E4 {proxy, begin, end,
  cap}, hook models +0x140/+0x144) and queues it at device +0x174 (list +0x178/+0x17C);
  FUN_006A1750 (end of the device update FUN_0083FE20, via FUN_0068BAF0(CPlayer, dt)) hands the
  queue to the scene and frees it. The frame (FUN_00781D60, 0x007823CE) runs FUN_0068BAF0 for
  [[0x0118FB24]+0x3570] only, so P2's queue grew (986 after a few grapples) and the next rope's
  allocation failed (fault at 0x00937039, FUN_00939130 <- 0x007DEBA3, seen twice while P1
  grappled). `Player2DeviceUpdate` runs it for P2 from terrain2's CStreamPatchManager update hook
  when called from 0x00782432 (the next virtual call in FUN_00781D60). Queue now drains (0/2).
  Not it: the launcher
  weapon (inventory +0x54; P1 and P2 identical, its model component +0x258 index -1 on both), the
  character "models" (+0x48C are animation rules), the CGrapplingWire objects (5 static ones,
  unchanged during grapples). Next: find what draws the rope (the device's draw / a line render
  block, CRenderBlockLine vtable 0x00F23EBC) and whether it is only called for the manager's
  player; the flicker suggests something per frame (alternate-frame views) toggles the hooks.
  Data watches (write) are fine; read watches on hot data and execute watches crashed the game.
  Tests: tools/tests/p2melee.py, p2launcherviz.py (launcher visibility, not useful in the end).
- Done (2026-10-28, user): P2's stunt jump onto vehicles (CGrapplingJumpAction 0x00EC7A14,
  input STUNT_JUMP 0x77, now also on pad B). Three parts: (1) the binding; (2) the roof loop:
  roof actions' press check (slot 5 FUN_00825AC0, CEnterDriverSeatFromRoofAction /
  CRoofToGroundJumpAction) also counts "released < [0x00E28850] s ago" from the entry's +4 float
  timer (FUN_00982F50: seconds since release while idle); P2's private entries had 0 there, so
  on a roof he entered the driver seat at once, failed and repeated. `SetPrivateAction` keeps the
  timer (since release / held so far / 1000 for never pressed); user confirmed; (3) the jump was
  offered only for what P1's crosshair targeted: slot 4 FUN_005A4230 reads
  [[0x0118FB24]+0x3570]+0x184 -> +0x3C; `StuntJumpCanStartHook` (slot 0x00EC7A24) lends P2's aim
  control for P2's calls (LendPlayer2AimControl, as for the fire hook). Other vehicle context
  actions' can-start checks may read P1's aim the same way (not checked). Entering vehicles is
  still not done for P2 (from the roof the seat entry fails).
  Both players on one vehicle's roof: tried and dropped (user: keep the game's one-per-spot rule).
  The stunt position is a CSmartObject (action +0x58) with one occupant weak_ptr (+0xC0/+0xC4);
  FUN_0081D330 refuses others. Hiding the other player for the check let the second jump on but
  crashed 2 of 2 times (FUN_00824550 -> FUN_00794240 -> FUN_008B96F0 on null). The test switch
  "p2sharestunt on" is still there, off by default.
- Done (2026-10-29, user: "feels good"): P2 enters/exits and drives vehicles. The seat actions
  (CEnterDriverSeatAction etc.) wait for USE_ITEM 0x26 (P2's Y sent only ENTER_VEHICLE 0x25);
  their can-start checks don't read P1's player or camera. `WriteVehicleInput` writes all
  vehicle actions as analog values every update (ACCELERATE/REVERSE on RT/LT, TURN on left X,
  bike tilt / heli / plane / boat sets, see the code); Y also sends EXIT_VEHICLE 0x2D, X
  HANDBRAKE 0x2B (like P1's pad); vehicle weapons LB VEHICLE_FIRE_LEFT 0x8A, RB
  VEHICLE_FIRE_RIGHT 0x8B + FIRE_VEHICLE_WEAPON 0x0E (untested). Car driving confirmed; bike,
  boat, heli and plane untested.
  The players can pull each other out of a vehicle (user: fine).
- 2026-10-29 (user report: P2's heli hijack prompts didn't register; asked for code entry too and
  for every P1 action P2 lacked). The button prompts are CDisarmAction (vtable 0x00EC7B4C, update
  slot 7 FUN_00813AD0, steps FUN_0080A6B0 -> FUN_008103E0). It reads SEQUENCE_BUTTON1..4 (0x85..0x88)
  from the input context passed to the update; P2's pad never sent them. P1's real pad layout comes
  from the binding blocks in memory: tools/tests/padbinds.py. Each block is [action<<24 | flags]
  [key name table + key code]; pad codes run 0x70 A .. 0x87 BACK (name map built at 0x00573E00).
  The sequence buttons are X, Y, B, A. kP2Bindings now matches P1's layout:
  - Sequence buttons X / Y / B / A.
  - Horn on left stick click.
  - EXIT_TO_STUNTPOS_PARACHUTE on Y; GRAPPLING_ACTION on A.
  - RB: DASH, EVADE, MC_FIRE (bike guns) and the parachute thrusters.
  - THROW_GRENADE moved from RB to LT.
  - Pickups: left hand X, right hand Y (was X).
  - D-pad down is EQUIP_DUAL (was EQUIP_EXPLOSIVE, which isn't on P1's pad).
  Left out on purpose:
  - The black market / extraction beacons (one shared GUI).
  - Camera and zoom actions (P2's camera is ours).
  - GUI and PDA actions.
  User confirmed: P2's hijack prompts work (the update gets P2's own input context).
  tools/tests/bindings.py dumps P1's per-action input entries; the entry +0xC device is 0x10000
  key, 0x20000 mouse, 0x40000 pad.
- Fixed (2026-10-29, user report: setting a map waypoint closed the game, also without P2): the
  game checks the contents of the TSpecializedDespawnFunctor<CSpawnManager> vtable (0x00E3748C)
  and quits when its slot 0 is changed: ExitProcess(0) "called from" a fake return address
  0x8000DEAD on its own thread, with no fault and without our hook running (found with the exit
  import hooks, `HookGameExits` in splitscreen.cpp, and install-time [Debug] Skip* switches in
  jc2ss.ini: SkipCamera/SkipTerrain2/SkipWorld2/SkipGameXInput and SkipW2Lod/Tiles/AreaSets/
  Pfx/RenderWait/Despawn/Despawn0..2/Update in World2InstallHooks; tests/waypoint.py runs F1 + 1
  and reports whether the game survived). That vtable is no longer touched: CSpawnManager holds
  five functors at +0x17F8..+0x1808 (vtable +0, member function +8, this [+0x18]+[+0xC]; slot 0
  FUN_007A17B0 is a thunk calling the member with the same arguments), and
  `HookSpawnManagerDespawn` (world update) points each member at DespawnHook<3..7>. p2cars.py
  passes; waypoint with and without P2 survives. The CHeatSpawnManager and CWorldSim despawn
  functors (0x00E330A8, 0x00E57C28) are still vtable hooks; the waypoint doesn't trip on them,
  but other checks might (same approach would apply). Second checked vtable (with P2 joined):
  the CWeapon +4 interface vtable 0x00E3830C (slot 7 weapon update, the shot shake/rumble hook).
  It is no longer hooked: `GivePlayer2WeaponVtable` (game tick) gives P2's inventory weapons
  (+0x3C, 5 shared_ptr) a private copy of the vtable ([-1] type info + 47 slots, slot 7 =
  WeaponUpdateHook) on their +4 interface; only the constructors store the original pointer.
  P2's grapple still works (p2reelchute.py); user confirmed waypoints with P2 joined. [Debug] SkipP2Brain/Weapon/Death/CamSetup/
  ActionInput/FireHook/StuntJump/SkyDive/Respawn/Shake leave P2's hooks out
  (tests/waypointp2.ps1 "key,key" restarts, joins P2 and sets a waypoint). The other P2 vtable
  hooks passed the waypoint test. Lesson: the game verifies at least some
  .rdata vtables; prefer heap/data pointers over vtable slots where there's a choice. The 8000DEAD
  exit is also written in the exe at 0x0041AF50 (CALL 0x00489AE0; PUSH 0x8000DEAD; CALL
  [ExitProcess]); no direct callers found.
- HUD, minimap (2026-10-29, user request: minimap and heat on both halves, P2's for P2). Each half
  is the middle band (rows H/4..3H/4) of a full frame, which cut the HUD's top-left corner off.
  splitrender: after caching a view's band, that frame's corner (0.15 W x 0.27 H) is copied to a
  scratch texture and drawn into the band (cache textures are now render targets) through a pixel
  shader keeping a circle around the minimap (centre 0.124/0.117 H, radius 0.093 H) and the icon
  at its lower left; "split hudcorner on|off", "split hudmask on|off". Minimap per player: the HUD
  minimap is CGuiMiniMap (vtable 0x00EB911C, interface 0x00E38A88 at +0x30, one instance); its
  update (slot 7 FUN_00802D40, __thiscall(minimap, dword), ret 4) copies the main camera's matrix
  (+0x138) and the manager's player's (FUN_0068E1F0, +0xF8; the camera's in a vehicle). The vtable
  isn't hooked (checked by the game, see the waypoint entry): `KeepMinimapPerPlayer` finds the
  instance by a background scan of the writable heap and gives it a private vtable copy whose slot
  7 (MinimapUpdateHook) runs the update with P2 in the manager's player slot and his view in the
  main camera when SplitRenderGameView() == 0 (the update precedes the frame's camera update;
  "== 1" showed P2's map in P1's half). Screenshot-verified: each half shows its own player's area
  and heading. "p2minimap on|off". User confirmed: both minimaps right, heat indicator shows on
  both halves.
- HUD per player (2026-10-30, user away; screenshot-verified, user hasn't played it yet). Health
  ring, weapon panels and the weapon switch popup.
  - Below/above the band: `CRenderPassAnark` (vtable 0x00F28F94, ~50 instances) draws the HUD; slot
    15 = Draw (FUN_0097A3D0, thiscall, no args). KeepHudPassHooked gives them private vtable copies
    (background scan); HudPassDrawHook -> SplitRenderBeforeHud copies the target (the back buffer,
    format 28) once a frame before the first non-empty pass. At Present, DrawHudStrips draws the
    frame's rows outside the band into the band (top quarter to its top, bottom quarter to its
    bottom) through PSStrip: alpha from |finished - before| * 16, so only the HUD comes, no
    scenery. "split hudstrips on|off".
  - Scripts: tools/tests/luadump.py disassembles the compiled Lua 5.0 chunks in memory (header
    ESC "LuaP"; chunk with update_hand_right = weapons, with health_mat = ring). All state is per
    script (self.v, self.o), one set for both players. tools/tests/luawords.py, luascan.py too.
  - Reads: CLuaPlayer's C functions read the manager's player. kHudLuaFunctions' closures go to
    HudLuaHook<I> (same heap scan as the markers), which puts P2 in the slot when the frame is
    his: ring functions forPlayer2 = !g_markerP2 (set by an event after that frame's update used
    them, so they show in the other view's frame), weapon functions g_markerP2 (the events write
    the panels for this frame). BeginHudFrame runs once a frame from whichever hook comes first.
    Check ammo against a solo reading: the number is reserve ammo, and P2's differs from P1's
    (user caught them swapped after I took P1's firing for the difference, 2026-10-30).
  - Refresh: the script reads only on GUI events, queued by FUN_00976F60 on [0x0118FB78] (name,
    "UIControl..." 0x00E4409C). RefreshHudPanels fires player_health_changed_event every frame, and
    gui_weapon_changed_<hand> (table 0x0106EBEC; hands left, right, back, fg, tg = mask 0x1F) for
    6 s after fire/reload/equip input of either player, since it keeps the panels up
    (show_hands); then they fade as usual. Those events run later in the same frame. A hand not in
    use must be skipped (HandInUse, FUN_00587B80 enabled output): its event clears the shared
    panel.
  - GetResistanceUnits (ring, CPlayer +0x1B0 of +0x1B4[+0x23C]): only P1's full update regenerates
    it, so the hook shows P2 full for the read. His health still comes from GetHealth.
  - Bytecode patches (KeepScriptPatches, FindScriptPatches; on only while split, 4 found: 2 scripts
    of each kind): get_ammo_data's diff stays 0 (alternating values showed false "+7" popups), so
    the real "+N" ammo pickup flash doesn't show while split; ring low-pass factor = 1 (it
    averaged the players), so the ring has no smoothing while split.
  - Weapon switch popup (show_dynamic): NoteHudPopup on an EQUIP_* or NEXT/PREV_WEAPON press (P2 in
    WritePlayer2Input, P1 in PanelInputHeld). Owner gets the weapon reads for 300 ms; the other half
    gets kHudPopupRects back from the pre-HUD frame for 4 s, so anything else there hides too.
    Each frame the script moves dyn_par (smoothed, dt * 5) toward Player:GetBonePosition(8), and the
    alternating cameras' values, e.g. (158, -40) and (-310, -110), made it wander (user report:
    fine facing each other, moved around when turning away, escaped the rects). In the split that
    hook returns a fixed spot (-190, -40, centred screen units; "p2hud popup <x> <y>|off") without
    calling the game. There's one popup: when both switch at once the later press owns it, so the
    earlier player doesn't see theirs. tests/hudpopup.py.
  - Tests: tests/hudswitch.py, hudhealth.py, hudsoak.py (40 fire/switch rounds, pause, P2 leave,
    rejoin: no crash, manager slot stayed P1), hudboot.ps1, pausefast.py (a save with a
    countdown: Enter on Restart, Esc as soon as gameplay starts; Esc may need a second press).
    "p2hud" prints call counts per function; "p2hud log|nolog", "p2hud refresh <weapon> <ammo>"
    masks. tools/crop.py crops screenshots.
  - Not tested: vehicle weapon panels (hands 5..7 aren't refreshed, so they show the last read in
    both halves), keyboard/mouse-wheel weapon cycling as popup owner (only EQUIP_* tracked),
    the Deck build. Real damage to P2 not tried; health was set in memory (ring followed it).
- FOV per player (2026-10-30). The aim zoom (SHOULDER_CAM 0x09, toggled; key C; pad right stick
  click) sets the camera's FOV +0x618 (radians, 0.698 -> 0.349) and its rebuild flag +4;
  CCamera::Update then rebuilds the projection (FUN_00967660). UsePlayer2Fov swaps +0x618 and
  forces the rebuild for P2's frames, RestoreGameFov after them ("split p2fov on|off"). A first try
  overwrote the projection matrix instead: the rebuild replaced it while the zoom changed, so
  the user saw P1's zoom in P2's half. P2 no longer gets P1's vehicle FOV either.
  P2's own zoom: pad right stick click (WritePlayer2Input toggles SplitRenderZoomPlayer2), on foot
  and on stunt positions (kAimControllers: the controllers with the weapon part, FUN_005DD210, and
  its offset; roof +0xC, cling +0x24, three-node cling +0x20), smoothed in UsePlayer2Fov (real time, QueryPerformanceCounter), aim speed scaled by FOV.
  Stance: sending SHOULDER_CAM for him zoomed P1 too (FUN_005C2600 goes by the manager's player,
  +0x300), so KeepPlayer2Stance calls its end, FUN_0058E060 on the on-foot controller +0xC, with
  him in the manager's slot, and keeps mode (+0x10) in step with the zoom. COnFootController is
  one object shared by all characters: P2's state (+0x10..+0x1F of that part) lives in
  g_p2AimState and is swapped in around his driver call (otherwise his stance blocked P1's aim).
  tests/p2zoom.py: scales sampled during each change. tests/p2stance.py.
- Holstered weapon sway per player (2026-10-30, user: each Rico's weapons moved with the other's
  movement). FUN_005E3100 (found with `watch w` on CWeapon +0x84) smooths the sway in function
  statics 0x01195F74..0x0119601B, one block for all characters. CombatantUpdateHook (private
  vtable copies of CCombatantComponent 0x00EC293C, slot 4 FUN_005CEEE0, found by the HUD pass scan)
  gives P2's run his own copy, under g_swayLock with P1's run. NPCs still share P1's block (vanilla).
  Measured with tests/holstersway.py: P2's weapons relative to him while P1 runs.
  "p2hud sway on|off".
- P2's car camera (2026-10-30, user): 5 m farther back (15 m) while Player2DrivingCar
  (CDriverSeatController, private vtable index 2), eased with kP2CarDistanceRate. Bikes, boats and
  ATVs keep 10 m. tests/p2carcam.py, spot car_stunt in places.txt.
- Audio (2026-10-30): the game decides what plays from its own listener, not FMOD's: with P2 as
  FMOD's only listener nothing played near him ("p2audio dump", "p2audio mode 0|1|2"). FUN_009DD490
  = distance from a point to CListener (position +4 via slot 0); GetPositionHook on a private
  CListener vtable (HUD pass scan) reads the point from that caller's frame and returns the nearer
  view; other readers get P1's view. FMOD Ex 4.20 + FMOD Event. src/audio.cpp hooks the import
  _FMOD_EventSystem_Create@4 (0x00D93844), gives the EventSystem a private vtable copy (slot 31
  set3DListenerAttributes, count slot 29) and sets two listeners, one per SplitViewCamera, with no
  velocity. The main camera alternating views made the one listener jump, so own grapples went
  quiet and everything sounded far off. The game's first calls pass a null position (a log line
  reading it crashed the load once). "p2audio on|off"; jc2ss.ini [Debug] SkipAudio=1.
- NEXT, later: two-monitor mode (a viewer's idea, user wants it in a future session). jc2ss.ini
  setting, split stays the default: a borderless no-activate window on the other monitor, a second
  swap chain on the same device, whole frames instead of bands, HUD strips off, game borderless.
  Watch frame pacing and mismatched monitors. A P2 window could also be streamed for online co-op.
- NEXT: traffic and NPCs near P2 (user asked 2026-10-30; stopped at the usage limit, no code yet).
  Done already: parked vehicles and base spawns (spawn points, world2). Not known yet: how much is
  missing with the players apart. tests/popcount.py counts vehicles and characters within 250 m of
  each; it only ran with them together (7 / 7 vehicles, 25 / 25 characters). Run it with P1 and P2 >1
  km apart in a city first. Leads: CRoadSpawner (vtable 0x00E3427C, 5 slots, two instances; slot 2
  FUN_0079A200 -> FUN_00590810, which writes +0x538), COpenAirSpawner 0x00E3DFCC, COpenWaterSpawner
  0x00E3EC88, CCreatureManager 0x00EBE7E0 (+0x24 is a position, seen near P2), CHeatSpawnManager.
  Spawner slots aren't read yet, so which camera or player they use is open. Likely shape: as with
  spawn points, private vtables on heap objects, run with P2 in the manager's slot / his view;
  keep it behind a "world2" switch and soak it, since spawners crashed around teardown before
  (see the spawn point entries).
- Combat roll (2026-10-30): RB sends EVADE 0x81; COnFootController slot 6 -> FUN_007B6520 -> on press,
  FUN_00A3D500(controller, character). Screenshot-verified for P2, standing and moving, ~5 m
  (tests/p2roll2.py; position in p2roll.py is the wrong field). "p2 roll" calls it directly,
  "p2 rollcheck" logs the checks. The user reported it as missing: not reproduced with padpress.
- Destructible labels per player (2026-11-01, user request). Aiming at a destructible shows its
  name and health bar by the crosshair ("Statue Torso", "Statue Base"), and it showed in both
  halves from P1's aim. It is a CTargetIndicator with flag 0x80 (+0x4C): FUN_00789650 (count) and
  FUN_0068E300 (n-th) list it only while FUN_00769DE0 says the manager's player (+0x3570, aim
  control +0x184) aims at its target. The marker script (chunk with gui_target_indicator_show_event)
  calls Game:GetNumberOfTargetIndicators (0x00A39CA0) and GetTargetIndicatorInfo (0x00A39D70). Both
  closures now go to TargetIndicatorHook<Function>, which on P2's HUD frames (g_markerP2) also puts
  P2 in the manager's player slot. Screenshot-verified (tests/aimlabel.py, spot "statue" in
  places.txt): P1 "Statue Base" and P2 "Statue Torso" at once, and each label goes when its player
  looks away. User: "it looks good". Still shared: the red crosshair (aiming at a target colours both halves' crosshair).
  spot.py now picks P1's camera from the alternating `cam` readings (wrong when P2 stands within
  ~20 m of P1), and it sets only the yaw, not the pitch.
- Pickup icons per player (2026-11-01, user request: a money crate's $ icon showed in P2's half by
  P1's distance). They are proximity CTargetIndicators (flag 0x20): a CDistanceTrigger (interface
  vtable 0x00E4C20C at +8, slot 7 FUN_005F0070 -> FUN_006EEFF0; sphere at +0x70, radius +0x3C, 30 m
  for crates; +0x38 bit 0 inside, bits 4/8/0x10 ignore an axis) tests the manager's player and
  posts the messages in its lists (+0x8C enter, +0x90 exit). The indicator's handler (+0x30
  interface, FUN_00610580) matches them against its keys (+0x30+0x94 activate, +0x98 deactivate)
  and calls SetActive (vtable +0x94, FUN_00749AA0: bit 0 of +0x4C, plus show/hide GUI events).
  Found with `watch w` on +0x4C run while P1 moved (start the watch on a thread: `mod` waits for
  it) and `trace 00749aa0`. The triggers drive mission logic too, so they stay P1's. Instead the
  HUD pass scan collects the triggers and PairProximityIcons pairs each flag-0x20 indicator with
  the trigger whose enter list shares a message with its activate keys, keeping the trigger's
  values. On P2's HUD frames, TargetIndicatorHook sets bit 0 of the paired indicators from P2's
  distance and restores it after the call. Screenshot-verified (tests/pickupicon.py, spot "pickup"
  in places.txt): $ only in the half of the player within 30 m, both ways. Log: "23 proximity icons
  of 36 triggers". "p2hud proximity on|off". Limit: pairs refresh with the scan every 20 s (a newly
  streamed-in crate follows P1 until then).
  Fixed the same day (user: no icon at a crate in front of P2 until P1 came near): the marker
  script (main.4 "show") only draws indicators after gui_target_indicator_show_event and takes
  their number (v.no_tins_on_screen) from GetNumberOfTargetIndicators at that moment. The game
  sends show/hide with P1's count when an indicator turns on or off, so with nothing near P1 it
  hid everything. RefreshHudPanels now fires the show event every frame while the split is on
  (under "p2hud proximity"), so each half's script gets its own count. Verified: crate at
  925.2 201.0 -1761.1 (docks), P2 at 930.5 202 -1761.7, P1 at the safehouse: icon in P2's half.
  Also fixed (user: the icon stayed after P2 picked the crate up): collecting disables the pickup's
  CShapeTrigger (+0x40 interface, +0x30 bit 0; e.g. 0x7C46 collected vs 0x2847), and neither the
  indicator nor its distance trigger change. Each paired icon also keeps the pickup trigger at the
  same spot (PickupTriggerAt; FindPickupTriggers now runs before PairProximityIcons), and while that
  is disabled the icon is off in both halves (PickupTriggerDisabled, only while the object still
  has our vtable copy). Verified at the docks crate: icon gone after P2 collected it.
- HUD strip artifact (2026-11-01, user screenshot, not reproduced). Whole strips of scenery
  (P1's own frame above the band, a beach) were drawn over the top of both halves, with ghosting
  and the HUD panels gone: the HUD-less frame (SplitRenderBeforeHud, at the first CRenderPassAnark
  with something queued) must have been kept before the 3D scene, so everything counted as HUD.
  P1 was on the hideout car's roof (car_stunt). P2 doing the same stunt didn't reproduce it (one
  extra pass, queued 1, for the stunt prompt). Guard added: while the motion blur pass runs (the
  scene's last step; OnMotionBlur stores the present count), the frame is kept only after it in the
  same frame. Normal play: kept 1875 times, skipped 0, so it costs nothing. Diagnostics if it comes
  back: "p2hud passes" (passes that drew in the last frame), "split hudblurgate on|off|stats".
  Weapon panels don't appear on a jc2ctl left click even solo, so they can't be used to check the
  strips from scripts.
- P2's health parameters (2026-11-01, user: P2's health bar dropped when P1 took damage).
  tests/healthrec.py showed P2's real health going 400 -> 50 in one hit, max following it down. P2's
  CPlayer +0x1B4..+0x290 (health/regeneration parameters from a parameter file plus armor
  upgrades; e.g. +0x1EC 1000 for P1, the default 50 for P2) are defaults in a player created
  mid-game. StartDriving copies P1's block (kPlayerHealthParams, leaving out the pointers at
  +0x21C/+0x220). Verified the blocks match after joining. Max health also follows health down
  for P1 (game behaviour). Not verified by the user yet with real damage.
  The actual cause of the report (user again: P2's bar dropped while only P1 was shot; P2 was at
  400/400): the green ring (health_mat.positionV) = v.health * 0.004 (get_health, on
  player_health_changed_event, so it shows a frame later) + 0.2 * GetResistanceUnits, which the
  per-frame update reads itself and shows in the same frame. HudLuaHook gave every ring function
  the other half (!g_markerP2), so P1's drained resistance meter (CPlayer +0x1B0, 0 after hits)
  showed in P2's ring. GetResistanceUnits now uses this frame's half like the weapon functions.
  Verified: both at 150 health, P1's meter held at 0: P1's green ring shorter than P2's. The red ring
  is heat (one wanted level, shared). GetMaxHealth is read once (cached by the script).
  Then (user: P2 died in one hit with his bar unchanged, and took no damage after respawning):
  P2's resistance meter (+0x1B0) stayed 0, since only the full CPlayer::Update refills it
  (FUN_005BF690, which also posts air supply GUI events and shakes the camera, so it isn't run
  for P2). Hits went straight to health. After the death, max health was -4 (max follows health
  down) and RespawnTick gave 100 health with that max. RegeneratePlayer2 (end of Player2Update)
  does the meter part: delay +0x1CC, rate +0x1C4, up to +0x1B4[+0x23C], the share +0x240, and the
  +0x1F0 decay. The respawn restores health and max to g_p2FullHealth (P1's max at join) and fills
  the meter. Verified: meter 0 -> 190 at 50/s after a 2 s delay; `p2 kill` -> respawn 400/400,
  meter 190. Real hits on P2 (do they drain the meter first, as for P1) are not checked yet.
- HUD strip overlays (2026-11-01, user: the bleed came back while the low-health heartbeat
  played after heavy damage). Each GUI pass draw is wrapped in an occlusion query
  (SplitRenderHudPassBegin/End from HudPassDrawHook, read back on a later frame without waiting).
  A pass with at most 2 queued items covering over half the screen counts as an overlay. The
  strips then take frames right before and after it, and PSStrip counts only what changed
  outside it. Normal passes cover 0.001-0.032. Setting P1's health to 30 only shows the
  desaturated post effect (no extra overlay coverage, no bleed), so the real damage overlay is
  still to be measured: "split hudblurgate stats" lists each pass's queued count and coverage.
- Testing money (2026-11-03, user request for black market tests): F9 (overlay) and "money [n]"
  add $50k / n on the next game tick. Money = manager's CPlayer +0x3BC (CBlackmarketHook) value 3
  (+8 + 4*3) = CPlayer +0x3D0, an int (Lua GetMoney FUN_00A335C0 -> FUN_0062E470(3)). Verified
  15900 -> 65900 (command) -> 115900 (F9).
- Background scan speed (2026-11-03, user: at the Reapers HQ some boxes opened for P2 only on a
  second try). Triggers only let P2 in once ScanForHudPasses found them (private vtable, lists),
  every 20 s, and a scan took ~7-10 s (FindScriptPatches at every dword). Now full scans every
  20 s and fast scans (only values in the exe vtable range or our shape trigger copy) every 2 s or
  twice their time (~3 s each). Bug found on the way: the scan rewrote its own stack (locals
  holding vtables swapped for our copies); it now skips its own stack region. HQ boxes: found
  ~11 s after a teleport there (streaming included). "p2hud pickups" reports scan times and counts.
- Weapon box icons for P2 (2026-11-03, user: they didn't show with P2 far from P1). Box icons have
  no activate keys and stay active (bit 0); they are selected (FUN_00789650 / FUN_0068E300: bit 0
  and (flags & 0xC or 0x10)) while bit 0x08 is set, which the game sets for P1 within the 30 m
  CDistanceTrigger at the box. PairBoxIcons pairs each box icon trigger's indicator (+0x20) with
  that radius; on P2's HUD frames SetProximityIcons sets 0x08 from his distance (ProximityIcon
  .bit, restore by mask). Verified: P2 at reapers_hq, P1 1.5 km away: P2's frames 7 markers
  (P1's 4), three box icons in P2's screenshot; user: works. "p2hud log" logs box icons and marker counts.
- Weapon boxes for P2 (2026-11-03, user: boxes opened for P1 only; P2 could take the weapon once
  P1 opened one). The box is a smart object (CWorldsimSpawnableSmartObject); its CSmartObjectTrigger
  (vtable 0x00F0D19C, filter FUN_00886B30 = any CCharacterBase) offers a CWeaponPickupAction
  (0x00EC2F0C) each frame to characters inside, and the action opens the box (smart object +0xF8 =
  user, char+0xF0). That trigger's phantom (hkpCachingShapePhantom, layer 11) is only in the physics
  world while the box is active: an "activate" message (handler FUN_008CDFB0 -> FUN_008EF6E0)
  from a player-only CShapeTrigger at the box. The box's other shape trigger links its proximity
  icon (+0x20, CTargetIndicator 0x00EB8FA4, flags & 0x20; pickups' and mission areas' triggers
  have none). Now FindPickupTriggers counts the shape triggers within 1.5 m of an icon trigger as
  the box's (filter lets P2 in) and ShapeTriggerEnterHook (slot 4, FUN_005766A0) runs his entry
  with him in the manager's player slot, so the activate message goes out for him. Verified: P2
  walking into a fresh box set its user to P2 (P1 ~17 m away); user: works, black market boxes too. Dead end: P2's phantoms with P1's
  system group 4 (filter info +0x2C) aren't needed. "watch xe|we|rwe" = a report row per ECX.
  Place weapon_box in tools/tests/places.txt (save loads next to it).
- P2 sprint shake (2026-11-02, user: P2's sprint (RB) shook P1's camera). Sprinting (FUN_005FF2F0)
  calls the camera shake FUN_0070D460 on the camera control manager [0x011900F8]: +0x24 amplitude
  (keeps the larger), +0x28 rate, +0x2C decay/s, +0x30..0x38 scale (each call sets 1). FUN_005E7B90
  applies it to the game camera: offsets eased toward random targets (scale * amplitude) at the
  rate, used as yaw/pitch/roll radians and added to the position. PlayerJobHook now sets amplitude
  and x scale to 0 around Player2Update and restores the state after; a nonzero x scale means P2's
  update shook, and his shake goes to Player2CameraShake (same easing; yaw/pitch and position in
  MakePlayer2Camera). Measured: P1's amplitude 0 while P2 sprints (was 0.005); P1's own sprint
  0.005; P2's state 0.005 while sprinting, fading to 0 after. User: feels good.
- P2 footsteps (2026-11-02, user: P2 makes no footsteps). His footstep anim events (character
  +0x978/+0x97C, fx_right/left_footstep, handled in FUN_0055CDC0) and footstep effects ran, but
  he had no sound object (+0xB6C; CPlayerSounds 0x00EC26A8 for players, flag +0x728, else
  CCharacterSounds 0x00EC2600). FUN_006FBFE0(character, dt) makes it on first use and updates it;
  the characters' update FUN_005CEEE0 skips it for players and only the local player's update
  runs it. Player2Update now calls it for P2. "p2audio dump": P2 running 13 -> 16..18 channels
  (before: none at him); user: footsteps work. The "watch" report now also lists the first code addresses on the stack.
- Dual wield for P2 (2026-11-02, user: holding D-pad right must always dual wield, and it
  stays until the next press; P2 put the other weapon away on release). Fixed in the input
  timers, not with a remap: the on-foot weapon part FUN_005DD210 takes EQUIP_RIGHT_SLOT (0x72)
  held (state 3, timer FUN_00982460) past [0x0107740C] as dual (hand mode 4, inventory +0x64),
  and a release (state 4) whose timer (FUN_00985A40 = how long it was held) is shorter as a tap
  that switches the right-hand weapon. SetPrivateAction wrote 0 as the release timer, so every
  hold ended in a tap. Now the release timer is the held time; D-pad right goes through as is
  (the TapOrHoldEquipRight / EQUIP_DUAL pulse workaround is gone). tools\tests\dualhold.py:
  holds 1 -> 4 that stays after the release, taps 4 -> 1. User tested: good.
- Markers off the half (2026-11-02, user: a mission marker going out of a player's view showed
  again near the top of his half). GetTargetIndicatorInfo returns y in HUD units (full frame
  -360..360). Off the whole screen the game pins it at +-360 with opacity 0, hidden. A half shows
  only -180..180, so markers in 180..360 sat in the top quarter and the HUD strip pasted them at the
  half's top. TargetIndicatorHook now sets the last result (opacity) to 0 when |y| > 180, writing
  the Lua stack slot (L->top at +8, 8-byte {tt, float} objects, tt 3 = number, checked first).
  Verified: P2 pitch 0 marker once in place; pitch 10 (y 252) no marker. Then (user: still seen
  when half out of frame): the strip pastes the full frame's top quarter onto the half's upper
  part, so a marker piece over the edge came back inside the half. The limit is now 160 (180 less
  the marker's ~20-unit half height). Verified: y 131/148 shown once, 165/182/200 hidden. "p2hud log" also logs
  marker x/y/opacity per half twice a second.
- Mission popup lower (2026-11-02, user: "Agency Mission Started" pressed against the top edge,
  under the status line in P1's half). It is the completion script (luadump chunk "show_completion",
  fired through gui_show_status_text -> GetStatusMessage -> its own gui_trigger_completion Lua
  event; neither event can be fired from outside to show it). Its root node's y is set once at the
  HUD's init after each load: all.position.y = 138 (50 with the larger HUD size). That node is an
  Anark CAttributeMapGroup (vtable 0x00F5F854) with y at +0x60. KeepMissionPopupPlace (game tick)
  finds it with a heap scan (every 2 s for a minute after the split comes on or the node is lost,
  then every 30 s) and keeps 60 while split, 138 otherwise. User: "that looked better". The first
  ~1 s after a load it still shows at the old spot (the node is found after the HUD's init).
  Test: tools/tests/loadjoin.py (P2 joins from the menu, screenshots during the load),
  tools/tests/popupy.py. Also new: "guievent <name>" fires a GUI event, "split hudtop <f>" (top
  strip shift, default 0: a shift cut "Get to the Casino" in two at the minimap corner boundary).
- 2026-11-02: KeepPlayerHitHooks is OFF. With the players' characters on a private vtable no
  hit reached them at all (user: P2 seemed to be in god mode; the hit log stayed empty), so the
  game's hit detection compares the character vtable itself. Never give characters a vtable copy.
  Done: P2 gets P1's damage table at join (kCharacterDamageTable, verified equal). Still open:
  bullets never / melee both ways, by some other route (the team check is inside the take-hit
  method; a hook needs a call site that isn't through the character's vtable).
- OPEN (2026-11-03, user: flying P2's jet to an oil rig far from P1 it blew up as if hitting a wall;
  P2's minimap showed chunks of other terrain). Code reading, not tested in game:
  collision: physics heights come from P1's CTerrainMapGame, 8x8 slots, slot = (cell x & 7,
  y & 7), and the heightfield lookup (FUN_00991900 -> FUN_00a56400) takes whatever cell the slot
  holds. ApplyPlayer2Collision (terrain2.cpp) points the 5x5 slots around P2 at P2's buffers but
  skips (1) slots P1 needs for his own 3x3 cells and (2) cells P2's map hasn't loaded yet. Far from
  P1 the slots repeat every 8 cells, so P2's 3x3 overlaps P1's 3x3 for 25 of 64 offsets (~40% of
  places): P2 then collides with P1's ground heights (invisible hills / walls). A fast plane also
  outruns P2's stream window (case 2), leaving stale cells under him. Possible fixes: a private
  vtable on the heightfield shape object choosing the map by the queried cell (P1's slot if it holds
  that cell, else P2's), or a separate shape for P2. Minimap: MinimapUpdateHook only swaps player
  and camera; the map image comes from streamed tiles, probably a shared cache with the same
  aliasing (not yet read in the code).
  Collision fix (installed, not yet tested in game): the physics ground is CCustomSampledHeightFieldShape
  (vtable 0x00F27970, 19 slots; CLandscapeManager [0x01191C48] +0x1485C, made in FUN_00a5e730 on the manager's +4 part; the
  first build used +0x14858 and never hooked). Its
  slot 9 (FUN_00991900) samples each point with FUN_00a5f600 on [0x01190290]. The shape gets a
  private vtable copy (KeepHeightFieldHooked, terrain2.cpp) whose slot 9 samples each point on the
  map whose slot holds that point's cell: the one in, else P1's or P2's (g_collisionMaps, set in
  ApplyPlayer2Collision). Log line "height field shape ... samples per cell". Test 2 (log line
  "ground samples ... P2 cell 28 28 at x y z (ground 537)"): P2 stood on a 537 m hill over the sea
  because a slot takes its new cell at request time and keeps the old cell's buffers until the
  patch is in (+0x14 loaded flag); sea cells near the rig stayed unloaded. Now a slot counts only
  when loaded, and while both stream a point no map has loaded gets no ground (installed after
  test 2, untested).
  Test 3: still a hill: P2's map slot for cell 28 28 is loaded and labelled 28 28 but its heights
  say ~520-535 m around (-2080, -1877). With P1 moved there ("p1top2") P1's own map gives 180.5 m
  (sea bed) at the same spot: P2's map has wrong heights in a slot it thinks holds 28 28. Next: how
  P2's height patches reach his slots (StreamPatchLoadedHook / ReplayLoads: the file name parse
  "%02d%02d", the slot chosen, a stale kept file, or sea cells without a file keeping old buffers).
  Found (teleport repro, "terrain2 slots x z"): both maps' slot held 28 28 with the same heights,
  yet sampling gave 523 m on P1's map and 181 m on P2's. FUN_00a5f600 picks its slot with
  floor((x + 16384) / 512) (HeightCell), StreamCell adds 256 m (it centres the stream window). So
  in the west/south half of every cell the hook (and Terrain2GroundHeight) checked the label of
  the wrong slot and sampled P1's map, whose slot for the real cell held one of P1's own cells (a
  hill near P1). Point lookups now use HeightCell. User: fixed; flew P2 past the oil rig into a
  mountain and the terrain there collided correctly.
  Minimap chunks: the minimap update (FUN_00802D40 -> FUN_00745990) takes each tile from the map
  texture handler [0x01191224] with FUN_00a5e950 (slot of cell & 7, no cell check; empty cells
  get its +0x508 default). Outside the hand-off that global is P1's (ground state), so P2's map
  showed P1's tiles wherever their slots met. MinimapUpdateHook now puts P2's handler in around his
  update (Terrain2UseMapTexture; only the stream loader and the minimap read that global).
  Swapping only for P2 blanked/mixed P1's map: the global is whichever ground state is in, often
  the other view's while its frame draws, so P1's update now gets P1's handler too.
  Tested by teleporting P2 (screenshots, crops of both minimaps): P2's map shows proper terrain at
  -6000 -6000, -9000 6000, 8000 -4000 (and dark at sea by the rig). P1's map is right at the last
  two, but with P2 at -6000 -6000 part of P1's map showed other tiles (a bay); with "p2minimap
  off" it was right, so P2's minimap update still leaks into P1's. FUN_00745990 sets each tile
  into one brush at minimap +0xAC (FUN_00a75c40) and queues sprite draws (FUN_00806150 on the
  sprites in the vector at +0xA0); maybe the draws resolve the texture later, or both updates'
  draws reach one HUD. Not empty tiles (logged: 0 of 64 empty in both handlers). Cause: the tile
  sprites keep their texture and the render job reads it while it draws; the next game update
  (the other view's minimap update) runs while the hand-off's render job is still drawing and
  re-textured them under it. MinimapUpdateHook now first waits for the frame in flight
  (Terrain2WaitFrameDrawn: engine [0x0118FBDC] +0x28, then 0, as the hand-off does). Screenshots
  with P2 at -6000 -6000 (x3) and -9000 6000: both maps right. Frame rate: ~120 fps (600 frames
  per ~4.9 s of hand-off log lines) vs ~143 earlier sessions in other scenes, not measured like
  for like. User: looks good.
- OPEN (2026-11-03, user: "weird leaf textures in the sky for p2"). Place sky_leaves in
  tests/places.txt (P2 there, "p2 face -0.072 -0.6 0.997" looks up into the canopy; screenshot
  tools/shots/leaves_p2.png). Seen: thin fog-blue frond/blade shapes drawn only over sky pixels in
  P2's half when he looks up under trees; none looking level; P1's half clean. Ruled out: falling
  leaves (CLeafManager [0x01382030], per vegetation copy; P2's has 0 alive, "terrain2 leaves
  <1|2> [chance]"), P1's camera/position (same with P1 319 m away), P2's landscape camera
  (g_landCamera is his). "terrain2 vegmask 600" (P1's renderers + render states for P2's turn):
  no clear change. "terrain2 vegmask 7FFFE" crashed (fault 00A8CAD5) - don't. Next: try single
  vegmask bits, or find the leaf-card / fin draw's camera constants.
  Intermittent: after a fresh load + teleport to sky_leaves (overcast sky) none, also after 15 s
  and after "p1top2"; vegmask C and F000 then showed nothing either way. Earlier sessions (blue
  sky) had them. Needs a live repro (user tells when they show) before more mask tests.
  Live repro (user, place sky_streaks: P2 on a rooftop, open sky, no trees overhead; screenshot
  tools/shots/leaves_p2_more.png). Streaks are world-fixed (move with the view). Results:
  "terrain2 partial" (P2's sky black): streaks still, bright on black, so not the sky/weather;
  "terrain2 off": gone; "terrain2 rebuild2": still; vegmask 2, C, 20, 200, 400, F000: still;
  vegmask 3 and 1: gone. So it is P2's own CVegetationManager (range 0, 0x011902F0) in his turn.
  "terrain2 vegfields" (read only): the manager's object fields are +0xC CBreakableTreePatchHandler
  (own), +0x4020 CLeafManager (own; falling leaves, 0 alive for P2 -> ruled out). Suspect: P2's
  breakable trees (palms; CBreakableTreePatch are CGameObjects the handler makes per stream cell,
  vtable 0x00EC17E4; handler 0x00EC1768 slot 1 FUN_00784B80 creates one) - maybe never updated
  for P2 (fronds' matrices garbage -> stretched), or the manager's inline data (0x414C bytes).
  A vegmask 1 test right after a fresh load + teleport made the game quit (no fault logged).
  Repro is reliable now: fresh load, join, "p2 at 700.40 241.26 -2514.30", wait 10 s,
  "p2 face 0.653 -0.200 0.731" (P1 at the hideout, adjacent height cells, so no height aliasing
  here). "terrain2 vegfield <hex start> [hex length]" (new): during P2's turn those bytes of his
  manager hold P1's. +C (breakable handler) and +4020 (leaf manager) alone: streaks stay - so it
  is the manager's inline data. Swapping +4..+2004 or +2004..+414C crashed (fault 00A8CA65 in the
  tree walk FUN_00a8c9a0 -> FUN_00a89b90; 7FFFE crashed at 00A8CAD5, same place): big swaps are
  unsafe. Next: read FUN_00a94250 (manager ctor) / FUN_00a8c9a0 for the inline layout (tree
  instance lists, per-cell arrays) and compare P1's and P2's field by field, or read what the
  vegetation draw jobs read (they may run after P2's turn, with P1's globals back in). Ray casts
  (FUN_00a64a90) still go through the slot redirect only. Minimap not changed.
- P2 black market (2026-11-03, user: P2 calls the dealer like P1 and drives the menu he opened;
  the drop by P2; only when P1 has the beacon). P2's pad: D-pad down EQUIP_BLACK_MARKET_BEACON
  (0x95, was EQUIP_DUAL), Y ACTIVATE_BLACK_MARKET_BEACON (0x96); FUN_007FA910 (on-foot weapon part).
  It needs inventory +0xD4..+0xE0 (item counts; +0xDC >= 1): P2's were -1, now copied from P1's
  every tick (KeepBlackMarketForPlayer2). The held beacon (character +0x470, CFlare) places its
  target in its +4 interface slot 7 (FUN_00839480: ray from the main camera, aim reach of the
  manager's player) and sets "can call here" ([0x011979F8]+0xD3); P2's flare gets a private copy
  of that vtable (0x00E3A1F4, 47 slots: a 10-slot copy crashed, call to 0) whose update runs with
  him in the player slot and his view in the main camera. His Y with the beacon out and the flag set
  claims the next black market menu (UI mode [0x011958C4] 5): while open, the game's XInput reads of
  pad 1 get pad 2 (GameGetStateHook, splitscreen.cpp). User: works. Known: P1 is moved to P2's
  beacon spot for the delivery (game behaviour); left as is for now (user).
- Player markers (2026-11-03, user: each player sees where the other is). Drawn by the overlay, not
  the game's indicators: SplitRenderMarker (splitrender.cpp) projects the other player's position
  + 2.3 m with the view's last world matrix and FOV (camera +0x618 after each view's update,
  g_viewFov) and the frame's aspect; the half is the full frame's NDC y -0.5..0.5. Off the half or
  behind the camera it goes on the half's edge toward him. DrawPlayerMarkers (overlay.cpp): dot,
  stem, label "P2 26 m" (P1 blue, P2 orange; two more GDI text textures). Screenshots: on screen
  over the head at 10-27 m in both halves, off screen pinned to the edge. User: looks good.
- P2 plane controls (2026-11-03, user: in the Dragonfly P2 could thrust and shoot but not turn or
  pull up). His actions did reach the plane: CAirPlaneController input (FUN_00797130) writes
  CAirVehicle +0x200 thrust, +0x208 roll, +0x210 turn, +0x214 pitch (tests/p2plane.py with
  "p2 sticks lx ly rx ry lt rt ms": all 1.00 when held; thrust alone took off). The layout was off:
  PLANE_ROLL_LEFT/RIGHT have no pad binding for P1, so his pad's left stick X turns; P2's left stick
  X rolled. Now left stick X turns, left Y pitches, right stick X rolls. User: controls work now.
  The plane is P2 character +0x2E0 (smart object) -> +0x18, minus 4. Careful: "watch x" on the
  setters (0066AF60 etc.) tripped the 8000DEAD exit. All planes use CAirPlaneController (one class).
  Plane camera (user: further back): Player2PilotingPlane gives planes the helicopter's follow camera
  (behind the heading, right stick doesn't orbit) at 30 m back, 2 m up (user: down a little from 4); "split planecam <d> [h]".
- Button prompts per player (2026-11-03, user: the B/Y on a car and the "Stunt jump B" panel at
  the right edge showed in both halves from P1). One double-buffered block of the HUD object
  [0x0118FC20] at +0x1C0 (layout in player2.cpp above PromptHook). Player 1's HUD update
  FUN_00792510 (world update 0x0078225B) walks his character's context actions (vector at char
  +0x314, 12-byte entries {id, shared_ptr}) and FUN_00624920(hud, &entry+4, id) (thiscall, ret 8)
  writes each prompt into the write half; the GUI update flips (FUN_0083D520, from FUN_0052B0F0,
  fires gui_facebuttons_changed). Script (luadump chunk with GetFloatingButton) reads
  Game:GetFloatingButton 0xA31E00 / GetStuntButtonInfo 0xA29540 every frame; the panel script
  reads GetFacebuttonString 0xA29740 / GetFaceButtonKey 0xA36680 only on that event and animates
  (fade, 14 s timer). Found with `watch w` on the block. Now: Player2UpdatePrompts (from world2's
  RenderWaitHook, right after P1's) runs the same loop for P2 into a private copy of the HUD object;
  PromptHook (closures redirected by the marker scan) swaps P2's read half in on his frames for the
  floating buttons. The panel can't alternate per frame, so it shows one player's rows (P1's if he
  has any, else P2's) and the other half hides its rect (SplitRenderHideFacePanel, band x
  0.84-1.0, y 0.15-0.8; "p2hud prompts rect l r t b"). P1's own rows flicker every other frame
  while split (his context actions differ on P2's frames, e.g. "P1 stunt 1" on his frames, 0 on
  P2's), so each player's rows are held 400 ms (g_heldPrompts) and the panel reads those; on an
  owner change it reads no rows once so the new owner's fade in. The floating script's fade
  factors are patched to 1 (kFloatingFadeCode, script patches 4 -> 6). Screenshot-verified: P2
  alone at the hideout car (car_stunt), P1 alone, both (each half its own B; panel in P1's half),
  P1 leaving after the 14 s timeout (panel fades in for P2). "p2hud prompts [on|off]" reports rows
  and flags; "p2hud log" logs owner changes and per-half flags. Limits: both with rows at once ->
  only P1 gets the panel; P2's prompts use P1's input mode (keyboard text like LALT when P1 last
  used the keyboard, pad glyphs when P1 uses a pad). Not changed: the green "( )" ring at the car
  in both halves (likely the crosshair's grapple indicator, see the older note). User played it:
  "looks right". In dist/JC2Splitscreen.zip; not run on the Deck yet.
- Camera wall collision (2026-11-03, user: cameras clip into buildings; spot cam_wall in
  tests/places.txt, P2 there with "p2 face 0.999 0.023 0.047", P1 with tools\spot.py). Both
  cameras (P2's orbit camera; P1's camera where PushPlayer1Back moves it back) come in along their
  line to where the game's ray cast hits, less 0.4 m (KeepCameraOutOfWalls, splitrender.cpp), at
  once, and ease back out at 4/s; min 0.6 m. P2's ray: head (body +1.8) to camera; P1's: the
  game's own camera position to the pushed one (the crosshair line). Ray cast = the aim's:
  FUN_009A9A50(thiscall on [0x0118FFF4]: ray {origin, unit dir}, min, max, result*, filter*, 0, 0,
  0; ret 0x20), filter CPlayerAimRaycastFilter (FUN_009ACCA0(0,0,0), vtable 0x00EB4D74,
  FUN_005EF990(1)); it skips P1's body and vehicle, AimFilterHook skips P2's body. Result +0 body
  (0 for terrain), +4 normal, +0x10 fraction; no hit = the collector's +0x60 empty. Cast from the
  camera update or at Present it never hits anything (not even the ground); cast from the brain
  think (where the aim casts) it works: the cameras leave their rays in g_wallRays and
  BrainThinkHook calls SplitRenderCastCameraRays, so the answer is one frame old. P2 in a vehicle:
  the ray starts 3.5 m (car) / 10 m (heli) / 12 m (plane) out, since his own vehicle isn't skipped
  (untested). "split camwalls [on|off]" reports allowed distances and pull counts; "split camwalls
  test x y z dx dy dz len" then "split camwalls result" casts a test ray. Screenshot-verified at
  cam_wall for P2 (inside the glass front -> 3.5 m behind him, outside) and P1 (black wall -> just
  outside), and at the hideout (P1's camera was inside a hut wall behind him; now in front of it).
  User played it: "feels right". Not tested: vehicles, thin objects (lamp posts, palms: the camera
  may bob in and out). Not in dist/JC2Splitscreen.zip yet.
  Seen while testing: the first "p2 at 956.23 200.94 -1742.68" blew up a gas pump at that station
  (chaos +, heat); "p2 at" 0.5 m higher didn't.
- Grapple crosshair per player (2026-11-03, user: P1 looking at the floor made P2's crosshair
  spin while P2 looked at the sky). The crosshair script (luadump chunk with GetCrosshairLook)
  reads Player:CanGrappleTarget (0xA39AD0) every frame, which returns the byte 0x01197A78 (in the
  singleton 0x011979F8 +0x80). FUN_0075FA20(CPlayer), the brain think's last call, sets it from
  FUN_00764C90, which uses the manager's player's aim control: P2's think set P1's answer, and
  both halves showed it. BrainThinkHook now runs FUN_0075FA20(P2) again after P2's think with P2 in
  the manager slot, keeps each player's byte (g_canGrapple), and HudLuaHook gives each half its
  own (same-frame, like the weapon reads). GetCrosshairLook (0xA33880, aim control +0xA8: the
  colour, red on a target) and GetAccuracy (0xA34410, spread) were added to the same list, so they
  are per half too (colour not tested with an enemy in view). Screenshot-verified (crops of both
  crosshairs): P1 ground + P2 sky -> ring only in P1's half, and the other way round. Side effect:
  FUN_00764C90 also writes its state into P2's own object (+0x44), which P2's grapple may read.
- Ammo on join (2026-11-03, user request): joining and rejoining (Start after Back+Start) refills
  both players' ammo like F5, one second after (Player2RefillAmmoSoon: AutoJoinTick for the first
  join, BringPlayer2Back in splitscreen.cpp for a rejoin, which keeps P2 driving and only
  teleports him). Log "P2 join: ammo refilled (as F5)"; verified for both paths with p2 leave/join.
- F5 refills grenades and C4 (2026-11-03, user request). They aren't ammo types: counts at
  inventory +0xD4 (grenades, item 0) and +0xD8 (triggered explosives, item 1); +0xDC beacon. Max =
  FUN_0064E790(weapon manager [0x011917F4], item, 1) (thiscall, ret 8; from the black market
  upgrade level; FUN_005068A0 compares with it). RefillAmmo raises each to that max (10 / 10 in
  the test save). Verified: P1 set to 2 / 3, "ammo" -> 10 / 10. Note: KeepBlackMarketForPlayer2
  copies P1's four item counts to P2 every tick, so P2's grenades and C4 are P1's (his throws
  don't lower his own count for long; P1's throws lower both).
- Foliage per player (2026-11-03, user: P1's plants change the plants around P2; the older entry
  "foliage changes for one player when the other moves away" below). Spot: P2 at 896 224 -2028,
  "p2 face 0.999 0.1 -0.04" (the hideout deck), P1 moved with "p1 at". Two separate causes:
  1. FIXED: placed vegetation (palms and lawn plants around Karl's hideout) was missing in P2's
     copy at any distance. A location's CModelCollection load (FUN_008A40F0) adds each plant with
     FUN_00A93450 -> FUN_00A91480 (stdcall: cell key {x,z} shorts, type, unused*, position, flag,
     zone, out index; ret 0x1C) into the instance vector 0x013820F0 (16-byte entries: position,
     +0xC flag, +0xD type, +0xE in use, +0xF zone) and the 19x19 cell lists (128 m cells,
     origin -16384, scale 1/128), and the unload (FUN_00909AF0) removes them with FUN_00A935C0
     (stdcall short). That runs in the world update with P1's state in. MirrorPlacedVegetation
     (terrain2.cpp, start of P2's turn) adds new/changed P1 entries to P2's copy and removes gone
     ones (~715 entries at the hideout). Screenshot-verified: palms in P2's half with P1 near and
     far. "terrain2 placed [on|off]".
  2. OPEN: P2's small ground plants take the TYPES of P1's surroundings: P1 in P2's 128 m cell ->
     lawn grass (correct); P1 70 m away in the next cell -> red heliconias; P1 320 m away ->
     grass; P1 3.4 km by a road -> roadside shrubs. "terrain2 keep veg" (P2's turn with P1's whole
     vegetation state and zone fields) gives the right plants. Ruled out: P2's manager object
     (unchanged byte for byte when P1 crosses the cell border, tools/tests/vegdiff.py), keeping
     P2's vegetation state in through his frame ("terrain2 vegstay"), regenerating every turn
     ("terrain2 vegregen 3", no visible change, cost unchanged), the whole CClimateZone object per
     player ("terrain2 wholezone"), keep ground, landstream1, sharedtex, and every vegetation-code
     global outside kVegetationState (tools/tests/vegglobals.py: only scratch buffers of
     FUN_00A92210 at 0x0138C2A8..0x013982D8 + 0x013BD4D0, a flag 0x0108C59C, a frame index
     0x013C16F8). The vegetation set collection only reorders list links (+0x74/+0x78, +0x94/+0x98
     of the zone sets) when P1 moves (tools/tests/vegsets.py). "terrain2 vegmask 1" crashed
     (fault 00A8CA65, as before). Next: diff heap objects the vegetation jobs read (type tables
     +0x4C of each zone set, 0xD0 each; the leaf/grass renderers in kVegetationShared), or
     trace which code picks a ground plant's type (the jobs FUN_00A88810 -> FUN_00A8E56B ->
     FUN_00A8CF91).
- FIXED (2026-11-04, user: a killing hit left P2's ring where it was until the respawn). The ring
  is health (GetHealth: % of player +0x1A4) plus the resistance meter (+0x1B0). HudLuaHook showed
  P2's meter at its maximum for every GetResistanceUnits read (a workaround from before
  RegeneratePlayer2 kept his meter); removed, the ring reads his real meter. RegeneratePlayer2 no
  longer refills a dead P2 (meter 0 while dead; the respawn fills it). Verified with grenadep2.py:
  ring empty while dead, full after the respawn.
- FIXED (2026-11-04): after P2 was killed for real and respawned, hits only drained his meter.
  The health write FUN_007E87A0 sets the dead byte character +0x40 at 0 health; the reset
  (place-in-world) leaves it, and the damage apply FUN_00681480 returns 0 while it (or +0x41) is
  set. The game's own respawn clears it; RespawnTick and the cutscene put-back now do. "p2 kill"
  calls the death vfunc without the health write, so it never showed in tests: use
  tools/tests/grenadep2.py <n> (P2 1.2 m beside P1, P1 drops a grenade, ~310 damage each; its
  P1-health refill sometimes picks P2: the manager slot swaps). Verified: killed by grenades
  (dead 1), respawned dead 0, next grenade 400 -> 187.
- P2 after respawning / P1's explosives (2026-11-04, user: after P2 died his ring stayed full
  until he suddenly died; after a reload P1's rockets couldn't kill him; wanted P1's explosives to
  hurt P2). Tested with P2 1.2 m beside P1 ("p2 tp 1.2 0 0"), P1 looking down, right mouse
  (grenade): P2 400 -> 90 (max follows), meter 0, delay 5 s set by the game; after "p2 kill" and
  the respawn (400/400/400, meter 190) the same grenade took him to 90 again and both rings showed
  it. So P1's grenades already hurt P2 and the ring updates after a respawn; the user's cases are
  not reproduced. In the build: "P2 health: ..." log line on every health change or meter drop
  (LogPlayer2Health), and TakeHitInPlaceHook (take-hit slot 6 hooked in place at 0x00EB8834) logs
  hits on P2 that the team check (damage[3] == character +0x480) would drop ("P2 hit by team
  mate", damage object vtable) and lets them through if that vtable is in g_explosionDamage
  (empty for now: no such hit was seen). Next: read the log after the user's next case (rockets
  after a reload).
- Health fixes (2026-11-04, user: P1 at times took no damage, both health bars off, worse after P2
  died and respawned). Three causes: (1) "god on" from my own tests was still on in the user's
  session (health 100000); "god off" now also puts health back to the maximum. (2) A character has
  a base maximum (+0xE4), health (+0xE8), maximum (+0xEC); place-in-world (slot 30) runs the
  character reset, which sets health and maximum to the base. P2's base was the runtime player's
  default 50 (the join copied only +0xE8/+0xEC), so every teleport/respawn/ground hold made him a
  50-health player; and teleports healed hurt players (the ground hold places every frame).
  PlaceKeepingHealth wraps all our place-in-world calls (keeps the three values of a living
  character); the join copies all three; KeepPlayer2BaseHealth (tick) keeps P2's base = P1's base
  (Player2FullHealth); respawns set all three to it. Verified with memory reads: P2 400/400/400
  after joining, hurt P2 (200) and hurt P1 (250) keep it through teleports, "p2 kill" -> respawn
  400/400/400. Not checked: real damage from enemies and the ring's look while hurt.
- Flak cannons for P2 (2026-11-04, user: P2 couldn't rotate the flak cannon, and P1's view went
  to the cannon when P2 used it). Spot "flak_p2" in places.txt (Y gets on and off). The fixed flak
  cannon isn't a CMountedGun: it's a scripted object (CEventAxis 0x00EB2B6C turns it); its user
  gets a CEventCharacterController (0x00EC26C0) whose update FUN_007B4980 reads the actions in its
  list (+0x10..+0x14, 8-byte entries) from the input context: FIRE_RIGHT and LOOK_UP/DOWN/LEFT/
  RIGHT (3..6). P2's pad never sent the look actions (his right stick drives our camera), so
  WriteVehicleInput now sends them from his LEFT stick (user: with the right stick the camera went
  round several times before the gun turned; right stick = camera only; user: "feels right") while Player2OnMountedGun() (that
  controller with look actions in its list, or a vehicle's CMountedGunController 0x00EC23E4, now
  also in kVehicleControllers for aim lending). Camera: getting on/off, the script messages the
  cannon's CCameraObject; its +0x30 handler (vtable 0x00E4A9D0 slot 1, FUN_00626630, thiscall 2
  args, ret 8) switches the one game camera (camera control manager setup -1, +0x10/+0x14
  toggled). Not via SetCameraSetup, and sent from inside P2's update (his enter action). The HUD
  pass scan gives CCameraObjects a private vtable copy; CameraObjectMessageHook drops the
  messages while in P2's update or while P2 is getting on/on/off a fixed gun, unless P1 is on one
  too ("P2 gun: camera message ... dropped"). Screenshot-verified: P1's view stays his on
  entering and leaving; P2's right stick turns the barrel. A first build hooked the handler with
  one argument: stack corruption ("P2 drive: CPlayer::Update: exception C0000096"). Not done: P2's
  view on the cannon is still his orbit camera (not behind the barrel); after getting off, both
  halves showed a "Flak Cannon" weapon panel for a while; firing not checked; P1 on a cannon while
  P2 is on another loses neither camera switch only if both are on guns (then P2's messages go
  through).
  Seen at the same spot: caught faults in IsWeaponBoxTrigger (+0x22CB4, HUD scan reading objects
  streaming in) right after a long teleport; and earlier a real crash in HeightFieldSampleHook on a
  physics thread (dump 14:35, jump to 0x3F400012) around P2's death/respawn far away: not solved.
- P2's Aeroliner camera (2026-11-04, user: commercial planes like the Aeroliner 474 need the camera
  "way back"): planes by model name (Player2VehicleModel: character +0x2E0 smart object -> +0x18,
  less 4 = the vehicle; std::string at +0x3F8, "V114_JumboJet" for the Aeroliner; no size/box found
  in the object), table g_bigPlanes in splitrender.cpp: Aeroliner 90 m back, 10 m up (other planes
  30 / 2), read once a second, eased. The wall check starts at 60% of the distance for planes (the
  tail). "split bigplanecam <d> [h]". User: "that's good". Spot "airport" in places.txt.
- P2's motorbike camera (2026-11-04, user: "a little back and down"): on a motorbike
  (Player2RidingMotorbike: CMCController 0x00EC28C0 or its private copy, class index 3) the orbit
  is 2.5 m farther back (12.5 m) and its target 0.6 m lower, eased in and out like the car's
  (kP2CarDistanceRate). "split bikecam <back> <down>" to tune live. User: "feels right".
- Crash (2026-11-03 15:22, user playing, P2 had just got into the Dragonfly jet; first time at this
  address): access violation at 0x009332B8 (FUN_00933260, reads a triangle's vertices: index
  0x4B6F * stride 0x14 past the vertex buffer) on a game worker thread, chain FUN_006F95D0 ->
  FUN_00566670 -> FUN_0096CEC0 (collects up to 576 triangles near a point, likely decals / impact
  marks) -> FUN_0092C3E0 -> FUN_00930B70 -> FUN_00954960 -> FUN_00956560. No mod code on the stack;
  the minidump (%LOCALAPPDATA%\CrashDumps\JustCause2.exe.28140.dmp) has no heap. Suspect: impacts
  on a mesh streamed for P2 (World 2 / Terrain 2). User's account: P2 stunt-jumped onto P1's jet,
  it crashed and P1 died; after the respawn a new jet was delivered and P2 got in (log 15:21:36
  "airplane controller ... private vtable"); a strong rumble burst on P1's pad at 15:21:58, then the
  crash. Likely an explosion / impact marking a mesh (the wreck?) being freed. Not reproduced. Reader: tools\dmp.py <file.dmp>
  (exception, registers, code addresses on the stack).
- Cutscenes (2026-11-03, user: P2's view off during them, P2 kept). Cutscene manager
  NCutscene::CCutsceneManager at [0x0118FB84]: +0x5C set from a cutscene's loading to its end
  (+0x10 while it plays, +0x1C while loaded); measured with the black market dealer's two
  cutscenes (P1: D-pad down, Y at the ground). SplitscreenCutscene() reads it; IsInGameplay
  (splitscreen.cpp) excludes cutscenes, so the split is off and the game shows its cutscene full
  screen. CutsceneTick: P2's pad does nothing (WritePlayer2Input), his health stays full, start /
  end logged with his state ("Cutscene started/ended"); after one, dead or > 50 m from P1, he is
  put next to P1. The earlier "P2 destroyed in a cutscene" case (a mission cutscene) is not
  reproduced; the log lines are there for it.
- Races (2026-11-04, user: P2's view off while the race challenge screen is up, back on when P1
  gets control). Y (key E) at a race flag (places.txt race_flag) opens the "Race challenge" screen;
  UI mode stays 3 and the cutscene flag stays 0, so the split stayed on and the screen was squeezed
  into P1's half. Found by diffing .data before / during / after (tools/datadiff.py; P1's objects:
  tools/objdiff.py, nothing there): byte [0x01196071] is set by script event handler
  FUN_006386E0 (event at +0x358: disables the player via the character manager's slots 0x10 /
  0x18, FUN_007DB780 sets the byte; it also ducks the game sound, FUN_006E9E40 -> mixer level
  [0x01196078]) and cleared by its partner event (+0x35C, FUN_00505460). SplitscreenPlayer1Held()
  reads it; IsInGameplay excludes it and WritePlayer2Input ignores P2's pad meanwhile. After Enter
  ("Challenge Started") P1 moves at once; the race start makes a checkpoint autosave. Pause ->
  Abort Mission ends a race (puts the players back at an earlier spot). Any other script that
  takes P1's control (likely other challenge / mission screens) also turns the split off now.
  Race rings for P2 (user: his flights through rings didn't count; wanted for any race). Each ring
  is a CCheckpointMarker (vtable 0x00EB9D3C, position +0xB4; it only draws) with player shape
  triggers centred on it (two per ring, tools/ringtrig.py). The challenge (CChallengeObject
  0x00EB47C4, message handler FUN_006D0230 posts gui.challenge.checkpoint.complete / .bonus) only
  hears the messages those triggers post. The HUD pass scan now also collects the markers, and
  FindPickupTriggers puts every shape trigger within 1 m of one into the weapon-box list: filter
  and entry run with P2 in the manager's player slot (FUN_00601800 checks the on-foot / vehicle
  flags through that slot, so planes work). Log "Race rings: N shape triggers at M checkpoint
  markers". Confirmed by the user: P2 started and finished the Sawah County Crossing air race on
  his own (48 entries let through). Not tried: car / boat races, mission checkpoint rings. A P2
  teleported into a ring before the race starts counted nothing (expected: not started).
- IN PROGRESS (2026-11-01): damage between players. User wants bullets never, melee both ways.
  Now: P2's melee hurts P1, P1's hits never hurt P2. The take-hit method CCharacter slot 6
  (FUN_006B3210) returns early when damage[3] (source team) == char +0x480, and both players are
  team 9. P2's hits get through, probably because WeaponUpdateHook makes him not player-driven.
  Damage apply: FUN_00681480(char, dmg, type, ..., apply) -> FUN_00843800 per-type table at
  char +0x78 + type*12 (9 types; P2's are all defaults, kind 9 = full damage; P1's has e.g.
  type 4 x0.5) -> FUN_007E87A0 writes +0xE8. TODO: copy P1's table (+0x78, 0x6C bytes) to P2 at
  join. Diagnostic in the build now: KeepPlayerHitHooks gives both players' characters a private
  vtable copy with TakeHitHook on slot 6, logging "Hit on ..." for the first 60 calls (any
  character). With the earlier version (logging only P1/P2) nothing was logged while P2's kick
  hurt P1, so player hits may not go through slot 6; the wider log (just installed, not yet
  read) tells. IsCharacter() accepts the copy (Character() checks the vtable). Scripted kicks
  missed (body facing); have the user do the hits. If slot 6 isn't the path, remove
  KeepPlayerHitHooks and look elsewhere (chain from `trace 007e87a0` while P1 is hit).
- P2 collects pickups (2026-11-01, user request). Parts and money crates (CResourceItem, vtable
  0x00EBD42C, position +0x7C) are collected by walking into them, no button (P1 walked into one
  with W; E did nothing). The volume is a CShapeTrigger at the item: FUN_0098AD10 (overlap
  collector) -> enter handler at the +0x40 interface (vtable 0x00E31AA4 slot 4, FUN_005766A0) ->
  filter slot 9 (FUN_005014B0). For "player" triggers (flags +0x30 & 6) that is FUN_00601800: the
  manager's player's character only. Found by `trace 00749aa0` while P1 walked in (the crate's
  icon is switched off on collection). Shape triggers also guard mission areas, so only pickup
  triggers (centre at object +0xA4 within 1 m of an item) let P2 through: ShapeTriggerFilterHook
  (private vtable copy of the +0x40 interface, set by the HUD pass scan, which also collects the
  items) runs the filter again with P2 in the manager's player slot when it rejected his
  character. Verified (tests/p2pickup.py, spot "vehicle_part"; the crate is at 1233.8 205.4 -1092.7
  and is single use, so run it right after a load): P2 walked in from 3 m with P1 25 m away,
  "+1 Weapon part", weapon parts 9 -> 10. "p2hud pickups on|off" prints triggers found and P2
  entries let through. The collect banner and counters show in both halves (one GUI). A crate
  that has just streamed in waits for the next scan (up to 20 s).
- Mission markers (2026-10-29, user: "Reapers HQ 1.50 km" on P2's half was P1's distance, labels
  moved on P2's screen when P1 moved). They are CTargetIndicators (list [0x011910A0], next +0x40);
  the HUD's Lua script (Game.GetTargetIndicatorInfo, C function 0x00A39D70 -> FUN_007EA800)
  projects each and shows its int distance +0x54, which FUN_006B0350 sets from the manager's
  player every frame before the script runs. The script's Lua C closures (several Lua states;
  tt 6 at +4, isC at +6, function +0xC) are pointed at TargetIndicatorInfoHook by a heap scan
  (KeepMarkersPerPlayer): once per frame it sets every distance for the half being projected
  (main camera's derived world +0x264 nearest to which view) and on P2's frames puts his view in
  the main camera's +0x24, which FUN_0094B590 uses to pull points in front of the camera (that was
  the movement with P1). "p2markers on|off". Screenshot-verified: P1 1.50 km / P2 1.80 km on the
  same marker; P2's label stays put while P1 turns and moves. User: "looks good".
- Helicopter (2026-10-29, user test). The heli turns toward the camera on P1's mouse (heli turn
  actions 0x33/0x34, CHelicopterController 0x00EC2848 slot 6 FUN_007B0F20); P2's right stick X
  now turns it and the left stick X rolls/strafes. While P2 pilots (Player2PilotingHelicopter),
  his camera follows behind the heli (40 m back, 3 m up, tuned live with "split helicam" until the
  user said "perfect"; rate 6/s, pitch +-0.7); user: rotation "feels much better". Gun aim lending
  now covers all six vehicle controllers (heli, airplane, car, bike, boat, ATV); only the heli
  is tested. Heli guns: the vehicle weapon fire FUN_007E2650 aims at the manager's player's aim
  point (+0x184 -> +0x44). The heli controller's slot 6 calls it directly (a hook on
  CWeaponController's slot 0x00EC2DF4 never ran), so KeepPlayer2HeliAim gives P2's controller a
  private copy of the 14-slot vtable whose slot 6 (HeliControllerUpdateHook, ret 8) lends P2's
  aim control. Screenshot-verified: P2's tracers hit his crosshair while P1 looks elsewhere.
  Other vehicles with guns (planes, boats, cars with mounted guns) likely need the same.
- Physics box and origin (2026-10-29): the Havok broadphase is a cube of [0x01092AAC] -
  [0x01092AA8] (FUN_0098CF70; game 8100 m: x/z +-4050). World2EnlargePhysicsWorld (DllMain) makes
  it jc2ss.ini [Physics] WorldSize (default 16100: +-8050), and ShiftOriginForPlayers puts the
  floating origin between the players (P1 kept 550 m inside). Before, P2 > ~4 km from P1 had no
  static collision (couldn't grapple anything) and bodies dropped by a shift stayed out until
  their location reloaded. Verified: P2 stays on the helipad with P1 5.5 km away.
- Pause (2026-10-29): SplitViewCamera keeps each view's last position while the split is off
  with P2 still in (g_havePosition), and SetEnabled(false) releases held spawn points only when P2
  is out: pausing with P1 far had despawned the helicopter P2 sat in.
- Fixed (2026-10-29, user: P2's heli froze in the air and went invisible far from P1, crash when
  P2 jumped out). The crash: CPlayer's death handler (vtable 0x00E4CD64 slot 18 at 0x00E4CDAC,
  FUN_006B0790, __thiscall(player +8, dword), ret 4) runs FUN_00660E50(1) -> FUN_00613B90 ->
  FUN_008660D0 -> FUN_008BF240 -> CGameWorld slot 0xC8 (the world respawn: every object with a
  respawn component reset). P2's CPlayer shares the vtable, so P2's death reset the world and the
  spawn manager crashed on a freed spawn point (0x0053AC68 <- FUN_007999F0) a moment later.
  PlayerDiedHook skips it for P2 ("P2's player death handler skipped"); verified: `p2 kill` 4.5
  km from P1, no world respawn, P2 back next to P1 after 5 s. P1's death with P2 in also crashed
  the same way (respawn while spawn points were held for P2): RespawnWalkHook now releases the
  holds first (World2ReleaseSpawnPoints(5000), no new holds for 5 s); verified: P1 died with P2
  2 km away, game-over screen, no crash. RespawnWalkHook logs the code addresses on its stack.
- Fixed (2026-10-29, user: P2's heli froze in the air far from P1, then the game crashed). Two
  causes. (1) Spawn points held for P2 had despawn distance FLT_MAX; the game's unregister
  (FUN_006615F0, e.g. in the CSpawnSet destructor when a location unloads) finds a point's cells
  from position +- sqrt(max(spawn, despawn)) (FUN_00672000, cell FUN_00851860 clamped 0..3), and
  FLT_MAX gave cell 0 only, so destroyed held points stayed listed and stage 2 crashed on them.
  Held points now get 32768^2 (all 16 cells), get taken out of extra cells when released
  (KeepOnlyHomeCells), and each listed point is handled once per frame (a point in two cells used
  to record the hold value as its own). A guard logs and drops listed points that aren't
  CSpawnSets anymore ("dead spawn point"). (2) Vehicles sleep by distance from the main camera
  (FUN_00842CE0: x/z vs +0x318/+0x320; +0x380 sleep, +0x37C wake, squared; +0x378 state,
  +0x368 active; heli 4000 m). VehicleUpdateHook on slot 7 of the vehicles' +4 vtables (8
  classes, FUN_008219F0's callers) adds P1's minus the nearer player's squared distance to both
  thresholds, kept per vehicle and put back without P2 ("world2 sleep1" = old way). Verified: heli
  at the pad stays awake and drawn for P2 with P1 8.5 km away; user flew far, works. CCar's +4
  vtable (0x00E48D24, slot 7 FUN_005F12B0) was missed at first (user: a tank at a far base was
  asleep); added as the 9th slot, not yet tested in game.
- Open: user hears both players' audio muffled when P2 is far from P1.
- Steam Deck / Proton (2026-10-29). (1) P2 never spawned: Wine leaves Dr6 empty, so patch.cpp's
  OnBreakpoint (which required the Dr6 bit) let the spawn's hardware breakpoint hits through and
  CreatePlayer died with 0x80000004 at the constructor. Hits are now matched by address; the slot
  also goes off for one trap-flag single step in case Wine ignores the resume flag; the handler sets
  CONTEXT_DEBUG_REGISTERS when it changes debug registers. (2) Opening the parachute while
  grappling quit the game (ExitProcess 0x8000DEAD from 0048DA68, the anti-tamper check that
  finds debug registers on the thread): clearing now zeroes Dr0-3/Dr6/Dr7 and logs any that
  stayed ("Breakpoints: thread ... still has debug registers"). Deck test: still quit, with the
  registers verified clear; the user found it happens only with P2 joined (not without the mod,
  not with the mod and no join). Next try (v4): ClearThreadBreakpoints also removes the breakpoint
  VEH, and under Wine (RunningUnderWine: ntdll exports wine_get_version) terrain2 installs no
  fault-logging VEH and no CRT invalid-parameter handler. Deck test pending. (3) Deck, P2 driving
  into a town: ExitProcess(0) "from 8000DEAD" (the vtable integrity check, from a CResourceItem
  init FUN_0055ADA0 -> FUN_00D92090 during the location load). The session had added in-place
  slot hooks again: the vehicles' +4 update slot 7 (sleep fix) and CPlayer brain slot 18
  (P2's death handler). Both are private copies now: P2's CPlayer +8 gets a copy of 0x00E4CD64
  (55 slots, slot 18 does nothing; KeepPlayer2BrainVtable), and ScanForVehicles (background,
  every 3 s with P2 in) puts each vehicle's +4 pointer on a private copy of its class's 47-slot
  vtable (main vtable one of the 9 detail-slot classes, +4 one of kVehicleIfaceVtables). PC
  verified: heli stays awake 8.5 km from P1, P2's death skips the world respawn. Remaining in-place
  .rdata slot hooks are old ones (brain think, camera update, vehicle detail slot 75, etc.).
  (4) v4 on the Deck still quit after a parachute from a grapple (handlers gone, registers read
  back clear). Theory: under Wine the debug registers set by the helper (SetThreadContext from
  another thread) and the ones changed in the breakpoint handler (exception context) can
  disagree, so the helper's clear may not reach the real registers; the first spawn attempt also
  always failed there (the body breakpoint never fired after the handler moved the other slot).
  v6: under Wine the spawn thread sets and clears its own registers the handler's way (a private
  exception 0xE0D2D2D2 whose VEH writes Dr0-3/Dr6/Dr7 into the continue context;
  SetOwnDebugRegisters). jc2ss.ini [Debug] ForceWine=1 takes the Wine paths on Windows: spawn
  works there that way. User confirmed on the Deck (2026-10-29): no more quit.
  Also on the Deck the
  first spawn attempt still faults in the body build (the body breakpoint didn't fire); the retry
  works.
- Fixed (2026-10-29): no sea for P2 without GPU water simulation (always on AMD: the Deck;
  reproduced on the PC with Advanced Display Settings > GPU Water Simulation Off). Each loaded
  patch cell takes a water render block (cell +4 = index, FUN_00A88C10 from the cell load
  FUN_00A67360; drawn by FUN_00A63A20 -> FUN_00A55F00 -> FUN_00A88CB0) from one pool of 704
  CWaterHighEndRenderBlocks (0x01382100, 0x30 each), free list 0x0138A500 (uint16), top
  0x0138AA80 (uint16). Shared, P2's cells ran the top below zero (seen 0xFD3F): junk indices,
  and blocks built up to 3 MB past the table, with GPU water on too. The pool is now in
  kGroundState (per player; g_groundState 0x26000), and CreateGround2 gives P2 a full pool. Found
  with "terrain2 groundmask 4" (P1's cells gave P2 his sea) and the new "terrain2 celldiff".
  Verified with GPU water off and on: both players see the sea; top stays 0xFFFF (all 704 used)
  in both states. "terrain2 sharedterraintex / sharedmaptex" split the old sharedtex (testing).
- Open (2026-10-29, user, next session): crash when P2 leaves (Back+Start), and P2's body stays
  in the world after leaving (should be removed safely). The fault (00:12:03 in jc2ss.log) has
  the same signature as the reload crash: 0x0053AC68 <- 0x00799A8C (FUN_007999F0, spawn point
  despawn) <- 0x00779E80 (spawn manager stage 2) <- 0x0078222E, reading freed memory through a
  point's object list (+0xC0). So leaving also lets stage 2 despawn points held for P2
  (`World2ReleaseSpawnPoints` from the split turning off) whose objects are already gone; on
  leave, unlike a load, nothing tore the world down, so find who freed the objects (or whether
  a held point's list goes stale while held) before restoring distances. Note: "Game rumble"
  diagnostics log every motor change and are noisy (splitscreen.cpp GameSetStateHook).
- Seen (user): P2 grappled the parked car and got stuck between CEnterDriverSeatFromRoofAction
  and nothing (on the roof / entering loop). Vehicles for P2 not done.
- Open (2026-10-29, user report): foliage changes for one player when the other moves away
  (both directions). Spot: P2 at 896 224 -2028 facing the sea (`p2 face 0.999 0.1 -0.04`), P1
  stepped south with `p1 at 896 230 <z>`: P1 20 m away -> P2 sees grass clumps along the deck;
  from ~45-60 m on P2 sees red heliconias there instead and fewer trees (P1's area's plants).
  P2's vegetation zone stays 1 for both (new log line "P%d's vegetation zone a -> b"), so it isn't
  the climate/vegetation zone. Fixed by `terrain2 keep veg` (P2's turn with P1's whole vegetation
  copy: grass and more palms, like `terrain2 off`), not by keep ground / landstream1 / sharedtex /
  nostream / split p2world off, nor by `terrain2 vegmask` single ranges 3, 4, 6, 7, 8, 12 (0x8,
  0x10, 0x40, 0x80, 0x100, 0x1000) or 2, 9, 10, 16-17 (those scored badly but the baseline drifted;
  re-check by eye). Grass blocks (CRenderBlockGrass vtable 0x00F21214, position +0x24): 338 in
  all; with P1 150 m away 149 within 40 m of P2 and only 27 of P1; with P1 next to P2 ~150 near
  both. Block arrays per copy differ (`terrain2 vegdump`: 0x01382040 P1 12043A70, P2 174F12C0);
  camera pointer 0x0138202C/50/54: P1 the real main camera, P2 the mod's copy. Tools:
  scratchpad red-pixel counter (bottom half), tools/tests/deckflicker-like steps.
  Next: find where a plant's type is chosen (per cell; the vegetation job types 0x0138200C) and
  what shared state it reads; candidates the shared set collection [0x01191378] and the shared
  renderers/render states (kVegetationShared).
- Fixed (2026-10-29, user report): P2's town (around 1130 201 -1794) had no buildings with P1 at
  the mountain base. Area sets (regional asset packs, CAreaSetsManager [0x01191660], 2 slots)
  followed P1 only: with P1 next to P2 the only extra loads were AreaSet01 (and one far cell).
  Update slot 0x38 FUN_00859200 -> FUN_008B37B0 (only when the snapped position +0x74 changed)
  -> slot 10 query FUN_008A5CC0 (sets of the 4 corners +-1000 m into a std::set) -> unload not
  wanted, load wanted into free slots. world2 `AreaSetsQueryHook` (slot 10, 0x00F0C23C) adds
  P2's wanted sets; `AreaSetsHook` forces the update (+0x74 = NaN) when P2's 1024 m cell changes.
  Result: AreaSet01 + AreaSet05 loaded, town complete, P1's base unchanged. `world2 areasets1` /
  `areasets2` = only P1's / P2's; `areasets` = both (default). With both players wanting 2 sets
  each the 2 slots can't hold all; not seen yet.
- Fixed (2026-10-29, user report): cars in P2's view used lower detail (blocky wheels, no steering
  wheel at ~60 m, blocky body further) by P1's distance. The user found the first step at ~60 m
  from P1; a recording (tools/cardist.py while P1 crossed it, then a scan for fields explained by
  a distance threshold) found the render flags; a write watch found FUN_00804770 / FUN_006E0550
  (level in part render flags bits 29-31), fed by vtable slot +0x12C FUN_0061BB00 in all 9
  vehicle vtables: distance from the main camera, scaled by [0x0107214C] and a part scale, over the
  camera zoom (+0x620), into a ramp (thresholds 0x00EBEDE4..). world2 `VehicleLodHook`: the more
  detailed of both players' cameras (same formula, constants read from the exe; the game's
  value kept when its debug override [0x011FC2A0] is on). `world2 lod1` = old way. Checked:
  hideout car detailed in P2's view with P1 at the base.
  - The caught fault at 00A289AB (seen with long teleports and P2's deaths) is the game's own
    assert: FUN_00A289A0 writes to address 0 on purpose, called from many places; the game's
    handler catches it and play goes on. Harmless as far as seen.
  - Seen: P1 died once with `god on` (game over), cause unknown; `jc2ctl loadsave` from the game
    over screen quit the game (the key sequence picked Quit).
- Fixed (2026-10-28): P2's view had no distant mountains (e.g. around the city across the sea
  from the deck spot), with P1 next to him too; P1 saw them. Patch cells (0x012AB578, 11 levels x
  64 slots x 0x34; array level 0 = finest, 10 = coarsest; +3 bit 2 built, +0xC/+0x10 cell,
  +0x14/+0x20 bounds): P2's levels 6-10 were all flat at 10 m (P1's up to ~2000 m). Their heights
  come from CTerrainData's whole-map tables (+0x49C, levels 14..10, read by FUN_00A5EE40 from
  FUN_00A64370; filled by FUN_00A643C0 in the global load FUN_00A67B60 = reader slot 2), and
  P2's copy got zeros and junk there (its "loaded" flag +0x4A0 set; probably the stream read out
  of step, maybe from the displacement-table pointer being zeroed during CreateStream2). Fix:
  CreateStream2 gives P2's CTerrainData P1's header (+0x478..+0x490) and table pointer (+0x49C);
  it's static data. Not checked: whether P2's other handlers (CTerrainMapGame etc.) got their
  global data right. Test helpers: `terrain2 rebuild2 [hex levels]` (P2's next turn rebuilds those
  patch levels, ForgetPatchCells); a cells-per-level dump is easy to redo from the layout above.
- Fixed (2026-10-28): with P1 far, the wooden deck, railings and lamp posts at Karl's hideout
  (km02.karlhideout, P2 at the sea spot above) flickered in P2's view, drawn in ~1 of 12 shots.
  Locations, decorations and the rigid props were all there. Cause: PlayerJobHook ran P1's whole
  player update job (worker thread) with P1's world swapped into the main camera on P2's frames
  (for P1's grapple camera, see "P1 flung" below), and jobs running alongside measured
  draw/detail distances from that, i.e. from P1 kilometres away. Now the swap is only around
  P1's camera setup switches (SetCameraSetupHook, when the setup id at camera control manager
  +0x18 changes); `p1jobcam on` brings the old whole-job swap back for comparison.
  tests/deckflicker.py [far|near|stay] samples the deck pixel: far 12/12 drawn (was 0-2/12).
  Ruled out on the way: `world2 updatepos2` (world update with P2's camera on his frames).
- Still open: P1's grapple camera strays. tests/p1grapcamfling.py with P2 4.5 km away: 72 of
  4877 samples more than 20 m from both players, farthest 1380 m (old whole-job swap: 55,
  1274 m; no swap at all: 246, 2524 m). The handoff's earlier "~67 m" was with P2 3 km away.
  Something else in P1's camera reads P2's world on P2's frames.
- Test helpers: `god on|off` keeps both players' health at 100000 (tools/cycle.ps1 turns it on
  after loading), `groundat x z` (see below).
- P1 very far (6-8 km) from P2 (2026-10-28, parked by the user):
  - P2 seen skydiving but stuck in the ground, rotating erratically (user, after scripted P2
    teleports between roads 400-1200 m apart while P1 stood at 3000 889 -6000).
  - With P1 in the sea at -6548 199 -2701, P2's ground at 1542 1170 never appeared: he sank to
    y ~199 in a blue void, then died and respawned next to P1. Same with `world2 roadterrain1`.
  - `groundat x z` (test command) reads P1's height map, so only cells loaded around P1 give
    true values (P1 was put "on land" at a sea spot that way).
- The HUD's grapple indicator (crosshair shows a surface can be grappled) follows P1's aim in both
  halves: one HUD state, drawn for both views. Needs per-view HUD state (not started).
- Earlier "flaky" P2 grapples were mostly P1's aim resting on P2 (fixed, see Aim). One case: P2
  landed on a car roof and the game started CEnterDriverSeatFromRoofAction (char+0x324 current
  action), which blocks other actions. Vehicles (entering, driving) untested.
- Grapple rope textures missing on P2's device (see Grapple notes); dual hook / tether untested.
- Follow-P1 teleport after a P1 death / save reload not tested in game.
- Real deaths (explosion/enemies) not tested; `p2 kill` calls the death vfunc directly.

## Other open issues

- User reports to re-check with the current build: P1's back weapons moving while P2 runs.
- Crash 2026-10-27 (once, with P2 far from P1): access violation at 0x00937039, the write in
  CRenderPassAnark slot 16 (FUN_00975f50, queue a draw item: [pass+0x1A78] + index*16). The pass
  flips and may reallocate that buffer once per frame (slot 32, FUN_009779c0), so most likely a
  draw was queued during the flip from another thread. The caller is unknown: the fault logger
  (terrain2 LogFault) only ran while the terrain copies were on. It now logs the first 8 access
  violations in game or mod code at any time, with a stack scan ("Terrain 2: stack ...").
- P2's aim ray starts at the camera 10 m behind him. A camera inside a slope (its ground height not
  readable) made the ray hit at once, and the grapple pulled him back toward the camera. His brain
  think now gets the camera moved along its line of sight to 1 m before his head
  (StartAimNearPlayer2). Test: tools/tests/p2grapdir.py.
- Roads: the builder works through the wanted set on a time budget (FUN_008C2A20 ->
  FUN_0089DBF0), making each mesh around the current frame's camera. Roads left in the set were
  built on the other player's frame with no mesh near the player who wanted them, and stayed that
  way because built roads aren't queued again (a 480 m road under P1 vanished). world2 now tags
  each built entry with the player whose frame built it (TagBuiltRoads). Each frame it drops wanted
  roads that aren't this player's (KeepOwnWanted) and queues his roads that were built for the
  other player, unless that player has them too.
- Roads flickered for P1 while P2, far away, turned his camera. The camera update runs after the
  world2 hook and swings P2's camera 10 m around him into another 50 m cell. The game's road update
  then saw a cell change and rebuilt the wanted set for P2 alone, removing P1's roads.
  World2SyncRoadCell (called at the end of the camera "after" hook) sets the builder cell from the
  main camera the road update reads.
- Climate zones, fixed. With the players in different zones, P1's plants changed: the one
  CClimateZone ([0x011907D0]) picked the camera's zone each landscape run (FUN_00a55680, current
  zone in [0x012B447C]). Its CVegetationZoneCallback (zone +8, vtable 0xE4B41C) then released the
  other zone's vegetation sets and loaded this one's (slot 0 load, slot 1 release, per-zone state at
  callback +0x364; the set collection holds 8 zones). That happened every frame: 438 state changes
  in 3 s. Now the globals 0x012B4478/447C/448C are in kVegetationState, the zone fields
  +0x78/+0xB4..+0xC0 are swapped in SwitchZone, and the load/release slots are hooked: no reload
  of a zone that's already in, and no release of the other player's zone. Stop() releases P2's
  zone. The zone atlases (+0x78) aren't used here (it reads −1). Test spot in zone 2:
  −9000 900 6000 (P1 in zone 1 around the city).
- `p1 at` over ~3 km (city to the jungle spot 1632 242 1248) slides P1 about 350 m north at about
  170 m/s, even with P2 gone and world2 off. It's a quirk of the test teleport, not seen in normal
  play. It killed P1 once (game over).
- Built road entry (0x28 bytes): +0 render object, +4..+0x18 bounding box (min, max), +0x1C flag,
  +0x20 CRoad, +0x24 shared count.

## Files

- `src/player2.cpp`: all Player 2 work (spawn, drive, input, aim, respawn, gear, test commands).
- `src/game.cpp`: camera manager access, camera hook, `SwapMainCameraWorld`.
- `tools/tests/*.py`: test scripts (see "How to test").
- `tools/ghidra/*.java` + `tools/ghidra/run.ps1`: headless Decompile / Disasm / FindInsn on the Ghidra
  project "JC2" (paths in the untracked `tools/ghidra/local.ps1`).
- `tools/rtti_map.json`, `findimm.py`, `objdiff.py`, `procmem.py`, ...: memory/RTTI analysis helpers.
