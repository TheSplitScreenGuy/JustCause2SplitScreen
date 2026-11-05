# Lists the spatial location entries (CGameWorld [0x01191720] + 0x58): name, kind, box, loaded
# object and its near flag (+0xC0: .nl vs .fl blocks). Filter by a substring or a point.
#   python -I locations.py [substring] [--at x z] [--loaded]
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from procmem import Game

g = Game()
args = sys.argv[1:]
at = None
if '--at' in args:
    i = args.index('--at'); at = (float(args[i + 1]), float(args[i + 2])); del args[i:i + 3]
only_loaded = '--loaded' in args
if only_loaded: args.remove('--loaded')
sub = args[0] if args else ''
sm = g.u32(0x01191720) + 0x58


def cstr(p):
    try:
        return g.read(p, 80).split(b'\0')[0].decode(errors='replace')
    except Exception:
        return '?'


loaded_set = set()
lb, le = g.u32(sm + 0x34), g.u32(sm + 0x38)
for i in range((le - lb) // 4 if lb else 0): loaded_set.add(g.u32(lb + i * 4))
print('manager %08X flags %02X loaded %d loading %d' % (sm, g.read(sm + 0x64, 1)[0], len(loaded_set),
      (g.u32(sm + 0x58) - g.u32(sm + 0x54)) // 4 if g.u32(sm + 0x54) else 0))
for vec in (0x4, 0x14, 0x24):
    b, e = g.u32(sm + vec), g.u32(sm + vec + 4)
    n = (e - b) // 0x28 if b else 0
    raw = g.read(b, n * 0x28) if n else b''
    shown = 0
    for i in range(n):
        ent = raw[i * 0x28:(i + 1) * 0x28]
        name = cstr(struct.unpack_from('<I', ent, 0)[0])
        mn = struct.unpack_from('<3f', ent, 4); mx = struct.unpack_from('<3f', ent, 0x18)
        f10, f14 = struct.unpack_from('<IB', ent, 0x10)
        kind = ((f10 << 28) & 0xFFFFFFFF)
        kind = (kind - (1 << 32) if kind & 0x80000000 else kind) >> 30
        obj = struct.unpack_from('<I', ent, 0x24)[0]
        if sub and sub not in name: continue
        if at and not (mn[0] < at[0] < mx[0] and mn[2] < at[1] < mx[2]): continue
        if only_loaded and not obj: continue
        near = g.read(obj + 0xC0, 1)[0] if obj else '-'
        print('  +%02X %-48s kind %2d f14 %02X box x %7.0f..%-7.0f z %7.0f..%-7.0f obj %08X near %s' % (
            vec, name[:48], kind, f14, mn[0], mx[0], mn[2], mx[2], obj, near))
        shown += 1
        if shown > 60: break
