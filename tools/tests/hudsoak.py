# HUD soak with Player 2 in: firing, switching, pause menu, P2 leaving and joining, then a mod
# health line. Reports whether the game survived, the slot stayed Player 1's and the state afterwards.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import re, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
p1 = lambda: g.u32(g.u32(0x0118FB24) + 0x3570)
start = p1()


def check(step):
    now = p1()
    print('%-16s manager player %s, game mode %s' % (step, 'same' if now == start else 'CHANGED %08X' % now, g.u32(0x011958C4)), flush=True)


for i in range(40):
    jc2ctl.focus()
    jc2ctl.click('left', 120)
    jc2ctl.mod('p2 padpress %X 80' % (1 << (i % 4)))   # the D-pad, each way
    jc2ctl.mod('p2 padpress 20000 120')               # RT: P2 fires
    time.sleep(0.35)
    if i % 10 == 0: check('switch %d' % i)
jc2ctl.tap('esc', 120)
time.sleep(2.0)
print(jc2ctl.mod(r'screenshot ' + os.path.join(_ROOT, r'tools\shots\soak_pause.png')))
jc2ctl.tap('esc', 120)
time.sleep(3.0)
check('after pause')
print(jc2ctl.mod('p2 leave'))
time.sleep(4.0)
check('p2 left')
print(jc2ctl.mod('p2hud'))
jc2ctl.focus()
jc2ctl.click('left', 200)
time.sleep(0.8)
print(jc2ctl.mod(r'screenshot ' + os.path.join(_ROOT, r'tools\shots\soak_solo.png')))
print(jc2ctl.mod('p2 join'))
time.sleep(10.0)
check('p2 joined')
print(jc2ctl.mod(r'screenshot ' + os.path.join(_ROOT, r'tools\shots\soak_rejoin.png')))
