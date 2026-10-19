#!/usr/bin/env python3
"""Convert a black-on-white logo image (24-bit BMP) into the intro's pixel-art badge.

usage: logo_from_image.py <image.bmp> [out.txt]

Writes assets/logo.txt: 192x84 characters, '#' = sprite pixel (badge fill + a 1px rim
outside the outline), '.' = transparent (the outline and lettering show the black
background through). The result can be hand-edited afterwards; mkintro.py reads it.
"""
import os
import struct
import sys

W, H = 192, 84            # 8 sprites wide x 4 sprite rows
INNER_W, INNER_H = W - 2, H - 2


def read_bmp(path):
    d = open(path, 'rb').read()
    off = struct.unpack_from('<I', d, 10)[0]
    w, h = struct.unpack_from('<ii', d, 18)
    bpp = struct.unpack_from('<H', d, 28)[0]
    assert bpp in (24, 32), 'need a 24/32-bit BMP'
    bpr = (w * bpp // 8 + 3) & ~3
    flip = h > 0
    h = abs(h)
    dark = bytearray(w * h)
    step = bpp // 8
    for y in range(h):
        row = off + (h - 1 - y if flip else y) * bpr
        for x in range(w):
            b, g, r = d[row + x * step:row + x * step + 3]
            dark[y * w + x] = (r * 299 + g * 587 + b * 114) < 128000
    return w, h, dark


def main():
    src = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), '..', 'assets', 'logo.txt')
    w, h, dark = read_bmp(src)
    xs = [x for x in range(w) if any(dark[y * w + x] for y in range(0, h, 4))]
    ys = [y for y in range(h) if any(dark[y * w + x] for x in range(0, w, 4))]
    x0, x1, y0, y1 = min(xs), max(xs) + 1, min(ys), max(ys) + 1

    # area-average into INNER_W x INNER_H cells
    ink = [[0.0] * INNER_W for _ in range(INNER_H)]
    for cy in range(INNER_H):
        sy0 = y0 + (y1 - y0) * cy // INNER_H
        sy1 = y0 + (y1 - y0) * (cy + 1) // INNER_H
        for cx in range(INNER_W):
            sx0 = x0 + (x1 - x0) * cx // INNER_W
            sx1 = x0 + (x1 - x0) * (cx + 1) // INNER_W
            tot = cnt = 0
            for yy in range(sy0, sy1, 2):
                base = yy * w
                for xx in range(sx0, sx1, 2):
                    tot += dark[base + xx]
                    cnt += 1
            ink[cy][cx] = tot / max(cnt, 1)
    inkb = [[v >= 0.42 for v in row] for row in ink]

    # outside = white cells reachable from the edge
    outside = [[False] * INNER_W for _ in range(INNER_H)]
    stack = [(x, y) for x in range(INNER_W) for y in (0, INNER_H - 1)] + \
            [(x, y) for y in range(INNER_H) for x in (0, INNER_W - 1)]
    while stack:
        x, y = stack.pop()
        if 0 <= x < INNER_W and 0 <= y < INNER_H and not outside[y][x] and not inkb[y][x]:
            outside[y][x] = True
            stack += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]

    grid = [['.'] * W for _ in range(H)]
    for y in range(INNER_H):
        for x in range(INNER_W):
            if not inkb[y][x] and not outside[y][x]:
                grid[y + 1][x + 1] = '#'
    # 1px rim around the outline so it reads against a dark background
    inside_or_ink = lambda x, y: 0 <= x < INNER_W and 0 <= y < INNER_H and (inkb[y][x] or not outside[y][x])
    for y in range(H):
        for x in range(W):
            ix, iy = x - 1, y - 1
            if inside_or_ink(ix, iy):
                continue
            if any(inside_or_ink(ix + dx, iy + dy) and inkb[iy + dy][ix + dx]
                   for dx in (-1, 0, 1) for dy in (-1, 0, 1)
                   if 0 <= ix + dx < INNER_W and 0 <= iy + dy < INNER_H):
                grid[y][x] = '#'
    # despeckle: flip single pixels whose 4 neighbours all disagree with them
    for _ in range(2):
        for y in range(1, H - 1):
            for x in range(1, W - 1):
                n = [grid[y - 1][x], grid[y + 1][x], grid[y][x - 1], grid[y][x + 1]]
                if all(c != grid[y][x] for c in n):
                    grid[y][x] = n[0]
    open(out, 'w').write('\n'.join(''.join(r) for r in grid) + '\n')
    print('bbox', (x0, y0, x1, y1), '->', out)


if __name__ == '__main__':
    main()
