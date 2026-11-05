# Two FMOD listeners: count, calls, a P2 grapple far from P1, the game alive. Sound itself is by ear.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
import jc2ctl

print(jc2ctl.mod('p2audio on'))
print(jc2ctl.mod('p2 at 892 225 -2500'))
time.sleep(6)
print(jc2ctl.mod('p2audio on'))
print(jc2ctl.mod('p2 padpress 100 200'))      # LB: grapple
time.sleep(2)
print(jc2ctl.mod('p2audio off'))
time.sleep(1)
print(jc2ctl.mod('p2audio on'))
time.sleep(1)
print(jc2ctl.mod('p2audio on'))
print(jc2ctl.mod('p2').splitlines()[0])
