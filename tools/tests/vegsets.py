import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import sys,struct,time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl
g=Game()
rd=lambda a:struct.unpack('<I',g.read(a,4))[0]
coll=rd(0x01191378)
sets=[rd(coll+4+i*4) for i in range(9)]
print('collection',hex(coll),[hex(s) for s in sets])
regions=[(coll,0x80)]
for s in set(sets):
    if not s: continue
    regions.append((s,0x100))
    b,e=rd(s+0x4c),rd(s+0x50)
    if b and e>b and e-b<0x100000: regions.append((b,e-b))
print('regions',len(regions), sum(n for _,n in regions))
def snap(): return [g.read(a,n) for a,n in regions]
jc2ctl.mod('p1 at 896 230 -2038'); time.sleep(8); A=snap(); time.sleep(3); A2=snap()
jc2ctl.mod('p1 at 1632 242 1248'); time.sleep(12); B=snap()
cnt=0
for (a,n),x,x2,y in zip(regions,A,A2,B):
    for o in range(0,n-3,4):
        if x[o:o+4]==x2[o:o+4] and x[o:o+4]!=y[o:o+4]:
            cnt+=1
            if cnt<=60: print('%08X (+%X of %08X) %08X -> %08X'%(a+o,o,a,struct.unpack_from('<I',x,o)[0],struct.unpack_from('<I',y,o)[0]))
print('changed',cnt)
