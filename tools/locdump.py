# Dumps the loaded location objects whose entry name contains a substring (near and far ones), raw
# 0x400 bytes each plus their +0xBC composer, to scans/locdump_<tag>.json; "diff a b" shows the
# dwords that changed per location.
#   python -I locdump.py save <tag> <substring>
#   python -I locdump.py diff <a> <b>
import json, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'scans')
SIZE = 0x400

if sys.argv[1] == 'save':
    from procmem import Game
    g = Game()
    sm = g.u32(0x01191720) + 0x58
    snap = {}
    b, e = g.u32(sm + 0x14), g.u32(sm + 0x18)
    for i in range((e - b) // 0x28 if b else 0):
        ent = g.read(b + i * 0x28, 0x28)
        obj = struct.unpack_from('<I', ent, 0x24)[0]
        name = g.read(struct.unpack_from('<I', ent, 0)[0], 64).split(b'\0')[0].decode(errors='replace')
        if not obj or sys.argv[3] not in name: continue
        key = '%s.%s' % (name, 'nl' if g.read(obj + 0xC0, 1)[0] else 'fl')
        comp = g.u32(obj + 0xBC)
        snap[key] = {'obj': '%08X' % obj, 'loc': g.read(obj, SIZE).hex(),
                     'comp': g.read(comp, SIZE).hex() if comp else ''}
    os.makedirs(OUT, exist_ok=True)
    json.dump(snap, open(os.path.join(OUT, 'locdump_%s.json' % sys.argv[2]), 'w'))
    print('saved', ', '.join('%s %s' % (k, v['obj']) for k, v in snap.items()))
else:
    A = json.load(open(os.path.join(OUT, 'locdump_%s.json' % sys.argv[2])))
    B = json.load(open(os.path.join(OUT, 'locdump_%s.json' % sys.argv[3])))
    for k in A:
        if k not in B: continue
        for part in ('loc', 'comp'):
            a, b = bytes.fromhex(A[k][part]), bytes.fromhex(B[k][part])
            diffs = ['+%03X %08X->%08X' % (i, struct.unpack_from('<I', a, i)[0], struct.unpack_from('<I', b, i)[0])
                     for i in range(0, min(len(a), len(b)), 4) if a[i:i + 4] != b[i:i + 4]]
            print('%s %s (%s -> %s): %s' % (k, part, A[k]['obj'], B[k]['obj'], ' '.join(diffs[:24])))
