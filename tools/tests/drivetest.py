# Spawn Player 2 (own Rico), place next to Rico 1, drive its CPlayer on the player thread, split, move.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import json, re, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

names = json.load(open(os.path.join(_ROOT, r'tools\rtti_map.json')))
g = Game()
cls = lambda p: names.get('%08X' % (g.u32(p) or 0), '?') if p else '-'
shots = os.path.join(_ROOT, r'tools\shots')
pos = lambda c: [round(v, 2) for v in g.floats(c + 0x18C, 3)]

jc2ctl.focus()
print(jc2ctl.mod('p2 spawn'))
time.sleep(2)
jc2ctl.focus()
print(jc2ctl.mod('p2 tp 2.5 0.3 -2'))
time.sleep(1)
w = jc2ctl.mod('watch x 400 00672CE0')
print(w)
tid = int(re.search(r'thread (\d+)', w).group(1))
jc2ctl.focus()
print(jc2ctl.mod('p2 drive %d' % tid))
time.sleep(1.5)
status = jc2ctl.mod('p2')
print(status)
body = int(status.split()[4], 16)
mgr = g.u32(0x0118FB24)
c1 = g.u32(g.u32(mgr + 0x3570) + 0x48)
print('rico1', pos(c1), 'rico2', pos(body), 'ctrl2', cls(g.u32(body + 0x2F0)))
jc2ctl.focus()
time.sleep(1)
print(jc2ctl.mod('screenshot ' + shots + r'\drive_full.png'))
print(jc2ctl.mod('split on'))
time.sleep(1.5)
print(jc2ctl.mod('screenshot ' + shots + r'\drive_split.png'))
print(jc2ctl.mod('p2 move 0 1'))
jc2ctl.focus()
for i in range(4):
    time.sleep(0.5)
    print('  moving: rico1', pos(c1), 'rico2', pos(body))
print(jc2ctl.mod('screenshot ' + shots + r'\drive_move.png'))
print(jc2ctl.mod('p2 move 0 0'))
