# Snapshots the exe's data sections (dwords) to find state that flips on a game event.
#   python -I tools\datadiff.py snap <name>        save two snapshots 0.5 s apart as <name>
#   python -I tools\datadiff.py diff <a> <b>       dwords stable within a and within b, different between them
# Snapshots go to tools\shots\diff_<name>.bin.
import os, struct, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from procmem import Game

START, END = 0x01000000, 0x01400000
DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'shots')


def grab(g):
    out = bytearray(END - START)
    for p in range(START, END, 0x1000):
        b = g.read(p, 0x1000)
        if b:
            out[p - START:p - START + 0x1000] = b
    return bytes(out)


def path(name, i):
    return os.path.join(DIR, 'diff_%s_%d.bin' % (name, i))


def main(argv):
    if argv[0] == 'snap':
        g = Game()
        os.makedirs(DIR, exist_ok=True)
        for i in range(2):
            with open(path(argv[1], i), 'wb') as f:
                f.write(grab(g))
            time.sleep(0.5)
    elif argv[0] == 'diff':
        a = [open(path(argv[1], i), 'rb').read() for i in range(2)]
        b = [open(path(argv[2], i), 'rb').read() for i in range(2)]
        n = 0
        for off in range(0, END - START, 4):
            va = a[0][off:off + 4]
            vb = b[0][off:off + 4]
            if va != vb and va == a[1][off:off + 4] and vb == b[1][off:off + 4]:
                x, y = struct.unpack('<I', va)[0], struct.unpack('<I', vb)[0]
                fx, fy = struct.unpack('<f', va)[0], struct.unpack('<f', vb)[0]
                print('%08X  %08X -> %08X   (%d -> %d)  (%g -> %g)' % (START + off, x, y, x, y, fx, fy))
                n += 1
        print('%d changed' % n)


main(sys.argv[1:])
