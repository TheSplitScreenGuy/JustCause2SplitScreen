# Reverse-engineering notes (JustCause2.exe, Steam, 1.0.0.2)

The exe has no ASLR (always loaded at 0x00400000), so `JustCause2.exe+offset` addresses are fixed.
The game keeps MSVC RTTI, so classes can be found by name: `python -I tools\rtti_vtables.py <exe> <name>`.

## Camera

| Thing | Where | Notes |
| --- | --- | --- |
| `NGraphicsEngine::CCameraManager*` | `[JustCause2.exe+0xD919F4]` | single instance; vtables `0xF21FD8` (+0) and `0xE5D908` (+4) |
| Main `NGraphicsEngine::CCamera` | manager `+0x18` | embedded; vtable `0xF21ED8` |
| Copy of main camera | `[manager+0x14]` | same values every frame |
| Camera world matrix | camera `+0x24` | 4x4 floats, row-major: right, up, forward, position (`+0x54`) |
| View matrix | camera `+0xA4` | inverse of the world matrix |
| More pointers to main camera | `JustCause2.exe+0xF8202C`, `+0xF82050`, `+0xF82054` | `+0xF82028..` looks like a list of camera pointers |

Coordinates: Y is up, sea level is Y = 200 (the water reflection camera mirrors the main one across it).
77 CCamera objects exist in gameplay; most are idle (position (0, 0, -623)) or shadow/reflection cameras.

### CCamera matrix blocks (16 floats each)

| Offset | Meaning |
| --- | --- |
| `+0x024` / `+0x064` / `+0x0A4` / `+0x0E4` | current world / projection / view / view-projection |
| `+0x164` / `+0x1A4` / `+0x1E4` / `+0x224` | previous frame's world / projection / view / view-projection |
| `+0x264` / `+0x2A4` / `+0x2E4` / `+0x324` | working set: `+0x264` is the pending world matrix `CCamera::Update` starts from |
| `+0x364` / `+0x3A4` / `+0x3E4` / `+0x424` | previous working set (copied to `+0x164..` by the update) |
| `+0x624` / `+0x628` | near / far plane |

### Per-frame camera flow

1. Game thread: `FUN_0081c960` (camera controller update, vtable `0xEB878C`) builds the world matrix and writes it to the
   main camera's `+0x264` and `+0x24`.
2. Game thread: `CCamera::Update` = `FUN_00933a10` (CCamera vtable slot 1, `0xF21EDC`) derives view/projection from
   `+0x264` and fills the current and previous blocks. **Changing `+0x264` before this call moves the rendered view**
   (game logic, HUD markers and rendering all follow it).
3. Game thread: `FUN_00942f60` copies the main camera into the render camera (`[manager+0x14]`) with the copy
   function `FUN_0091afc0`, then adjusts it with `FUN_00944070`.
4. Render thread: reads only the render camera (e.g. `FUN_009521e0` copies it into per-pass state).

### Motion blur

`FUN_0096a420` (render thread, `__thiscall`, 6 stack args, ends `ret 0x18`) is the motion blur pass (it names itself
"MotionBlur"). Its object keeps the previous frame's view-projection at `this+0x10` and overwrites it with the render
camera's `+0xE4` at the end of the pass. With alternating cameras this must be kept per view, or everything smears.

### Threads

The game thread updates cameras; a separate render thread draws and calls Present. Seen with hardware watchpoints
(`jc2ctl.py mod watch ...`).

Found 2026-10-23 by scanning for the CCamera vtable and watching which instances follow the player's camera.
Confirmed after a game restart: the manager moved (10BA56E0 → 10BA36E0) but the path from `+0xD919F4` still held.
