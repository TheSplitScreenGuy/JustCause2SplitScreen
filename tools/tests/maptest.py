# With Player 2 joined: pause menu, then the map, each opened and closed twice; reports whether the
# game survives each step.
# usage: python -I maptest.py [holdMode]   (holdMode for "p2 hold <n>", testing only)
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
import jc2ctl

jc2ctl.focus()
print(jc2ctl.mod('p2 join'))
time.sleep(2)
if len(sys.argv) > 1:
    print(jc2ctl.mod('p2 hold %s' % sys.argv[1]))
time.sleep(1)


def step(tag, key):
    jc2ctl.focus()
    jc2ctl.tap(key)
    time.sleep(2)
    try:
        ok = jc2ctl.game_window() is not None
    except Exception:
        ok = False
    print(tag, 'alive' if ok else 'GAME GONE')
    if not ok:
        raise SystemExit(1)


for _ in range(2):
    step('pause menu open:', 'esc')
    step('pause menu closed:', 'esc')
for _ in range(2):
    step('map open:', 'f1')
    step('map closed:', 'esc')
print(jc2ctl.mod('p2'))
