# Spawns Player 2, possesses the nearest pedestrian, teleports it in front of Rico, tries moving it.
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

print(jc2ctl.mod('p2 spawn'))
time.sleep(2)
jc2ctl.focus()
time.sleep(1)
mgr = g.u32(0x0118FB24)
c1 = g.u32(g.u32(mgr + 0x3570) + 0x48)
p1 = g.floats(c1 + 0x18C, 3)
best = None
for c in g.find_dwords([0xEB881C], limit=500)[0xEB881C]:
    ctrl = cls(g.u32(c + 0x2F0))
    if ctrl in ('CPedestrianController', 'CAgentOnFootController', 'CCivilianSOController') and cls(g.u32(c + 0x9C8)).startswith('CAgentAvatar'):
        d = math.dist(g.floats(c + 0x18C, 3), p1)
        if not best or d < best[0]:
            best = (d, c, ctrl)
print('target', best and ('%08X %s %.0fm' % (best[1], best[2], best[0])))
if not best:
    raise SystemExit('no pedestrian found')
npc = best[1]
print(jc2ctl.mod('p2 possess %X' % npc))
time.sleep(2)
print('ctrl', cls(g.u32(npc + 0x2F0)), 'brain', cls(g.u32(npc + 0x9C8)))
jc2ctl.focus()
print(jc2ctl.mod('p2 tp 0 0.3 -4'))
time.sleep(2)
jc2ctl.focus()
time.sleep(1)
print('npc pos', [round(v, 2) for v in g.floats(npc + 0x18C, 3)])
print(jc2ctl.mod('screenshot ' + shots + r'\possess1.png'))
print(jc2ctl.mod('p2 move 0 1'))
jc2ctl.focus()
for i in range(4):
    time.sleep(0.5)
    print('moving: npc pos', [round(v, 2) for v in g.floats(npc + 0x18C, 3)], 'ctrl', cls(g.u32(npc + 0x2F0)))
print(jc2ctl.mod('screenshot ' + shots + r'\possess2.png'))
print(jc2ctl.mod('p2 move 0 0'))
