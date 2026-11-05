# Snapshots each loaded location (CLocation, 0x200 bytes) and its +0xBC collision composer
# (0x200 bytes) whose name matches; diff two snapshots.
#   python -I locsnap.py save <name> [substring]
#   python -I locsnap.py diff <a> <b>
import json, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'scans')

if sys.argv[1] == 'save':
    from procmem import Game
    g = Game()
    sub = sys.argv[3] if len(sys.argv) > 3 else ''
    sm = g.u32(0x01191720) + 0x58
    snap = {}
    b, e = g.u32(sm + 0x14), g.u32(sm + 0x18)
    for i in range((e - b) // 0x28 if b else 0):
        ent = g.read(b + i * 0x28, 0x28)
        obj = struct.unpack_from('<I', ent, 0x24)[0]
        if not obj: continue
        name = g.read(struct.unpack_from('<I', ent, 0)[0], 64).split(b'\0')[0].decode(errors='replace')
        kind = 'nl' if g.read(obj + 0xC0, 1)[0] else 'fl'
        if sub not in name: continue
        key = '%s.%s' % (name, kind)
        snap[key] = {'loc': g.read(obj, 0x200).hex()}
        comp = g.u32(obj + 0xBC)
        if comp: snap[key]['comp'] = g.read(comp, 0x200).hex()
    os.makedirs(OUT, exist_ok=True)
    json.dump(snap, open(os.path.join(OUT, 'loc_%s.json' % sys.argv[2]), 'w'))
    print('saved', len(snap))
else:
    A = json.load(open(os.path.join(OUT, 'loc_%s.json' % sys.argv[2])))
    B = json.load(open(os.path.join(OUT, 'loc_%s.json' % sys.argv[3])))
    for part in ('loc', 'comp'):
        changed = {}
        for k in A:
            if k not in B or part not in A[k] or part not in B[k]: continue
            a, b = bytes.fromhex(A[k][part]), bytes.fromhex(B[k][part])
            for i in range(0, 0x200, 4):
                if a[i:i + 4] != b[i:i + 4]:
                    changed.setdefault(i, []).append((k, a[i:i + 4].hex(), b[i:i + 4].hex()))
        print('==', part)
        for i, ch in sorted(changed.items(), key=lambda kv: -len(kv[1]))[:20]:
            print('+%03X in %3d, e.g. %s %s -> %s' % (i, len(ch), ch[0][0], ch[0][1], ch[0][2]))
