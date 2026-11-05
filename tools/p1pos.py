# Prints Player 1's character position (and the camera forward), for places.txt.
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from procmem import Game

g = Game()
manager = g.u32(0x0118FB24)
player = g.u32(manager + 0x3570)
character = g.u32(player + 0x48)
print('p1 %.2f %.2f %.2f' % g.floats(character + 0x18C, 3))
