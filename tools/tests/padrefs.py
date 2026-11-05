# Who points at Player 1's pad input entries (device 0x40000, second entry of an action's list)?
# Prints each reference with the dwords around it, to find the pad key code -> entry table.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import sys
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game

g = Game()
p1 = g.u32(g.u32(0x0118FB24) + 0x3570)
ctx = g.u32(g.u32(p1 + 0x188) + 4)
heads = g.u32(ctx + 0x10)
want = [int(a, 16) for a in sys.argv[1:]] or [0x23, 0x7C, 0x85, 0x86, 0x87, 0x88]
pads = {}
for i in want:
    e = g.u32(heads + i * 4)
    while e:
        if g.u32(e + 0xC) == 0x40000: pads[i] = e
        e = g.u32(e + 0x10)
refs = g.find_dwords(list(pads.values()), limit=20)
for i, e in pads.items():
    print('action %02X pad entry %08X' % (i, e))
    for r in refs[e]:
        print('   ref %08X: %s' % (r, ' '.join('%08X' % (g.u32(r + k) or 0) for k in range(-16, 20, 4))))
