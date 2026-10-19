#!/usr/bin/env python3
"""Build an English Camel Trophy tape from the original Italian TZX.

Each game part is a turbo block holding a memory image loaded at $4000:
  - parts 1 and 2 are Sinclair BASIC programs: string literals are replaced and the
    program is re-linked (system variables that point past the program are moved);
  - part 3 is machine code: text runs are replaced in place at the same length;
  - captions drawn into pictures are redrawn with glyphs taken from the same picture.

Usage: translate.py original.tzx english.tzx
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from captions import Caption  # noqa: E402
import mappanel  # noqa: E402
import rlepic  # noqa: E402
from zxtape import checksum, encode_basic, parse_basic, read_tzx, segments, sparse, srepr, w16  # noqa: E402
import translation_en as T  # noqa: E402

PROG, VARS = 0x5C53, 0x5C4B
# system variables that point at or beyond VARS and must follow the program when it changes size
MOVABLE = {'VARS': 0x5C4B, 'NXTLIN': 0x5C55, 'E_LINE': 0x5C59, 'K_CUR': 0x5C5B, 'CH_ADD': 0x5C5D,
           'WORKSP': 0x5C61, 'STKBOT': 0x5C63, 'STKEND': 0x5C65}
HEADROOM = 1024  # bytes kept free above STKEND for the calculator stack


def set16(m, a, v):
    m[a], m[a + 1] = v & 0xFF, v >> 8


def translate_basic(m, strings, line_patches, part):
    prog, vars_ = w16(m, PROG), w16(m, VARS)
    lines, end = parse_basic(m, prog, vars_)
    assert end == vars_, 'part %d: program does not end at VARS' % part
    used = set()
    out = []
    for no, body in lines:
        new = bytearray()
        for kind, seg in segments(body):
            if kind == 'str' and srepr(seg) in strings:
                used.add(srepr(seg))
                seg = sparse(strings[srepr(seg)])
            new += seg
        text = srepr(bytes(new))
        for old, rep in line_patches.get(no, []):
            assert old in text, 'part %d line %d: %r not found' % (part, no, old)
            text = text.replace(old, rep)
        out.append((no, sparse(text)))
    unused = set(strings) - used
    assert not unused, 'part %d: strings not found: %s' % (part, sorted(unused)[:5])

    new_prog = encode_basic(out)
    delta = len(new_prog) - (vars_ - prog)
    stkend = w16(m, MOVABLE['STKEND'])
    if delta > 0:
        # growing is only safe into untouched zero memory above STKEND
        free_end = stkend
        while free_end < 0xFF00 and not any(m[free_end:free_end + 16]):
            free_end += 16
        assert stkend + delta + HEADROOM <= free_end, 'part %d: program grew by %d bytes, only %d free' % (
            part, delta, free_end - stkend - HEADROOM)
    tail = bytes(m[vars_:stkend])
    m[prog:stkend + max(delta, 0)] = bytes(stkend + max(delta, 0) - prog)
    m[prog:prog + len(new_prog)] = new_prog
    m[prog + len(new_prog):prog + len(new_prog) + len(tail)] = tail
    for name, a in MOVABLE.items():
        v = w16(m, a)
        if v >= vars_:
            set16(m, a, v + delta)
    print('part %d: %d strings translated, program %+d bytes' % (part, len(used), delta))


def runs(m, start):
    """Yield (address, length, terminated) for each text run of a machine-code message stream."""
    a = start
    while True:
        c = m[a]
        if c == 0x16:
            a += 3
            continue
        if 0x10 <= c <= 0x15:
            a += 2
            continue
        b = a
        while True:
            c = m[b]
            if 0x10 <= c <= 0x16:
                yield a, b - a, False
                break
            b += 1
            if c & 0x80:
                yield a, b - a, True
                break
        a = b


def translate_mc(m, blocks):
    n = 0
    for start, new_lines in blocks:
        it = runs(m, start)
        for new in new_lines:
            a, length, term = next(it)
            if new is None:
                continue
            old = bytes(m[a:a + length - 1]) + bytes([m[a + length - 1] & 0x7F])
            enc = sparse(new)
            assert len(enc) <= length, '%04x: %r longer than original %r (%d)' % (a, new, old.decode('latin-1'), length)
            enc = enc.ljust(length, b' ')
            if term:
                enc = enc[:-1] + bytes([enc[-1] | 0x80])
            m[a:a + length] = enc
            n += 1
    print('part 3: %d text runs translated' % n)


def apply_map_extras(scr, font, spec):
    for (x0, x1, y0, y1), old, new in spec.get('labels', []):
        mappanel.draw(scr, font, x0, x1, y0, y1, new)
    for args, new, border in spec.get('captions', []):
        cap = Caption(0, *args)
        cap.redraw(scr, new)
        if border:  # close the gap left in the frame lines around the shorter caption
            width = cap.pitch * len(new)
            start = cap.x_lo + (cap.x_hi - cap.x_lo + 1 - width) // 2
            for y in border:
                for x in list(range(cap.x_lo, start - 2)) + list(range(start + width + 1, cap.x_hi + 1)):
                    mappanel.setpx(scr, x, y, 1)


def translate_maps(m):
    """Re-letter the route-map pictures of part 2 and re-encode them in place."""
    maps = T.MAPS
    decoded = {src: rlepic.decode(m, src) for src in maps}
    font = {}
    for src, spec in maps.items():
        scr = decoded[src][0]
        for k, (label, value) in enumerate(zip(T.MAP_LABELS_IT, spec['it'])):
            y0 = T.MAP_LINE_Y0 + 16 * k
            mappanel.learn(font, scr, *T.MAP_LABEL_X, y0, y0 + 12, label)
            mappanel.learn(font, scr, *T.MAP_VALUE_X, y0, y0 + 12, value)
        for (x0, x1, y0, y1), old, new in spec.get('labels', []):
            n = len(mappanel.glyphs(scr, x0, x1, y0, y1))
            assert n == len(old.replace(' ', '')), 'map label %r not found' % old
    font.update({ch: mappanel.glyph(*rows) for ch, rows in T.MAP_EXTRA_GLYPHS.items()})
    if 'y' not in font:  # 'u' with the descender of 'g'
        font['y'] = tuple(u[:10] + g[10:] for u, g in zip(font['u'], font['g']))
    lx, vx = T.MAP_LABEL_X, T.MAP_VALUE_X
    for src, spec in maps.items():
        base, end = decoded[src]
        best = None
        # nudging the columns by a pixel changes how many screen bytes hold ink; keep the smallest
        for dl in range(4):
            for dv in range(3):
                scr = bytearray(base)
                for k, label in enumerate(T.MAP_LABELS_EN):
                    y0 = T.MAP_LINE_Y0 + 16 * k
                    mappanel.draw(scr, font, lx[0], lx[1], y0, y0 + 12, label, offset=dl)
                    if spec['en'][k] is not None:
                        mappanel.draw(scr, font, vx[0], vx[1], y0, y0 + 12, spec['en'][k], offset=1 + dv)
                apply_map_extras(scr, font, spec)
                size = len(rlepic.encode(bytes(scr), spec.get('new_src', src)))
                if best is None or size < best[0]:
                    best = (size, scr)
        scr = best[1]
        dst, end = spec.get('new_src', src), spec.get('end', end)
        enc = rlepic.encode(bytes(scr), dst)
        assert len(enc) <= end - dst, 'map at %04x: %d bytes, only %d available' % (dst, len(enc), end - dst)
        m[dst:end] = enc + bytes(end - dst - len(enc))
        assert bytes(rlepic.decode(m, dst)[0]) == bytes(scr)
        print('part 2: map at %04x re-encoded, %d bytes spare' % (dst, end - dst - len(enc)))


def main(src, dst):
    d, blocks = read_tzx(src)
    out = bytearray(d)
    turbo = [b for b in blocks if b['id'] == 0x11]
    assert len(turbo) == 3, 'expected the three-part Camel Trophy tape'
    for part, b in enumerate(turbo, 1):
        data = bytes(d[b['data_off']:b['data_off'] + b['data_len']])
        assert checksum(data[:-1]) == data[-1], 'part %d: bad checksum in source tape' % part
        m = bytearray(65536)
        n = len(data) - 2
        m[0x4000:0x4000 + n] = data[1:-1]
        if part in (1, 2):
            translate_basic(m, T.STRINGS[part], T.LINE_PATCHES.get(part, {}), part)
            if part == 2:
                translate_maps(m)
        else:
            translate_mc(m, T.MC_BLOCKS)
        for (p, base, y0, y1, x_lo, x_hi, old, pitch), new in T.CAPTIONS:
            if p == part:
                Caption(base, y0, y1, x_lo, x_hi, old, pitch).redraw(m, new, T.EXTRA_GLYPHS.get((p, base, y0)))
        body = bytes([data[0]]) + bytes(m[0x4000:0x4000 + n])
        out[b['data_off']:b['data_off'] + b['data_len']] = body + bytes([checksum(body)])
    open(dst, 'wb').write(out)
    print('wrote', dst)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
