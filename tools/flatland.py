# Flat land 3-5 km from Player 1's start, from the coarse island height map (CTerrainMapGame +0x804,
# 512x512 u16, FUN_00a5f210: u = (x - [0x0108AB34]) * scale, height = value * [0x0108AB40]).
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # project folder
import sys, struct, math
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
g = Game()
m = g.u32(0x01190290)
off = g.floats(0x0108AB34, 1)[0]
hscale = g.floats(0x0108AB40, 1)[0]
scale = g.floats(0x01381B9C, 1)[0] or (512 / 32768.0)
raw = g.read(m + 0x804, 512 * 512 * 2)
h = struct.unpack('<%dH' % (512 * 512), raw)
print('offset', off, 'scale', scale, 'hscale', hscale)
def H(u, v): return h[v * 512 + u] * hscale
cell = 1 / scale
P1 = (892, -2010)
print('height at P1 start', H(int((P1[0] - off) * scale), int((P1[1] - off) * scale)))
out = []
for v in range(1, 511):
    for u in range(1, 511):
        x, z = off + (u + 0.5) * cell, off + (v + 0.5) * cell
        d = math.hypot(x - P1[0], z - P1[1])
        if not 3000 < d < 5000: continue
        c = H(u, v)
        if c < 215: continue
        nb = [H(u + du, v + dv) for du in (-1, 0, 1) for dv in (-1, 0, 1)]
        if max(nb) - min(nb) < 3:
            out.append((d, x, c, z))
out.sort()
for o in out[:8]:
    print('flat at %.0f %.0f %.0f (%.0f m from P1 start)' % (o[1], o[2], o[3], o[0]))
