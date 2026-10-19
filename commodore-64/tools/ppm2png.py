#!/usr/bin/env python3
"""Convert P6 PPM files to PNG (stdlib only), doubling pixels for readability."""
import struct, sys, zlib

def convert(src, dst, scale=2):
    data = open(src, 'rb').read()
    parts = data.split(b'\n', 1)
    _, w, h, _ = parts[0].split()
    w, h = int(w), int(h)
    px = parts[1]
    raw = bytearray()
    for y in range(h):
        row = px[y * w * 3:(y + 1) * w * 3]
        line = bytearray()
        for x in range(w):
            line += row[x * 3:x * 3 + 3] * scale
        for _ in range(scale):
            raw += b'\x00' + line
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w * scale, h * scale, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(raw), 6)) + chunk(b'IEND', b'')
    open(dst, 'wb').write(png)

for f in sys.argv[1:]:
    convert(f, f[:-4] + '.png')
