# Player 1's momentum when opening the parachute out of a grapple: P1 is lifted, grapples the
# ground ahead (F), opens the parachute during the reel (Space at 1.1 s), and the script prints
# P1's horizontal speed over time. Usage: python -I p1chutespeed.py join|solo [mouse-down] [lift m]
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import sys, time, threading
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
if len(sys.argv) > 1 and sys.argv[1] == 'join' and 'updates' not in jc2ctl.mod('p2'):  # 'vanilla': no mod commands
    jc2ctl.focus()
    print(jc2ctl.mod('p2 join'))
    time.sleep(3)
c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
pos = lambda: g.floats(c1 + 0x18C, 3)
jc2ctl.focus()
time.sleep(0.3)
look = int(sys.argv[2]) if len(sys.argv) > 2 else 120
lift = sys.argv[3] if len(sys.argv) > 3 else '40'
jc2ctl.mouse(0, look)
if lift != '0':  # 0: stay on the ground (works without the mod)
    jc2ctl.mod('p1lift ' + lift)
time.sleep(0.6)
threading.Thread(target=lambda: jc2ctl.tap('f', 200)).start()
t0 = time.time()
last, lt = pos(), t0
pressed = False
out = []
while time.time() - t0 < 5:
    t = time.time() - t0
    if not pressed and t > 1.1:
        pressed = True
        threading.Thread(target=lambda: jc2ctl.tap('space', 120)).start()
    time.sleep(0.25)
    p, now = pos(), time.time()
    v = ((p[0] - last[0]) ** 2 + (p[2] - last[2]) ** 2) ** 0.5 / (now - lt)
    out.append('%.2f:%.1f@%.0f' % (now - t0, v, p[1]))
    last, lt = p, now
print('P1 horizontal speed (time:m/s@height):', ' '.join(out))
jc2ctl.mouse(0, -look)
