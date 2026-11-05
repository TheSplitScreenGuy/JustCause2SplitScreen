# The spawn manager's spawn-point queue ([0x01191918]: +0xB8 current point, +0xCC queue size,
# +0xD1 spawning on; point +0x68 position, +0xA4 state 0 none 1-3 loading 4 spawned 5 failed,
# +0xAC retries, +0x84 despawn distance squared), sampled a few times.
#   python -I spawnqueue.py [samples]
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from procmem import Game

g = Game()
count = int(sys.argv[1]) if len(sys.argv) > 1 else 4
m = g.u32(0x01191918)
for t in range(count):
    cur = g.u32(m + 0xB8)
    s = 'current %08X queue %d on %d' % (cur, g.u32(m + 0xCC), g.read(m + 0xD1, 1)[0])
    if cur:
        x, y, z = g.floats(cur + 0x68, 3)
        s += ' | at %.0f %.0f %.0f state %d retries %d despawn %.3g' % (
            x, y, z, g.u32(cur + 0xA4), g.u32(cur + 0xAC), g.floats(cur + 0x84, 1)[0])
    print(s)
    time.sleep(1)
