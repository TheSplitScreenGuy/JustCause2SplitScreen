# Writes "forward" into the possessed NPC's driver input context (AI avatar +0x88) for a few seconds
# and watches whether the NPC moves.
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import struct, sys, time
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game
import jc2ctl

g = Game(write=True)
ctx = int(sys.argv[1], 16)
npc = int(sys.argv[2], 16)
begin, end = g.u32(ctx + 0x10), g.u32(ctx + 0x14)
print('context %08X: %d actions, enabled %d' % (ctx, (end - begin) // 4, g.read(ctx + 0x20, 1)[0]))
for a in (0x1E, 0x1F, 0x20, 0x21, 0x22):
    e = g.u32(begin + a * 4)
    chain = []
    while e and len(chain) < 4:
        chain.append('%08X[%08X dev %d]' % (e, g.u32(e), g.u32(e + 12)))
        e = g.u32(e + 16)
    print('  action %02X:' % a, ' '.join(chain) or '(none)')
entry = g.u32(begin + 0x1E * 4)
if not entry:
    raise SystemExit('no entry for forward')
jc2ctl.focus()
time.sleep(0.3)
p0 = g.floats(npc + 0x18C, 3)
t_end = time.time() + 2.5
writes = 0
while time.time() < t_end:
    before = g.u32(entry)
    g.write_u32(entry, 0x03000000 | 0xFFF)
    writes += 1
    time.sleep(0.004)
after = g.u32(entry)
print('entry now %08X (last before-write %08X), %d writes' % (after, before, writes))
print('npc moved from', [round(v, 2) for v in p0], 'to', [round(v, 2) for v in g.floats(npc + 0x18C, 3)])
g.write_u32(entry, 0)
