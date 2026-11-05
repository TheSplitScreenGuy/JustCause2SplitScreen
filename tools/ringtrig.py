# Shape triggers (exe vtable 0x00E31AA4 at +0x40, or the mod's private copy) near the race's
# checkpoint markers (CCheckpointMarker 0x00EB9D3C, position +0xB4).
import math, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from procmem import Game

g = Game()
SHAPE, MARKER = 0x00E31AA4, 0x00EB9D3C
exe = g.read(SHAPE - 4, 44)
# The mod's copy: the same 11 dwords except slots 4 and 9 (hooked); match slots 0..3.
copy = None
for base, size in g.regions():
    b = g.read(base, size) if size < (64 << 20) else None
    if not b:
        continue
    i = b.find(exe[4:20])
    while i >= 0:
        if base + i != SHAPE:
            copy = base + i
            break
        i = b.find(exe[4:20], i + 1)
    if copy:
        break
print('private shape vtable copy: %s' % (hex(copy) if copy else None))
f = g.find_dwords([SHAPE, MARKER] + ([copy] if copy else []), 20000)
ifaces = f[SHAPE] + (f[copy] if copy else [])
markers = [(m, g.floats(m + 0xB4, 3)) for m in f[MARKER]]
print('%d shape triggers, %d markers' % (len(ifaces), len(markers)))
for t in ifaces:
    c = g.floats(t - 0x40 + 0xA4, 3)
    if not c:
        continue
    for m, p in markers:
        d = math.dist(c, p)
        if d < 60:
            obj = t - 0x40
            print('trigger obj %08X iface %08X at %.0f %.0f %.0f, %.1f m from marker %08X; flags +0x30 %04X vt0 %08X' %
                  (obj, t, c[0], c[1], c[2], d, m, struct.unpack('<H', g.read(t + 0x30, 2))[0], g.u32(obj)))
