# Finds a class's vtables from its RTTI name in the exe file (no ASLR, base 0x400000).
#   python -I tools\rtti.py CChallenge@NMissionSystem [more names...]
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gamedir import find_game_dir

EXE = os.path.join(find_game_dir(), 'JustCause2.exe')
d = open(EXE, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3C)[0]
n = struct.unpack_from('<H', d, pe + 6)[0]
opt = struct.unpack_from('<H', d, pe + 20)[0]
secs = [struct.unpack_from('<8sIIII', d, pe + 24 + opt + 40 * i) for i in range(n)]


def va(off):
    for _, vs, vaddr, rs, rp in secs:
        if rp <= off < rp + rs:
            return 0x400000 + vaddr + off - rp


def off_of(v):
    for _, vs, vaddr, rs, rp in secs:
        if 0x400000 + vaddr <= v < 0x400000 + vaddr + rs:
            return rp + v - 0x400000 - vaddr


def refs(value):
    pat = struct.pack('<I', value)
    i = d.find(pat)
    while i >= 0:
        if i % 4 == 0:
            yield i
        i = d.find(pat, i + 1)


for name in sys.argv[1:]:
    i = d.find(b'.?AV' + name.encode() + b'@@\0')
    if i < 0:
        print(name, 'not found')
        continue
    td = va(i) - 8
    for r in refs(td):
        col = va(r) - 12  # COL: sig, offset, cdOffset, typeDescriptor
        if struct.unpack_from('<I', d, r - 12)[0] != 0:
            continue
        offset = struct.unpack_from('<I', d, r - 8)[0]
        for r2 in refs(col):
            print('%s vtable %08X (offset %d)' % (name, va(r2) + 4, offset))
