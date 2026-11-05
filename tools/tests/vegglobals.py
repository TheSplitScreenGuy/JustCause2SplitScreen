import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import sys,struct,time,re
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl
g=Game()
refs=[]
for l in open(os.path.join(_ROOT, r'tools\ghidra\vegrefs.txt')):
    m=re.match(r'([0-9a-f]{8}) (\S+)\s+(\d+) (.*)',l)
    if m: refs.append((int(m.group(1),16),m.group(2),m.group(4)))
# state ranges swapped per copy (skip)
state=[(0x011902F0,4),(0x01191378,4),(0x0108B8F8,0x38),(0x0108BCC8,0x28),(0x0108C454,4),(0x01381D40,8),(0x01381F78,4),(0x01381F88,4),(0x01381F94,4),(0x01381FB0,0x40),(0x01381FF0,0x68),(0x013820F0,0xC),(0x0138AAAC,4),(0x0138AAB0,0x1690),(0x0138C140,0xC),(0x0138C1A8,0x28),(0x012B4478,8),(0x012B448C,4)]
def swapped(a): return any(s<=a<s+n for s,n in state)
lo=0x01030000; hi=0x01400000
def snap(): return g.read(lo,hi-lo)
def mod(c): jc2ctl.mod(c)
mod('p1 at 896 230 -2038'); time.sleep(8); A=snap(); time.sleep(3); A2=snap()
mod('p1 at 1632 242 1248'); time.sleep(12); B=snap()
for a,rw,fns in refs:
    if not (lo<=a<hi) or swapped(a): continue
    o=a-lo
    va,va2,vb=A[o:o+4],A2[o:o+4],B[o:o+4]
    if va==va2 and va!=vb:
        print('%08X %s %08X -> %08X  %s'%(a,rw,struct.unpack('<I',va)[0],struct.unpack('<I',vb)[0],fns[:80]))
