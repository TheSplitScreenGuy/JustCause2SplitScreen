# P2's view of the deck at Karl's hideout (km02.karlhideout) flickers / is missing while P1 is far
# (user report, 2026-10-28). Puts P2 at the deck looking out to sea, P1 far away (or next to P2
# with "near"), then samples a deck pixel in P2's half over a burst of screenshots: D = deck drawn,
# . = sea where the deck should be.
#   python -I tools\tests\deckflicker.py [far|near|stay] [shots]
import os, struct, sys, time, zlib
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import jc2ctl


def png_pixel(path, fx, fy):
    """RGB at a fraction of the width/height of an 8-bit RGB/RGBA PNG (stdlib only)."""
    data = open(path, 'rb').read()
    pos, idat = 8, b''
    while pos < len(data):
        n, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b'IHDR':
            w, h, depth, color = struct.unpack('>IIBB', body[:10])
        elif kind == b'IDAT':
            idat += body
        pos += 12 + n
    bpp = 4 if color == 6 else 3
    raw = zlib.decompress(idat)
    stride = w * bpp
    x, y = int(w * fx), int(h * fy)
    prev = bytearray(stride)
    for row in range(y + 1):
        f = raw[row * (stride + 1)]
        line = bytearray(raw[row * (stride + 1) + 1:(row + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
    return tuple(prev[x * bpp:x * bpp + 3])

where = sys.argv[1] if len(sys.argv) > 1 else 'far'
count = int(sys.argv[2]) if len(sys.argv) > 2 else 12
shots = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'shots')
P2 = (890.4, 223.4, -2027.7)
FACE = (0.99, 0.1, 0.137)       # p2 face takes the reverse of the view direction
P1_FAR = (3000.0, 889.0, -6000.0)


def mod(cmd):
    jc2ctl.focus()
    return jc2ctl.mod(cmd)


if not mod('p2').startswith('player 2'):
    mod('p2 join')
    time.sleep(5)
mod('god on')
if where != 'stay':
    mod('p2 at %.1f %.1f %.1f' % P2)
    time.sleep(6)
    if where == 'near':
        # A long jump slides P1 (and brings P2 along): a second, short hop puts both at the deck.
        mod('p1top2')
        time.sleep(10)
        mod('p2 at %.1f %.1f %.1f' % P2)
        time.sleep(5)
        mod('p1top2')
    else:
        mod('p1 at %.1f %.1f %.1f' % P1_FAR)
    time.sleep(15)
mod('p2 face %.3f %.3f %.3f' % FACE)
time.sleep(2)
pattern = ''
path = os.path.abspath(os.path.join(shots, 'deck_burst.png'))
for i in range(count):
    jc2ctl.mod('screenshot ' + path)
    r, g, b = png_pixel(path, 0.6, 0.87)
    pattern += 'D' if r > 60 else '.'
    time.sleep(0.2)
print(where, pattern, '(%d of %d drawn)' % (pattern.count('D'), count))
