# Records every reference to Player 1's character (and its main subobjects), spawns Player 2,
# then reports references that now point at Player 2's character instead.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import sys, time, struct
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
mgr = g.u32(0x0118FB24)
p1 = g.u32(mgr + 0x3570)
c1 = g.u32(p1 + 0x48)
subs = {'char': 0, 'char+4': 4, 'char+38': 0x38, 'char+F0': 0xF0, 'pfx': None}
targets = {name: c1 + off for name, off in subs.items() if off is not None}
targets['pfx'] = g.u32(c1 + 0x230)
targets['player'] = p1
targets['player+8'] = p1 + 8
before = g.find_dwords(list(targets.values()), limit=20000)
total = sum(len(v) for v in before.values())
print('player1 %08X char %08X, %d references recorded' % (p1, c1, total))

print(jc2ctl.mod('p2 spawn'))
time.sleep(3)
jc2ctl.focus()
time.sleep(2)
status = jc2ctl.mod('p2')
print(status)
p2 = int(status.split()[2], 16)
c2 = g.u32(p2 + 0x48)
map2 = {'char': c2, 'char+4': c2 + 4, 'char+38': c2 + 0x38, 'char+F0': c2 + 0xF0, 'pfx': g.u32(c2 + 0x230),
        'player': p2, 'player+8': p2 + 8}
for name, t in targets.items():
    for a in before[t]:
        now = g.u32(a)
        if now != t:
            hit = [n for n, v in map2.items() if v == now]
            print('%-9s ref at %08X: %08X -> %08X %s' % (name, a, t, now or 0, ('NOW PLAYER 2 ' + hit[0]) if hit else ''))
