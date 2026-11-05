# Kills Player 2 for death tests: places him in Player 1's crosshair and Player 1 shoots him (left mouse),
# printing P2's health, dead flag and the UI mode while it happens and for a while after.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import re, struct, sys, time, threading
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
if 'updates' not in jc2ctl.mod('p2'):
    jc2ctl.focus()
    print(jc2ctl.mod('p2 join'))
    time.sleep(2)
p1 = g.u32(g.u32(0x0118FB24) + 0x3570)
c1 = g.u32(p1 + 0x48)
c2 = g.u32(int(re.search(r'player 2 ([0-9A-F]+)', jc2ctl.mod('p2')).group(1), 16) + 0x48)
cam = g.u32(0x011919F4) + 0x18
watch = float(sys.argv[1]) if len(sys.argv) > 1 else 12

m = g.floats(cam + 0x24, 16)
back, pos = m[8:11], m[12:15]
target = [pos[i] - back[i] * 7 for i in range(3)]
w = g.floats(c1 + 0x15C, 16)
d = [target[i] - w[12 + i] for i in range(3)]
local = [sum(d[i] * w[a * 4 + i] for i in range(3)) for a in range(3)]
local[1] = max(local[1] - 1.0, 0.3)
jc2ctl.focus()
print(jc2ctl.mod('p2 tp %.2f %.2f %.2f' % tuple(local)))
time.sleep(1.5)
jc2ctl.mod('ammo')


def state():
    return 'mode %d pending %d | P2 health %.0f flags %08X' % (
        g.u32(0x011958C4), g.u32(0x0105F754), g.floats(c2 + 0xE8, 1)[0], g.u32(c2 + 0x590))


t0 = time.time()
shooting = True


def shoot():
    while shooting and g.floats(c2 + 0xE8, 1)[0] > 0 and time.time() - t0 < 6:
        jc2ctl.click('left', 400)


th = threading.Thread(target=shoot)
th.start()
while time.time() - t0 < watch:
    print('%5.1f %s' % (time.time() - t0, state()))
    time.sleep(0.5)
shooting = False
th.join()
