# Finds globals (exe .data, 0x0108xxxx-0x0140xxxx) holding a position that follows Player 1:
# float x at +0, z at +8 within tolerance of P1, before and after moving P1 with "p1 at".
#   python focusglobals.py <x> <y> <z> [tolerance]      (P1 is moved there; needs numpy)
import os, sys, time
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from procmem import Game
import jc2ctl

g = Game()
LO, HI = 0x01080000, 0x01410000
tol = float(sys.argv[4]) if len(sys.argv) > 4 else 80.0


def p1():
    c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
    return g.floats(c1 + 0x18C, 3)


def scan(x, z):
    b = g.read(LO, HI - LO)
    f = np.frombuffer(b[:len(b) // 4 * 4], dtype='<f4')
    with np.errstate(invalid='ignore'):
        ok = (np.abs(f[:-2] - x) < tol) & (np.abs(f[2:] - z) < tol)
    return set(LO + 4 * int(i) for i in np.nonzero(ok)[0])


a = p1()
first = scan(a[0], a[2])
print('P1 at %.0f %.0f: %d candidates' % (a[0], a[2], len(first)))
print(jc2ctl.mod('p1 at %s %s %s' % tuple(sys.argv[1:4])))
time.sleep(12)
b = p1()
second = scan(b[0], b[2])
print('P1 at %.0f %.0f' % (b[0], b[2]))
for addr in sorted(first & second):
    print('%08X  %.1f %.1f %.1f' % ((addr,) + tuple(g.floats(addr, 3))))
