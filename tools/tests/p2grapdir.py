# Player 2's grapple direction far from Player 1: P2 stands at a spot (default the flat jungle
# 3 km from the city) and fires the grapple facing several headings, toward Player 1 among them;
# prints how far off the aim point and his pull are from where he faces.
#   python -I p2grapdir.py [x y z]
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import math, re, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
spot = [float(v) for v in sys.argv[1:4]] if len(sys.argv) > 3 else [1632.0, 240.0, 1248.0]
c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
c2 = g.u32(int(re.search(r'player 2 ([0-9A-F]+)', jc2ctl.mod('p2')).group(1), 16) + 0x48)
pos = lambda c: g.floats(c + 0x18C, 3)
aim = lambda: g.floats(g.u32(c2 + 0xA34) + 0x44, 3)


def angle(a, b):
    a, b = (a[0], a[2]), (b[0], b[2])  # heading only
    la, lb = math.hypot(*a) or 1, math.hypot(*b) or 1
    return math.degrees(math.acos(max(-1, min(1, (a[0] * b[0] + a[1] * b[1]) / (la * lb)))))


jc2ctl.focus()
p1 = pos(c1)
to1 = [p1[0] - spot[0], 0, p1[2] - spot[2]]
base = math.atan2(to1[0], to1[2])
print('player 1 at %s, %.0f m away' % (' '.join('%.0f' % v for v in p1), math.hypot(to1[0], to1[2])))
# The camera sits along +f from P2 (looking along -f); f[1] < 0 looks up, with the camera low.
for off, up in ((0, 0.25), (10, 0.25), (-10, 0.25), (30, 0.25), (90, 0.25), (180, 0.25),
                (0, 0.5), (10, 0.5), (180, 0.5), (190, 0.5)):
    yaw = base + math.radians(off)
    f = [math.sin(yaw), -up, math.cos(yaw)]
    jc2ctl.mod('p2 at %f %f %f' % tuple(spot))
    time.sleep(6)
    jc2ctl.mod('p2 face %f %f %f' % tuple(f))
    time.sleep(1.5)
    b, a = pos(c2), aim()
    d = [a[i] - b[i] for i in range(3)]
    jc2ctl.mod('p2 press 51 300')
    time.sleep(2.0)
    y = pos(c2)
    m = [y[i] - b[i] for i in range(3)]
    # Right: the aim point and the pull ~180 deg from f (ahead of the camera, past P2).
    print('camera %4d deg from P1, up %.2f: aim %.0f m away, %.0f deg from f; moved %.1f m, %.0f deg from f' % (
        off, up, math.dist(a, b), angle(d, f), math.hypot(m[0], m[2]), angle(m, f) if math.hypot(m[0], m[2]) > 1 else -1))
