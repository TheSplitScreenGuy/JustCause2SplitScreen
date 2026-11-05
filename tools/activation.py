# How many Havok bodies are in the world within 60 m of a point (y above a floor), with Player 1
# placed at several distances from it ("p1 at"): finds the radius objects get physics within.
#   python -I activation.py <x> <y> <z> <dist,dist,...> [dx dz]
import math, os, struct, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from procmem import Game
import jc2ctl

g = Game()
x, y, z = (float(v) for v in sys.argv[1:4])
dists = [float(d) for d in sys.argv[4].split(',')]
dx, dz = (float(sys.argv[5]), float(sys.argv[6])) if len(sys.argv) > 6 else (1.0, 0.0)
VT = 0xFAEC58


def count():
    ps = g.u32(0x0118FFF4)
    ox, _, oz = g.floats(ps + 0x28, 3)
    n = 0
    for b in g.find_dwords([VT], limit=50000)[VT]:
        try:
            raw = g.read(b, 0x20)
            if struct.unpack_from('<I', raw, 8)[0] == 0: continue
            px, py, pz = g.floats(struct.unpack_from('<I', raw, 0x18)[0] + 0x30, 3)
        except Exception:
            continue
        if py > y - 30 and math.hypot(px + ox - x, pz + oz - z) < 60: n += 1
    return n


for d in dists:
    jc2ctl.mod('p1 at %f %f %f' % (x + dx * d, y + 2, z + dz * d))
    time.sleep(12)
    jc2ctl.mod('p2 at %f %f %f' % (x, y + 0.5, z))
    time.sleep(5)
    p2y = float(jc2ctl.mod('p2').split('pos ')[1].split()[1])
    print('P1 %5.0f m away: %d bodies, P2 stands at y %.2f' % (d, count(), p2y))
