# Finds a text in the game's memory (HUD scripts: Lua source or strings) and prints the printable
# text around each hit: luascan.py <text> [before] [after] [max hits]
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import re, sys
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game

g = Game()
pat = sys.argv[1].encode()
before = int(sys.argv[2]) if len(sys.argv) > 2 else 200
after = int(sys.argv[3]) if len(sys.argv) > 3 else 400
limit = int(sys.argv[4]) if len(sys.argv) > 4 else 20
n = 0
for base, size in g.regions():
    if base >= 0x400000 and base < 0x1400000: continue
    d = g.read(base, size)
    if not d: continue
    i = d.find(pat)
    while i >= 0 and n < limit:
        s = d[max(0, i - before):i + after]
        text = re.sub(rb'[^\x20-\x7e\r\n\t]', b'.', s).decode('latin1')
        print('=== %08X' % (base + i)); print(text)
        n += 1
        i = d.find(pat, i + 1)
