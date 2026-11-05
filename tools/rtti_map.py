# Builds a vtable -> class name map for every MSVC RTTI class in JustCause2.exe and saves it
# as tools\rtti_map.json (used by objdump.py).
# usage: python -I rtti_map.py <exe>
import json, os, re, struct, sys

data = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', data, 0x3C)[0]
nsec = struct.unpack_from('<H', data, pe + 6)[0]
opt = struct.unpack_from('<H', data, pe + 20)[0]
base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
sections = []
for i in range(nsec):
    o = pe + 24 + opt + i * 40
    vsize, va, rawsize, raw = struct.unpack_from('<IIII', data, o + 8)
    sections.append((va, vsize, raw, rawsize))


def va_to_off(v):
    rva = v - base
    for va, vs, raw, rs in sections:
        if va <= rva < va + max(vs, rs) and rva - va < rs:
            return raw + rva - va
    return None


def off_to_va(off):
    for va, vs, raw, rs in sections:
        if raw <= off < raw + rs:
            return base + va + off - raw
    return None


# Type descriptors: name string 8 bytes in.
tds = {}
for m in re.finditer(rb'\.\?A[VU]([A-Za-z0-9_@?$<>,]+?)@@\x00', data):
    td = off_to_va(m.start() - 8)
    if td:
        tds[td] = m.group(1).decode(errors='replace')

# Complete object locators: signature 0, offset, cdOffset, pTypeDescriptor, pClassHierarchy.
cols = {}
for off in range(0, len(data) - 20, 4):
    td = struct.unpack_from('<I', data, off + 12)[0]
    if td in tds and struct.unpack_from('<I', data, off)[0] == 0:
        col = off_to_va(off)
        if col:
            cols[col] = (tds[td], struct.unpack_from('<I', data, off + 4)[0])

# vtables: the dword before a vtable points at its COL.
vtables = {}
for off in range(0, len(data) - 4, 4):
    v = struct.unpack_from('<I', data, off)[0]
    if v in cols:
        vt = off_to_va(off + 4)
        name, this_off = cols[v]
        vtables['%08X' % vt] = name if this_off == 0 else '%s(+%X)' % (name, this_off)

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'rtti_map.json')
json.dump(vtables, open(out, 'w'), indent=0, sort_keys=True)
print('%d classes, %d vtables -> %s' % (len(tds), len(vtables), out))
