# Records Player 2's hand mode (inventory +0x64: 1/2 one weapon, 4 dual, 0 switching) and his pad
# buttons while the user plays.   python -I handrec.py [seconds]
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import re, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 60
g = Game()
c2 = int(re.search(r'character ([0-9A-F]+)', jc2ctl.mod('p2')).group(1), 16)
inv = g.u32(c2 + 0x484)
print(jc2ctl.mod('p2 padlog on'), flush=True)
t0 = time.time()
last = None
while time.time() - t0 < seconds:
    m = g.u32(inv + 0x64)
    if m != last:
        print('%6.2f s  hand mode %d' % (time.time() - t0, m), flush=True)
        last = m
    time.sleep(0.01)
