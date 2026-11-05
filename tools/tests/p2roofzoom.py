# P2 stunt jumps onto the hideout car (places.txt car_stunt), zooms on the roof, jumps off.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import json, re, struct, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
names = json.load(open(os.path.join(_ROOT, r'tools\rtti_map.json')))
S = os.path.join(_ROOT, r'tools\shots\rz_%s.png')
c2 = int(re.search(r'character ([0-9A-F]+)', jc2ctl.mod('p2')).group(1), 16)
cam = g.u32(0x011919F4) + 0x18


def state(t):
    a = g.u32(c2 + 0x2F0)
    seen = set()
    for i in range(200):
        seen.add(round(struct.unpack('<f', g.read(cam + 0x2B8, 4))[0], 1))
        time.sleep(0.001)
    print('%-12s controller %s, pos %s, scales %s' % (t, names.get('%08X' % g.u32(a), '?'), re.search(r'pos ([-0-9. ]+?) \|', jc2ctl.mod('p2')).group(1), sorted(seen)), flush=True)


print(jc2ctl.mod('p2 at 896.27 223.30 -2027.90'))
print(jc2ctl.mod('p2 face -0.133 0.1 0.991'))
time.sleep(2)
state('start')
print(jc2ctl.mod('p2 padpress 2000 150'))     # B: stunt jump
time.sleep(3)
state('after B')
print(jc2ctl.mod('screenshot ' + S % 'roof'))
print(jc2ctl.mod('p2 padpress 80 120'))       # right stick click
time.sleep(0.8)
state('zoom on')
print(jc2ctl.mod('screenshot ' + S % 'zoom'))
print(jc2ctl.mod('p2 padpress 80 120'))
time.sleep(0.8)
state('zoom off')
print(jc2ctl.mod('p2 padpress 80 120'))
time.sleep(0.8)
state('zoom on')
print(jc2ctl.mod('p2 padpress 1000 150'))     # A: off the roof, zoomed
time.sleep(2.5)
state('jumped off')
print(jc2ctl.mod('screenshot ' + S % 'off'))
print(jc2ctl.mod('p2 padpress 80 120'))
time.sleep(0.8)
state('click')
print(jc2ctl.mod('p2 padpress 80 120'))
time.sleep(0.8)
state('click')
