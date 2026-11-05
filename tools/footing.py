# Player 2's footing at a spot with Player 1 at points along a line away from it: P1 is placed on
# the ground (coarse island height map + 3 m), then P2 is placed at the spot and his height read.
#   python -I footing.py <x> <y> <z> <tx> <tz> <fractions,...>
import os, struct, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from procmem import Game
import jc2ctl

g = Game()
x, y, z, tx, tz = (float(v) for v in sys.argv[1:6])
fracs = [float(f) for f in sys.argv[6].split(',')]


def ground(px, pz):
    m = g.u32(0x01190290)
    off = g.floats(0x0108AB34, 1)[0]
    hs = g.floats(0x0108AB40, 1)[0]
    u, v = int((px - off) / 64), int((pz - off) / 64)
    return struct.unpack('<H', g.read(m + 0x804 + (v * 512 + u) * 2, 2))[0] * hs


for f in fracs:
    px, pz = x + (tx - x) * f, z + (tz - z) * f
    py = max(ground(px, pz), 202) + 3
    jc2ctl.mod('p1 at %f %f %f' % (px, py, pz))
    time.sleep(14)
    jc2ctl.mod('p2 at %f %f %f' % (x, y + 0.5, z))
    time.sleep(5)
    s = jc2ctl.mod('p2')
    cam = jc2ctl.mod('cam').split()[1:4]
    print('P1 at %.0f %.0f (%.0f m): P2 y %s, P1 camera %s' % (px, pz, ((px - x) ** 2 + (pz - z) ** 2) ** 0.5,
          s.split('pos ')[1].split()[1], ' '.join(cam)))
