import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import sys,time,struct,re,collections
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
import jc2ctl
from procmem import Game
g=Game(write=True)
rd=lambda a:struct.unpack('<I',g.read(a,4))[0]
def p1char():
    mgr=rd(0x0118FB24); p=collections.Counter(rd(mgr+0x3570) for i in range(100)).most_common(1)[0][0]; return rd(p+0x48)
def p2char(): return int(re.search(r'character ([0-9A-F]+)',jc2ctl.mod('p2')).group(1),16)
def grenade():
    jc2ctl.mod('p2 tp 1.2 0 0'); time.sleep(3)
    jc2ctl.focus(); jc2ctl.mouse(0,700); time.sleep(0.5); jc2ctl.click('right',150); time.sleep(5)
    c1=p1char(); g.write(c1+0xE8, struct.pack('<2f',400.0,400.0))   # keep P1 alive
    c2=p2char(); h=struct.unpack('<3f',g.read(c2+0xE4,12)); print('after grenade: P2 base %.0f health %.1f max %.1f dead %d'%(h+(g.read(c2+0x40,1)[0],)))
for i in range(int(sys.argv[1])): grenade()
