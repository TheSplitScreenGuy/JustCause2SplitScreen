# Floats in the camera objects that change while Player 1 aims (right mouse held).
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import struct, sys, threading, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
mgr = g.u32(0x011919F4)
cams = {'main': mgr + 0x18, 'render': g.u32(mgr + 0x14), 'logic': g.u32(mgr + 0x10)}
print({k: '%08X' % v for k, v in cams.items()})
SIZE = 0x440


def snap(c):
    return struct.unpack('<%df' % (SIZE // 4), g.read(c, SIZE))


proj = lambda c: struct.unpack('<6f', g.read(c + 0x2A4, 24))
print('before', ['%.3f' % v for v in proj(cams['main'])])
base = {k: [snap(c) for i in range(5)] for k, c in cams.items()}
jc2ctl.focus()
t = threading.Thread(target=lambda: jc2ctl.click('right', 1800))
t.start()
time.sleep(1.4)
print('aiming', ['%.3f' % v for v in proj(cams['main'])])
print(jc2ctl.mod(r'screenshot ' + os.path.join(_ROOT, r'tools\shots\fov_aim.png')))
aim = {k: [snap(c) for i in range(5)] for k, c in cams.items()}
t.join()
for k in cams:
    for i in range(SIZE // 4):
        a = [b[i] for b in base[k]]; c = [b[i] for b in aim[k]]
        if abs(a[0]) < 1e4 and abs(c[0]) < 1e4 and max(a) - min(a) < 0.02 and abs(a[0] - c[0]) > 0.015:
            print(k, '+%03X %.5f -> %.5f' % (i * 4, a[0], c[0]))
