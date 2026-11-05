# Lists objects of interesting classes that reference each player's character (or its interfaces).
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import sys, json
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game

names = json.load(open(os.path.join(_ROOT, r'tools\rtti_map.json')))
g = Game()
mgr = g.u32(0x0118FB24)
c1 = g.u32(g.u32(mgr + 0x3570) + 0x48)
c2 = int(sys.argv[1], 16)


def owner(a):
    for back in range(0, 0x1000, 4):
        vt = g.u32(a - back)
        n = names.get('%08X' % vt) if vt is not None else None
        if n and '(+' not in n:
            return n, a - back, back
    return '?', 0, 0


for label, c in (('player 1', c1), ('player 2', c2)):
    targets = [c, c + 4, c + 0x38, c + 0xF0, c + 0xF4]
    found = g.find_dwords(targets, limit=200)
    rows = []
    for t in targets:
        for a in found[t]:
            if c <= a < c + 0xBC4:
                continue
            n, o, back = owner(a)
            rows.append('%s(+%X)->char+%X' % (n, back, t - c))
    print('%s %08X: %d refs' % (label, c, len(rows)))
    for r in sorted(set(rows)):
        print('   ', r, rows.count(r))
