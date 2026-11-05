# Minimal minidump reader: exception thread's registers and the code addresses on its stack
# (exe and modules), plus the module list.   python -I dmp.py <file.dmp>
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # project folder
import struct, sys
d = open(sys.argv[1], 'rb').read()
sig, ver, nstreams, rva = struct.unpack_from('<4sIII', d, 0)
streams = {}
for i in range(nstreams):
    t, size, r = struct.unpack_from('<III', d, rva + 12 * i)
    streams.setdefault(t, (size, r))
mem = []  # (start, data offset, size)
if 9 in streams:
    size, r = streams[9]
    n, base = struct.unpack_from('<QQ', d, r)
    off = base
    for i in range(n):
        start, sz = struct.unpack_from('<QQ', d, r + 16 + 16 * i)
        mem.append((start, off, sz)); off += sz
if 5 in streams:
    size, r = streams[5]
    n = struct.unpack_from('<I', d, r)[0]
    for i in range(n):
        start, sz, dr = struct.unpack_from('<QII', d, r + 4 + 16 * i)
        mem.append((start, dr, sz))
def read(addr, n):
    for start, off, sz in mem:
        if start <= addr and addr + n <= start + sz:
            return d[off + addr - start: off + addr - start + n]
    return None
mods = []
size, r = streams[4]
n = struct.unpack_from('<I', d, r)[0]
for i in range(n):
    o = r + 4 + 108 * i
    base, sz = struct.unpack_from('<QI', d, o)
    nr = struct.unpack_from('<I', d, o + 20)[0]
    ln = struct.unpack_from('<I', d, nr)[0]
    name = d[nr + 4: nr + 4 + ln].decode('utf-16le').split('\\')[-1]
    mods.append((base, sz, name))
def mod(a):
    for b, s, nm in mods:
        if b <= a < b + s: return '%s+%X' % (nm, a - b)
    return None
size, r = streams[6]
tid = struct.unpack_from('<I', d, r)[0]
code, flags, rec, addr = struct.unpack_from('<IIQQ', d, r + 8)
nparams = struct.unpack_from('<I', d, r + 8 + 24)[0]
info = struct.unpack_from('<QQ', d, r + 8 + 32)
ctxsize, ctxrva = struct.unpack_from('<II', d, r + 8 + 152)
ctx = d[ctxrva: ctxrva + ctxsize]
# x86 CONTEXT: Edi..Eax at 0x9C.., Ebp 0xB4, Eip 0xB8, Esp 0xC4
names = ['edi', 'esi', 'ebx', 'edx', 'ecx', 'eax', 'ebp', 'eip', 'cs', 'eflags', 'esp']
regs = dict(zip(names, struct.unpack_from('<11I', ctx, 0x9C)))
print('thread %X exception %08X at %08X (%s) access %X addr %08X' % (tid, code, addr, mod(addr), info[0], info[1]))
print(' '.join('%s %08X' % (k, regs[k]) for k in names if k not in ('cs', 'eflags')))
esp = regs['esp']
stack = read(esp, 0x1000) or b''
print('stack read %d bytes' % len(stack))
for i in range(0, len(stack) - 3, 4):
    v = struct.unpack_from('<I', stack, i)[0]
    m = mod(v)
    if m and ('JustCause2' in m or 'dinput8' in m): print('  esp+%03X %08X %s' % (i, v, m))
for b, s, nm in mods:
    if 'dinput8' in nm or 'JustCause2' in nm: print('module %s %08X size %X' % (nm, b, s))
import json
names = json.load(open(os.path.join(_ROOT, r'tools\rtti_map.json')))
def dump(a, n=0x40, label=''):
    b = read(a, n)
    if not b: print(label, '%08X not in dump' % a); return
    vals = struct.unpack('<%dI' % (n // 4), b)
    print(label, '%08X:' % a, ' '.join('%08X%s' % (v, '(' + names['%08X' % v] + ')' if '%08X' % v in names else '') for v in vals))
dump(regs['ebx'], 0x40, 'mesh')
dump(regs['esi'], 0x40, 'esi')
for i in range(0, min(len(stack), 0x800) - 3, 4):
    v = struct.unpack_from('<I', stack, i)[0]
    b = read(v, 4) if 0x10000 < v < 0x7fff0000 else None
    if b:
        vt = struct.unpack('<I', b)[0]
        if '%08X' % vt in names: print('  esp+%03X -> %08X %s' % (i, v, names['%08X' % vt]))
