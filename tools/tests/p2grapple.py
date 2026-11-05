# Player 2 fires the grapple (FIRE_GRAPPLE, 0x51) and the test watches whether anything happens.
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
c = int(re.search(r'character ([0-9A-F]+)', st).group(1), 16)
inv = g.u32(c + 0x484)
grap = g.u32(inv + 0xD0)
pos = lambda: [round(v, 1) for v in g.floats(c + 0x18C, 3)]
snap = lambda: g.read(grap, 0x320)
before = snap()
print('P2 at', pos())
jc2ctl.focus()
print(jc2ctl.mod('p2 press 51 300'))
for i in range(6):
    time.sleep(0.15)
    if i == 1:
        print(jc2ctl.mod('screenshot %s\\p2_grapple.png' % shots))
    after = snap()
    changed = [o for o in range(0, 0x320, 4) if before[o:o + 4] != after[o:o + 4]]
    print('  %.2fs: P2 at %s, grapple device dwords changed: %d %s' % ((i + 1) * 0.15, pos(), len(changed),
                                                                       ' '.join('+%X' % o for o in changed[:12])))
print(jc2ctl.mod('p2'))
