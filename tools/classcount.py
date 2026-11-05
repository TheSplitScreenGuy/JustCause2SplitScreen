# Counts live objects of every RTTI class (dwords equal to a known vtable, 4-aligned) and saves
# the counts; with two saved snapshots, prints the classes whose counts changed most.
#   python classcount.py save <name>        (needs numpy, run without -I)
#   python classcount.py diff <a> <b> [n]
import json, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'scans')


def save(name):
    from procmem import Game
    g = Game()
    vt = {int(k, 16): v for k, v in json.load(open(os.path.join(HERE, 'rtti_map.json'))).items()}
    keys = np.array(sorted(vt), dtype=np.uint32)
    counts = np.zeros(len(keys), dtype=np.int64)
    for base, size in g.regions():
        if base < 0x01400000 and base >= 0x00400000: continue   # the exe image itself
        try:
            raw = g.read(base, size)
        except Exception:
            continue
        a = np.frombuffer(raw[:len(raw) // 4 * 4], dtype=np.uint32)
        a = a[(a >= keys[0]) & (a <= keys[-1])]
        if not len(a): continue
        idx = np.searchsorted(keys, a)
        idx[idx >= len(keys)] = len(keys) - 1
        hit = keys[idx] == a
        np.add.at(counts, idx[hit], 1)
    result = {vt[int(k)]: int(c) for k, c in zip(keys, counts) if c}
    os.makedirs(OUT, exist_ok=True)
    json.dump(result, open(os.path.join(OUT, 'classes_%s.json' % name), 'w'), indent=0)
    print('saved', len(result), 'classes')


def diff(a, b, n):
    A = json.load(open(os.path.join(OUT, 'classes_%s.json' % a)))
    B = json.load(open(os.path.join(OUT, 'classes_%s.json' % b)))
    rows = [(B.get(k, 0) - A.get(k, 0), k, A.get(k, 0), B.get(k, 0)) for k in set(A) | set(B)]
    rows.sort(key=lambda r: -abs(r[0]))
    for d, k, x, y in rows[:n]:
        print('%7d  %6d -> %6d  %s' % (d, x, y, k))


if __name__ == '__main__':
    if sys.argv[1] == 'save': save(sys.argv[2])
    else: diff(sys.argv[2], sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else 40)
