# User report (2026-10-29): setting a map waypoint closes the game (mod in, even without Player 2;
# the game calls ExitProcess itself). Runs the given mod commands, opens the map (F1), sets a
# waypoint (1), and reports whether the game survived. Relaunches with loadsave first if needed.
#   python -I tools\tests\waypoint.py ["world2 off" "terrain2 ..." ...]
import os, subprocess, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import jc2ctl


def running():
    out = subprocess.run(['powershell', '-NoProfile', '-Command', '[bool](Get-Process JustCause2 -ErrorAction SilentlyContinue)'],
                         capture_output=True, text=True).stdout.strip()
    return out == 'True'


if not running():
    subprocess.run([sys.executable, '-I', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'jc2ctl.py'), 'loadsave'])
for cmd in sys.argv[1:]:
    print(cmd, '->', jc2ctl.mod(cmd))
jc2ctl.focus()
time.sleep(1)
jc2ctl.tap('f1', 100)
time.sleep(4)
jc2ctl.tap('1', 100)
time.sleep(6)
alive = running()
print('game survived the waypoint' if alive else 'game closed')
if alive:
    jc2ctl.tap('f1', 100)  # close the map again
