# User report (2026-10-29): P2's grapple goes where P1 points. P2 at the deck spot with P1 next
# to him; P2's camera faces one way (P1's another), P2 fires with LB (test pad) or action 0x51;
# prints the angle of P2's aim point (aim control +0x44 via char +0xA34) and of his pull to both
# players' view directions.
#   python -I tools\tests\p2grapaim.py [pad|action] [p2 face fx fy fz]
import math, os, re, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import jc2ctl
from procmem import Game

how = sys.argv[1] if len(sys.argv) > 1 else 'pad'
face = sys.argv[2] if len(sys.argv) > 2 else '-0.83 -0.35 -0.55'
g = Game()


def mod(cmd):
    jc2ctl.focus()
    return jc2ctl.mod(cmd)


def heading_deg(v):
    return math.degrees(math.atan2(v[0], v[1]))


def diff(a, b):
    d = (a - b + 180) % 360 - 180
    return abs(d)


mod('god on')
st = mod('p2')
c2 = int(re.search(r'character ([0-9A-F]+)', st).group(1), 16)
mod('p2 at 892 224.5 -2029')
time.sleep(5)
mod('p2 face ' + face)
time.sleep(1.5)
st = mod('p2')
cam1 = [float(v) for v in re.search(r'cam1 f ([-\d.]+) ([-\d.]+)', st).groups()]
cam2 = [float(v) for v in re.search(r'cam2 f ([-\d.]+) ([-\d.]+)', st).groups()]
before = g.floats(c2 + 0x18C, 3)
mod('p2 padpress 100 300' if how == 'pad' else 'p2 press 51 300')
time.sleep(0.25)
aim = g.floats(g.u32(c2 + 0xA34) + 0x44, 3)
time.sleep(1.5)
after = g.floats(c2 + 0x18C, 3)
to_aim = (aim[0] - before[0], aim[2] - before[2])
moved = (after[0] - before[0], after[2] - before[2])
# "cam1/cam2 f" is the look direction (x, z).
look1, look2 = tuple(cam1), tuple(cam2)
print('P1 looks %.0f deg, P2 looks %.0f deg' % (heading_deg(look1), heading_deg(look2)))
print('aim point %.0f m away at %.0f deg: %.0f deg from P2\'s view, %.0f from P1\'s' % (
    math.hypot(*to_aim), heading_deg(to_aim), diff(heading_deg(to_aim), heading_deg(look2)), diff(heading_deg(to_aim), heading_deg(look1))))
if math.hypot(*moved) > 1:
    print('pulled %.1f m at %.0f deg: %.0f deg from P2\'s view, %.0f from P1\'s' % (
        math.hypot(*moved), heading_deg(moved), diff(heading_deg(moved), heading_deg(look2)), diff(heading_deg(moved), heading_deg(look1))))
else:
    print('not pulled (%.1f m)' % math.hypot(*moved))
