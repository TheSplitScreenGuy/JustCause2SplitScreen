# User report (2026-10-28): P2 loses all momentum opening the parachute out of a grapple reel (P1
# keeps gliding). Records both players for a while (the user plays): per change of current action
# (char +0x324) or movement controller (char +0x2F0), plus the speed every 0.1 s around them.
#   python -I tools\tests\chuterec.py [seconds]
import json, math, os, re, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import jc2ctl
from procmem import Game

secs = float(sys.argv[1]) if len(sys.argv) > 1 else 60
g = Game()
names = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'rtti_map.json')))
c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
c2 = int(re.search(r'character ([0-9A-F]+)', jc2ctl.mod('p2')).group(1), 16)


def name(p):
    if not p:
        return '-'
    v = '%08X' % g.u32(p)
    return names.get(v, v).replace('class ', '')


state = {1: None, 2: None}
last = {1: None, 2: None}
t0 = time.time()
interesting = {1: 0.0, 2: 0.0}
while time.time() - t0 < secs:
    t = time.time() - t0
    for who, c in ((1, c1), (2, c2)):
        pos = g.floats(c + 0x18C, 3)
        s = (name(g.u32(c + 0x324)), name(g.u32(c + 0x2F0)))
        speed = None
        if last[who]:
            lt, lp = last[who]
            if t - lt >= 0.1:
                speed = (math.hypot(pos[0] - lp[0], pos[2] - lp[2]) / (t - lt), (pos[1] - lp[1]) / (t - lt))
                last[who] = (t, pos)
        else:
            last[who] = (t, pos)
        if s != state[who]:
            print('%6.2f P%d action %-35s controller %s' % (t, who, s[0], s[1]), flush=True)
            state[who] = s
            interesting[who] = t + 3.0
        if speed and t < interesting[who]:
            print('%6.2f P%d   speed %5.1f m/s horizontal, %5.1f vertical' % (t, who, speed[0], speed[1]), flush=True)
    time.sleep(0.01)
