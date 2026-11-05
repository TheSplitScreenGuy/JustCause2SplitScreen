# Player 2's view of the city spot (docs/positions.md) with Player 1 at several distances along a
# line towards a target (placed on the coarse ground, stepping out at most 1 km at a time);
# screenshots shots/viewdist_<metres>.png.
#   python -I viewdist.py <tx> <tz> <dist,dist,...>
import math, os, struct, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from procmem import Game
import jc2ctl

SPOT = (1117.34, 201.5, -1792.13)
FORWARD = (0.601, -0.011, -0.800)
SHOTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'shots')
g = Game()
tx, tz = float(sys.argv[1]), float(sys.argv[2])
dists = [float(d) for d in sys.argv[3].split(',')]
length = math.hypot(tx - SPOT[0], tz - SPOT[2])
ux, uz = (tx - SPOT[0]) / length, (tz - SPOT[2]) / length


def ground(px, pz):
    m = g.u32(0x01190290)
    off, hs = g.floats(0x0108AB34, 1)[0], g.floats(0x0108AB40, 1)[0]
    u, v = int((px - off) / 64), int((pz - off) / 64)
    return max(struct.unpack('<H', g.read(m + 0x804 + (v * 512 + u) * 2, 2))[0] * hs, 202.0)


jc2ctl.focus()
jc2ctl.mod('p1 at %f %f %f' % (SPOT[0] + 6, SPOT[1] + 2, SPOT[2] - 8))
time.sleep(10)
jc2ctl.mod('p2 at %f %f %f' % SPOT)
time.sleep(6)
last = 0.0
for d in dists:
    while d - last > 1000:
        last += 1000
        px, pz = SPOT[0] + ux * last, SPOT[2] + uz * last
        jc2ctl.mod('p1 at %f %f %f' % (px, ground(px, pz) + 3, pz))
        time.sleep(10)
    px, pz = SPOT[0] + ux * d, SPOT[2] + uz * d
    jc2ctl.mod('p1 at %f %f %f' % (px, ground(px, pz) + 3, pz))
    last = d
    time.sleep(15)
    jc2ctl.mod('p2 face %f %f %f' % FORWARD)
    time.sleep(2)
    jc2ctl.mod('screenshot ' + os.path.join(SHOTS, 'viewdist_%d.png' % d))
    print('P1 %.0f m away; P2: %s' % (d, jc2ctl.mod('p2').split('|')[0]))
