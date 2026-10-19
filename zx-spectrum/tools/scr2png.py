"""Render a 6912-byte Spectrum screen (from a memory image at a given address) to PNG."""
import struct, sys, zlib

PAL = [(0, 0, 0), (0, 0, 0xD7), (0xD7, 0, 0), (0xD7, 0, 0xD7), (0, 0xD7, 0), (0, 0xD7, 0xD7), (0xD7, 0xD7, 0), (0xD7, 0xD7, 0xD7)]


def render(scr, path):
    rows = []
    for y in range(192):
        row = bytearray([0])
        pa = ((y & 0xC0) << 5) | ((y & 7) << 8) | ((y & 0x38) << 2)
        for cx in range(32):
            p, a = scr[pa + cx], scr[6144 + (y >> 3) * 32 + cx]
            br = 0x28 if a & 0x40 else 0
            ink = [min(255, c + br) if c else 0 for c in PAL[a & 7]]
            pap = [min(255, c + br) if c else 0 for c in PAL[(a >> 3) & 7]]
            for b in range(8):
                row += bytes(ink if p & (0x80 >> b) else pap)
        rows.append(bytes(row))

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 256, 192, 8, 2, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(b''.join(rows))) + chunk(b'IEND', b''))


if __name__ == '__main__':
    m = open(sys.argv[1], 'rb').read()
    render(m[int(sys.argv[2], 16):], sys.argv[3])
