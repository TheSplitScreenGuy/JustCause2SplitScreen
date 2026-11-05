# Reference for p2reelchute.py: P1 on the ground fires the grapple (F) where his camera looks and
# presses Space during the reel; logs P1's current action (char +0x324).
#   python -I tools\tests\p1reelchute.py [delay_s] [key]
import json, os, sys, threading, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import jc2ctl
from procmem import Game

delay = float(sys.argv[1]) if len(sys.argv) > 1 else 0.5
key = sys.argv[2] if len(sys.argv) > 2 else 'space'
g = Game()
names = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'rtti_map.json')))
c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
jc2ctl.focus()
time.sleep(0.3)
threading.Thread(target=lambda: jc2ctl.tap('f', 300)).start()
t0 = time.time()
pressed = False
seen = []
while time.time() - t0 < 6:
    t = time.time() - t0
    if not pressed and t > delay:
        threading.Thread(target=lambda: jc2ctl.tap(key, 150)).start()
        pressed = True
    a = g.u32(c1 + 0x324)
    action = names.get('%08X' % g.u32(a), '%08X' % g.u32(a)) if a else '-'
    if not seen or seen[-1][1] != action:
        seen.append((round(t, 2), action, round(g.floats(c1 + 0x190, 1)[0], 1)))
    time.sleep(0.02)
for t, action, y in seen:
    print('%5.2f s  action %-45s y %.1f' % (t, action, y))
