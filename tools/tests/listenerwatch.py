# Which code reads the game's sound listener position (CListener +0x1C): a read watch while P2 acts.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
hits = [a for a in g.find_dwords([0x00F0971C], limit=10)[0x00F0971C] if a > 0x02000000]
print('listeners', ['%08X' % a for a in hits])
a = hits[0]
print('position', g.floats(a + 0x1C, 3), 'dir', g.floats(a + 0x28, 3))
print(jc2ctl.mod('watch rw 1500 %X' % (a + 0x1C)))
jc2ctl.mod('p2 padpress 100 200')
time.sleep(0.5)
jc2ctl.focus()
jc2ctl.click('left', 200)
time.sleep(2)
print(jc2ctl.mod('ping'))
