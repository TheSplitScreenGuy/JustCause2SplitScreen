# Triggers and checkpoint markers within R metres of a point (default: the race_flag area).
#   python -I tools\racetrig.py [x z R]
import math, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from procmem import Game

x0, z0, R = (float(v) for v in sys.argv[1:4]) if len(sys.argv) > 3 else (9324.0, 1089.0, 3000.0)
g = Game()
DIST, SHAPE, MARKER = 0x00E4C20C, 0x00E31AA4, 0x00EB9D3C
# The mod gives shape triggers a private vtable copy: find it from a known vtable slot value.
f = g.find_dwords([DIST, SHAPE, MARKER], 20000)


def ids(lst):
    b = g.read(lst, 4 + 4 * 63) if lst else None
    return list(struct.unpack_from('<%dI' % (b[0] & 0x3F), b, 4)) if b else []


print('distance triggers:')
for a in f[DIST]:
    t = a - 8
    c = g.floats(t + 0x70, 3)
    if c and math.hypot(c[0] - x0, c[2] - z0) < R:
        print('  %08X at %.0f %.0f %.0f r %.1f flags %02X enter %s exit %s' % (t, c[0], c[1], c[2], g.floats(t + 0x3C, 1)[0],
              g.read(t + 0x38, 1)[0], ['%08X' % m for m in ids(g.u32(t + 0x8C))], ['%08X' % m for m in ids(g.u32(t + 0x90))]))
print('markers:')
for m in f[MARKER]:
    c = g.floats(m + 0xB4, 3)
    print('  %08X at %.0f %.0f %.0f %s' % (m, c[0], c[1], c[2], '<-- near' if math.hypot(c[0] - x0, c[2] - z0) < R else ''))
print('shape triggers (exe vtable only; the mod swaps found ones to a copy):', len(f[SHAPE]))
