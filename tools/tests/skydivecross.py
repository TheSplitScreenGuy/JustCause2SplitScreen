# User report (2026-10-28): in free fall P1's keys also steered P2. Both players lifted ~650 m;
# phases: nothing, P1 holds W, P2 pushes forward (p2 move), both. Per phase each player's mean
# horizontal velocity (x, z) and speed, so a reaction to the other player's input shows up.
#   python -I tools\tests\skydivecross.py [key, default w]
import math, os, re, sys, threading, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import jc2ctl
from procmem import Game

key = sys.argv[1] if len(sys.argv) > 1 else 'w'
g = Game()


def mod(cmd):
    jc2ctl.focus()
    return jc2ctl.mod(cmd)


c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
c2 = int(re.search(r'character ([0-9A-F]+)', mod('p2')).group(1), 16)
mod('p1 at 905 900 -2040')
time.sleep(2)
mod('p2 at 925 900 -2040')
time.sleep(2)


def measure(secs):
    p0 = [g.floats(c + 0x18C, 3) for c in (c1, c2)]
    time.sleep(secs)
    p1 = [g.floats(c + 0x18C, 3) for c in (c1, c2)]
    return [((b[0] - a[0]) / secs, (b[2] - a[2]) / secs, (b[1] - a[1]) / secs) for a, b in zip(p0, p1)]


def steer(c):
    # Free fall stick after smoothing: controller state (char +0xA34) +0x148 forward, +0x14C side.
    return g.floats(g.u32(c + 0xA34) + 0x148, 2)


def report(phase, v):
    s = [steer(c) for c in (c1, c2)]
    k = ['%08X' % g.u32(g.u32(c + 0x2F0)) for c in (c1, c2)]  # controller vtable (0x00EC2C20 = free fall)
    print('%-14s ' % phase + '   '.join('P%d %s steer %5.2f %5.2f v %6.1f %6.1f (%5.1f m/s)' % (i + 1, k[i], s[i][0], s[i][1], x, z, math.hypot(x, z))
                                     for i, (x, z, y) in enumerate(v)), flush=True)


def hold_key(secs):
    jc2ctl.key_event(key, False)
    time.sleep(secs)
    jc2ctl.key_event(key, True)


jc2ctl.focus()
report('nothing', measure(1.5))
t = threading.Thread(target=hold_key, args=(2.0,)); t.start(); time.sleep(0.5)
report('P1 holds ' + key, measure(1.5)); t.join()
report('nothing', measure(1.5))
mod('p2 move 0 1')
time.sleep(0.5)
report('P2 forward', measure(1.5))
mod('p2 move 0 0')
report('nothing', measure(1.5))
