# Player 2's plane controls: holds his sticks / triggers with "p2 sticks" and prints the plane's control
# fields (CAirVehicle +0x200 thrust, +0x208 roll, +0x210 turn, +0x214 pitch; FUN_00797130 writes
# them) and its position / forward axis.   python -I p2plane.py
# The plane: P2's character +0x2E0 (smart object he's in) -> +0x18, minus 4.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import re, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl
g = Game()
c2 = int(re.search(r'character ([0-9A-F]+)', jc2ctl.mod('p2')).group(1), 16)
so = g.u32(c2 + 0x2E0)
plane = (g.u32(so + 0x18) or 4) - 4 if so else 0
print('character %08X smart object %08X plane %08X' % (c2, so, plane))
if not plane: sys.exit()
def show(label):
    th, _, roll, _, turn, pitch = g.floats(plane + 0x200, 6)
    m = g.floats(plane + 0x15C, 16) if False else None
    print('%-26s thrust %6.2f roll %6.2f turn %6.2f pitch %6.2f' % (label, th, roll, turn, pitch), flush=True)
for label, sticks in [('idle', '0 0 0 0 0 0'), ('thrust (RT)', '0 0 0 0 0 1'), ('pitch up (stick back)', '0 -1 0 0 0 1'),
                      ('roll right (stick right)', '1 0 0 0 0 1'), ('turn right (right stick)', '0 0 1 0 0 1')]:
    jc2ctl.mod('p2 sticks %s 1500' % sticks)
    time.sleep(0.4)
    for i in range(3):
        show(label); time.sleep(0.3)
