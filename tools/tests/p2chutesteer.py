# Parachute steering and closing for both players: both up in the air, chutes open (P1 Space,
# P2 action 0x47), then forward (P1 holds W, P2 stick forward) for 3 s, then both close (press
# again) and fall. Prints height, fall speed and horizontal speed per player.
#   python -I tools\tests\p2chutesteer.py [height]
import math, os, re, sys, threading, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import jc2ctl
from procmem import Game

height = float(sys.argv[1]) if len(sys.argv) > 1 else 150.0
shots = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'shots')
g = Game()


def mod(cmd):
    jc2ctl.focus()
    return jc2ctl.mod(cmd)


if not mod('p2').startswith('player 2'):
    mod('p2 join')
    time.sleep(5)
mod('god on')
c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
c2 = int(re.search(r'character ([0-9A-F]+)', jc2ctl.mod('p2')).group(1), 16)
p = g.floats(c1 + 0x18C, 3)
mod('p2 at %.1f %.1f %.1f' % (p[0] + 6, p[1] + height, p[2]))
mod('p1lift %.0f' % height)
time.sleep(0.7)
jc2ctl.focus()
jc2ctl.tap('space', 150)
mod('p2 press 47 150')
last = {}
t0 = time.time()
phase = 'open'


def sample(t):
    line = '%4.1f s %-7s' % (t, phase)
    for name, c in (('P1', c1), ('P2', c2)):
        q = g.floats(c + 0x18C, 3)
        if name in last:
            dt = t - last[name][0]
            v = [(q[i] - last[name][1][i]) / dt for i in range(3)]
            line += '  %s y %6.1f vy %5.1f horiz %5.1f' % (name, q[1], v[1], math.hypot(v[0], v[2]))
        last[name] = (t, q)
    print(line, flush=True)


while time.time() - t0 < 14:
    t = time.time() - t0
    if phase == 'open' and t > 4:
        phase = 'forward'
        threading.Thread(target=lambda: jc2ctl.tap('w', 3000)).start()
        mod('p2 move 0 1')
    elif phase == 'forward' and t > 7:
        phase = 'closed'
        mod('p2 move 0 0')
        mod('screenshot ' + os.path.abspath(os.path.join(shots, 'p2chutesteer_fwd.png')))
        jc2ctl.focus()
        jc2ctl.tap('space', 150)
        mod('p2 press 47 150')
    sample(t)
    time.sleep(0.5)
mod('screenshot ' + os.path.abspath(os.path.join(shots, 'p2chutesteer_end.png')))
