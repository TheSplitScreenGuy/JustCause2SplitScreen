# Where the zoom shows, with Player 2's override off and on (P1 aiming assumed toggled by C).
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import struct, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
cam = g.u32(0x011919F4) + 0x18
OFFS = (0x78, 0xF8, 0x138, 0x1B8, 0x238, 0x2B8, 0x338, 0x3B8, 0x438)


def line():
    seen = {o: set() for o in OFFS}
    for i in range(80):
        for o in OFFS:
            seen[o].add(round(struct.unpack('<f', g.read(cam + o, 4))[0], 2))
        time.sleep(0.003)
    return ' '.join('%X:%s' % (o, sorted(seen[o])) for o in OFFS)


print('override on, aiming?  ', line())
print(jc2ctl.mod('split p2fov 0'))
time.sleep(1.0)
print('override off          ', line())
jc2ctl.focus(); jc2ctl.tap('c', 120); time.sleep(1.2)
print('off, C toggled        ', line())
print(jc2ctl.mod('split p2fov 2.75442'))
time.sleep(1.0)
print('on again              ', line())
jc2ctl.focus(); jc2ctl.tap('c', 120); time.sleep(1.2)
print('on, C toggled         ', line())
