# Counts live objects per vtable: python -I vtscan.py <hex vtable> [...]
# Prints the count and the first few addresses for each value.
import sys
sys.path.insert(0, __import__('os').path.dirname(__file__))
from procmem import Game

values = [int(v, 16) for v in sys.argv[1:]]
found = Game().find_dwords(values, limit=5000)
for v in values:
    addrs = found[v]
    print('%08X: %5d  %s' % (v, len(addrs), ' '.join('%08X' % a for a in addrs[:6])))
