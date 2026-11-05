# User report (2026-10-29): mission markers ("Reapers HQ 1.49 km") on P2's half follow P1. Takes
# screenshots after turning only P1's camera (mouse) and only P2's camera (p2 face), so the marker
# positions in each half can be compared.   python -I tools\tests\markers.py <out dir>
import os, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import jc2ctl

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'shots')
os.makedirs(out, exist_ok=True)


def shot(name):
    jc2ctl.mod('screenshot ' + os.path.abspath(os.path.join(out, name + '.png')))
    print(os.path.abspath(os.path.join(out, name + '.png')))


jc2ctl.focus()
jc2ctl.mod('p1 at 892 225 -2010')
jc2ctl.mod('p2 at 896 225 -2008')
time.sleep(6)
jc2ctl.mod('p2 face 0 -0.15 1')
time.sleep(1)
shot('markers_0')
jc2ctl.mouse(400, 0, 20)   # P1's camera turns
time.sleep(1.5)
shot('markers_p1turned')
jc2ctl.mod('p2 face 1 -0.15 0')
time.sleep(1.5)
shot('markers_p2turned')
