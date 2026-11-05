# For each address, walks back to the nearest RTTI object start and prints its class and the field offset.
# usage: python -I owner.py <hexaddr> [...]
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from procmem import Game

names = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'rtti_map.json')))
g = Game()
for a in (int(x, 16) for x in sys.argv[1:]):
    found = None
    for back in range(0, 0x2000, 4):
        vt = g.u32(a - back)
        n = names.get('%08X' % vt) if vt is not None else None
        if n and '(+' not in n:
            found = (a - back, n, back)
            break
    print('%08X: %s' % (a, '%s at %08X +0x%X' % (found[1], found[0], found[2]) if found else '?'))
