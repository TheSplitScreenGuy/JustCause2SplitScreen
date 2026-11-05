# Vehicles and characters within reach of each player, sampled a few times; driven vehicles apart.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import math, re, struct, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
VEH = [0x00EBF51C, 0x00EBF67C, 0x00EBF804, 0x00EBF98C, 0x00EBFB04, 0x00EBFC7C, 0x00EBFDDC, 0x00EBFF64, 0x00EC00DC]
CHAR = 0x00EB881C
R = 250.0


def once():
    p2 = [float(x) for x in re.search(r'pos ([-0-9.]+) ([-0-9.]+) ([-0-9.]+)', jc2ctl.mod('p2')).groups()]
    c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
    p1 = g.floats(c1 + 0x18C, 3)
    r = g.find_dwords(VEH + [CHAR], limit=3000)
    out = {}
    for name, keys, off in (('vehicles', VEH, 0x118), ('characters', [CHAR], 0x18C)):
        n1 = n2 = total = 0
        for k in keys:
            for a in r[k]:
                if a < 0x02000000: continue
                q = g.floats(a + off, 3)
                if not q or not all(abs(x) < 40000 for x in q): continue
                total += 1
                if math.dist(p1, q) < R: n1 += 1
                if math.dist(p2, q) < R: n2 += 1
        out[name] = (n1, n2, total)
    return p1, p2, out


for i in range(int(sys.argv[1]) if len(sys.argv) > 1 else 3):
    p1, p2, o = once()
    print('P1 %.0f %.0f  P2 %.0f %.0f | within %d m: vehicles P1 %d P2 %d (all %d), characters P1 %d P2 %d (all %d)' % (
        p1[0], p1[2], p2[0], p2[2], R, *o['vehicles'], *o['characters']), flush=True)
    time.sleep(3)
