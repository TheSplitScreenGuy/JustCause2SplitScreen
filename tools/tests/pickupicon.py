# Pickup icons per player (money crate at the "pickup" spot in places.txt): the $ icon should show
# in a half only while that half's player is within the crate's trigger (30 m).
# Screenshots: tools\shots\pickup_<case>.png      python -I pickupicon.py
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import os, subprocess, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
import jc2ctl

SHOTS = os.path.join(_ROOT, r'tools\shots')
SPOT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'spot.py')


def spot(*args):
    print(subprocess.run([sys.executable, '-I', SPOT] + list(args), capture_output=True, text=True).stdout.strip())


def shot(name):
    time.sleep(2)
    jc2ctl.mod('screenshot %s\\pickup_%s.png' % (SHOTS, name))
    time.sleep(0.5)


jc2ctl.focus()
# Case 1: P1 beside the crate, P2 ~85 m away looking at it.
spot('p1', '609.2', '223.5', '-2624.7', '0.810', '0.014', '-0.587')
print(jc2ctl.mod('p2 at 659.3 223.2 -2557.5'))
time.sleep(6)
print(jc2ctl.mod('p2 face 0.653 0.1 0.757'))
shot('p1near')
# Case 2: P2 beside the crate looking at it, P1 ~85 m away (looking at it too).
print(jc2ctl.mod('p2 at 609.2 223.5 -2624.7'))
time.sleep(5)
print(jc2ctl.mod('p2 face 0.810 0.1 -0.587'))
spot('p1', '659.3', '223.2', '-2557.5', '0.653', '0.1', '0.757')
shot('p2near')
