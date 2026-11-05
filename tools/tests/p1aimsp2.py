# Player 1's crosshair on Player 2 vs next to him: P2 is placed 7 m along P1's camera ray (in the
# crosshair) or 4 m to the side of that point, alternately, and fires the grapple each time.
# Prints how far P2 got pulled and whether P1's aim point was on P2's body. Screenshots
# tools/shots/p1aimsp2_<n>.png at 0.4 s.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import re, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
if 'updates' not in jc2ctl.mod('p2'):
    jc2ctl.focus()
    print(jc2ctl.mod('p2 join'))
    time.sleep(3)
c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
c2 = g.u32(int(re.search(r'player 2 ([0-9A-F]+)', jc2ctl.mod('p2')).group(1), 16) + 0x48)
cam = g.u32(0x011919F4) + 0x18
pos = lambda c: g.floats(c + 0x18C, 3)
shots = os.path.join(_ROOT, r'tools\shots')


def place(side):
    m = g.floats(cam + 0x24, 16)
    target = [m[12 + i] - m[8 + i] * 7 + m[i] * side for i in range(3)]
    w = g.floats(c1 + 0x15C, 16)
    d = [target[i] - w[12 + i] for i in range(3)]
    local = [sum(d[i] * w[a * 4 + i] for i in range(3)) for a in range(3)]
    local[1] = max(local[1] - 1.0, 0.3)
    jc2ctl.mod('p2 tp %.2f %.2f %.2f' % tuple(local))
    time.sleep(1.5)


def p1_aims_at_p2():
    a = g.floats(g.u32(c1 + 0xA34) + 0x44, 3)
    p = pos(c2)
    return (a[0] - p[0]) ** 2 + (a[2] - p[2]) ** 2 < 1.0 and -0.3 < a[1] - p[1] < 2.2


jc2ctl.focus()
n = int(sys.argv[1]) if len(sys.argv) > 1 else 3
for k in range(2 * n):
    side = 0 if k % 2 == 0 else 4
    place(side)
    on = p1_aims_at_p2()
    b = pos(c2)
    jc2ctl.mod('p2 press 51 300')
    time.sleep(0.4)
    jc2ctl.mod('screenshot %s\\p1aimsp2_%d.png' % (shots, k))
    time.sleep(2)
    y = pos(c2)
    print('%-15s P1 aim on P2: %-5s P2 pulled %.1f m' % ('in crosshair' if side == 0 else 'beside it', on,
                                                       sum((y[i] - b[i]) ** 2 for i in range(3)) ** 0.5))
