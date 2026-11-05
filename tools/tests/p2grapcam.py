# Does Player 2's grapple disturb Player 1? P2 is placed at several spots next to P1 and fires the
# grapple; prints how far P1's camera direction (main camera world, sampled; can catch P2's view
# while it's swapped in) and P1's rendered view (render camera samples near P1) turned, P1's facing,
# and how far P2 got pulled. Optional args are sent as a "p2 ..." command first.
# Known (2026-10-25): P1's view turns up to ~100 degrees while P2 reels in, even with the split off.
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
    time.sleep(3)
mgr = g.u32(0x011919F4)
cam = mgr + 0x18
c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
c2 = g.u32(int(re.search(r'player 2 ([0-9A-F]+)', jc2ctl.mod('p2')).group(1), 16) + 0x48)
pos = lambda c: g.floats(c + 0x18C, 3)
facing = lambda c: g.floats(c + 0x15C + 0x20, 3)
if len(sys.argv) > 1:
    print(jc2ctl.mod('p2 ' + ' '.join(sys.argv[1:])))
spread = lambda s: max(max(v[i] for v in s) - min(v[i] for v in s) for i in range(3))
jc2ctl.focus()
for off in ('-25 0.3 0', '-8 0.3 4', '8 0.3 4', '25 0.3 0'):
    jc2ctl.mod('p2 tp ' + off)
    time.sleep(1.5)
    b = pos(c2)
    cams, faces, renders = [], [], []
    end = time.time() + 1.8
    jc2ctl.mod('p2 press 51 300')
    while time.time() < end:
        cams.append(g.floats(cam + 0x24 + 0x20, 3))
        faces.append(facing(c1))
        # The render camera alternates between the two views; keep Player 1's (near Player 1).
        r = g.floats(g.u32(mgr + 0x14) + 0x24, 16)
        p = pos(c1)
        if sum((r[12 + i] - p[i]) ** 2 for i in range(3)) < 36:
            renders.append(r[8:11])
        time.sleep(0.005)
    y = pos(c2)
    print('P2 at %-11s P1 main camera turn %.3f, rendered view turn %.3f (%d), facing turn %.3f | P2 pulled %.1f m' % (
        off, spread(cams), spread(renders) if renders else -1, len(renders), spread(faces),
        sum((y[k] - b[k]) ** 2 for k in range(3)) ** 0.5))
