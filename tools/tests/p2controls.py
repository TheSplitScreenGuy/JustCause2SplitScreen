# Player 2's controls: movement follows Player 2's camera (for two camera headings), and jump works.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import math, re, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
shots = os.path.join(_ROOT, r'tools\shots')
st = jc2ctl.mod('p2')
if 'updates' not in st:
    jc2ctl.focus()
    print(jc2ctl.mod('p2 join'))
    time.sleep(2)
    st = jc2ctl.mod('p2')
body = int(re.search(r'character ([0-9A-F]+)', st).group(1), 16)
pos = lambda: g.floats(body + 0x18C, 3)


def cams():
    s = jc2ctl.mod('p2')
    m = re.search(r'cam2 f (\S+) (\S+) r (\S+) (\S+)', s)
    return [float(x) for x in m.groups()] if m else None


def walk(r, f, secs=2.0):
    jc2ctl.mod('p2 tp 2.5 0.3 -2')  # same open spot each time, so walls don't skew the result
    time.sleep(1)
    jc2ctl.focus()
    c = cams()
    jc2ctl.mod('p2 move %g %g' % (r, f))
    time.sleep(0.8)  # turning on the spot first
    a = pos()
    time.sleep(secs - 0.8)
    b = pos()
    jc2ctl.mod('p2 move 0 0')
    d = (b[0] - a[0], b[2] - a[2])
    n = math.hypot(*d) or 1
    want = (c[0], c[1]) if f else (c[2], c[3])
    print('  move %g %g: moved %.1f m, dot with P2 camera %s = %.2f' % (r, f, n, 'forward' if f else 'right',
                                                                        (d[0] * want[0] + d[1] * want[1]) / n))
    time.sleep(1)


for turn in (0, 90):
    if turn:
        print(jc2ctl.mod('p2 turn %d' % turn))
        time.sleep(0.5)
    print('P2 camera turned %d deg:' % turn)
    walk(0, 1)
    walk(1, 0)
jc2ctl.focus()
print(jc2ctl.mod('screenshot ' + shots + r'\controls_turned.png'))

def jump(tag):
    y0 = pos()[1]
    jc2ctl.focus()
    jc2ctl.mod('p2 press 23 300')
    peak = y0
    for _ in range(15):
        time.sleep(0.07)
        peak = max(peak, pos()[1])
    print('  jump (%s): height gained %.2f m' % (tag, peak - y0))


time.sleep(1)
jump('standing')
jc2ctl.mod('p2 move 0 1')
time.sleep(1)
jump('running')
jc2ctl.mod('p2 move 0 0')
print(jc2ctl.mod('p2'))
