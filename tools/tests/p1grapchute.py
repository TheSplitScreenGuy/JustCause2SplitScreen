# Player 1 grapples (F) ahead, presses Space during the reel (parachute), then waits: prints the
# camera control manager's setup over time and P1's height, and screenshots at the end.
# Usage: python -I p1grapchute.py join|solo [space press times in s...]   (join: P2 joins first)
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import struct, sys, time, threading
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
if len(sys.argv) > 1 and sys.argv[1] == 'join' and 'updates' not in jc2ctl.mod('p2'):
    jc2ctl.focus()
    print(jc2ctl.mod('p2 join'))
    time.sleep(3)
mgr = g.find_dwords([0x00EB8774], limit=4)[0x00EB8774][0]
c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
setup = lambda: struct.unpack('<i', g.read(mgr + 0x18, 4))[0]
cam = g.u32(0x011919F4) + 0x18


def cam_rel():
    m = g.floats(cam + 0x24, 16)
    p = g.floats(c1 + 0x18C, 3)
    off = [m[12 + i] - p[i] for i in range(3)]
    return 'camera offset from P1 %s (distance %.2f), back %s' % ([round(v, 2) for v in off], sum(v * v for v in off) ** 0.5,
                                                                   [round(v, 2) for v in m[8:11]])


jc2ctl.focus()
time.sleep(0.3)
print('before:', cam_rel())
jc2ctl.mouse(0, 120)  # look down: grapple the ground ahead
print(jc2ctl.mod('p1lift 40'))
time.sleep(0.6)
seen = []
t0 = time.time()
threading.Thread(target=lambda: jc2ctl.tap('f', 200)).start()
presses = [float(a) for a in sys.argv[2:]] or [1.0]  # parachute while reeling
while time.time() - t0 < 10:
    t = time.time() - t0
    if presses and t > presses[0]:
        presses.pop(0)
        threading.Thread(target=lambda: jc2ctl.tap('space', 120)).start()
    s = setup()
    p1 = g.u32(g.u32(0x0118FB24) + 0x3570)
    chooser = struct.unpack('<ii', g.read(p1 + 0x380, 8)) + struct.unpack('<i', g.read(p1 + 0x3B8, 4))
    dev = g.u32(p1 + 0x37C)
    devst = struct.unpack('<ii', g.read(dev + 8, 8)) if dev else ()
    key = (s, chooser, devst)
    if not seen or seen[-1][1] != key:
        seen.append((round(t, 2), key, round(g.floats(c1 + 0x190, 1)[0], 1)))
    for mark in (1.5, 2.5, 4.0, 9.0):
        if mark < t < mark + 0.05:
            jc2ctl.mod('screenshot ' + os.path.join(_ROOT, r'tools\shots\p1grapchute_%d.png') % int(mark * 10))
            time.sleep(0.06)
    time.sleep(0.02)
print('camera setups (time, setup, P1 height):', seen)
jc2ctl.mod('screenshot ' + os.path.join(_ROOT, r'tools\shots\p1grapchute.png'))
jc2ctl.mouse(0, -120)
time.sleep(1.5)
print('after: ', cam_rel())
jc2ctl.tap('w', 600)
time.sleep(1.0)
print('after walking:', cam_rel())
