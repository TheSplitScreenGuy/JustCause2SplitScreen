# Both players up in the air, then each opens the parachute (P1: Space, P2: action 0x47
# PARACHUTE_OPEN_CLOSE): prints both heights and fall speeds over time and screenshots.
#   python -I tools\tests\p2chute.py [height] [open_after_s]
import math, os, re, sys, threading, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import jc2ctl
from procmem import Game

height = float(sys.argv[1]) if len(sys.argv) > 1 else 100.0
open_after = float(sys.argv[2]) if len(sys.argv) > 2 else 0.7
shots = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'shots')
g = Game()


def mod(cmd):
    jc2ctl.focus()
    return jc2ctl.mod(cmd)


def chars():
    c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
    m = re.search(r'character ([0-9A-F]+)', jc2ctl.mod('p2'))
    return c1, int(m.group(1), 16) if m else 0


if not mod('p2').startswith('player 2'):
    mod('p2 join')
    time.sleep(5)
mod('god on')
c1, c2 = chars()
p = g.floats(c1 + 0x18C, 3)
mod('p2 at %.1f %.1f %.1f' % (p[0] + 4, p[1] + height, p[2]))
mod('p1lift %.0f' % height)
time.sleep(open_after)
t0 = time.time()
threading.Thread(target=lambda: jc2ctl.tap('space', 150)).start()
mod('p2 press 47 150')
last = {}
marks = [1.0, 2.5, 5.0]
while time.time() - t0 < 8:
    t = time.time() - t0
    line = '%4.1f s' % t
    for name, c in (('P1', c1), ('P2', c2)):
        y = g.floats(c + 0x190, 1)[0]
        v = (y - last[name][1]) / (t - last[name][0]) if name in last and t > last[name][0] else 0.0
        last[name] = (t, y)
        action = g.u32(c + 0x324)
        vt = g.u32(action) if action else 0
        line += '  %s y %6.1f  vy %6.1f  action vt %08X' % (name, y, v, vt)
    print(line, flush=True)
    if marks and t > marks[0]:
        mod('screenshot ' + os.path.abspath(os.path.join(shots, 'p2chute_%02d.png' % int(marks.pop(0) * 10))))
    time.sleep(0.25)
