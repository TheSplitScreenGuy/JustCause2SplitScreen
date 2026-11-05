# Full Player 2 test: spawn, possess nearest pedestrian, teleport next to Rico, split on, move.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import json, math, sys, time
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
time.sleep(1)
mgr = g.u32(0x0118FB24)
c1 = g.u32(g.u32(mgr + 0x3570) + 0x48)
best = None
for c in g.find_dwords([0xEB881C], limit=500)[0xEB881C]:
    ctrl = cls(g.u32(c + 0x2F0))
    if ctrl in ('CPedestrianController', 'CAgentOnFootController', 'CCivilianSOController') and cls(g.u32(c + 0x9C8)).startswith('CAgentAvatar'):
        d = math.dist(g.floats(c + 0x18C, 3), g.floats(c1 + 0x18C, 3))
        if not best or d < best[0]:
            best = (d, c)
if not best:
    raise SystemExit('no pedestrian found')
npc = best[1]
print('possessing %08X (%.0f m away)' % (npc, best[0]), jc2ctl.mod('p2 possess %X' % npc))
time.sleep(1)
jc2ctl.focus()
print(jc2ctl.mod('p2 tp 2.5 0.3 -2'))
time.sleep(1.5)
jc2ctl.focus()
print(jc2ctl.mod('split on'))
time.sleep(1.5)
print('rico', pos(c1), 'p2', pos(npc), 'ctrl', cls(g.u32(npc + 0x2F0)))
print(jc2ctl.mod('screenshot ' + shots + r'\p2_split_idle.png'))
print(jc2ctl.mod('p2 move 0 1'))
jc2ctl.focus()
for i in range(5):
    time.sleep(0.5)
    print('  moving: rico', pos(c1), 'p2', pos(npc))
print(jc2ctl.mod('screenshot ' + shots + r'\p2_split_move.png'))
print(jc2ctl.mod('p2 move 0 0'))
time.sleep(0.5)
print('stopped: p2', pos(npc))
if len(sys.argv) > 1 and sys.argv[1] == 'look':
    jc2ctl.focus()
    print(jc2ctl.mod('p2 look'))
    time.sleep(2)
    jc2ctl.focus()
    time.sleep(1)
    print(jc2ctl.mod('screenshot ' + shots + r'\p2_look.png'))
