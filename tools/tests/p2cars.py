# Cars near Player 2 while Player 1 goes far away (user report, 2026-10-28: the car in front of P2
# at Karl's hideout despawned when P1 left). Both players at the deck spot first, then P1 3000 889
# -6000; counts CCar objects (vtable 0x00EBFF64, position +0x118) within 60 m of P2 every few
# seconds.   python -I tools\tests\p2cars.py [seconds] [stay]
import math, os, re, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import jc2ctl
from procmem import Game

seconds = int(sys.argv[1]) if len(sys.argv) > 1 else 40
stay = 'stay' in sys.argv
g = Game()
CAR = 0x00EBFF64
P2 = (890.4, 223.4, -2027.7)


def mod(cmd):
    jc2ctl.focus()
    return jc2ctl.mod(cmd)


def cars_near(p, radius=60.0):
    out = []
    for a in g.find_dwords([CAR], limit=2000)[CAR]:
        if a < 0x02000000:
            continue
        x, y, z = g.floats(a + 0x118, 3)
        if math.hypot(x - p[0], z - p[2]) < radius:
            out.append('%08X' % a)
    return out


if not mod('p2').startswith('player 2'):
    mod('p2 join')
    time.sleep(5)
mod('god on')
if not stay:
    mod('p2 at %.1f %.1f %.1f' % P2)
    time.sleep(5)
    mod('p1top2')
    time.sleep(10)
    mod('p2 at %.1f %.1f %.1f' % P2)
    time.sleep(4)
    mod('p1top2')
    time.sleep(8)
    print('both at the deck: cars near P2', cars_near(P2))
    mod('p1 at 3000 889 -6000')
t0 = time.time()
while time.time() - t0 < seconds:
    time.sleep(5)
    st = mod('p2')
    m = re.search(r'pos ([-\d.]+) ([-\d.]+) ([-\d.]+)', st)
    p2 = tuple(float(v) for v in m.groups()) if m else P2
    print('%3.0f s: cars within 60 m of P2: %s' % (time.time() - t0, cars_near(p2)))
