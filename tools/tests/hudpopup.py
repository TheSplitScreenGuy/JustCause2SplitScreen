# Weapon switch popup with the cameras facing apart: native bone values first (P2 absent from the
# reads, for the fixed spot), then each player switches with P2's camera turned; screenshots.
#   hudpopup.py [x y]
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
import jc2ctl

S = os.path.join(_ROOT, r'tools\shots\pp_%s.png')
shot = lambda n: print(n, jc2ctl.mod('screenshot ' + S % n))
jc2ctl.SCAN['3'] = (0x04, 0)
jc2ctl.SCAN['1'] = (0x02, 0)
if len(sys.argv) > 2:
    print(jc2ctl.mod('p2hud popup %s %s' % (sys.argv[1], sys.argv[2])))
print(jc2ctl.mod('p2 turn 2.2'))
time.sleep(1.0)
jc2ctl.focus(); jc2ctl.tap('3', 150)      # P1 switches
time.sleep(1.0)
shot('1p1')
time.sleep(4.5)
print(jc2ctl.mod('p2 padpress 1 200'))    # P2 switches
time.sleep(1.0)
shot('2p2')
time.sleep(1.5)
shot('3p2late')
time.sleep(4)
print(jc2ctl.mod('p2 padpress 8 200'))
time.sleep(1.0)
jc2ctl.focus(); jc2ctl.tap('1', 150)      # both at once
time.sleep(0.8)
shot('4both')
