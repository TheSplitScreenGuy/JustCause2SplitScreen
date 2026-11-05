# Logs the script's weapon reads around P2's switch, then P2 leaves and P1 fires alone twice.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
import jc2ctl

S = os.path.join(_ROOT, r'tools\shots\%s.png')
print(jc2ctl.mod('p2 padpress 1 200'))
time.sleep(4)
print(jc2ctl.mod('p2hud log'))
print(jc2ctl.mod('p2 padpress 8 200'))
time.sleep(1.2)
print(jc2ctl.mod('p2hud nolog'))
print(jc2ctl.mod('screenshot ' + S % 'pop_p2'))
time.sleep(5)
print(jc2ctl.mod('p2 leave'))
time.sleep(4)
jc2ctl.focus()
jc2ctl.click('left', 200)
time.sleep(1.5)
print(jc2ctl.mod('screenshot ' + S % 'solo1'))
time.sleep(1)
jc2ctl.click('left', 500)
time.sleep(0.3)
print(jc2ctl.mod('screenshot ' + S % 'solo2'))
