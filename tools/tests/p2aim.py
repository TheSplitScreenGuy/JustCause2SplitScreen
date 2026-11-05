# Player 2's aim follows Player 2's camera: aim point direction vs P2 camera, then grapple and fire.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import math, re, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
shots = os.path.join(_ROOT, r'tools\shots')
st = jc2ctl.mod('p2')
if 'updates' not in st:
    jc2ctl.focus()
    print(jc2ctl.mod('p2 join'))
    time.sleep(2)
p1 = g.u32(g.u32(0x0118FB24) + 0x3570)
p2 = int(re.search(r'player 2 ([0-9A-F]+)', jc2ctl.mod('p2')).group(1), 16)
c2 = g.u32(p2 + 0x48)
inv = g.u32(c2 + 0x484)


def report(tag):
    s = jc2ctl.mod('p2')
    f = [float(x) for x in re.search(r'cam2 f (\S+) (\S+)', s).groups()]
    pos = g.floats(c2 + 0x18C, 3)
    a2 = g.floats(g.u32(p2 + 0x184) + 0x44, 3)
    a1 = g.floats(g.u32(p1 + 0x184) + 0x44, 3)
    d = (a2[0] - pos[0], a2[2] - pos[2])
    n = math.hypot(*d) or 1
    print('%s: P2 aim %s (%.0f m away), dot with P2 camera forward %.2f | P1 aim %s' % (
        tag, [round(v, 1) for v in a2], n, (d[0] * f[0] + d[1] * f[1]) / n, [round(v, 1) for v in a1]))


report('camera 0')
print(jc2ctl.mod('p2 turn 90'))
time.sleep(0.5)
report('camera +90')
jc2ctl.focus()
print(jc2ctl.mod('ammo'))
time.sleep(0.5)
clips = lambda: [g.u32(g.u32(inv + 0x3C + s * 8) + 0x1E8) for s in range(3)]
print('clips', clips(), 'hand mode', g.u32(inv + 0x64))
jc2ctl.mod('p2 press D 1500')
time.sleep(0.8)
print(jc2ctl.mod('screenshot %s\\p2_aim_fire.png' % shots))
time.sleep(1)
print('clips after fire', clips())
time.sleep(0.5)
jc2ctl.mod('p2 press 51 300')
time.sleep(0.4)
print(jc2ctl.mod('screenshot %s\\p2_aim_grapple.png' % shots))
