# Identifiers in the memory around a hit (Lua bytecode constants):
#   luawords.py <hex addr> <hex size>
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import re, sys
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game

g = Game()
a, n = int(sys.argv[1], 16), int(sys.argv[2], 16)
d = g.read(a, n)
seen = []
for m in re.finditer(rb'[A-Za-z_][A-Za-z0-9_.{}%: -]{3,}', d):
    s = m.group().decode()
    print('%08X %s' % (a + m.start(), s))
