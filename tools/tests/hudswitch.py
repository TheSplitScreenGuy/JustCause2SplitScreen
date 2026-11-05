# Weapon panels and the switch popup: P1 fires, P2 switches to two-handed (D-pad up), then each
# player switches; screenshots right after each step.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
import jc2ctl

S = os.path.join(_ROOT, r'tools\shots\sw_%s.png')
shot = lambda n: print(n, jc2ctl.mod('screenshot ' + S % n))
jc2ctl.focus()
jc2ctl.click('left', 300)
time.sleep(1.2)
shot('1fire')
print(jc2ctl.mod('p2 padpress 1 200'))   # D-pad up: P2 two-handed
time.sleep(0.5)
shot('2p2up')
time.sleep(1.5)
shot('3p2after')
print(jc2ctl.mod('p2 padpress 8 200'))   # D-pad right: P2 right hand
time.sleep(0.5)
shot('4p2right')
time.sleep(1.0)
jc2ctl.focus()
jc2ctl.SCAN['3'] = (0x04, 0)
jc2ctl.tap('3', 150)                     # P1 two-handed on keyboard
time.sleep(0.5)
shot('5p1key')
print(jc2ctl.mod('p2hud'))
