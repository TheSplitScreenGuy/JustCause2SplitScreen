# Kills Player 2 (health 0) and checks they come back next to Player 1 with full health and can move.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import re, struct, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game(write=True)
shots = os.path.join(_ROOT, r'tools\shots')
st = jc2ctl.mod('p2')
if 'updates' not in st:
    jc2ctl.focus()
    print(jc2ctl.mod('p2 join'))
    time.sleep(2)
    st = jc2ctl.mod('p2')
c = int(re.search(r'character ([0-9A-F]+)', st).group(1), 16)
c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
pos = lambda x: [round(v, 1) for v in g.floats(x + 0x18C, 3)]
jc2ctl.focus()
jc2ctl.mod('p2 move 0 1')
time.sleep(1.5)
jc2ctl.mod('p2 move 0 0')
print('alive at', pos(c), 'health %.0f/%.0f' % tuple(g.floats(c + 0xE8, 2)))
g.write(c + 0xE8, struct.pack('<f', 0.0))
for i in range(8):
    jc2ctl.focus()
    time.sleep(1)
    print('  %ds: health %.0f pos %s' % (i + 1, g.floats(c + 0xE8, 1)[0], pos(c)))
print('rico1 at', pos(c1))
print(jc2ctl.mod('screenshot ' + shots + r'\p2_respawned.png'))
a = pos(c)
jc2ctl.mod('p2 move 0 1')
jc2ctl.focus()
time.sleep(1.5)
jc2ctl.mod('p2 move 0 0')
b = pos(c)
print('moves after respawn: %.1f m' % ((a[0] - b[0]) ** 2 + (a[2] - b[2]) ** 2) ** 0.5)
