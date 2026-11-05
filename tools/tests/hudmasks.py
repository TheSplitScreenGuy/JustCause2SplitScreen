# Tries refresh masks: P1 fires, a screenshot per mask (tools\shots\mask_<m>.png).
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
import jc2ctl

for m in sys.argv[1:]:
    print(m, jc2ctl.mod('p2hud refresh %s 0' % m))
    jc2ctl.focus()
    jc2ctl.click('left', 250)
    time.sleep(1.0)
    print(jc2ctl.mod(r'screenshot ' + os.path.join(_ROOT, r'tools\shots\mask_%s.png') % m))
    time.sleep(0.5)
