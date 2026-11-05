# Jump arcs of Player 1 (Space) and Player 2 (JUMP action), standing still: height over time.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import re, sys, time, threading
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
if 'updates' not in jc2ctl.mod('p2'):
    jc2ctl.focus()
    print(jc2ctl.mod('p2 join'))
    time.sleep(2)
c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
c2 = g.u32(int(re.search(r'player 2 ([0-9A-F]+)', jc2ctl.mod('p2')).group(1), 16) + 0x48)
jc2ctl.mod('p2 tp 2.5 0.3 -2')
time.sleep(1.5)


def arc(who, c, start):
    jc2ctl.focus()
    time.sleep(0.4)
    y0 = g.floats(c + 0x190, 1)[0]
    t0 = time.time()
    start()
    pts = []
    while time.time() - t0 < 1.6:
        pts.append((time.time() - t0, g.floats(c + 0x190, 1)[0] - y0))
        time.sleep(0.02)
    peak = max(pts, key=lambda p: p[1])
    land = next((p[0] for p in pts if p[0] > peak[0] and p[1] <= 0.02), None)
    print('%s: peak %.2f m at %.2f s, back down at %s s | %s' % (who, peak[1], peak[0], '%.2f' % land if land else '?',
          ' '.join('%.2f' % p[1] for p in pts[::4])))


arc('P1', c1, lambda: threading.Thread(target=lambda: jc2ctl.tap('space', 120)).start())
time.sleep(1)
arc('P2', c2, lambda: jc2ctl.mod('p2 press 23 120'))
