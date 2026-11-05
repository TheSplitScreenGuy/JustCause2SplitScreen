import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import sys,struct,time,subprocess
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl
mgr=int(sys.argv[1],16)
g=Game()
def snap(): return g.read(mgr,0x414C)
def mod(c): print(jc2ctl.mod(c))
mod('p1 at 896 230 -2038'); time.sleep(5)
A=snap(); time.sleep(2); A2=snap()
mod('p1 at 896 230 -2078'); time.sleep(5)
B=snap()
noise={i for i in range(0,0x414C,4) if A[i:i+4]!=A2[i:i+4]}
sig=[i for i in range(0,0x414C,4) if A[i:i+4]!=B[i:i+4] and i not in noise]
print('noise',len(noise),'signal',len(sig))
for i in sig[:80]:
    print('+%04X %08X -> %08X'%(i,struct.unpack_from('<I',A,i)[0],struct.unpack_from('<I',B,i)[0]))
