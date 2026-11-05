# Checks Player 1's and Player 2's movement input are independent: P1 walks with W (keyboard)
# while P2 has no input, then P2 walks on test input while P1 has none.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import re, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
st = jc2ctl.mod('p2')
if 'updates' not in st:
    jc2ctl.focus()
    print(jc2ctl.mod('p2 join'))
    time.sleep(2)
    st = jc2ctl.mod('p2')
print(st)
body = int(re.search(r'character ([0-9A-F]+)', st).group(1), 16)
c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
pos = lambda c: g.floats(c + 0x18C, 3)
dist = lambda a, b: ((a[0] - b[0]) ** 2 + (a[2] - b[2]) ** 2) ** 0.5

jc2ctl.mod('p2 move 0 0')
jc2ctl.focus()
time.sleep(1)
a1, a2 = pos(c1), pos(body)
jc2ctl.tap('w', 1500)
time.sleep(0.3)
print('P1 holds W:      rico1 moved %.2f m, rico2 moved %.2f m' % (dist(a1, pos(c1)), dist(a2, pos(body))))

time.sleep(1)
a1, a2 = pos(c1), pos(body)
jc2ctl.mod('p2 move 0 1')
jc2ctl.focus()
time.sleep(1.5)
jc2ctl.mod('p2 move 0 0')
time.sleep(0.3)
print('P2 test input:   rico1 moved %.2f m, rico2 moved %.2f m' % (dist(a1, pos(c1)), dist(a2, pos(body))))
print(jc2ctl.mod('p2'))
