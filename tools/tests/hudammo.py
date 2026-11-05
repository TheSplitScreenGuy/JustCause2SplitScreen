# Which half's ammo drops: P2 fires, then P1, then both, screenshots of the right corners. Then RB
# for P2, standing and moving, with P2's controller and action logged.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import json, os, re, sys, threading, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game()
names = json.load(open(os.path.join(_ROOT, r'tools\rtti_map.json')))
S = os.path.join(_ROOT, r'tools\shots\am_%s.png')
shot = lambda n: print(n, jc2ctl.mod('screenshot ' + S % n))
if 'ammo' in sys.argv:
    print(jc2ctl.mod('p2 padpress 20000 500'))  # P2 RT
    time.sleep(1.0); shot('1p2')
    jc2ctl.focus(); jc2ctl.click('left', 400)
    time.sleep(1.0); shot('2p1')
    print(jc2ctl.mod('p2 padpress 20000 500'))
    jc2ctl.click('left', 500)
    time.sleep(1.0); shot('3both')
    time.sleep(8)
    print(jc2ctl.mod('p2 padpress 20000 300'))
    time.sleep(1.0); shot('4p2again')
if 'roll' in sys.argv:
    m = re.search(r'character ([0-9A-F]+)', jc2ctl.mod('p2'))
    c2 = int(m.group(1), 16)
    c1 = g.u32(g.u32(g.u32(0x0118FB24) + 0x3570) + 0x48)
    def name(p): return names.get('%08X' % g.u32(p), '%08X' % g.u32(p)) if p else '-'
    stop = []
    def rec():
        last = None
        t0 = time.time()
        while not stop:
            s = (name(g.u32(c2 + 0x324)), name(g.u32(c2 + 0x2F0)), name(g.u32(c1 + 0x324)), name(g.u32(c1 + 0x2F0)))
            if s != last:
                print('%5.2f P2 action %s controller %s | P1 action %s controller %s' % (time.time() - t0, *s), flush=True)
                last = s
            time.sleep(0.01)
    threading.Thread(target=rec, daemon=True).start()
    print(jc2ctl.mod('p2 padpress 200 150')); time.sleep(1.5)
    print(jc2ctl.mod('p2 move 1 0')); time.sleep(0.5)
    print(jc2ctl.mod('p2 padpress 200 150')); time.sleep(1.5)
    print(jc2ctl.mod('p2 move 0 0'))
    stop.append(1)
