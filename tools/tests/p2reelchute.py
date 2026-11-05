# User report (2026-10-29): P2 can't open the parachute from a grapple reel-in (P1 can: Space
# while reeling). P2 at the deck spot facing the shack fires the grapple (action 0x51), then
# during the reel presses the given actions (pad A = 23+47 jump + parachute, plus 49 / 90);
# logs P2's current action (char +0x324) and whether the parachute controller is in.
#   python -I tools\tests\p2reelchute.py [actions, default 23+47] [delay_s]
import json, os, re, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import jc2ctl
from procmem import Game

actions = sys.argv[1] if len(sys.argv) > 1 else '23+47'
delay = float(sys.argv[2]) if len(sys.argv) > 2 else 0.5
g = Game()
names = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'rtti_map.json')))
CHUTE = 0x00EC24AC


def mod(cmd):
    jc2ctl.focus()
    return jc2ctl.mod(cmd)


def state(c):
    a = g.u32(c + 0x324)
    action = names.get('%08X' % g.u32(a), '%08X' % g.u32(a)) if a else '-'
    chute = any(g.u32(v) == CHUTE for v in [int.from_bytes(g.read(c + 0x2F0, 4), 'little')] if 0x02000000 < v < 0x7FFF0000)
    return action, chute


mod('god on')
c2 = int(re.search(r'character ([0-9A-F]+)', jc2ctl.mod('p2')).group(1), 16)
mod('p2 at 892 224.5 -2029')
time.sleep(5)
mod('p2 face ' + (sys.argv[3] if len(sys.argv) > 3 else '0.55 0.1 -0.83'))
time.sleep(1.5)
pad = actions == 'pad'   # 'pad': grapple with LB and press A on the (test) pad
mod('p2 padpress 100 300' if pad else 'p2 press 51 300')
t0 = time.time()
pressed = False
seen = []
while time.time() - t0 < 6:
    t = time.time() - t0
    if not pressed and t > delay:
        mod('p2 padpress 1000 150' if pad else 'p2 press %s 150' % actions)
        pressed = True
    s = state(c2)
    if not seen or seen[-1][1] != s:
        seen.append((round(t, 2), s, round(g.floats(c2 + 0x190, 1)[0], 1)))
    time.sleep(0.03)
for t, (action, chute), y in seen:
    print('%5.2f s  action %-45s parachute %-5s y %.1f' % (t, action, chute, y))

