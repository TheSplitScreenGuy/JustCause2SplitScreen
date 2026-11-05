# P2 stands still, P1 runs in circles (and the other way round): how much each player's weapons
# move relative to their standing character. Also prints what the weapon objects are.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import json, re, sys, threading, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
names = json.load(open(os.path.join(_ROOT, r'tools\rtti_map.json')))
p2 = int(re.search(r'player 2 ([0-9A-F]+)', jc2ctl.mod('p2')).group(1), 16)
p1 = g.u32(g.u32(0x0118FB24) + 0x3570)
c1, c2 = g.u32(p1 + 0x48), g.u32(p2 + 0x48)


def weapons(c):
    inv = g.u32(c + 0x484)
    out = []
    for s in range(6):
        w = g.u32(inv + 0x3C + s * 8)
        if w and g.u32(w) is not None and g.floats(w + 0x84, 3): out.append(w)
    return out


def sample(c, ws, seconds):
    r = {w: [] for w in ws}
    t0 = time.time()
    while time.time() - t0 < seconds:
        p = g.floats(c + 0x18C, 3)
        for w in ws:
            q = g.floats(w + 0x84, 3)
            r[w].append((q[0] - p[0], q[1] - p[1], q[2] - p[2]))
        time.sleep(0.005)
    return r


def spread(r):
    return ['%.3f' % max(max(v[i] for v in x) - min(v[i] for v in x) for i in range(3)) for x in r.values()]


w1, w2 = weapons(c1), weapons(c2)
print('P1 weapons', [names.get('%08X' % g.u32(w), '%08X' % g.u32(w)) for w in w1])
print('P2 weapons', [names.get('%08X' % g.u32(w), '%08X' % g.u32(w)) for w in w2])
print('idle: P2 weapon spread', spread(sample(c2, w2, 2.0)), 'P1', spread(sample(c1, w1, 2.0)))


def run():
    jc2ctl.focus()
    jc2ctl.key_event('w', False)
    for i in range(16):
        jc2ctl.mouse(180, 0, 6)
        time.sleep(0.2)
    jc2ctl.key_event('w', True)


t = threading.Thread(target=run)
t.start()
print('P1 running: P2 weapon spread', spread(sample(c2, w2, 3.0)))
t.join()
time.sleep(5); print('P1 settled: spread', spread(sample(c1, w1, 2.0)))
print(jc2ctl.mod('p2 move 0 1'))
print('P2 running: P1 weapon spread', spread(sample(c1, w1, 3.0)))
print(jc2ctl.mod('p2 move 0 0'))
