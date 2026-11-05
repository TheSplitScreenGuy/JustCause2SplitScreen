# Lists objects that both players' characters (and their inventories) point to: anything with
# per-character state in there would make one player affect the other.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import json, re, sys
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

names = json.load(open(os.path.join(_ROOT, r'tools\rtti_map.json')))
g = Game()
p1 = g.u32(g.u32(0x0118FB24) + 0x3570)
p2 = int(re.search(r'player 2 ([0-9A-F]+)', jc2ctl.mod('p2')).group(1), 16)
def cls(v):
    vt = g.u32(v) if v and 0x10000 <= v < 0x7FFF0000 else None
    return names.get('%08X' % vt, '') if vt else ''
for what, a, b, size in (('CPlayer', p1, p2, 0x1200), ('CCharacter', g.u32(p1 + 0x48), g.u32(p2 + 0x48), 0xBC4),
                         ('CInventory', g.u32(g.u32(p1 + 0x48) + 0x484), g.u32(g.u32(p2 + 0x48) + 0x484), 0x120)):
    A, B = g.read(a, size), g.read(b, size)
    print('== %s P1 %08X P2 %08X' % (what, a, b))
    for o in range(0, size, 4):
        va = int.from_bytes(A[o:o + 4], 'little')
        vb = int.from_bytes(B[o:o + 4], 'little')
        if va == vb and 0x10000000 <= va < 0x7FFF0000 and not (a <= va < a + size):
            c = cls(va)
            if c and not c.startswith('?$sp_counted'):
                print('  +%03X shared %08X %s' % (o, va, c))
