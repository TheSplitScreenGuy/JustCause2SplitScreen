# Finds memory blocks that reference many different characters (registries / update lists),
# and whether Player 1 and Player 2 are referenced there.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import sys, collections
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game

g = Game()
mgr = g.u32(0x0118FB24)
c1 = g.u32(g.u32(mgr + 0x3570) + 0x48)
c2 = int(sys.argv[1], 16)
chars = g.find_dwords([0xEB881C], limit=500)[0xEB881C]
offs = (0, 4, 0x38, 0xF0)
targets = {c + o: (c, o) for c in chars for o in offs}
refs = g.find_dwords(list(targets.keys()), limit=400)
blocks = collections.defaultdict(set)
where = collections.defaultdict(list)
for t, addrs in refs.items():
    c, o = targets[t]
    for a in addrs:
        if any(x <= a < x + 0xBC4 for x in chars):
            continue
        blk = a & ~0xFFF
        blocks[blk].add(c)
        where[blk].append((a, c, o))
for blk, cs in sorted(blocks.items(), key=lambda kv: -len(kv[1])):
    if len(cs) < 4:
        break
    entries = sorted(where[blk])
    print('block %08X: %d characters, p1 %s, p2 %s, offsets %s' % (
        blk, len(cs), c1 in cs, c2 in cs, sorted(set('+%X' % o for _, _, o in entries))))
    for a, c, o in entries[:6]:
        print('    %08X -> char %08X+%X%s' % (a, c, o, ' P1' if c == c1 else ' P2' if c == c2 else ''))
