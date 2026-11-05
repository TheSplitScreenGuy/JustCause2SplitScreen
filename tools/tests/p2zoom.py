# FOV per view, sampled fast during the changes: P1's zoom must never show on P2's frames.
# Prints the set of projection scales seen right after each toggle, and screenshots.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import struct, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
cam = g.u32(0x011919F4) + 0x18
S = os.path.join(_ROOT, r'tools\shots\zoom_%s.png')
jc2ctl.SCAN['3'] = (0x04, 0)


def samples(seconds=0.5):
    seen = {}
    t0 = time.time()
    while time.time() - t0 < seconds:
        v = round(struct.unpack('<f', g.read(cam + 0x2B8, 4))[0], 1)
        seen[v] = seen.get(v, 0) + 1
    return dict(sorted(seen.items()))


jc2ctl.focus(); jc2ctl.tap('3', 150); time.sleep(2)
print('start', samples())
jc2ctl.focus(); jc2ctl.tap('c', 120)
print('P1 zoom in ', samples()); time.sleep(0.7)
print('P1 zoomed  ', samples())
print(jc2ctl.mod('screenshot ' + S % 'p1'))
jc2ctl.focus(); jc2ctl.tap('c', 120)
print('P1 zoom out', samples()); time.sleep(0.7)
print(jc2ctl.mod('p2 padpress 80 120'))
print('P2 zoom in ', samples()); time.sleep(0.7)
print('P2 zoomed  ', samples())
print(jc2ctl.mod('screenshot ' + S % 'p2'))
print(jc2ctl.mod('p2 padpress 80 120'))
print('P2 zoom out', samples()); time.sleep(0.7)
print(jc2ctl.mod('screenshot ' + S % 'none'))
print('end', samples())
print(jc2ctl.mod('p2 padpress 80 120'))
time.sleep(0.8)
jc2ctl.focus(); jc2ctl.tap('c', 120)
time.sleep(0.8)
print('both', samples())
print(jc2ctl.mod('screenshot ' + S % 'both'))
