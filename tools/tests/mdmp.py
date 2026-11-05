# Minimal x86 minidump reader: exception, registers, and stack values that point into modules.
import struct, sys

data = open(sys.argv[1], 'rb').read()
assert data[:4] == b'MDMP'
nstreams, dir_rva = struct.unpack_from('<II', data, 8)
streams = {}
for i in range(nstreams):
    t, size, rva = struct.unpack_from('<III', data, dir_rva + i * 12)
    streams.setdefault(t, (size, rva))

def mdstr(rva):
    n, = struct.unpack_from('<I', data, rva)
    return data[rva + 4: rva + 4 + n].decode('utf-16le')

mods = []
if 4 in streams:
    _, rva = streams[4]
    n, = struct.unpack_from('<I', data, rva)
    for i in range(n):
        off = rva + 4 + i * 108
        base, size = struct.unpack_from('<QI', data, off)
        name_rva, = struct.unpack_from('<I', data, off + 20)
        mods.append((base, size, mdstr(name_rva).split('\\')[-1]))

ranges = []
if 5 in streams:
    _, rva = streams[5]
    n, = struct.unpack_from('<I', data, rva)
    for i in range(n):
        start, dsize, drva = struct.unpack_from('<QII', data, rva + 4 + i * 16)
        ranges.append((start, dsize, drva))
if 9 in streams:
    _, rva = streams[9]
    n, base_rva = struct.unpack_from('<QQ', data, rva)
    cur = base_rva
    for i in range(n):
        start, size = struct.unpack_from('<QQ', data, rva + 16 + i * 16)
        ranges.append((start, size, cur))
        cur += size

def read(addr, n):
    for start, size, rva in ranges:
        if start <= addr and addr + n <= start + size:
            return data[rva + addr - start: rva + addr - start + n]
    return None

def sym(addr):
    for base, size, name in mods:
        if base <= addr < base + size:
            return '%s+0x%X' % (name, addr - base)
    return None

_, rva = streams[6]
tid, = struct.unpack_from('<I', data, rva)
code, flags, rec, addr, nparams = struct.unpack_from('<IIQQI', data, rva + 8)
params = struct.unpack_from('<2Q', data, rva + 8 + 32)
ctx_size, ctx_rva = struct.unpack_from('<II', data, rva + 8 + 152)
print('thread %d exception 0x%08X at 0x%X (%s) params %s' % (tid, code, addr, sym(addr), [hex(p) for p in params[:nparams]]))
regs = dict(zip(['edi', 'esi', 'ebx', 'edx', 'ecx', 'eax', 'ebp', 'eip', 'cs', 'eflags', 'esp'],
                struct.unpack_from('<11I', data, ctx_rva + 0x9C)))
for k in ['eax', 'ebx', 'ecx', 'edx', 'esi', 'edi', 'ebp', 'esp', 'eip']:
    print('  %s = %08X %s' % (k, regs[k], sym(regs[k]) or ''))

print('stack:')
esp = regs['esp']
for i in range(400):
    b = read(esp + i * 4, 4)
    if b is None:
        break
    v, = struct.unpack('<I', b)
    s = sym(v)
    if s and not s.startswith('ntdll') or (s and i < 40):
        print('  [esp+%03X] %08X %s' % (i * 4, v, s))

print('modules of interest:')
for base, size, name in mods:
    if name.lower() in ('dinput8.dll', 'd3d10.dll', 'd3d10core.dll', 'dxgi.dll', 'justcause2.exe', 'gameoverlayrenderer.dll', 'xinput1_3.dll'):
        print('  %08X %08X %s' % (base, size, name))

def hexat(a, n):
    b = read(a, n)
    return b.hex(' ') if b else 'unreadable'
print('before ret 50C05C4C:', hexat(0x50C05C4C - 24, 24))
print('dxgi Present 63D20B90:', hexat(0x63D20B90, 16))
print('vtable[0..14]:', [hex(struct.unpack('<I', read(0x63CB49C0 + i*4, 4))[0]) for i in range(14)] if read(0x63CB49C0, 4) else 'unreadable')
