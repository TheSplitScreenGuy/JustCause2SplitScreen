# P2 far from P1, on the ground at the "markers_p2" place: audible channels near each view with the
# listeners in each order, around P2's grapple and melee.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
import jc2ctl

print(jc2ctl.mod('p2 at 892 230 -1700'))
time.sleep(7)


def round_(label):
    print('---', label)
    print('quiet   ', jc2ctl.mod('p2audio dump'))
    jc2ctl.mod('p2 padpress 100 300')          # LB: grapple
    for t in (0.1, 0.25, 0.4):
        time.sleep(t)
        print('grapple ', jc2ctl.mod('p2audio dump'))
    time.sleep(1.5)
    jc2ctl.mod('p2 padpress 2000 150')         # B: melee
    time.sleep(0.25)
    print('melee   ', jc2ctl.mod('p2audio dump'))
    time.sleep(1.5)


for mode, label in ((0, 'both, P1 first'), (1, 'swapped, P2 first'), (2, 'P2 only'), (0, 'both again')):
    print(jc2ctl.mod('p2audio mode %d' % mode))
    time.sleep(2.5)
    round_(label)
