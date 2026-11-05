# Player 2 dual wield on D-pad right: holds and taps it with "p2 padpress" and prints the hand mode
# (inventory +0x64: 1/2 one weapon, 4 dual, 0 switching). Expect a hold -> 4 that stays after the
# release, a tap from 4 -> one weapon.   python -I dualhold.py
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import re, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
p2 = jc2ctl.mod('p2')
if 'character' not in p2:
    print(jc2ctl.mod('p2 join')); time.sleep(5); p2 = jc2ctl.mod('p2')
inv = g.u32(int(re.search(r'character ([0-9A-F]+)', p2).group(1), 16) + 0x484)

def watch(label, ms, after=2.0):
    seen = [g.u32(inv + 0x64)]
    if ms: jc2ctl.mod('p2 padpress 8 %d' % ms)
    end = time.time() + ms / 1000.0 + after
    while time.time() < end:
        m = g.u32(inv + 0x64)
        if m != seen[-1]: seen.append(m)
        time.sleep(0.01)
    print('%-12s %s' % (label, ' -> '.join(map(str, seen))), flush=True)

for label, ms in [('hold 0.8', 800), ('tap', 120), ('hold 0.65', 650), ('hold 2.0', 2000), ('tap', 120), ('tap', 120), ('hold 1.0', 1000)]:
    watch(label, ms)
