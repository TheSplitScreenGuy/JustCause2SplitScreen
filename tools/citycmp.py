# Player 2's view of the city spot (docs/positions.md) with Player 1 next to him and with Player 1
# 3 km away; saves both screenshots and P2's halves side by side (shots/citycmp.png).
#   python citycmp.py [wait_s]       (needs Pillow, run without -I)
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jc2ctl

SPOT = (1117.34, 201.5, -1792.13)
FORWARD = (0.601, -0.011, -0.800)          # camera forward ("cam"); it looks along -forward
BEHIND = tuple(SPOT[i] + FORWARD[i] * 9 for i in range(3))
FAR = (1632.0, 240.0, 1248.0)              # flat jungle 3 km away
SHOTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'shots')
wait = float(sys.argv[1]) if len(sys.argv) > 1 else 20


def at(who, p):
    print(jc2ctl.mod('%s at %f %f %f' % (who, p[0], p[1], p[2])))


jc2ctl.focus()
if 'updates' not in jc2ctl.mod('p2'):
    print(jc2ctl.mod('p2 join')); time.sleep(8)
at('p2', SPOT)
at('p1', BEHIND)
time.sleep(wait)
jc2ctl.mod('p2 face %f %f %f' % FORWARD)
time.sleep(2)
near = os.path.join(SHOTS, 'citycmp_near.png')
jc2ctl.mod('screenshot ' + near)
# In 1 km steps, like travelling: a 3 km jump in one frame shifts the physics origin by 3 km and
# Player 2 drops through the city ground.
import struct
from procmem import Game
g = Game()


def ground(px, pz):
    # The coarse island height map (CTerrainMapGame +0x804, 64 m samples), not below sea level.
    m = g.u32(0x01190290)
    off, hs = g.floats(0x0108AB34, 1)[0], g.floats(0x0108AB40, 1)[0]
    u, v = int((px - off) / 64), int((pz - off) / 64)
    return max(struct.unpack('<H', g.read(m + 0x804 + (v * 512 + u) * 2, 2))[0] * hs, 202.0)


for sx, sz in ((1297, -728), (1478, 336), (FAR[0], FAR[2])):
    at('p1', (sx, ground(sx, sz) + 3, sz))
    time.sleep(12)
time.sleep(max(0, wait - 12))
jc2ctl.mod('p2 face %f %f %f' % FORWARD)
time.sleep(2)
far = os.path.join(SHOTS, 'citycmp_far.png')
jc2ctl.mod('screenshot ' + far)
try:
    from PIL import Image
    a, b = Image.open(near), Image.open(far)
    w, h = a.size
    out = Image.new('RGB', (w, h))
    out.paste(a.crop((0, h // 2, w, h)), (0, 0))
    out.paste(b.crop((0, h // 2, w, h)), (0, h // 2))
    out.save(os.path.join(SHOTS, 'citycmp.png'))
    print('saved shots/citycmp.png (top: P1 next to P2, bottom: P1 3 km away)')
except ImportError:
    print('no Pillow: see', near, far)
