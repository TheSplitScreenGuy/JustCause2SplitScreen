# User report (2026-10-28): P2's jump and grapple feel delayed, presses need repeating. Taps pad 2's
# A (jump) or LB (grapple) for the given lengths and reports, per tap, whether P2 reacted (height
# gain for A, current action change for LB) and after how long.
#   python -I tools\tests\p2tap.py [a|lb] [ms,ms,...] [repeats]
import os, re, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import jc2ctl
from procmem import Game

which = sys.argv[1] if len(sys.argv) > 1 else 'a'
lengths = [int(v) for v in (sys.argv[2] if len(sys.argv) > 2 else '30,60,100,150').split(',')]
repeats = int(sys.argv[3]) if len(sys.argv) > 3 else 3
button = '1000' if which == 'a' else '100'
g = Game()


def mod(cmd):
    jc2ctl.focus()
    return jc2ctl.mod(cmd)


c2 = int(re.search(r'character ([0-9A-F]+)', mod('p2')).group(1), 16)
for ms in lengths:
    hits = []
    for _ in range(repeats):
        mod('p2 at 892 224.5 -2029')
        time.sleep(4)
        if which == 'lb':
            mod('p2 face -0.7 -0.6 -0.45')
            time.sleep(1)
        y0 = g.floats(c2 + 0x190, 1)[0]
        a0 = g.u32(c2 + 0x324)
        mod('p2 padpress %s %d' % (button, ms))
        t0 = time.time()
        when = None
        rise = 0.0
        while time.time() - t0 < 1.2:
            rise = max(rise, g.floats(c2 + 0x190, 1)[0] - y0)
            if which == 'a' and rise > 0.15 and when is None:
                when = time.time() - t0
            if which == 'lb' and g.u32(c2 + 0x324) not in (0, a0):
                when = time.time() - t0; break
            time.sleep(0.005)
        hits.append((when, rise))
        time.sleep(1.5)
    print('%s tap %4d ms: %d/%d reacted  %s' % (which, ms, sum(h is not None for h, _ in hits), repeats,
          ' '.join(('-' if h is None else '%.0fms' % (h * 1000)) + ' (%.2f m)' % r for h, r in hits)))
