# The game's file I/O queue ([0x012A1448], 4 priority queues at +0x4C, 0x14 apart): the vehicle
# type loader (FUN_005A5D50) waits until all four are empty (FUN_004D63E0). Samples how often
# each is non-empty and prints names found in queued items.
#   python -I ioqueue.py [samples]
import os, re, sys, time, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from procmem import Game

g = Game()
n = int(sys.argv[1]) if len(sys.argv) > 1 else 200
q = g.u32(0x012A1448)
busy = collections.Counter()
names = collections.Counter()
for i in range(n):
    any_busy = False
    for k in range(4):
        d = g.read(q + 0x4C + k * 0x14, 0x14)
        w = [int.from_bytes(d[j:j + 4], 'little') for j in range(0, 0x14, 4)]
        if w[0]:
            busy[k] += 1
            any_busy = True
            if i % 20 == 0:
                # look for printable file names near the queued items
                for p in w:
                    if 0x02000000 < p < 0x7FFF0000:
                        blob = g.read(p, 0x200) or b''
                        for m in re.finditer(rb'[\w\\/.]{6,}\.(?:ee|bl|nl|fl|dat|rbm|dds|blz|eez|lod)', blob):
                            names[m.group(0).decode(errors='replace')] += 1
    busy['any'] += any_busy
    time.sleep(0.01)
print('busy samples of %d:' % n, dict(busy))
print('w of queue 0..3 now:', [' '.join('%08X' % int.from_bytes(g.read(q + 0x4C + k * 0x14 + j, 4), 'little') for j in range(0, 0x14, 4)) for k in range(4)])
for name, c in names.most_common(15):
    print('  ', c, name)
