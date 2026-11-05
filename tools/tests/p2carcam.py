# P2's camera distance on foot and at the wheel of the hideout car (places.txt car_stunt).
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import json, math, re, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
names = json.load(open(os.path.join(_ROOT, r'tools\rtti_map.json')))
S = os.path.join(_ROOT, r'tools\shots\cc_%s.png')
c2 = int(re.search(r'character ([0-9A-F]+)', jc2ctl.mod('p2')).group(1), 16)
cam = g.u32(0x011919F4) + 0x18


def state(t):
    a = g.u32(c2 + 0x2F0)
    p = [float(x) for x in re.search(r'pos ([-0-9.]+) ([-0-9.]+) ([-0-9.]+)', jc2ctl.mod('p2')).groups()]
    best = 1e9
    for i in range(300):
        q = g.floats(cam + 0x264 + 0x30, 3)     # both views alternate here; his is the nearer one
        best = min(best, math.dist(p, q))
        time.sleep(0.001)
    v = g.u32(a)
    print('%-12s %s camera to body %.1f m' % (t, names.get('%08X' % v, 'private %08X' % v), best), flush=True)


print(jc2ctl.mod('p1 at 870 225 -2000'))
print(jc2ctl.mod('p2 at 896.27 223.30 -2027.90'))
print(jc2ctl.mod('p2 face -0.133 0.1 0.991'))
time.sleep(2)
state('on foot')
print(jc2ctl.mod('p2 padpress 2000 150'))     # B: onto the roof
time.sleep(3)
state('roof')
print(jc2ctl.mod('p2 padpress 8000 150'))     # Y: into the driver's seat
time.sleep(3.5)
state('driving')
print(jc2ctl.mod('screenshot ' + S % 'drive'))
print(jc2ctl.mod('p2 padpress 8000 150'))     # Y: out
time.sleep(3.5)
state('out')
print(jc2ctl.mod('screenshot ' + S % 'out'))
