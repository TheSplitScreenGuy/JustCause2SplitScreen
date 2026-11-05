# Finds who references an address: for each reference, walks back up to <maxback> bytes looking for the
# start of an RTTI object (a known vtable) and prints owner class + field offset.
# usage: python -I whoref.py <hexaddr> [maxback_hex]
import json, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from procmem import Game

names = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'rtti_map.json')))
g = Game()
target = int(sys.argv[1], 16)
maxback = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x4000
refs = g.find_dwords([target], limit=200)[target]
for r in refs:
    owner = None
    for back in range(0, maxback, 4):
        vt = g.u32(r - back)
        if vt is not None and '%08X' % vt in names and '(+' not in names['%08X' % vt]:
            owner = (r - back, names['%08X' % vt], back)
            break
    if owner:
        print('%08X  in %-40s at %08X +0x%X' % (r, owner[1], owner[0], owner[2]))
    else:
        print('%08X  (no owner found)' % r)
