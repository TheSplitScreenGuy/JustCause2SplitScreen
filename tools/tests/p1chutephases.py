# Reel -> parachute transition of Player 1, phase by phase: P1 is lifted, grapples ahead/down, opens
# the parachute during the reel, and the script prints P1's active controller, current context action
# and horizontal / vertical speed every 50 ms. Usage: python -I p1chutephases.py [fpscap]
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import json, sys, time, threading
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
n = json.load(open(os.path.join(_ROOT, r'tools\rtti_map.json')))
if len(sys.argv) > 1:
    print(jc2ctl.mod('fpscap ' + sys.argv[1]))
c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
pos = lambda: g.floats(c1 + 0x18C, 3)
name = lambda p: (n.get('%08X' % g.u32(p), '?')[:22] if p else '-')
jc2ctl.focus()
time.sleep(0.3)
jc2ctl.mouse(0, 90)
jc2ctl.mod('p1lift 40')
time.sleep(0.6)
threading.Thread(target=lambda: jc2ctl.tap('f', 200)).start()
t0 = time.time()
last, lt = pos(), t0
pressed = False
while time.time() - t0 < 3.0:
    t = time.time() - t0
    if not pressed and t > 1.1:
        pressed = True
        threading.Thread(target=lambda: jc2ctl.tap('space', 120)).start()
    time.sleep(0.05)
    p, now = pos(), time.time()
    dt = now - lt
    h = ((p[0] - last[0]) ** 2 + (p[2] - last[2]) ** 2) ** 0.5 / dt
    v = (p[1] - last[1]) / dt
    if t > 0.9:
        print('%.2f %-22s %-22s h %5.1f v %6.1f' % (now - t0, name(g.u32(c1 + 0x2F0)), name(g.u32(c1 + 0x324)), h, v))
    last, lt = p, now
jc2ctl.mouse(0, -90)
