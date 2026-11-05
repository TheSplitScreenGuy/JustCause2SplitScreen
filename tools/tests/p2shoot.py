# Player 2 shoots along their own camera: P2 is moved out of Player 1's crosshair (being aimed at
# by the player blocks firing, state +0x120), fires (FIRE + FIRE_RIGHT like the right trigger) at
# two camera headings and checks the clip drops and the gun points along P2's aim.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import math, re, sys, time
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
state = g.u32(c + 0xA34)
jc2ctl.mod('p2 tp -6 0.3 4')
time.sleep(1.5)
jc2ctl.focus()
jc2ctl.mod('ammo')
time.sleep(0.8)
for turn in (0, 90):
    if turn:
        jc2ctl.mod('p2 turn %d' % turn)
        time.sleep(0.5)
    w = g.u32(inv + 0x44)  # hand mode 2 = slot 1 (pistol)
    before = g.u32(w + 0x1E8)
    jc2ctl.focus()
    jc2ctl.mod('p2 press B+D 200')
    time.sleep(0.6)
    pos, aim, muzzle = g.floats(c + 0x18C, 3), g.floats(state + 0x44, 3), g.floats(w + 0x194, 3)
    a = (aim[0] - pos[0], aim[2] - pos[2])
    m = (muzzle[0] - pos[0], muzzle[2] - pos[2])
    dot = (a[0] * m[0] + a[1] * m[1]) / ((math.hypot(*a) or 1) * (math.hypot(*m) or 1))
    print('camera +%d: clip %d -> %d, gun vs aim direction %.2f' % (turn, before, g.u32(w + 0x1E8), dot))
