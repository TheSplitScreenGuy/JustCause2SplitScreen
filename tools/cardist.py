# Prints Player 1's distance to the car nearest a point (default: the hideout car by the deck spot,
# 897 -2036) twice a second, to find the distance where P2's view switches the car's detail level.
#   python -I cardist.py [x z]
import math, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from procmem import Game

g = Game()
x, z = (float(sys.argv[1]), float(sys.argv[2])) if len(sys.argv) > 2 else (897.0, -2036.0)
car = None
best = None
for a in g.find_dwords([0x00EBFF64], limit=2000)[0x00EBFF64]:
    if a < 0x02000000:
        continue
    px, py, pz = g.floats(a + 0x118, 3)
    d = math.hypot(px - x, pz - z)
    if best is None or d < best:
        best, car = d, a
print('car %08X; Ctrl+C to stop' % car)
while True:
    c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
    p = g.floats(c1 + 0x18C, 3)
    q = g.floats(car + 0x118, 3)
    print('P1 to car: %6.1f m (flat %6.1f m)' % (math.dist(p, q), math.hypot(p[0] - q[0], p[2] - q[2])), flush=True)
    time.sleep(0.5)
