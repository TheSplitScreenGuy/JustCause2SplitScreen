# Lists CRenderBlockSplineRoad blocks with their 4 textures (+0x20/+0x28/+0x30/+0x38) and texture names.
#   python -I tools\tests\roadblocks.py
import json, os, struct, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from procmem import Game

names = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'rtti_map.json')))
g = Game()
VT = 0x00F22C64


def cls(p):
    vt = g.u32(p) if p and 0x10000 < p < 0x7FFF0000 else None
    return names.get('%08X' % vt, '?') if vt else '-'


def tex_name(p):
    # look for a std::string-ish printable path inside the texture object
    b = g.read(p, 0x100) if p else None
    if not b:
        return ''
    for off in range(0, 0x100 - 0x1C, 4):
        n, cap = struct.unpack_from('<II', b, off + 0x14)
        if 0 < n < 200 and n <= cap and (cap == 15 or 16 <= cap < 512):
            if cap == 15:
                s = b[off + 4:off + 4 + n]
            else:
                q = struct.unpack_from('<I', b, off + 4)[0]
                s = g.read(q, n) if 0x10000 < q < 0x7FFF0000 else None
            if s and all(32 <= c < 127 for c in s):
                return s.decode()
    return ''


blocks = g.find_dwords([VT], limit=2000)[VT]
print(len(blocks), 'road blocks (vtable matches)')
near = len(sys.argv) > 1 and sys.argv[1] == 'near'
if not near:
    blocks = [b for b in blocks if g.read(b + 0x10, 1)[0] in (0, 1)]
    print(len(blocks), 'with a valid +0x10 flag')
try:
    import jc2ctl
    st = jc2ctl.mod('p2')
    import re
    p2 = [float(v) for v in re.search(r'pos ([-\d.]+) ([-\d.]+) ([-\d.]+)', st).groups()]
except Exception:
    p2 = None
for b in blocks:
    box = g.floats(b + 0x50, 6)
    cx, cz = (box[0] + box[3]) / 2, (box[2] + box[5]) / 2
    d2 = ((cx - p2[0]) ** 2 + (cz - p2[2]) ** 2) ** 0.5 if p2 else -1
    if near and not (0 <= d2 < 400):
        continue
    print('  %08X flag %d box x %.0f..%.0f z %.0f..%.0f  dist to P2 %.0f  tex %08X %08X %08X %08X' % (
        b, g.read(b + 0x10, 1)[0], box[0], box[3], box[2], box[5], d2,
        g.u32(b + 0x20), g.u32(b + 0x28), g.u32(b + 0x30), g.u32(b + 0x38)))
seen = {}
for b in blocks:
    texs = [g.u32(b + o) for o in (0x20, 0x28, 0x30, 0x38)]
    key = tuple(texs)
    seen.setdefault(key, []).append(b)
for key, bl in sorted(seen.items(), key=lambda kv: -len(kv[1])):
    print('%3d blocks e.g. %08X:' % (len(bl), bl[0]), ' | '.join('%08X %s %s' % (t, cls(t), tex_name(t)) for t in key))
