# Player 1 grapples (F) several times, turning a little between grapples, and reports how far he
# moved in each: a fling shows as tens or hundreds of metres. Put Player 2 far away first.
#   python -I p1grapfling.py [count]
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import math, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
count = int(sys.argv[1]) if len(sys.argv) > 1 else 8
jc2ctl.focus()
c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
for r in range(count):
    jc2ctl.mouse(60, 0, 5)
    time.sleep(0.3)
    p0 = g.floats(c1 + 0x18C, 3)
    jc2ctl.tap('f', 200)
    most = 0.0
    for i in range(15):
        p = g.floats(c1 + 0x18C, 3)
        most = max(most, math.dist(p, p0))
        time.sleep(0.1)
    print('grapple %d: moved up to %.1f m, ends at %s' % (r + 1, most, [round(v) for v in p]))
    time.sleep(1.0)
