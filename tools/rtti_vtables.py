# Finds the vtable address(es) of MSVC RTTI classes in JustCause2.exe.
# usage: python -I rtti_vtables.py <exe> <substring | =ExactName> [...]
#
# Chain: type descriptor (".?AV<name>@@" string, 8 bytes in) <- complete object
# locator (+12 points at the descriptor) <- vtable[-1] points at the locator.
import re, struct, sys

data = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', data, 0x3C)[0]
nsec = struct.unpack_from('<H', data, pe + 6)[0]
opt = struct.unpack_from('<H', data, pe + 20)[0]
base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
sections = []
for i in range(nsec):
    o = pe + 24 + opt + i * 40
    vsize, va, rawsize, raw = struct.unpack_from('<IIII', data, o + 8)
    sections.append((va, max(vsize, rawsize), raw, rawsize))

def off_to_va(off):
    for va, vs, raw, rs in sections:
        if raw <= off < raw + rs:
            return base + va + off - raw
    return None

def find_dwords(value):
    pat = re.escape(struct.pack('<I', value))
    return [m.start() for m in re.finditer(pat, data)]

for want in sys.argv[2:]:
    if want.startswith('='):
        pattern = rb'\.\?A[VU]' + re.escape(want[1:].encode()) + rb'@@\x00'
    else:
        pattern = rb'\.\?A[VU][A-Za-z0-9_@]*' + re.escape(want.encode()) + rb'[A-Za-z0-9_@]*@@\x00'
    for m in re.finditer(pattern, data):
        name = m.group()[:-1].decode()
        td = off_to_va(m.start() - 8)
        vtables = []
        for col_ref in find_dwords(td):
            col = off_to_va(col_ref - 12)
            sig = struct.unpack_from('<I', data, col_ref - 12)[0]
            if sig != 0:
                continue
            for vt_ref in find_dwords(col):
                vtables.append(off_to_va(vt_ref) + 4)
        print('%-60s td %08X  vtables %s' % (name, td, ' '.join('%08X' % v for v in sorted(set(vtables)))))
