# Snapshots Player 1's CPlayer, its character and its input context, to find control-lock state.
#   python -I tools\objdiff.py snap <name>     two snapshots 0.5 s apart
#   python -I tools\objdiff.py diff <a> <b>    stable within each, different between them
import os, struct, sys, time, pickle
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from procmem import Game

DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'shots')


def objects(g):
    manager = g.u32(0x0118FB24)
    player = g.u32(manager + 0x3570)
    character = g.u32(player + 0x48)
    steering = g.u32(player + 0x188)
    ctx = g.u32(steering + 4) if steering else 0
    return {'manager': (manager, 0x3600), 'player': (player, 0x800), 'character': (character, 0x1000),
            'steering': (steering, 0x200), 'inputctx': (ctx, 0x400)}


def grab(g):
    return {k: (a, g.read(a, n) if a else None) for k, (a, n) in objects(g).items()}


def path(name, i):
    return os.path.join(DIR, 'obj_%s_%d.pkl' % (name, i))


def main(argv):
    if argv[0] == 'snap':
        g = Game()
        for i in range(2):
            with open(path(argv[1], i), 'wb') as f:
                pickle.dump(grab(g), f)
            time.sleep(0.5)
    else:
        a = [pickle.load(open(path(argv[1], i), 'rb')) for i in range(2)]
        b = [pickle.load(open(path(argv[2], i), 'rb')) for i in range(2)]
        for k in a[0]:
            base, a0 = a[0][k]
            if not a0 or not b[0][k][1] or b[0][k][0] != base:
                print(k, 'missing or moved')
                continue
            for off in range(0, len(a0), 4):
                va, vb = a0[off:off + 4], b[0][k][1][off:off + 4]
                if va != vb and va == a[1][k][1][off:off + 4] and vb == b[1][k][1][off:off + 4]:
                    x, y = struct.unpack('<I', va)[0], struct.unpack('<I', vb)[0]
                    print('%-9s +%04X  %08X -> %08X  (%g -> %g)' % (k, off, x, y, struct.unpack('<f', va)[0], struct.unpack('<f', vb)[0]))


main(sys.argv[1:])
