# Drives Just Cause 2 for automated tests.
#
#   python -I jc2ctl.py mod <command...>        send a command to the mod (ping, cam, watch ...)
#   python -I jc2ctl.py shot [file.png]         screenshot via the mod (default tools\shots\shot.png)
#   python -I jc2ctl.py key <name> [hold_ms]    tap a key (esc, enter, up, down, left, right, w, a, s, d, space, ...)
#   python -I jc2ctl.py keys <name> <name> ...  tap several keys, 400 ms apart
#   python -I jc2ctl.py mouse <dx> <dy> [steps] move the mouse relative (turns the camera)
#   python -I jc2ctl.py focus                   bring the game window to the front
#   python -I jc2ctl.py launch | close          start the game through Steam / close it
#   python -I jc2ctl.py waitcam [seconds]       wait until the main camera exists (gameplay)
import ctypes, ctypes.wintypes as wt, os, subprocess, sys, time

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)
from gamedir import find_game_dir
GAME_DIR = find_game_dir()
CMD = os.path.join(GAME_DIR, 'jc2ss_cmd.txt')
RESULT = os.path.join(GAME_DIR, 'jc2ss_result.txt')

user32 = ctypes.WinDLL('user32', use_last_error=True)

# DirectInput reads scan codes; (code, extended)
SCAN = {
    'esc': (0x01, 0), 'enter': (0x1C, 0), 'space': (0x39, 0), 'tab': (0x0F, 0), 'backspace': (0x0E, 0),
    'up': (0x48, 1), 'down': (0x50, 1), 'left': (0x4B, 1), 'right': (0x4D, 1),
    'w': (0x11, 0), 'a': (0x1E, 0), 's': (0x1F, 0), 'd': (0x20, 0), 'e': (0x12, 0), 'f': (0x21, 0),
    'q': (0x10, 0), 'r': (0x13, 0), 'shift': (0x2A, 0), 'ctrl': (0x1D, 0), 'f8': (0x42, 0), 'f1': (0x3B, 0), 'f5': (0x3F, 0),
    '1': (0x02, 0), '2': (0x03, 0), 'm': (0x32, 0), 'v': (0x2F, 0), 'c': (0x2E, 0), 'x': (0x2D, 0), 'z': (0x2C, 0), 'g': (0x22, 0),
}


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [('wVk', wt.WORD), ('wScan', wt.WORD), ('dwFlags', wt.DWORD), ('time', wt.DWORD),
                ('dwExtraInfo', ctypes.c_size_t)]


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [('dx', wt.LONG), ('dy', wt.LONG), ('mouseData', wt.DWORD), ('dwFlags', wt.DWORD),
                ('time', wt.DWORD), ('dwExtraInfo', ctypes.c_size_t)]


class INPUTUNION(ctypes.Union):
    _fields_ = [('ki', KEYBDINPUT), ('mi', MOUSEINPUT), ('pad', ctypes.c_byte * 32)]


class INPUT(ctypes.Structure):
    _fields_ = [('type', wt.DWORD), ('u', INPUTUNION)]


KEYEVENTF_EXTENDEDKEY, KEYEVENTF_KEYUP, KEYEVENTF_SCANCODE = 0x1, 0x2, 0x8
MOUSEEVENTF_MOVE = 0x1


def send(inp):
    user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT))


def key_event(name, up):
    code, ext = SCAN[name]
    inp = INPUT(type=1)
    inp.u.ki = KEYBDINPUT(0, code, KEYEVENTF_SCANCODE | (KEYEVENTF_EXTENDEDKEY if ext else 0) | (KEYEVENTF_KEYUP if up else 0), 0, 0)
    send(inp)


def tap(name, hold_ms=80):
    key_event(name, False)
    time.sleep(hold_ms / 1000)
    key_event(name, True)


def mouse(dx, dy, steps=20):
    for i in range(steps):
        inp = INPUT(type=0)
        inp.u.mi = MOUSEINPUT(int(dx / steps), int(dy / steps), 0, MOUSEEVENTF_MOVE, 0, 0)
        send(inp)
        time.sleep(0.016)


def click(button='left', hold_ms=80):
    down, up = (0x2, 0x4) if button == 'left' else (0x8, 0x10)
    for flags in (down, up):
        inp = INPUT(type=0)
        inp.u.mi = MOUSEINPUT(0, 0, 0, flags, 0, 0)
        send(inp)
        if flags == down:
            time.sleep(hold_ms / 1000.0)


def game_window():
    pid = subprocess.run(['powershell', '-NoProfile', '-Command', '(Get-Process JustCause2 -ErrorAction SilentlyContinue | Select-Object -First 1).Id'],
                         capture_output=True, text=True).stdout.strip()
    if not pid:
        raise SystemExit('JustCause2.exe is not running')
    pid = int(pid)
    found = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(hwnd, _):
        p = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        if p.value == pid and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True

    user32.EnumWindows(cb, 0)
    if not found:
        raise SystemExit('game window not found')
    return found[0]


def focus():
    hwnd = game_window()
    # Windows only lets the foreground app hand over focus; a synthetic Alt press satisfies that.
    user32.keybd_event(0x12, 0, 0, 0)
    user32.keybd_event(0x12, 0, 2, 0)
    user32.ShowWindow(hwnd, 9)
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.3)
    return user32.GetForegroundWindow() == hwnd


def mod(command, timeout=30):
    cid = str(int(time.time() * 1000))
    try:
        os.remove(RESULT)
    except FileNotFoundError:
        pass
    with open(CMD + '.tmp', 'w') as f:
        f.write('id %s\n%s\n' % (cid, command))
    os.replace(CMD + '.tmp', CMD)
    end = time.time() + timeout
    while time.time() < end:
        try:
            with open(RESULT) as f:
                text = f.read()
            if text.startswith('id %s\n' % cid):
                return text.split('\n', 1)[1].rstrip()
        except (FileNotFoundError, PermissionError):
            pass
        time.sleep(0.05)
    raise SystemExit('no answer from the mod (game not rendering?)')


def main(argv):
    if not argv:
        print(__doc__ if __doc__ else open(__file__).read().split('import')[0])
        return
    cmd, args = argv[0], argv[1:]
    if cmd == 'mod':
        print(mod(' '.join(args), timeout=120))
    elif cmd == 'shot':
        out = os.path.abspath(args[0]) if args else os.path.join(TOOLS, 'shots', 'shot.png')
        os.makedirs(os.path.dirname(out), exist_ok=True)
        r = mod('screenshot ' + out)
        print(out if r == 'ok' else r)
    elif cmd == 'key':
        focus()
        tap(args[0], int(args[1]) if len(args) > 1 else 80)
    elif cmd == 'keys':
        focus()
        for k in args:
            tap(k)
            time.sleep(0.4)
    elif cmd == 'mouse':
        focus()
        mouse(int(args[0]), int(args[1]), int(args[2]) if len(args) > 2 else 20)
    elif cmd == 'focus':
        print('focused' if focus() else 'could not focus the game')
    elif cmd == 'launch':
        os.startfile('steam://rungameid/8190')
    elif cmd == 'close':
        subprocess.run(['powershell', '-NoProfile', '-Command',
                        '$p = Get-Process JustCause2 -ErrorAction SilentlyContinue; if ($p) { $p.CloseMainWindow() | Out-Null; if (-not $p.WaitForExit(15000)) { Stop-Process -Id $p.Id -Force } };'
                        'for ($i = 0; $i -lt 60 -and (Get-Process JustCause2 -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 500 }; Start-Sleep 1'])
    elif cmd == 'loadsave':
        # Launch (if needed), pass the title screen and pick "Load Latest Autosave".
        running = subprocess.run(['powershell', '-NoProfile', '-Command', '[bool](Get-Process JustCause2 -ErrorAction SilentlyContinue)'],
                                 capture_output=True, text=True).stdout.strip() == 'True'
        if not running:
            os.startfile('steam://rungameid/8190')
        end = time.time() + 120
        while True:
            try:
                mod('ping', timeout=5)
                break
            except SystemExit:
                if time.time() > end:
                    raise SystemExit('game never started rendering')
        # Title screen -> main menu -> "Load Latest Autosave". Presses Enter until loading starts,
        # which is when the camera leaves (0, 0, 0).
        time.sleep(10)
        for attempt in range(8):
            focus()
            tap('enter')
            time.sleep(4)
            if not mod('cam').startswith('pos 0.000 0.000 0.000'):
                break
        else:
            raise SystemExit('could not start loading the save')
        time.sleep(float(args[0]) if args else 30)
        print('loaded (check with: shot)')
    elif cmd == 'waitcam':
        end = time.time() + (float(args[0]) if args else 120)
        while time.time() < end:
            try:
                r = mod('cam', timeout=5)
                if r.startswith('pos'):
                    print(r)
                    return
            except SystemExit:
                pass
            time.sleep(1)
        raise SystemExit('camera never became available')
    else:
        raise SystemExit('unknown command ' + cmd)


if __name__ == '__main__':
    main(sys.argv[1:])
