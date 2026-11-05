# Dumps the CCar (vtable 0x00EBFF64, position +0x118) nearest a point to scans/cardump_<tag>.bin,
# or diffs two dumps of the same car (dwords that changed, with the class of pointers).
#   python -I cardump.py save <tag> <x> <z>
#   python -I cardump.py diff <a> <b>
import json, math, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from procmem import Game

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'scans')
SIZE = 0x1400
g = Game()
names = json.load(open(os.path.join(HERE, 'rtti_map.json')))


def label(v):
    if '%08X' % v in names:
        return 'vt ' + names['%08X' % v]
    if 0x02000000 < v < 0x7FFF0000:
        vt = g.u32(v)
        return names.get('%08X' % vt, '') if vt is not None else ''
    f = struct.unpack('<f', struct.pack('<I', v))[0]
    return '%.3f' % f if v and 1e-4 < abs(f) < 1e7 else ''


if sys.argv[1] == 'save':
    tag, x, z = sys.argv[2], float(sys.argv[3]), float(sys.argv[4])
    best = None
    for a in g.find_dwords([0x00EBFF64], limit=2000)[0x00EBFF64]:
        if a < 0x02000000:
            continue
        px, py, pz = g.floats(a + 0x118, 3)
        d = math.hypot(px - x, pz - z)
        if best is None or d < best[0]:
            best = (d, a)
    d, a = best
    data = g.read(a, SIZE)
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, 'cardump_%s.bin' % tag), 'wb') as f:
        f.write(struct.pack('<I', a) + data)
    print('car %08X (%.0f m) saved' % (a, d))
else:
    da = open(os.path.join(OUT, 'cardump_%s.bin' % sys.argv[2]), 'rb').read()
    db = open(os.path.join(OUT, 'cardump_%s.bin' % sys.argv[3]), 'rb').read()
    aa, ab = struct.unpack('<I', da[:4])[0], struct.unpack('<I', db[:4])[0]
    print('cars %08X / %08X' % (aa, ab))
    for k in range(0, SIZE, 4):
        va = struct.unpack_from('<I', da, 4 + k)[0]
        vb = struct.unpack_from('<I', db, 4 + k)[0]
        if va != vb:
            print('+%03X %08X %-28s %08X %s' % (k, va, label(va)[:28], vb, label(vb)[:40]))
