# Player 2's grapple far from Player 1: P2 is placed at a spot (default the flat jungle 3 km from
# the city) and fires the grapple at a few camera pitches; prints where his aim point is and how
# far he moved toward it.   python -I p2grapfar.py [x y z]
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import re, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
spot = [float(v) for v in sys.argv[1:4]] if len(sys.argv) > 3 else [1632.0, 240.0, 1248.0]
c2 = g.u32(int(re.search(r'player 2 ([0-9A-F]+)', jc2ctl.mod('p2')).group(1), 16) + 0x48)
pos = lambda: g.floats(c2 + 0x18C, 3)
aim = lambda: g.floats(g.u32(c2 + 0xA34) + 0x44, 3)
jc2ctl.focus()
for name, pitch in (('looking down 30', 0.5), ('looking down 15', 0.25), ('level', 0.08)):
    jc2ctl.mod('p2 at %f %f %f' % tuple(spot))
    time.sleep(6)
    jc2ctl.mod('p2 pitch %.1f' % (pitch * 57.2958))
    time.sleep(1.5)
    b, a = pos(), aim()
    d = [a[i] - b[i] for i in range(3)]
    dist = sum(v * v for v in d) ** 0.5 or 1
    jc2ctl.mod('p2 press 51 300')
    time.sleep(2.5)
    y = pos()
    m = [y[i] - b[i] for i in range(3)]
    along = sum(m[i] * d[i] for i in range(3)) / dist
    print('%-16s aim point %.1f m away (%s), moved %.1f m, %.1f m toward it' % (
        name, dist, ' '.join('%.0f' % v for v in a), sum(v * v for v in m) ** 0.5, along))
jc2ctl.mod('p2 pitch 4.6')
