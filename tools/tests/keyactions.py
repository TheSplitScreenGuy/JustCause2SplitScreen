# Holds each given key (or mouse button) and prints which game actions become active in Player 1's
# input context, with names from the action table at 0x01071DE8.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import sys, threading, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
p1 = g.u32(g.u32(0x0118FB24) + 0x3570)
ctx = g.u32(g.u32(p1 + 0x188) + 4)
heads = g.u32(ctx + 0x10)
count = (g.u32(ctx + 0x14) - heads) // 4


def name(i):
    p = g.u32(0x01071DE8 + i * 4)
    b = g.read(p, 40) if p else b''
    return b.split(b'\x00')[0].decode('latin1') if b else '?'


def active():
    out = set()
    for i in range(count):
        e = g.u32(heads + i * 4)
        while e:
            v = g.u32(e)
            if v and ((v >> 24) & 7) in (2, 3) and (v & 0xFFF):
                out.add(i)
            e = g.u32(e + 0x10)
    return out


for key in sys.argv[1:]:
    jc2ctl.focus()
    time.sleep(0.3)
    base = active()
    if key in ('left', 'right'):
        t = threading.Thread(target=lambda: jc2ctl.click(key, 500))
    else:
        t = threading.Thread(target=lambda: jc2ctl.tap(key, 500))
    t.start()
    time.sleep(0.25)
    now = active() - base
    t.join()
    print('%-6s -> %s' % (key, ', '.join('0x%X %s' % (i, name(i)) for i in sorted(now)) or '-'))
    time.sleep(0.6)
