# Player 2 fire: refill ammo, equip, hold fire, check P2's clips go down. Then fire the grapple.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import re, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
shots = os.path.join(_ROOT, r'tools\shots')
st = jc2ctl.mod('p2')
if 'updates' not in st:
    jc2ctl.focus()
    print(jc2ctl.mod('p2 join'))
    time.sleep(2)
    st = jc2ctl.mod('p2')
c2 = int(re.search(r'character ([0-9A-F]+)', st).group(1), 16)
inv = g.u32(c2 + 0x484)
def clips():
    return [g.u32(g.u32(inv + 0x3C + s * 8) + 0x1E8) if g.u32(inv + 0x3C + s * 8) else None for s in range(4)]
def equipped():
    return [g.u32(inv + o) for o in (0xD4, 0xD8, 0xDC, 0xE0)]
jc2ctl.focus()
print(jc2ctl.mod('ammo'))
time.sleep(1)
print('clips after refill', clips(), 'equipped', equipped())
for action, name in (('72', 'equip right'), ('75', 'equip two-handed')):
    jc2ctl.focus()
    jc2ctl.mod('p2 press %s 200' % action)
    time.sleep(1.5)
    print(name, '-> equipped', equipped())
    before = clips()
    jc2ctl.focus()
    jc2ctl.mod('p2 press D 1500')  # FIRE_RIGHT, what the on-foot fire handler reads (RT sends it)
    time.sleep(0.8)
    print(jc2ctl.mod('screenshot %s\\p2_fire_%s.png' % (shots, action)))
    time.sleep(1)
    print('  fire: clips', before, '->', clips())
jc2ctl.focus()
jc2ctl.mod('p2 press 51 300')
time.sleep(0.4)
print(jc2ctl.mod('screenshot %s\\p2_grapple.png' % shots))
