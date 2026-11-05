# Development notes

Moved here from the old README when it became the player-facing page. Building is in the README;
this covers the internals and the test tools. The full working log is `docs/HANDOFF.md`, game
addresses are in `docs/re-notes.md` (all for the Steam `JustCause2.exe` 1.0.0.2).

## How it works

- `dinput8.dll` proxy: the game loads it on startup and it forwards `DirectInput8Create` to the real system DLL.
- Hooks `IDXGISwapChain::Present` through the swap chain vtable.
- `src/splitrender.cpp` draws two views by alternating cameras each frame (even frames Player 1, odd
  frames Player 2) and composing the middle band of each into the top and bottom halves. Motion blur
  is kept per view. Each view runs at half the frame rate.
- The game itself is never patched in code: hooks go through vtables and heap data (the game checks
  some of its own code and quits if it is changed).

Install a local build into the game folder with `install.ps1`, remove it with `uninstall.ps1`.

## Test automation (tools/jc2ctl.py)

The mod reads commands from `jc2ss_cmd.txt` in the game folder and answers in `jc2ss_result.txt`. The
`jc2ctl.py` script wraps that and also sends keyboard and mouse input to the game:

```
python -I tools\jc2ctl.py loadsave             # start the game if needed and load the latest autosave
python -I tools\jc2ctl.py shot [file.png]      # half-size screenshot from the back buffer
python -I tools\jc2ctl.py mod split on|off     # force the split view without a second controller
python -I tools\jc2ctl.py mod camoffset 0 15 0 [yaw] | camoffset off
python -I tools\jc2ctl.py mod watch w|rw|x <ms> <addr...>   # hardware watchpoints: who writes/reads/runs this
python -I tools\jc2ctl.py key enter | mouse 800 0 | close
```

`tools\cycle.ps1` builds, restarts the game and loads the autosave. Decompiling functions headless:
`tools\ghidra\Decompile.java` (usage in the file).

## Memory scanner (tools/memscan)

A small command-line replacement for Cheat Engine, used to find game addresses by narrowing down values
across scans. Build it with `tools\build_tools.bat`, which produces `tools\bin\memscan.exe`. It attaches to the
running `JustCause2.exe` and keeps scan state in `tools\scans` between runs.

```
memscan new f32 unknown        # snapshot all values (~600 MB on disk)
memscan next changed           # after changing something in game
memscan next unchanged         # after leaving it alone
memscan next inc               # value went up (also: dec, incby <d>, decby <d>)
memscan new i32 eq 1920        # or start from an exact value / range <a> <b>
memscan list 20                # show matches with old and current values
memscan read JustCause2.exe+0x1000 64
memscan watch 0x12345678 f32 10
memscan write 0x12345678 f32 100
```

Types are `u8`, `i32`, `f32` and `f64`. Float comparisons accept `tol <x>` (default 0.01).

## Layout

- `src/dllmain.cpp`: proxy export and startup thread
- `src/hooks.cpp`: Present vtable hook
- `src/splitscreen.cpp`: Player 2 join/leave and when the split is allowed
- `src/player2.cpp`: Player 2's Rico, input, weapons, HUD, races
- `src/game.cpp`: game addresses and hooks (camera read/override, motion blur pass)
- `src/splitrender.cpp`: alternate-frame split view
- `src/terrain2.cpp`, `src/world2.cpp`: terrain, roads, vehicles and objects streamed around Player 2
- `src/control.cpp`, `src/screenshot.cpp`, `src/watch.cpp`: automation commands, screenshots, hardware watchpoints
- `src/overlay.cpp`: D3D10 overlay (status line, divider, player markers, join hint)
- `src/log.cpp`: `jc2ss.log` writer
- `tools/memscan/memscan.cpp`: memory scanner
- `tools/rtti.py`, `tools/rtti_vtables.py`: a class's vtable address from its RTTI name
- `tools/procmem.py`: read-only game memory access for analysis scripts
