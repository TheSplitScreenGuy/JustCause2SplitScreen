# Prints Player 1's input bindings: per action (names from the table at 0x01071DE8) each entry of
# its list in the input context (+0x10 next) with its raw dwords, and pad key names where a dword
# is a pad key code (0x70 RPADA .. 0x87 BACK, from the key name map built at 0x00573E00).
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import sys
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game

PAD = {0x70: 'A', 0x71: 'B', 0x72: 'X', 0x73: 'Y', 0x74: 'LT', 0x75: 'RT', 0x76: 'RB', 0x77: 'LB',
       0x78: 'DUP', 0x79: 'DDOWN', 0x7A: 'DLEFT', 0x7B: 'DRIGHT', 0x7C: 'LS_UP', 0x7D: 'LS_DOWN',
       0x7E: 'LS_RIGHT', 0x7F: 'LS_LEFT', 0x80: 'RS_UP', 0x81: 'RS_DOWN', 0x82: 'RS_RIGHT', 0x83: 'RS_LEFT',
       0x84: 'LS_CLICK', 0x85: 'RS_CLICK', 0x86: 'START', 0x87: 'BACK'}

g = Game()
p1 = g.u32(g.u32(0x0118FB24) + 0x3570)
ctx = g.u32(g.u32(p1 + 0x188) + 4)
heads = g.u32(ctx + 0x10)
count = (g.u32(ctx + 0x14) - heads) // 4


def name(i):
    p = g.u32(0x01071DE8 + i * 4)
    b = g.read(p, 40) if p else b''
    return b.split(b'\x00')[0].decode('latin1') if b else '?'


for i in range(count):
    e, out = g.u32(heads + i * 4), []
    while e and len(out) < 8:
        w = [g.u32(e + 4 * k) for k in range(5)]
        out.append(' '.join('%08X' % x for x in w))
        e = w[4]
    print('%02X %-38s %s' % (i, name(i), ' | '.join(out)))
