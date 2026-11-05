# From the title screen: start a trace on an address, then load the save.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

addr = sys.argv[1]
ms = sys.argv[2] if len(sys.argv) > 2 else '60000'
end = time.time() + 120
while True:
    try:
        jc2ctl.mod('ping', timeout=5)
        break
    except SystemExit:
        if time.time() > end:
            raise
time.sleep(10)
g = Game()
mgr = g.u32(0x0118FB24)
c1 = g.u32(g.u32(mgr + 0x3570) + 0x48)
print('player 1 character %08X (+F0 = %08X)' % (c1, c1 + 0xF0))
print(jc2ctl.mod('trace %s %s' % (addr, ms)))
for attempt in range(8):
    jc2ctl.focus()
    jc2ctl.tap('enter')
    time.sleep(4)
    try:
        if not jc2ctl.mod('cam', timeout=10).startswith('pos 0.000 0.000 0.000'):
            break
    except SystemExit:
        break
time.sleep(40)
print('done')
