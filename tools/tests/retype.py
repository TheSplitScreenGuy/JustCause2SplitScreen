# Sets the possessed NPC's character type to "player" and gives it Player 2's on-foot controller again,
# then tries Player 2 movement input.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import json, struct, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

names = json.load(open(os.path.join(_ROOT, r'tools\rtti_map.json')))
g = Game(write=True)
cls = lambda p: names.get('%08X' % (g.u32(p) or 0), '?') if p else '-'
npc = int(sys.argv[1], 16)
p2char_orig = int(sys.argv[2], 16)
print('type before', g.u32(npc + 0x584), 'p2 own char type', g.u32(p2char_orig + 0x584))
g.write_u32(npc + 0x584, 4)
ctrl, cb = g.u32(p2char_orig + 0x2F0), g.u32(p2char_orig + 0x2F4)
g.write_u32(cb + 4, g.u32(cb + 4) + 1)
g.write_u32(npc + 0x2F4, cb)
g.write_u32(npc + 0x2F0, ctrl)
jc2ctl.focus()
time.sleep(1)
print('ctrl now', cls(g.u32(npc + 0x2F0)))
print(jc2ctl.mod('p2 move 0 1'))
jc2ctl.focus()
for i in range(6):
    time.sleep(0.5)
    print('pos', [round(v, 2) for v in g.floats(npc + 0x18C, 3)], 'ctrl', cls(g.u32(npc + 0x2F0)))
print(jc2ctl.mod('screenshot ' + os.path.join(_ROOT, r'tools\shots\retype.png')))
print(jc2ctl.mod('p2 move 0 0'))
