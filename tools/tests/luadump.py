# Finds the compiled Lua 5.0 chunks in the game's memory (HUD scripts), writes a listing per chunk.
#   luadump.py <out dir> [word that must be in the chunk]
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # project folder
import os, re, struct, sys
sys.path.insert(0, os.path.join(_ROOT, r'tools'))
from procmem import Game

OPS = 'MOVE LOADK LOADBOOL LOADNIL GETUPVAL GETGLOBAL GETTABLE SETGLOBAL SETUPVAL SETTABLE NEWTABLE SELF ADD SUB MUL DIV POW UNM NOT CONCAT JMP EQ LT LE TEST CALL TAILCALL RETURN FORLOOP TFORLOOP TFORPREP SETLIST SETLISTO CLOSE CLOSURE'.split()


class R:
    def __init__(s, d, o): s.d, s.o = d, o
    def u8(s): s.o += 1; return s.d[s.o - 1]
    def i32(s): s.o += 4; return struct.unpack_from('<i', s.d, s.o - 4)[0]
    def f(s): s.o += s.nsize; return struct.unpack_from('<f' if s.nsize == 4 else '<d', s.d, s.o - s.nsize)[0]
    def str(s):
        n = s.i32()
        if n == 0: return None
        b = s.d[s.o:s.o + n - 1]; s.o += n
        return b.decode('latin1')


def func(r, out, name, depth=0):
    source = r.str(); line = r.i32(); nups = r.u8(); params = r.u8(); var = r.u8(); stack = r.u8()
    n = r.i32(); r.o += 4 * n                     # lines
    n = r.i32(); locs = []
    for i in range(n):
        locs.append(r.str()); r.i32(); r.i32()
    n = r.i32(); ups = [r.str() for i in range(n)]
    n = r.i32(); k = []
    for i in range(n):
        t = r.u8()
        if t == 3: k.append(r.f())
        elif t == 4: k.append(r.str())
        elif t == 0: k.append(None)
        else: raise ValueError('constant type %d' % t)
    n = r.i32(); protos = [lambda: None] * 0
    start = len(out)
    out.append('\nfunction %s params %d ups %s locals %s' % (name, params, ups, locs[:12]))
    subs = []
    for i in range(n):
        p = []
        func(r, p, '%s.%d' % (name, i), depth + 1)
        subs.append(p)
    n = r.i32()
    names = {}
    for pc in range(n):
        v = struct.unpack_from('<I', r.d, r.o + 4 * pc)[0]
        op = v & 63; c = (v >> 6) & 511; b = (v >> 15) & 511; a = v >> 24 if False else (v >> 24)
        bx = v >> 6 & 0x3FFFF
        o = OPS[op] if op < len(OPS) else '?%d' % op
        rk = lambda x: repr(k[x - 250]) if x >= 250 and x - 250 < len(k) else 'r%d' % x
        if o in ('LOADK', 'GETGLOBAL', 'SETGLOBAL'): t = 'r%d %r' % (a, k[bx] if bx < len(k) else '?')
        elif o in ('GETTABLE', 'SELF'): t = 'r%d r%d %s' % (a, b, rk(c))
        elif o == 'SETTABLE': t = 'r%d %s %s' % (a, rk(b), rk(c))
        elif o in ('ADD', 'SUB', 'MUL', 'DIV', 'POW', 'EQ', 'LT', 'LE'): t = 'r%d %s %s' % (a, rk(b), rk(c))
        elif o in ('JMP', 'FORLOOP', 'TFORPREP'): t = '-> %d' % (pc + 1 + (bx - 131071))
        elif o == 'CLOSURE':
            t = 'r%d proto %d' % (a, bx)
        else: t = 'r%d %d %d' % (a, b, c)
        out.append('%4d %-9s %s' % (pc, o, t))
    r.o += 4 * n
    for p in subs: out.extend(p)


g = Game()
outdir = sys.argv[1]
word = sys.argv[2].encode() if len(sys.argv) > 2 else None
os.makedirs(outdir, exist_ok=True)
count = 0
seen = set()
for base, size in g.regions():
    if 0x400000 <= base < 0x1400000: continue
    d = g.read(base, size)
    if not d: continue
    i = d.find(b'\x1bLuaP')
    while i >= 0:
        r = R(d, i + 4)
        ver = r.u8(); end = r.u8(); sint = r.u8(); ssize = r.u8(); sins = r.u8(); r.o += 4; r.nsize = r.u8()
        r.o += r.nsize  # test number
        out = []
        try:
            func(r, out, 'main')
            text = '\n'.join(out)
            key = hash(text)
            if key not in seen and (not word or word.decode() in text):
                seen.add(key)
                p = os.path.join(outdir, 'chunk%03d.txt' % count)
                open(p, 'w', encoding='latin1').write('; at %08X\n' % (base + i) + text)
                print(p, len(out), 'lines')
                count += 1
        except Exception as e:
            print('%08X failed: %r' % (base + i, e))
        i = d.find(b'\x1bLuaP', i + 1)
