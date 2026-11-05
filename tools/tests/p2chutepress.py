# P2 in the air presses one or more actions together (like pad A: jump 23 + parachute 47 + ...)
# and we see whether the chute opens (fall speed settles near -1.7 m/s).
#   python -I tools\tests\p2chutepress.py <hexAction>[+<hexAction2>] [height]
import os, re, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import jc2ctl
from procmem import Game

actions = sys.argv[1] if len(sys.argv) > 1 else '47'
height = float(sys.argv[2]) if len(sys.argv) > 2 else 120.0
g = Game()
jc2ctl.focus()
jc2ctl.mod('god on')
c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
c2 = int(re.search(r'character ([0-9A-F]+)', jc2ctl.mod('p2')).group(1), 16)
p = g.floats(c1 + 0x18C, 3)
CHUTE = 0x00EC24AC  # CParachuteController


def has_chute(c):
    d = g.read(c, 0x1000) or b''
    for k in range(0, len(d) - 3, 4):
        v = int.from_bytes(d[k:k + 4], 'little')
        if 0x02000000 < v < 0x7FFF0000 and g.u32(v) == CHUTE:
            return '+%X' % k
    return ''


jc2ctl.mod('p2 at %.1f %.1f %.1f' % (p[0] + 6, p[1] + height, p[2]))
time.sleep(2.0)
print('before: chute controller', has_chute(c2) or 'none')
jc2ctl.mod('p2 press %s 150' % actions)
last = None
for i in range(12):
    time.sleep(0.3)
    y = g.floats(c2 + 0x190, 1)[0]
    if last is not None:
        print('%.1f s  P2 y %6.1f  vy %6.1f  chute controller %s' % (i * 0.3, y, (y - last) / 0.3, has_chute(c2) or 'none'))
    last = y
