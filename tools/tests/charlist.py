# Finds containers of characters reachable from the character manager (vectors/arrays of CCharacter*),
# and reports whether Player 1 / Player 2 are in each.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import sys, struct
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game

g = Game()
mgr = g.u32(0x0118FB24)
c1 = g.u32(g.u32(mgr + 0x3570) + 0x48)
c2 = int(sys.argv[1], 16)
chars = set(g.find_dwords([0xEB881C], limit=500)[0xEB881C])
print('manager %08X, %d characters live, p1 %08X p2 %08X' % (mgr, len(chars), c1, c2))
blob = g.read(mgr, 0x3600)
for off in range(0, 0x3600 - 8, 4):
    begin, end = struct.unpack_from('<II', blob, off)
    if not (0x10000 < begin < end < 0x7FFF0000 and end - begin <= 0x4000):
        continue
    data = g.read(begin, end - begin)
    if not data:
        continue
    for stride in (4, 8):
        vals = [struct.unpack_from('<I', data, i)[0] for i in range(0, len(data) - 3, stride)]
        hits = [v for v in vals if v in chars]
        if len(hits) >= 3:
            print('manager+%04X: %d entries (stride %d), %d characters, p1 %s, p2 %s' % (
                off, len(vals), stride, len(hits), c1 in hits, c2 in hits))
