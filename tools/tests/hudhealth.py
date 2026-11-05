# Ring per player: sets each player's health in turn and takes a screenshot, restoring it after.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import re, struct, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game(write=True)
line = jc2ctl.mod('p2')
c2 = int(re.search(r'character ([0-9A-F]+)', line).group(1), 16)
p1 = g.u32(g.u32(0x0118FB24) + 0x3570)
print(jc2ctl.mod('p2hud'))
S = os.path.join(_ROOT, r'tools\shots\ring_%s.png')
start = struct.unpack('<f', g.read(c2 + 0xE8, 4))[0]
print('P2 health', start)
g.write(c2 + 0xE8, struct.pack('<f', 120.0))
time.sleep(1.0)
print(jc2ctl.mod('screenshot ' + S % 'p2low'))
g.write(c2 + 0xE8, struct.pack('<f', start))
time.sleep(1.0)
print(jc2ctl.mod('screenshot ' + S % 'even'))
