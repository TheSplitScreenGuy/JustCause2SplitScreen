# User request (2026-10-28): P2 melee on B, like P1 (repeated presses alternate the swing side).
# Puts P2 on open ground (no car nearby: B also climbs onto cars), presses B on pad 2 a few
# times and logs P2's current action (char +0x324) changes; P1's for comparison with 'p1 <key>'.
#   python -I tools\tests\p2melee.py [p2|p1 <key>] [presses] [gap_s]
import json, os, re, sys, threading, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import jc2ctl
from procmem import Game

who = sys.argv[1] if len(sys.argv) > 1 else 'p2'
key = sys.argv[2] if who == 'p1' and len(sys.argv) > 2 else None
rest = sys.argv[3:] if who == 'p1' else sys.argv[2:]
presses = int(rest[0]) if rest else 4
gap = float(rest[1]) if len(rest) > 1 else 0.6
g = Game()
names = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'rtti_map.json')))


def mod(cmd):
    jc2ctl.focus()
    return jc2ctl.mod(cmd)


c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
c2 = int(re.search(r'character ([0-9A-F]+)', mod('p2')).group(1), 16)
c = c2 if who == 'p2' else c1
if who == 'p2':
    mod('p2 at 960 228 -2060')
    time.sleep(5)


def action():
    a = g.u32(c + 0x324)
    return names.get('%08X' % g.u32(a), '%08X' % g.u32(a)) if a else '-'


seen = []
t0 = time.time()
next_press, done = 0.5, 0
while time.time() - t0 < 0.5 + presses * gap + 2:
    t = time.time() - t0
    if done < presses and t >= next_press:
        if who == 'p2':
            mod('p2 padpress 2000 120')  # XINPUT_GAMEPAD_B
        else:
            threading.Thread(target=lambda: jc2ctl.tap(key, 120)).start()
        seen.append((round(t, 2), 'press %d' % (done + 1)))
        done += 1
        next_press += gap
    a = action()
    if not [s for s in seen if not s[1].startswith('press')] or [s for s in seen if not s[1].startswith('press')][-1][1] != a:
        seen.append((round(t, 2), a))
    time.sleep(0.01)
for t, a in seen:
    print('%5.2f %s' % (t, a))
