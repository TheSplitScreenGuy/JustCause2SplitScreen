# Snapshots the objects of a class within a radius of a point (raw bytes), or diffs two snapshots
# field by field (the dwords that changed in most objects first).
#   python -I objsnap.py save <name> <hex vtable> <hex pos offset> <x> <z> [radius] [size]
#   python -I objsnap.py diff <a> <b>
import json, os, struct, sys, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'scans')

if sys.argv[1] == 'save':
    from procmem import Game
    g = Game()
    name, vt, off, x, z = sys.argv[2], int(sys.argv[3], 16), int(sys.argv[4], 16), float(sys.argv[5]), float(sys.argv[6])
    radius = float(sys.argv[7]) if len(sys.argv) > 7 else 60
    size = int(sys.argv[8], 16) if len(sys.argv) > 8 else 0x300
    snap = {}
    for o in g.find_dwords([vt], limit=20000)[vt]:
        try:
            px, py, pz = struct.unpack('<3f', g.read(o + off, 12))
            if math.hypot(px - x, pz - z) < radius: snap['%08X' % o] = g.read(o, size).hex()
        except Exception:
            pass
    os.makedirs(OUT, exist_ok=True)
    json.dump(snap, open(os.path.join(OUT, 'obj_%s.json' % name), 'w'))
    print('saved', len(snap), 'objects')
else:
    A = json.load(open(os.path.join(OUT, 'obj_%s.json' % sys.argv[2])))
    B = json.load(open(os.path.join(OUT, 'obj_%s.json' % sys.argv[3])))
    common = [k for k in A if k in B]
    print(len(common), 'objects in both')
    changed = {}
    for k in common:
        a, b = bytes.fromhex(A[k]), bytes.fromhex(B[k])
        for i in range(0, min(len(a), len(b)) - 3, 4):
            if a[i:i + 4] != b[i:i + 4]:
                changed.setdefault(i, []).append((a[i:i + 4], b[i:i + 4]))
    for i, ch in sorted(changed.items(), key=lambda kv: -len(kv[1]))[:30]:
        ex = ch[0]
        print('+%03X changed in %3d: e.g. %08X -> %08X' % (i, len(ch), struct.unpack('<I', ex[0])[0], struct.unpack('<I', ex[1])[0]))
