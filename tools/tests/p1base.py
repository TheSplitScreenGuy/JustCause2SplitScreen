# Enemies and vehicles at a base didn't spawn for P1 until P2 came near (user report,
# 2026-10-29). P2 stays where he is (e.g. the deck spot, busy spawn points around him), P1 goes to
# the mountain base at 3678 1347 -5430 (21 spawn points); prints the states of the base's spawn
# points (manager [0x01191918], point +0xA4: 0 none, 1-3 loading, 4 spawned, 5 failed) and the
# queue every few seconds.   python -I tools\tests\p1base.py [seconds] [stay]
import math, os, sys, time, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import jc2ctl
from procmem import Game

seconds = int(sys.argv[1]) if len(sys.argv) > 1 else 40
stay = 'stay' in sys.argv
g = Game()
BASE = (3678.0, 1347.0, -5430.0)


def mod(cmd):
    jc2ctl.focus()
    return jc2ctl.mod(cmd)


def points_near(p, radius=400.0):
    m = g.u32(0x01191918)
    out = []
    for cell in range(16):
        b, e = g.u32(m + 0x148 + cell * 0x10 + 4), g.u32(m + 0x148 + cell * 0x10 + 8)
        for a in range(b, e, 4) if b else []:
            pt = g.u32(a)
            x, y, z = g.floats(pt + 0x68, 3)
            if math.hypot(x - p[0], z - p[2]) < radius:
                out.append(pt)
    return out


mod('god on')
if not stay:
    mod('p1 at %.1f %.1f %.1f' % (BASE[0], BASE[1] + 3, BASE[2]))
t0 = time.time()
while time.time() - t0 < seconds:
    time.sleep(5)
    m = g.u32(0x01191918)
    states = collections.Counter(g.u32(pt + 0xA4) for pt in points_near(BASE))
    cur = g.u32(m + 0xB8)
    curs = ''
    if cur:
        x, y, z = g.floats(cur + 0x68, 3)
        curs = 'current at %.0f %.0f state %d' % (x, z, g.u32(cur + 0xA4))
    print('%3.0f s: base points by state %s | queue %d %s' % (time.time() - t0, dict(sorted(states.items())), g.u32(m + 0xCC), curs))
