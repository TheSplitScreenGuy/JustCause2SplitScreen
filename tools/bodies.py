# Havok rigid bodies (hkpRigidBody, vtable 0xFAEC58) within a radius of a point: address, world
# pointer (+8, 0 = not in a world), position (offset found by looking for floats near the point,
# relative to the floating origin when --origin is given), saved for a diff.
#   python -I bodies.py save <tag> <x> <z> [radius]
#   python -I bodies.py diff <a> <b>
import json, os, struct, sys, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'scans')
VT = 0xFAEC58

if sys.argv[1] == 'save':
    from procmem import Game
    g = Game()
    x, z = float(sys.argv[3]), float(sys.argv[4])
    # Havok positions are relative to the floating origin (physics system [0x0118FFF4] +0x28/+0x30).
    ps = g.u32(0x0118FFF4)
    ox, _, oz = g.floats(ps + 0x28, 3)
    print('origin %.0f %.0f' % (ox, oz))
    x, z = x - ox, z - oz
    radius = float(sys.argv[5]) if len(sys.argv) > 5 else 60
    bodies = g.find_dwords([VT], limit=50000)[VT]
    # Havok works relative to the floating origin: try both world and origin-relative positions.
    out = {}
    for b in bodies:
        try:
            raw = g.read(b, 0x300)
        except Exception:
            continue
        world = struct.unpack_from('<I', raw, 8)[0]
        # hkpWorldObject: collidable at +0x10 (hkpCdBody: shape, shape key, motion -> hkTransform,
        # translation at +0x30).
        shape, transform = struct.unpack_from('<I', raw, 0x10)[0], struct.unpack_from('<I', raw, 0x18)[0]
        try:
            px, py, pz = g.floats(transform + 0x30, 3)
        except Exception:
            continue
        if px == px and pz == pz and math.hypot(px - x, pz - z) < radius:
            out['%08X' % b] = {'world': '%08X' % world, 'shape': '%08X' % shape, 'pos': [px + ox, py, pz + oz]}
    os.makedirs(OUT, exist_ok=True)
    json.dump(out, open(os.path.join(OUT, 'bodies_%s.json' % sys.argv[2]), 'w'))
    worlds = {}
    for v in out.values(): worlds[v['world']] = worlds.get(v['world'], 0) + 1
    print(len(bodies), 'bodies,', len(out), 'near the point; by world:', worlds)
else:
    A = json.load(open(os.path.join(OUT, 'bodies_%s.json' % sys.argv[2])))
    B = json.load(open(os.path.join(OUT, 'bodies_%s.json' % sys.argv[3])))
    print('only in', sys.argv[2], len(set(A) - set(B)), ' only in', sys.argv[3], len(set(B) - set(A)))
    for k in sorted(set(A) - set(B))[:20]: print('  gone', k, A[k])
    changed = [k for k in set(A) & set(B) if A[k]['world'] != B[k]['world']]
    print('world changed', len(changed))
    for k in changed[:20]: print('  ', k, A[k]['world'], '->', B[k]['world'], A[k]['pos'])
