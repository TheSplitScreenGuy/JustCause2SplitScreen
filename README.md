# JC2 Splitscreen

Local two-player splitscreen for **Just Cause 2** (Steam version) on one PC or a Steam Deck.

Player 2 gets their own Rico, their own camera in the bottom half of the screen and their own
controls. You can explore Panau together or go off on your own. The two players can be kilometres
apart: the world, terrain, roads and vehicles load around each of them.

One download works on both Windows and the Steam Deck.

> **Version 1.0.** Please report problems in the Issues tab and attach `jc2ss.log` (see
> [Troubleshooting](#troubleshooting)).

## What you get

- **Join any time.** Load your save, then press **Start** on the second controller. **Back + Start**
  makes Player 2 leave. With only one player, the game plays exactly as usual.
- **A full second player.** Player 2 can run, shoot, grapple, parachute, drive, fly, hijack, use
  stunt positions and zoom in. Player 2 has their own health, HUD, crosshair and button prompts.
- **Their own camera.** Player 2's camera works like the normal one: it follows them on foot, in
  cars, on bikes and in planes.
- **Races.** Either player can fly or drive through race checkpoints. Player 2 can start and finish
  a race alone.
- **Full screen when it matters.** The pause menu, the map, cutscenes and the race intro screen
  turn the split off, then it comes back.
- **Find each other.** Each half shows a marker over the other player with the distance
  ("P2 120 m").
- **Respawning.** If Player 2 dies, they respawn next to Player 1 after a few seconds, and
  their death doesn't end your game. If Player 1 dies, the game ends and reloads like normal.

## Requirements

- Just Cause 2 on **Steam** (version 1.0.0.2, the current Steam build). Other versions (GOG, retail
  discs) are not supported.
- A second controller for Player 2 (XInput: Xbox controllers or anything Steam presents as one).
- Player 1 can use keyboard and mouse, a controller, or the Deck's own controls.
- Each half draws every other frame, so each player gets about half your normal frame rate. A
  game that runs at 60 FPS gives each player about 30.

## Install

> **Use a fresh save** (or one you don't mind messing up). The mod changes a lot about how the game
> runs, so keep your main playthrough safe.

Download `JC2Splitscreen.zip` from the [Releases](../../releases) page. It contains:

| File | What it is |
| --- | --- |
| `dinput8.dll` | the mod |
| `jc2ss.ini` | settings (optional: the defaults are the same) |
| `README.txt` | these install steps |

### Windows

1. In Steam, right-click **Just Cause 2** > **Properties** > **Installed Files** > **Browse**.
2. Copy `dinput8.dll` and `jc2ss.ini` into that folder, next to `JustCause2.exe`.
3. Start the game as usual. No launch options are needed.

### Steam Deck

1. Switch to **Desktop Mode**.
2. In Steam, open **Just Cause 2** > **Properties** > **Installed Files** > **Browse**. It's usually
   `~/.local/share/Steam/steamapps/common/Just Cause 2`, or on an SD card
   `/run/media/mmcblk0p1/steamapps/common/Just Cause 2`.
3. Copy `dinput8.dll` and `jc2ss.ini` into that folder, next to `JustCause2.exe`.
4. In Steam, open **Just Cause 2** > **Properties** > **General** > **Launch Options** and enter:

   ```
   WINEDLLOVERRIDES="dinput8=n,b" %command%
   ```

   Without this, Proton uses its own `dinput8` and the mod doesn't load.
5. Go back to Gaming Mode and start the game. The Deck's built-in controls are Player 1. Connect a
   second controller for Player 2.

### Uninstall

Delete `dinput8.dll` and `jc2ss.ini` from the game folder. On the Deck, also clear the launch option.
The mod doesn't change any of the game's own files.

## Playing

1. Load your save as usual.
2. A few seconds after you're in the world, a hint says **"P2: press Start to join"**. Press
   **Start** on the second controller. Player 2 appears next to Player 1 and the screen splits:
   Player 1 on top, Player 2 below.
3. Player 2's buttons are the same as Player 1's controller layout.
4. To leave, Player 2 holds **Back** and presses **Start**. The screen goes back to full size.

Missions, money, upgrades and progress are Player 1's game. Player 2 helps out and uses Player 1's
weapons and upgrades.

### Tips

- **Bring Player 2 back to Player 1:** have Player 2 leave (**Back/Select + Start**) and join again
  (**Start**). They reappear right next to Player 1.
- **Dual boosters on the parachute:** have Player 1 equip them first, then have Player 2 join.
- **Player 2's melee makes an explosion.** When Player 2 melees an object, it sets off a
  non-damaging explosion. It started as a bug but it was too much fun to remove, so it stays.

## The F8 status bar and keyboard shortcuts

Press **F8** to show or hide a status bar in the top-left corner. It's hidden when the game starts.

```
JC2 Splitscreen v1.0 | 60 FPS | cam 9324 214 1089 | P2: press Start to join [pad 2] | F5 ammo | F6 swap | F8 hide | F9 $50k
```

| Part | Meaning |
| --- | --- |
| `v1.0` | the mod's version |
| `60 FPS` | frames per second for the whole screen (each player gets about half) |
| `cam x y z` | Player 1's camera position in the world (handy for bug reports) |
| `P2: ...` | Player 2's state: **no controller** (nothing found in Player 2's slot), **press Start to join**, or **joined (Back+Start leaves)** |
| `[pad N]` | which controller is Player 2's (1 = first, 2 = second ...). Set with `ControllerSlot` in `jc2ss.ini` |

The keys work whether or not the bar is showing:

| Key | Does |
| --- | --- |
| **F5** | Refills ammo, grenades and C4 for both players |
| **F6** | Player 1 and Player 2 swap places |
| **F8** | Shows or hides the status bar |
| **F9** | Adds $50,000 (a cheat, mainly for testing) |

On the Steam Deck, use the on-screen keyboard (**Steam + X**) or bind these keys to back buttons
in the controller settings.

## Settings (`jc2ss.ini`)

```ini
[Player2]
; Which controller is Player 2's (0-3). 0 is the first controller (on the Deck, its own controls),
; so a second pad is usually 1. If Start on your second pad does nothing, try 2.
ControllerSlot=1

[Physics]
; Size in metres of the area around the players where objects have collision (the game's own is
; 8100). Bigger lets the players be farther apart.
WorldSize=16100
```

Restart the game after changing it.

## Troubleshooting

The mod writes `jc2ss.log` in the game folder. Its first line should read
`JC2 Splitscreen v1.0 loaded`.

- **No `jc2ss.log` at all:** the DLL isn't next to `JustCause2.exe`. On the Deck, also check the
  launch option.
- **Start on the second controller does nothing:** press F8 and look at the `P2:` part. If it says
  `no controller`, change `ControllerSlot` in `jc2ss.ini` to `2` (or `0`) and restart the game.
- **The game crashes:** please open an issue and attach `jc2ss.log`. On Windows, also attach the
  newest `JustCause2.exe.*.dmp` file from `%LOCALAPPDATA%\CrashDumps` if there is one.

## Known issues

- **Road pieces stretching up into the sky:** have Player 2 leave and join again. That usually
  fixes it.
- **Missing collision during a mission** (you fall through terrain, buildings or objects, or drive
  through them): reload the last checkpoint. Moving the players far apart quickly, for example by
  teleporting, is known to cause this.
- Player 2 sometimes sees stray tree leaves floating in the sky.
- Player 2 has infinite ammo for some weapons.
- When Player 2 is in a stunt position (standing on a vehicle's roof), the controls can be inverted
  on some vehicles.
- Traffic and people are only spawned around Player 1. When the players are far apart, Player 2's
  area is quieter.
- Small ground plants near Player 2 can be the kinds from around Player 1.
- On a flak cannon, Player 2's camera doesn't move behind the barrel, and the "Flak Cannon" panel
  stays on both halves for a short while after getting off.
- There have been rare crashes after big explosions. A safety net now catches the known one, but
  report it if you still see one.
- Races were tested with a plane race. Car and boat races should work the same way but haven't
  been tried yet.

## Building from source

Requires Visual Studio 2019 with the C++ desktop toolset (32-bit build).

```
build.bat
powershell -ExecutionPolicy Bypass -File install.ps1
```

`build.bat` produces `build\dinput8.dll`, and `install.ps1` copies it into the game folder.
Developer notes, test tools and the code layout are in [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

## License

[MIT](LICENSE). This covers the mod's own code only, not Just Cause 2 or any of its files.

## Disclaimer

A fan-made mod, not affiliated with or endorsed by Square Enix, Eidos or Avalanche Studios. You
need your own copy of Just Cause 2.
