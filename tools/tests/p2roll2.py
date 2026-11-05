# P2's RB, standing and moving: frames right after the press (position alone doesn't show a roll).
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import re, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
import jc2ctl

S = os.path.join(_ROOT, r'tools\shots\roll_%s.png')
p = lambda: re.search(r'pos ([-0-9. ]+?) \|', jc2ctl.mod('p2')).group(1)
print(p())
print(jc2ctl.mod('p2padtest') if 0 else '', end='')
print(jc2ctl.mod('p2 padpress 200 120'))
time.sleep(0.2)
print(jc2ctl.mod('screenshot ' + S % 'rb1'))
time.sleep(0.5)
print(p())
print(jc2ctl.mod('p2 move 1 0'))
time.sleep(0.6)
print(jc2ctl.mod('p2 padpress 200 120'))
time.sleep(0.2)
print(jc2ctl.mod('screenshot ' + S % 'rb2'))
time.sleep(0.5)
print(p())
print(jc2ctl.mod('p2 move 0 0'))
