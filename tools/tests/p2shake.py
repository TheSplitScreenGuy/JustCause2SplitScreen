# Player 2's shots shouldn't shake Player 1's camera: jitter of Player 1's camera (main camera world,
# which holds Player 1's view between frames) while P2 idles vs while P2 fires.
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
mgr = g.u32(0x011919F4)
cam = mgr + 0x18
jc2ctl.mod('p2 tp -6 0.3 4')  # out of Player 1's crosshair, or P2 can't fire
time.sleep(1.5)
jc2ctl.focus()
jc2ctl.mod('ammo')
time.sleep(0.5)


def jitter(tag, firing):
    samples = []
    end = time.time() + 1.5
    shot = 0
    while time.time() < end:
        if firing and time.time() > shot:
            jc2ctl.mod('p2 press B+D 120')
            shot = time.time() + 0.3
        samples.append(g.floats(cam + 0x24 + 0x20, 3))  # camera back vector (rotation shake)
        time.sleep(0.01)
    spread = max(max(s[i] for s in samples) - min(s[i] for s in samples) for i in range(3))
    print('%s: P1 camera direction spread %.4f over %d samples' % (tag, spread, len(samples)))


jitter('P2 idle  ', False)
jitter('P2 firing', True)
