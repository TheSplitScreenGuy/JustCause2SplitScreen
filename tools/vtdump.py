# Prints vtable entries side by side (from the exe file): python -I vtdump.py <exe> <hexvtable> [...]
# Stops each table at the first entry that does not point into .text.
import struct, sys

data = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', data, 0x3C)[0]
nsec = struct.unpack_from('<H', data, pe + 6)[0]
opt = struct.unpack_from('<H', data, pe + 20)[0]
base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
secs = []
for i in range(nsec):
    o = pe + 24 + opt + i * 40
    name = data[o:o + 8].rstrip(b'\0').decode()
    vsize, va, rawsize, raw = struct.unpack_from('<IIII', data, o + 8)
    secs.append((name, base + va, vsize, raw))
text = next(s for s in secs if s[0] == '.text')


def read_u32(v):
    for name, va, vs, raw in secs:
        if va <= v < va + vs:
            return struct.unpack_from('<I', data, raw + v - va)[0]
    return None


tables = []
for a in sys.argv[2:]:
    vt = int(a, 16)
    entries = []
    for i in range(400):
        e = read_u32(vt + i * 4)
        if e is None or not (text[1] <= e < text[1] + text[2]):
            break
        entries.append(e)
    tables.append(entries)
print('slot ' + ' '.join('%-9s' % a for a in sys.argv[2:]))
for i in range(max(len(t) for t in tables)):
    print('%4d ' % i + ' '.join('%08X ' % t[i] if i < len(t) else '         ' for t in tables))
