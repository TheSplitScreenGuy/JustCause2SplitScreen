# Player 2 fires the grapple (FIRE_GRAPPLE 0x51) at whatever his crosshair is on; prints both
# players' positions over time (P2 should be pulled toward the hook, P1 should stay put), and P2's
# launcher clip (counts down per shot from -1) to show the launcher fired.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import re, struct, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
if 'updates' not in jc2ctl.mod('p2'):
    jc2ctl.focus()
    print(jc2ctl.mod('p2 join'))
    time.sleep(3)
c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
c2 = g.u32(int(re.search(r'player 2 ([0-9A-F]+)', jc2ctl.mod('p2')).group(1), 16) + 0x48)
launcher = g.u32(g.u32(c2 + 0x484) + 0x54)
pos = lambda c: g.floats(c + 0x18C, 3)
clip = lambda: struct.unpack('<i', g.read(launcher + 0x1E8, 4))[0]
p1, p2 = pos(c1), pos(c2)
print('launcher %08X active %d clip %d' % (launcher, g.read(launcher + 0x42, 1)[0] & 1, clip()))
jc2ctl.focus()
time.sleep(0.3)
print(jc2ctl.mod('p2 press 51 300'))
t0 = time.time()
shot = False
while time.time() - t0 < 2.5:
    a, b = pos(c1), pos(c2)
    d1 = sum((a[i] - p1[i]) ** 2 for i in range(3)) ** 0.5
    d2 = sum((b[i] - p2[i]) ** 2 for i in range(3)) ** 0.5
    print('%.2f  P1 moved %5.1f m | P2 moved %5.1f m | clip %d' % (time.time() - t0, d1, d2, clip()))
    if not shot and time.time() - t0 > 0.6:
        shot = True
        print(jc2ctl.mod('screenshot ' + os.path.join(_ROOT, r'tools\shots\p2_hook.png')))
    time.sleep(0.1)
