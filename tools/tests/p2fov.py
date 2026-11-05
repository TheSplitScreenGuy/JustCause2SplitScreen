# Zoom independence: with a rifle out P1 toggles the aim zoom (C); the derived projection is sampled
# over frames (both players' values should alternate while zoomed), with screenshots.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import struct, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
cam = g.u32(0x011919F4) + 0x18
S = os.path.join(_ROOT, r'tools\shots\fov_%s.png')
jc2ctl.SCAN['3'] = (0x04, 0)


def samples():
    seen = {}
    for i in range(200):
        v = round(struct.unpack('<f', g.read(cam + 0x2B8, 4))[0], 1)
        seen[v] = seen.get(v, 0) + 1
        time.sleep(0.002)
    return seen


jc2ctl.focus(); jc2ctl.tap('3', 150); time.sleep(2)
print('start', samples())
print(jc2ctl.mod('screenshot ' + S % 'a'))
for name in 'bcde':
    jc2ctl.focus(); jc2ctl.tap('c', 120); time.sleep(1.2)
    print('after C', samples())
    print(jc2ctl.mod('screenshot ' + S % name))
