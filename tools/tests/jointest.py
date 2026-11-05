# Simulates Start on pad 2 and checks that Player 2's Rico gets spawned, driven and split on by itself.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import re, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
shots = os.path.join(_ROOT, r'tools\shots')
pos = lambda c: [round(v, 2) for v in g.floats(c + 0x18C, 3)]

jc2ctl.focus()
print(jc2ctl.mod('p2 join'))
for _ in range(10):
    time.sleep(0.5)
    jc2ctl.focus()
    st = jc2ctl.mod('p2')
    if 'updates' in st and not re.search(r'updates 0 ', st):
        break
print(st)
body = int(re.search(r'character ([0-9A-F]+)', st).group(1), 16)
c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
print('rico1', pos(c1), 'rico2', pos(body))
print(jc2ctl.mod('screenshot ' + shots + r'\join_idle.png'))
print(jc2ctl.mod('p2 move 0 1'))
jc2ctl.focus()
for _ in range(3):
    time.sleep(0.5)
    print('  moving: rico1', pos(c1), 'rico2', pos(body))
print(jc2ctl.mod('screenshot ' + shots + r'\join_move.png'))
print(jc2ctl.mod('p2 move 0 0'))
