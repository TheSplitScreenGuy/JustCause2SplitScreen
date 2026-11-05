# Race checkpoints: the CChallengeObject's message lists and the triggers that post those messages.
# A message list = count in the low 6 bits of the first byte, message ids (dwords) from +4.
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from procmem import Game

g = Game()


def ids(lst):
    if not lst:
        return []
    b = g.read(lst, 4 + 4 * 63)
    if not b:
        return []
    return list(struct.unpack_from('<%dI' % (b[0] & 0x3F), b, 4))


o = g.find_dwords([0x00EB47C4], 4)[0x00EB47C4][0]
print('CChallengeObject %08X' % o)
names = {}
for off in (0x6C, 0x70, 0x74, 0x78):
    for m in ids(g.u32(o + off)):
        names[m] = 'obj+%X' % off
first, last = g.u32(o + 0x80), g.u32(o + 0x84)
for i, a in enumerate(range(first, last, 8)):
    for m in ids(g.u32(a + 4)):
        names.setdefault(m, 'bonus%d(%g)' % (i, g.floats(a, 1)[0]))
print('message ids:', ' '.join('%08X=%s' % kv for kv in list(names.items())[:12]), '...')

# Every dword equal to a message id, then show its surroundings' vtables.
f = g.find_dwords(list(names), 200)
for m, where in f.items():
    if where:
        print('%s %08X at %s' % (names[m], m, ' '.join('%08X' % w for w in where[:8])))
