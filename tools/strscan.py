# Finds MSVC 2005 std::string fields inside an object: {4 bytes, 16-byte buffer or pointer, size, capacity}.
# usage: python -I strscan.py <hexaddr> <hexsize>
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from procmem import Game

g = Game()
base, size = int(sys.argv[1], 16), int(sys.argv[2], 16)
b = g.read(base, size + 0x1C)
for off in range(0, size, 4):
    n, cap = struct.unpack_from('<II', b, off + 0x14)
    if not (n <= cap and (cap == 15 or 16 <= cap < 4096) and n < 1024):
        continue
    if cap == 15:
        s = b[off + 4:off + 4 + n]
        if b[off + 4 + n:off + 5 + n] != b'\0':
            continue
    else:
        p = struct.unpack_from('<I', b, off + 4)[0]
        s = g.read(p, n + 1) if 0x10000 < p < 0x7FFF0000 else None
        if not s or s[n:n + 1] != b'\0':
            continue
        s = s[:n]
    if n and all(32 <= c < 127 for c in s):
        print('+%04X  len %3d cap %4d  %s' % (off, n, cap, s.decode()))
    elif n == 0 and cap == 15:
        print('+%04X  (empty)' % off)
