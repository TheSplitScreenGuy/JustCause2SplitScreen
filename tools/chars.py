# Lists every live CCharacter: position, distance to Rico, controller (+0x2F0) and brain (+0x9C8) classes.
import json, math, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from procmem import Game

names = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'rtti_map.json')))
g = Game()
CHAR_VT = 0xEB881C
PLAYER_PTR_CHAR = 0x48


def cls(p):
    vt = g.u32(p) if p else None
    return names.get('%08X' % vt, '?') if vt else '-'


chars = g.find_dwords([CHAR_VT], limit=500)[CHAR_VT]
# Rico is the character whose brain is CPlayer(+8).
rico = next((c for c in chars if cls(g.u32(c + 0x9C8)).startswith('CPlayer')), None)
rp = g.floats(rico + 0x18C, 3) if rico else (0, 0, 0)
for c in chars:
    p = g.floats(c + 0x18C, 3)
    d = math.dist(p, rp)
    print('%08X %s pos(%7.1f %6.1f %7.1f) dist %6.1f  ctrl %-28s brain %s' % (
        c, 'RICO' if c == rico else '    ', p[0], p[1], p[2], d, cls(g.u32(c + 0x2F0)), cls(g.u32(c + 0x9C8))))
