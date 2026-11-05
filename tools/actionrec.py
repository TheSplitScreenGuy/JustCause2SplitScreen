# Records both players' current actions (char +0x324, class names) and heights for a while; for
# watching what a move does (e.g. parachute out of a grapple reel) while the user plays.
#   python -I actionrec.py [seconds]
import json, os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jc2ctl
from procmem import Game

seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 40
g = Game()
names = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'rtti_map.json')))
c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
m = re.search(r'character ([0-9A-F]+)', jc2ctl.mod('p2'))
c2 = int(m.group(1), 16) if m else 0
last = {}
t0 = time.time()
while time.time() - t0 < seconds:
    t = time.time() - t0
    for name, c in (('P1', c1), ('P2', c2)):
        if not c:
            continue
        a = g.u32(c + 0x324)
        action = names.get('%08X' % g.u32(a), '%08X' % g.u32(a)) if a else '-'
        if last.get(name) != action:
            print('%6.2f s  %s %-45s y %.1f' % (t, name, action, g.floats(c + 0x190, 1)[0]), flush=True)
            last[name] = action
    time.sleep(0.02)
