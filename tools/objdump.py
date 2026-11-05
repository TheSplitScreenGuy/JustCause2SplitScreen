# Dumps an object in the running game, labelling vtables, pointers to objects (by class name),
# floats and small ints.
# usage: python -I objdump.py <hexaddr> [hexsize]
import json, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from procmem import Game

names = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'rtti_map.json')))
g = Game()


def describe(v):
    key = '%08X' % v
    if key in names:
        return 'vtable ' + names[key]
    if 0x10000 <= v < 0x7FFF0000:
        vt = g.u32(v)
        if vt is not None and '%08X' % vt in names:
            return '-> %s' % names['%08X' % vt]
    f = struct.unpack('<f', struct.pack('<I', v))[0]
    if v and 1e-4 < abs(f) < 1e6:
        return 'float %.4f' % f
    if v < 0x10000:
        return 'int %d' % v
    return ''


addr = int(sys.argv[1], 16)
size = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x200
b = g.read(addr, size)
if not b:
    raise SystemExit('unreadable')
for off in range(0, size, 4):
    v = struct.unpack_from('<I', b, off)[0]
    d = describe(v)
    if d or v:
        print('+%04X %08X  %s' % (off, v, d))
