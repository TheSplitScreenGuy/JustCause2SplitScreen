# P2's aiming stance with his zoom: mode on the controller, P1's state untouched, P2 fires while
# aiming, a weapon switch, zoom off; screenshots.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import re, struct, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
S = os.path.join(_ROOT, r'tools\shots\st_%s.png')
c2 = int(re.search(r'character ([0-9A-F]+)', jc2ctl.mod('p2')).group(1), 16)
p2 = int(re.search(r'player 2 ([0-9A-F]+)', jc2ctl.mod('p2')).group(1), 16)
p1 = g.u32(g.u32(0x0118FB24) + 0x3570)
c1 = g.u32(p1 + 0x48)
ctl = lambda c: g.u32(c + 0x2F0)


def state(t):
    print('%-14s P2 mode %d +300 %d | P1 mode %d +300 %d' % (t, 0, g.u32(p2 + 0x300), g.u32(ctl(c1) + 0xC + 0x10), g.u32(p1 + 0x300)), flush=True)


state('start')
print(jc2ctl.mod('p2 padpress 80 120')); time.sleep(0.8); state('zoom on')
print(jc2ctl.mod('screenshot ' + S % 'on'))
print(jc2ctl.mod('p2 padpress 20000 300')); time.sleep(0.15)
print(jc2ctl.mod('screenshot ' + S % 'fire')); time.sleep(0.6); state('fired')
print(jc2ctl.mod('p2 padpress 1 200')); time.sleep(1.5); state('two-handed')
print(jc2ctl.mod('screenshot ' + S % 'rifle'))
print(jc2ctl.mod('p2 padpress 80 120')); time.sleep(0.8); state('zoom off')
print(jc2ctl.mod('screenshot ' + S % 'off'))
print(jc2ctl.mod('p2 padpress 80 120')); time.sleep(0.5); state('zoom on again')
jc2ctl.focus(); jc2ctl.tap('c', 120); time.sleep(0.8); state('P1 C too')
jc2ctl.focus(); jc2ctl.tap('c', 120); time.sleep(0.8); state('P1 C off')
print(jc2ctl.mod('p2 padpress 80 120')); time.sleep(0.5); state('end')
