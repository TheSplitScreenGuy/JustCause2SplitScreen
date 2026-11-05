# P2's combat roll: gate checks, then the roll called directly, then RB; P2's position over time
# shows whether he rolled, plus a frame mid-roll.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import re, struct, sys, threading, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
c2 = int(re.search(r'character ([0-9A-F]+)', jc2ctl.mod('p2')).group(1), 16)
S = os.path.join(_ROOT, r'tools\shots\roll_%s.png')
pos = lambda: g.floats(c2 + 0x188, 3)


def watch(label, seconds=1.2):
    a = pos(); t0 = time.time(); peak = 0.0
    while time.time() - t0 < seconds:
        b = pos()
        peak = max(peak, ((b[0] - a[0]) ** 2 + (b[2] - a[2]) ** 2) ** 0.5)
        time.sleep(0.02)
    print('%-12s moved up to %.2f m' % (label, peak), flush=True)


print(jc2ctl.mod('p2 rollcheck'))
time.sleep(0.3)
if 'evade' in sys.argv:
    print(jc2ctl.mod('p2 press 81 150')); watch('press EVADE')
print(jc2ctl.mod('p2 padpress 200 150')); watch('pad RB')
print(jc2ctl.mod('p2 roll'))
time.sleep(0.25)
print(jc2ctl.mod('screenshot ' + S % 'direct'))
watch('direct roll')
print(jc2ctl.mod('p2 rollcheck'))
