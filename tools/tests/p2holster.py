# Player 2's holstered weapons should sit on their body, not at their feet (root position).
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
p2 = int(re.search(r'player 2 ([0-9A-F]+)', jc2ctl.mod('p2')).group(1), 16)
c = g.u32(p2 + 0x48)
inv = g.u32(c + 0x484)


def show(tag):
    p = g.floats(c + 0x18C, 3)
    out = []
    for s in range(4):
        w = g.u32(inv + 0x3C + s * 8)
        if w:
            q = g.floats(w + 0x84, 3)
            out.append(round(q[1] - p[1], 2))
    print('%s: hand mode %d, weapon heights above P2\'s feet %s' % (tag, g.u32(inv + 0x64), out))


time.sleep(0.5)
show('after join')
jc2ctl.focus()
jc2ctl.mod('p2 move 0 1')
time.sleep(1)
jc2ctl.mod('p2 move 0 0')
show('after running')
print(jc2ctl.mod('screenshot ' + os.path.join(_ROOT, r'tools\shots\p2_holster.png')))
