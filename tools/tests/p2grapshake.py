# Does Player 2's grapple shake the game camera (Player 1's)? Samples the camera control
# manager's shake state (+0x24 current amplitude, +0x28 strength) while P2 grapples from several
# spots and camera pitches. Usage: python -I p2grapshake.py
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import re, sys, time, struct
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
if 'updates' not in jc2ctl.mod('p2'):
    jc2ctl.focus()
    print(jc2ctl.mod('p2 join'))
    time.sleep(3)
mgr = g.find_dwords([0x00EB8774], limit=4)[0x00EB8774][0]
c2 = g.u32(int(re.search(r'player 2 ([0-9A-F]+)', jc2ctl.mod('p2')).group(1), 16) + 0x48)
pos = lambda: g.floats(c2 + 0x18C, 3)
setups = [('ground ahead', '2.5 0.3 -2', 4.6), ('far left', '-25 0.3 0', 4.6), ('looking down 45', '2.5 0.3 -2', 45),
          ('right', '8 0.3 4', 4.6), ('looking up 10', '-8 0.3 4', -10)]
jc2ctl.focus()
for name, tp, pitch in setups:
    jc2ctl.mod('p2 tp ' + tp)
    jc2ctl.mod('p2 pitch %.1f' % pitch)
    time.sleep(1.5)
    b = pos()
    peak, start = 0.0, None
    t0 = time.time()
    jc2ctl.mod('p2 press 51 300')
    while time.time() - t0 < 3.0:
        amp, strength = g.floats(mgr + 0x24, 2)
        if amp > peak:
            peak = amp
        if amp > 0.001 and start is None:
            start = time.time() - t0
        time.sleep(0.005)
    y = pos()
    print('%-16s P2 pulled %5.1f m | shake peak %.3f%s' % (name, sum((y[i] - b[i]) ** 2 for i in range(3)) ** 0.5, peak,
                                                          ' (from %.2f s)' % start if start is not None else ''))
jc2ctl.mod('p2 pitch 4.6')
