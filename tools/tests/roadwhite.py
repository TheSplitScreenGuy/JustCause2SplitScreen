# P2's road looks white/overbright (user report, 2026-10-28). Reproduces it and takes a reference.
#   python -I tools\tests\roadwhite.py [tag]
# 1. P1 far away in the city, P2 at the road spot looking north -> shots\road_<tag>_split.png
# 2. P1 moved to the road spot as well (P2 next to him)       -> shots\road_<tag>_near.png
import os, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import jc2ctl

tag = sys.argv[1] if len(sys.argv) > 1 else 'x'
shots = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'shots')
P2 = (1542.3, 233.5, 1170.7)
FACE = (0.06, 0.2, 0.998)
P1_FAR = (1077.6, 210.9, -1456.8)


def mod(cmd, timeout=60):
    r = jc2ctl.mod(cmd, timeout=timeout)
    print('>', cmd, '->', r.splitlines()[0] if r else r)
    return r


def shot(name):
    jc2ctl.focus()
    time.sleep(0.5)
    print(mod('screenshot ' + os.path.abspath(os.path.join(shots, 'road_%s_%s.png' % (tag, name)))))


jc2ctl.focus()
if not mod('p2').startswith('player 2'):
    mod('p2 join')
    time.sleep(4)
jc2ctl.focus()
mod('p1 at %.1f %.1f %.1f' % P1_FAR)
time.sleep(6)
jc2ctl.focus()
mod('p2 at %.1f %.1f %.1f' % P2)
time.sleep(6)
jc2ctl.focus()
mod('p2 face %.3f %.3f %.3f' % FACE)
time.sleep(4)
shot('split')
mod('p1 at %.1f %.1f %.1f' % (P2[0] + 3, P2[1], P2[2] - 3))
time.sleep(8)
jc2ctl.focus()
mod('p2 face %.3f %.3f %.3f' % FACE)
time.sleep(3)
shot('near')
