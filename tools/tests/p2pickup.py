# Player 2 collects a pickup (the weapon part crate by the "vehicle_part" spot in places.txt):
# P1 waits 25 m away, P2 walks into the crate from 3 m. Crates are single use: run it right after
# a load (hudboot.ps1 or "jc2ctl.py loadsave").   python -I p2pickup.py
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import math, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
import jc2ctl
from procmem import Game

CRATE = (1233.8, 205.4, -1092.7)
g = Game()


def crate():
    for o in g.find_dwords([0x00EBD42C], limit=20000)[0x00EBD42C]:
        if math.dist(g.floats(o + 0x7C, 3), CRATE) < 1: return o
    return 0


jc2ctl.focus()
print(jc2ctl.mod('p1 at 1234.7 206.4 -1067.7'))
time.sleep(5)
print(jc2ctl.mod('p2 at 1233.8 206.4 -1096.2'))
time.sleep(5)
print(jc2ctl.mod('p2 face 0 0.2 -1'))
time.sleep(25)  # the background scan (every 20 s) finds the crate's trigger
print(jc2ctl.mod('p2hud pickups on'))
print('crate item before: %08X' % crate())
print(jc2ctl.mod('p2 move 0 1'))
time.sleep(1.6)
print(jc2ctl.mod('p2 move 0 0'))
time.sleep(1)
jc2ctl.mod('screenshot ' + os.path.join(_ROOT, r'tools\shots\p2pickup.png'))
time.sleep(2)
print('crate item after: %08X' % crate())
print(jc2ctl.mod('p2hud pickups on'))
