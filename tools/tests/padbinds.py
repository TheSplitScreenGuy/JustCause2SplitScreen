# Player 1's pad bindings. Binding blocks (16 bytes): [action << 24 | flags | key] [key name
# table + key code] ...; pad keys are 0x70 RPADA .. 0x87 BACK (key name map at 0x00573E00), the
# second dword points into the key name table (base + code, e.g. 0x103A0F70 for RPADA).
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import struct, sys
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game

PAD = {0x70: 'A', 0x71: 'B', 0x72: 'X', 0x73: 'Y', 0x74: 'LT', 0x75: 'RT', 0x76: 'RB', 0x77: 'LB',
       0x78: 'DUP', 0x79: 'DDOWN', 0x7A: 'DLEFT', 0x7B: 'DRIGHT', 0x7C: 'LS_UP', 0x7D: 'LS_DOWN',
       0x7F: 'LS_LEFT', 0x7E: 'LS_RIGHT', 0x80: 'RS_UP', 0x81: 'RS_DOWN', 0x83: 'RS_LEFT', 0x82: 'RS_RIGHT',
       0x84: 'LS_CLICK', 0x85: 'RS_CLICK', 0x86: 'START', 0x87: 'BACK'}
g = Game()


def name(i):
    p = g.u32(0x01071DE8 + i * 4)
    b = g.read(p, 40) if p else b''
    return b.split(b'\x00')[0].decode('latin1') if b else '?'


keybase = int(sys.argv[1], 16) if len(sys.argv) > 1 else 0x103A0F00
found = {}
for base, size in g.regions():
    data = g.read(base, size)
    if not data: continue
    for off in range(0, len(data) - 8, 4):
        k = struct.unpack_from('<I', data, off + 4)[0]
        if not (keybase + 0x70 <= k <= keybase + 0x87): continue
        v = struct.unpack_from('<I', data, off)[0]
        act = v >> 24
        if act > 0x9C: continue
        found.setdefault(act, set()).add((PAD[k - keybase], '%06X' % (v & 0xFFFFFF), '%08X' % (base + off)))
for act in sorted(found):
    print('%02X %-38s %s' % (act, name(act), '  '.join('%s(%s @%s)' % x for x in sorted(found[act]))))
