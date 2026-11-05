# Each half's HUD shows its own player: lowers P2's resistance meter (CPlayer +0x1B0, the HUD ring),
# prints both players' values, P1 fires so the weapon panels show, then a screenshot.
#   p2hudcheck.py <shot.png> [ring] [nofire] [refresh <weapon mask> <ammo mask>]
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import re, struct, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game(write=True)
line = jc2ctl.mod('p2')
p2 = int(re.search(r'player 2 ([0-9A-F]+)', line).group(1), 16)
p1 = g.u32(g.u32(0x0118FB24) + 0x3570)
f = lambda a: struct.unpack('<f', g.read(a, 4))[0]
for name, p in (('P1', p1), ('P2', p2)):
    print(name, 'meter', f(p + 0x1B0), 'max index', g.u32(p + 0x23C), 'max', f(p + 0x1B4 + 4 * g.u32(p + 0x23C)))
if 'ring' in sys.argv:
    g.write(p2 + 0x1B0, struct.pack('<f', 0.3 * f(p2 + 0x1B4 + 4 * g.u32(p2 + 0x23C))))
if 'refresh' in sys.argv:
    i = sys.argv.index('refresh')
    print(jc2ctl.mod('p2hud refresh %s %s' % (sys.argv[i + 1], sys.argv[i + 2])))
if 'nofire' not in sys.argv:
    jc2ctl.focus()
    jc2ctl.click('left', 300)
time.sleep(0.15)
print(jc2ctl.mod('screenshot ' + sys.argv[1]))
print(jc2ctl.mod('p2hud on'))
