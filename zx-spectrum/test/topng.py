import sys, zlib, struct, glob, os
W, H = 320, 256
def png(path, raw):
    rows = b''.join(b'\x00' + raw[y*W*4:(y+1)*W*4] for y in range(H))
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))
for f in sorted(glob.glob(os.path.join(sys.argv[1], '*.rgba'))):
    png(f[:-5] + '.png', open(f, 'rb').read()); os.remove(f)
