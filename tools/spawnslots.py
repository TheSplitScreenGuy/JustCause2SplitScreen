# The spawn manager's resource slots ([0x01191918] +0x1720 count, +0x1740 array of 0x58-byte
# entries; +0x2C bits = priorities using it): raw dwords of each slot, to compare states.
#   python -I spawnslots.py
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from procmem import Game

g = Game()
m = g.u32(0x01191918)
n = g.u32(m + 0x1720)
arr = g.u32(m + 0x1740)
print('%d slots at %08X; per-priority waits +0x130: %s' % (n, arr, ' '.join('%.2f' % v for v in g.floats(m + 0x130, 6))))
for i in range(n):
    d = g.read(arr + i * 0x58, 0x58)
    w = [int.from_bytes(d[k:k + 4], 'little') for k in range(0, 0x58, 4)]
    print('%2d %s' % (i, ' '.join('%08X' % x for x in w)))
