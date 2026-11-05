# Player 1 grapples (F) several times with Player 2 far away, while the render camera is sampled:
# every rendered camera should be within ~15 m of one of the players' heads; samples far from
# both are Player 1's camera flung away.   python -I p1grapcamfling.py [count]
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import math, re, sys, threading, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
count = int(sys.argv[1]) if len(sys.argv) > 1 else 6
jc2ctl.focus()
c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
c2 = g.u32(int(re.search(r'player 2 ([0-9A-F]+)', jc2ctl.mod('p2')).group(1), 16) + 0x48)
mgr = g.u32(0x011919F4)
worst = [0.0]
flung = [0]
samples = [0]
stop = [False]


def sample():
    while not stop[0]:
        try:
            cam = g.floats(g.u32(mgr + 0x14) + 0x54, 3)
            p1, p2 = g.floats(c1 + 0x18C, 3), g.floats(c2 + 0x18C, 3)
            d = min(math.dist(cam, p1), math.dist(cam, p2))
            samples[0] += 1
            worst[0] = max(worst[0], d)
            if d > 20: flung[0] += 1
        except Exception:
            pass
        time.sleep(0.003)


t = threading.Thread(target=sample)
t.start()
for r in range(count):
    jc2ctl.mouse(60, 0, 5)
    time.sleep(0.3)
    jc2ctl.tap('f', 200)
    time.sleep(1.8)
stop[0] = True
t.join()
print('%d camera samples, %d farther than 20 m from both players, farthest %.1f m' % (samples[0], flung[0], worst[0]))
