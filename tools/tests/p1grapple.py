# Player 1 grapples onto the ground a little ahead (keyboard E, camera tilted down); logs P1's
# position and the main camera's position each step, and screenshots the split.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
key = sys.argv[1] if len(sys.argv) > 1 else 'e'
shots = os.path.join(_ROOT, r'tools\shots')
if 'updates' not in jc2ctl.mod('p2'):
    jc2ctl.focus()
    print(jc2ctl.mod('p2 join'))
    time.sleep(2)
c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
mgr = g.u32(0x011919F4)
cam = mgr + 0x18
pos = lambda a: [round(v, 1) for v in g.floats(a, 3)]
jc2ctl.focus()
time.sleep(0.3)
jc2ctl.mouse(0, 120)  # look down a bit
time.sleep(0.5)
print('before: P1', pos(c1 + 0x18C), 'render cam', pos(g.u32(mgr + 0x14) + 0x54))
if key in ('left', 'right'):
    jc2ctl.click(key, 100)
else:
    jc2ctl.tap(key, 100)
for i in range(12):
    time.sleep(0.12)
    print('  %.2fs P1 %s render cam %s' % ((i + 1) * 0.12, pos(c1 + 0x18C), pos(g.u32(mgr + 0x14) + 0x54)))
    if i in (2, 5, 8):
        jc2ctl.mod('screenshot %s\\p1_grapple_%d.png' % (shots, i))
jc2ctl.mouse(0, -120)
