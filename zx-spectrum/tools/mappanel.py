"""Rewrite the small proportional text drawn in the part 2 route-map pictures.

Works on a decoded 6912-byte screen. Text lines are found inside a box, glyphs are split
on blank pixel columns, and a font is learned from lines whose Italian text is known."""

GAP, SPACE = 1, 3  # pixels between glyphs / width of a space


def px(scr, x, y):
    return scr[(((y & 0xC0) << 5) | ((y & 7) << 8) | ((y & 0x38) << 2)) + (x >> 3)] >> (7 - (x & 7)) & 1


def setpx(scr, x, y, v):
    a, bit = (((y & 0xC0) << 5) | ((y & 7) << 8) | ((y & 0x38) << 2)) + (x >> 3), 0x80 >> (x & 7)
    scr[a] = (scr[a] | bit) if v else (scr[a] & ~bit)


def glyphs(scr, x0, x1, y0, y1):
    """Split x0..x1 (inclusive) of band y0..y1 (exclusive) into (x, columns) glyphs."""
    cols = [tuple(px(scr, x, y) for y in range(y0, y1)) for x in range(x0, x1 + 1)]
    out, x = [], 0
    while x < len(cols):
        if any(cols[x]):
            e = x
            while e < len(cols) and any(cols[e]):
                e += 1
            out.append((x0 + x, tuple(cols[x:e])))
            x = e
        else:
            x += 1
    return out


def learn(font, scr, x0, x1, y0, y1, text):
    """Add the glyphs of a known line to font. Lines whose letters touch are skipped."""
    gs = glyphs(scr, x0, x1, y0, y1)
    letters = [c for c in text if c != ' ']
    if len(gs) != len(letters):
        return False
    for ch, (_, g) in zip(letters, gs):
        font.setdefault(ch, g)
    return True


def draw(scr, font, x0, x1, y0, y1, text, align='left', offset=0):
    """Clear x0..x1 of the band and draw text (left-aligned at x0, or centred)."""
    for x in range(x0, x1 + 1):
        for y in range(y0, y1):
            setpx(scr, x, y, 0)
    width = sum(SPACE if c == ' ' else len(font[c]) for c in text) + GAP * (len(text) - 1)
    assert width <= x1 - x0 + 1, 'map text %r too wide (%d px)' % (text, width)
    assert width + offset <= x1 - x0 + 1, 'map text %r too wide' % text
    x = x0 + offset if align == 'left' else x0 + (x1 - x0 + 1 - width) // 2
    for c in text:
        if c == ' ':
            x += SPACE + GAP
            continue
        for col in font[c]:
            for dy, v in enumerate(col):
                if v:
                    setpx(scr, x, y0 + dy, 1)
            x += 1
        x += GAP


def glyph(*rows):
    """Hand-drawn glyph given as rows of '#'/'.' (top to bottom), returned as columns."""
    return tuple(tuple(1 if r[c] == '#' else 0 for r in rows) for c in range(len(rows[0])))
