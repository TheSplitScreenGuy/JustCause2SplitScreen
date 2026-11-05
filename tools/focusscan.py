# Finds memory that tracks Player 1's position: vec3s (x at +0, z at +8) near P1, then moves P1
# next to P2 (mod command p1top2) and keeps those that followed him. Each survivor is printed with
# the nearest preceding known vtable (class from tools/rtti_map.json).
# Needs P2 joined and placed far away first. Usage: python -I focusscan.py [tolerance_m]
import sys, os, time, json, struct
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from procmem import Game
import jc2ctl

tol = float(sys.argv[1]) if len(sys.argv) > 1 else 70.0
g = Game()
rtti = {int(k, 16): v for k, v in json.load(open(os.path.join(os.path.dirname(__file__), 'rtti_map.json'))).items()}


def p1():
    c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
    return g.floats(c1 + 0x18C, 3)


def scan(x, z):
    hits = []
    for base, size in g.regions():
        for off in range(0, size, 1 << 24):
            n = min(1 << 24, size - off)
            b = g.read(base + off, n)
            if not b or len(b) < 16:
                continue
            f = np.frombuffer(b[:len(b) // 4 * 4], dtype='<f4')
            with np.errstate(invalid='ignore'):
                ok = (np.abs(f[:-2] - x) < tol) & (np.abs(f[2:] - z) < tol)
            for i in np.nonzero(ok)[0]:
                hits.append(base + off + int(i) * 4)
    return hits


def owner(addr):
    for back in range(0, 0x4000, 4):
        v = g.u32(addr - back)
        if v in rtti:
            return '%s+0x%X' % (rtti[v], back)
    return '?'


x0, _, z0 = p1()
print('P1 at %.1f %.1f' % (x0, z0))
before = scan(x0, z0)
print('candidates', len(before))
print(jc2ctl.mod('p1top2'))
time.sleep(4)
x1, _, z1 = p1()
print('P1 now at %.1f %.1f' % (x1, z1))
kept = []
for a in before:
    v = g.read(a, 12)
    if not v:
        continue
    x, _, z = struct.unpack('<3f', v)
    if abs(x - x1) < tol and abs(z - z1) < tol:
        kept.append((a, x, z))
print('followed', len(kept))
seen = {}
for a, x, z in kept:
    o = owner(a)
    seen.setdefault(o.split('+')[0], []).append('%08X %s (%.0f %.0f)' % (a, o, x, z))
for cls, rows in sorted(seen.items(), key=lambda kv: -len(kv[1])):
    print('%-50s %d' % (cls, len(rows)))
    for r in rows[:4]:
        print('    ' + r)
