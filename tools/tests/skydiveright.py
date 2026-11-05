# User report (2026-10-28): in free fall, when P1 stops steering and rights himself, P2 seems to
# right himself too. Both lifted; P1 holds a key for a while, then lets go; logs each player's
# body tilt (angle of the up axis from vertical), heading and smoothed steer every 0.1 s.
#   python -I tools\tests\skydiveright.py [key, default a] [hold_s]
import math, os, re, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import jc2ctl
from procmem import Game

key = sys.argv[1] if len(sys.argv) > 1 else 'a'
hold = float(sys.argv[2]) if len(sys.argv) > 2 else 1.5
g = Game()


def mod(cmd):
    jc2ctl.focus()
    return jc2ctl.mod(cmd)


c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
c2 = int(re.search(r'character ([0-9A-F]+)', mod('p2')).group(1), 16)
mod('p1 at 905 900 -2040')
time.sleep(2)
mod('p2 at 925 900 -2040')
time.sleep(2.5)


p2 = int(re.search(r'player 2 ([0-9A-F]+)', mod('p2')).group(1), 16)
ctx2 = g.u32(g.u32(p2 + 0x188) + 4)  # P2's private input context
heads2 = g.u32(ctx2 + 0x10)


def raw2():
    # P2's MOVE_FORWARD/BACK/LEFT/RIGHT (0x1E..0x21) values, 0..1.
    return [(g.u32(g.u32(heads2 + 4 * a)) & 0xFFF) / 4095.0 for a in (0x1E, 0x1F, 0x20, 0x21)]


def pose(c):
    m = g.floats(c + 0x15C, 12)  # right, up, forward (x y z w each)
    up, fwd = m[4:7], m[8:11]
    tilt = math.degrees(math.acos(max(-1, min(1, up[1]))))
    heading = math.degrees(math.atan2(fwd[0], fwd[2]))
    s = g.floats(g.u32(c + 0xA34) + 0x148, 2)
    return tilt, heading, s


jc2ctl.focus()
t0 = time.time()
down = up_sent = False
while time.time() - t0 < hold + 4:
    t = time.time() - t0
    # key "p2": P2's stick held left instead (p2 move), P1 untouched.
    if not down and t > 0.5:
        jc2ctl.mod('p2 move -1 0') if key == 'p2' else jc2ctl.key_event(key, False)
        down = True
    if down and not up_sent and t > 0.5 + hold:
        jc2ctl.mod('p2 move 0 0') if key == 'p2' else jc2ctl.key_event(key, True)
        up_sent = True
    a, b = pose(c1), pose(c2)
    r = raw2()
    print('%5.1f %s  P1 tilt %5.1f head %6.1f steer %5.2f %5.2f   P2 tilt %5.1f head %6.1f steer %5.2f %5.2f  P2 raw f%.1f b%.1f l%.1f r%.1f' % (
        t, key if down and not up_sent else ' ', a[0], a[1], a[2][0], a[2][1], b[0], b[1], b[2][0], b[2][1], *r), flush=True)
    time.sleep(0.1)
if not up_sent:
    jc2ctl.mod('p2 move 0 0') if key == 'p2' else jc2ctl.key_event(key, True)
