# Crops parts of a screenshot, scales up, writes a BMP-less PNG without PIL:
#   crop.py in.png out.png x0 y0 x1 y1 [scale] ... (coordinates as fractions of the image)
import struct, sys, zlib


def read_png(path):
    d = open(path, 'rb').read()
    o, idat = 8, b''
    while o < len(d):
        n, t = struct.unpack('>I4s', d[o:o + 8])
        if t == b'IHDR':
            w, h, depth, ctype = struct.unpack('>IIBB', d[o + 8:o + 18])
        elif t == b'IDAT':
            idat += d[o + 8:o + 8 + n]
        o += 12 + n
    bpp = {2: 3, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    rows, prev, stride = [], bytearray(w * bpp), w * bpp
    for y in range(h):
        p = y * (stride + 1)
        f, line = raw[p], bytearray(raw[p + 1:p + 1 + stride])
        if f == 1:
            for i in range(bpp, stride): line[i] = (line[i] + line[i - bpp]) & 255
        elif f == 2:
            for i in range(stride): line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                line[i] = (line[i] + (((line[i - bpp] if i >= bpp else 0) + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b, c = prev[i], prev[i - bpp] if i >= bpp else 0
                q = a + b - c
                pa, pb, pc = abs(q - a), abs(q - b), abs(q - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return w, h, bpp, rows


def write_png(path, w, h, rows):
    def chunk(t, c): return struct.pack('>I', len(c)) + t + c + struct.pack('>I', zlib.crc32(t + c))
    raw = b''.join(b'\0' + bytes(r) for r in rows)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


w, h, bpp, rows = read_png(sys.argv[1])
x0, y0, x1, y1 = [float(v) for v in sys.argv[3:7]]
s = int(sys.argv[7]) if len(sys.argv) > 7 else 2
X0, X1, Y0, Y1 = int(x0 * w), int(x1 * w), int(y0 * h), int(y1 * h)
out = []
for y in range(Y0, Y1):
    line = bytearray()
    for x in range(X0, X1):
        line += rows[y][x * bpp:x * bpp + 3] * s
    out.extend([line] * s)
write_png(sys.argv[2], (X1 - X0) * s, (Y1 - Y0) * s, out)
print(w, h)
