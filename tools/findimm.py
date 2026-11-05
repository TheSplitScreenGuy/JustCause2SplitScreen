# Finds code that uses a 32-bit immediate (e.g. a vtable address being stored by a constructor).
# usage: python -I findimm.py <exe> <hexvalue> [...]
import struct, sys

data = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', data, 0x3C)[0]
nsec = struct.unpack_from('<H', data, pe + 6)[0]
opt = struct.unpack_from('<H', data, pe + 20)[0]
base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
for i in range(nsec):
    o = pe + 24 + opt + i * 40
    if data[o:o + 5] == b'.text':
        vsize, va, rawsize, raw = struct.unpack_from('<IIII', data, o + 8)
        text = (base + va, raw, rawsize)
for a in sys.argv[2:]:
    pat = struct.pack('<I', int(a, 16))
    va, raw, size = text
    seg = data[raw:raw + size]
    hits = []
    i = seg.find(pat)
    while i >= 0:
        hits.append('%08X(%s)' % (va + i, seg[max(0, i - 2):i].hex()))
        i = seg.find(pat, i + 1)
    print(a, ' '.join(hits))
