# Records both players' health (character +0xE8) whenever either changes, while the user plays.
#   python -I healthrec.py [seconds]
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import re, struct, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 120
g = Game()
c2 = int(re.search(r'character ([0-9A-F]+)', jc2ctl.mod('p2')).group(1), 16)
t0 = time.time()
last = None
while time.time() - t0 < seconds:
    try:
        c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
        h = (struct.unpack('<f', g.read(c1 + 0xE8, 4))[0], struct.unpack('<f', g.read(c2 + 0xE8, 4))[0])
    except Exception:
        time.sleep(0.05)
        continue
    if h != last:
        print('%6.2f s  P1 %6.1f  P2 %6.1f' % (time.time() - t0, h[0], h[1]), flush=True)
        last = h
    time.sleep(0.01)
