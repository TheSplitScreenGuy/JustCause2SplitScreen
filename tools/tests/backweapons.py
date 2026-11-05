# P1's holstered weapons should stay put on P1's back while only P2 moves.
# Prints each P1 weapon's position relative to P1 (in P1's local axes) with P2 idle vs running.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import re, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
if 'updates' not in jc2ctl.mod('p2'):
    jc2ctl.focus()
    print(jc2ctl.mod('p2 join'))
    time.sleep(2)
c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
inv = g.u32(c1 + 0x484)


def local():
    m = g.floats(c1 + 0x15C, 16)
    p = m[12:15]
    out = []
    for s in range(4):
        w = g.u32(inv + 0x3C + s * 8)
        if not w:
            continue
        q = g.floats(w + 0x84, 3)
        d = [q[i] - p[i] for i in range(3)]
        out.append(tuple(round(sum(d[i] * m[r * 4 + i] for i in range(3)), 2) for r in range(3)))
    return out


def spread(tag):
    samples = []
    for _ in range(10):
        samples.append(local())
        time.sleep(0.1)
    worst = 0.0
    for s in range(len(samples[0])):
        for axis in range(3):
            vals = [smp[s][axis] for smp in samples]
            worst = max(worst, max(vals) - min(vals))
    print('%s: P1 weapon local positions %s, max wobble %.2f m' % (tag, samples[0], worst))


jc2ctl.focus()
jc2ctl.mod('p2 move 0 0')
time.sleep(1)
spread('P2 idle   ')
jc2ctl.mod('p2 move 0 1')
time.sleep(0.5)
spread('P2 running')
jc2ctl.mod('p2 move 0 0')
