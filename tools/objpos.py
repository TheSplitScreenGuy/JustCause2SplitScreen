# Where live objects of a class are: finds instances by vtable, reads a world position at the given
# offset (float x, y, z; auto-detected when omitted from the first instance whose floats look like
# a world matrix translation), and histograms them by 512 m cell and distance to two points.
#   python -I objpos.py <hex vtable> [hex offset] [--near x z] [--near x z]
import os, struct, sys, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from procmem import Game

g = Game()
args = sys.argv[1:]
points = []
while '--near' in args:
    i = args.index('--near'); points.append((float(args[i + 1]), float(args[i + 2]))); del args[i:i + 3]
vt = int(args[0], 16)
off = int(args[1], 16) if len(args) > 1 else None
objs = g.find_dwords([vt], limit=20000)[vt]
print(len(objs), 'objects')
if off is None:
    # A plausible world position: |x|,|z| < 17000, 0 < y < 3000, at a 16-byte aligned offset.
    best = {}
    for o in objs[:200]:
        raw = g.read(o, 0x200)
        for k in range(0, 0x200 - 12, 4):
            x, y, z = struct.unpack_from('<3f', raw, k)
            if all(v == v for v in (x, y, z)) and 50 < abs(x) < 17000 and 50 < abs(z) < 17000 and 0 < y < 3000:
                best[k] = best.get(k, 0) + 1
    off = max(best, key=best.get) if best else 0
    print('position offset +0x%X (%d of %d)' % (off, best.get(off, 0), min(len(objs), 200)))
cells = {}
near = [0] * len(points)
for o in objs:
    try:
        x, y, z = struct.unpack('<3f', g.read(o + off, 12))
    except Exception:
        continue
    c = (int((x + 16384) // 512), int((z + 16384) // 512))
    cells[c] = cells.get(c, 0) + 1
    for i, p in enumerate(points):
        if math.hypot(x - p[0], z - p[1]) < 500: near[i] += 1
for i, p in enumerate(points):
    print('within 500 m of %.0f %.0f: %d' % (p[0], p[1], near[i]))
for c, n in sorted(cells.items(), key=lambda kv: -kv[1])[:12]:
    print('cell %2dx%2d: %d' % (c[0], c[1], n))
