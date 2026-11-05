# Finds strings in the game's heap matching a regex (bytes), with their addresses.
#   python -I memstrings.py <regex> [max]
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from procmem import Game

g = Game()
pat = re.compile(sys.argv[1].encode())
limit = int(sys.argv[2]) if len(sys.argv) > 2 else 200
seen = {}
n = 0
for base, size in g.regions():
    if 0x00400000 <= base < 0x01400000: continue
    try:
        raw = g.read(base, size)
    except Exception:
        continue
    for m in re.finditer(rb'[ -~]{6,160}', raw):
        s = m.group()
        if pat.search(s):
            seen.setdefault(s, []).append(base + m.start())
for s, addrs in sorted(seen.items(), key=lambda kv: -len(kv[1]))[:limit]:
    print('%3d  %08X  %s' % (len(addrs), addrs[0], s.decode()))
print(len(seen), 'distinct')
