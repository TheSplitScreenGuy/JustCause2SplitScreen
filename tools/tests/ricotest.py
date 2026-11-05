# Spawn Player 2 with a factory-made Rico body, split on, move it.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import json, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

names = json.load(open(os.path.join(_ROOT, r'tools\rtti_map.json')))
g = Game()
cls = lambda p: names.get('%08X' % (g.u32(p) or 0), '?') if p else '-'
shots = os.path.join(_ROOT, r'tools\shots')
pos = lambda c: [round(v, 2) for v in g.floats(c + 0x18C, 3)]

print(jc2ctl.mod('p2 spawn'))
time.sleep(2)
jc2ctl.focus()
print(jc2ctl.mod('p2 rico'))
time.sleep(2)
jc2ctl.focus()
time.sleep(1)
status = jc2ctl.mod('p2')
print(status)
body = int(status.split()[4], 16) if 'character' in status else 0
mgr = g.u32(0x0118FB24)
c1 = g.u32(g.u32(mgr + 0x3570) + 0x48)
print('rico1', pos(c1), 'body', body and pos(body), 'ctrl', body and cls(g.u32(body + 0x2F0)))
print(jc2ctl.mod('screenshot ' + shots + r'\rico2_full.png'))
print(jc2ctl.mod('split on'))
time.sleep(1.5)
print(jc2ctl.mod('screenshot ' + shots + r'\rico2_split.png'))
print(jc2ctl.mod('p2 move 0 1'))
jc2ctl.focus()
for i in range(4):
    time.sleep(0.5)
    print('  moving: rico1', pos(c1), 'p2', body and pos(body))
print(jc2ctl.mod('screenshot ' + shots + r'\rico2_move.png'))
print(jc2ctl.mod('p2 move 0 0'))
