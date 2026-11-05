import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import sys,struct,time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl
g=Game()
st=jc2ctl.mod('p2'); import re
ch=int(re.search(r'player 2 ([0-9A-F]+)',st).group(1),16); print('char',hex(ch))
N=0x800
A=g.read(ch,N); time.sleep(1); A2=g.read(ch,N)
jc2ctl.mod('p2 kill'); time.sleep(8)
st2=jc2ctl.mod('p2'); ch2=int(re.search(r'player 2 ([0-9A-F]+)',st2).group(1),16); print('char after',hex(ch2))
B=g.read(ch2,N); time.sleep(1); B2=g.read(ch2,N)
for o in range(0,N,4):
    a,a2,b,b2=(x[o:o+4] for x in (A,A2,B,B2))
    if a==a2 and b==b2 and a!=b:
        ia,ib=struct.unpack('<I',a)[0],struct.unpack('<I',b)[0]
        fa,fb=struct.unpack('<f',a)[0],struct.unpack('<f',b)[0]
        if 0x10000000<=ia<0x40000000 and 0x10000000<=ib<0x40000000: continue  # pointer changes
        print('+%03X %08X (%.3g) -> %08X (%.3g)'%(o,ia,fa,ib,fb))
