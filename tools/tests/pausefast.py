# Presses Enter (start the highlighted restart/load), then pauses the moment gameplay starts (for
# saves with a countdown, e.g. outside mission bounds), and prints the UI mode changes.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
state = lambda: (g.u32(0x011958C4), g.u32(0x0105F754))
if 'noenter' not in sys.argv:
    jc2ctl.focus()
    jc2ctl.tap('enter')
    time.sleep(1.5)
last = None
end = time.time() + 120
while time.time() < end:
    s = state()
    if s != last:
        print('%.1f mode %s pending %s' % (time.time(), s[0], s[1]), flush=True)
        last = s
    if s == (3, 6):
        time.sleep(0.5)
        jc2ctl.focus()
        jc2ctl.tap('esc')
        time.sleep(1.0)
        print('paused, state', state())
        break
    time.sleep(0.05)
