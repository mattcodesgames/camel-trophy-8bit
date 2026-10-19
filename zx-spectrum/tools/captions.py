"""Redraw text that is drawn as pixels inside the game's pictures.

A caption is described by its pixel band (y0..y1), the x of its first glyph and its
glyph pitch. Glyphs are lifted from the original Italian caption; letters it never uses
are hand-drawn in the matching style."""


def addr(base, x8, y):
    return base + (((y & 0xC0) << 5) | ((y & 7) << 8) | ((y & 0x38) << 2)) + x8


def getpx(img, base, x, y):
    return img[addr(base, x >> 3, y)] >> (7 - (x & 7)) & 1


def setpx(img, base, x, y, v):
    a, bit = addr(base, x >> 3, y), 0x80 >> (x & 7)
    img[a] = (img[a] | bit) if v else (img[a] & ~bit)


class Caption:
    """Caption in pixel band y0..y1 spanning x_lo..x_hi; glyphs are found by blank columns."""

    def __init__(self, base, y0, y1, x_lo, x_hi, text, pitch):
        self.base, self.y0, self.y1, self.x_lo, self.x_hi, self.text, self.pitch = base, y0, y1, x_lo, x_hi, text, pitch

    def font(self, img):
        cols = [any(getpx(img, self.base, x, y) for y in range(self.y0, self.y1)) for x in range(self.x_lo, self.x_hi + 1)]
        runs, x = [], 0
        while x < len(cols):
            if cols[x]:
                e = x
                while e < len(cols) and cols[e]:
                    e += 1
                runs.append((self.x_lo + x, e - x))
                x = e
            else:
                x += 1
        letters = [c for c in self.text if c != ' ']
        assert len(runs) == len(letters), 'caption %r: found %d glyphs' % (self.text, len(runs))
        f = {}
        for ch, (gx, w) in zip(letters, runs):
            f.setdefault(ch, tuple(tuple(getpx(img, self.base, gx + k, y) for k in range(w)) for y in range(self.y0, self.y1)))
        return f

    def redraw(self, img, new_text, extra=None):
        """Erase the caption span and draw new_text centred in it, one glyph per pitch."""
        f = self.font(img)
        for ch, rows in (extra or {}).items():
            assert len(rows) == self.y1 - self.y0, 'glyph %r needs %d rows' % (ch, self.y1 - self.y0)
            f[ch] = tuple(tuple(1 if c == '#' else 0 for c in r.rstrip('.') or '.') for r in rows)
        missing = sorted(set(new_text) - set(f) - {' '})
        assert not missing, 'caption %r: no glyph for %s' % (new_text, missing)
        for x in range(self.x_lo, self.x_hi + 1):
            for y in range(self.y0, self.y1):
                setpx(img, self.base, x, y, 0)
        width = self.pitch * len(new_text)
        span = self.x_hi - self.x_lo + 1
        assert width <= span, 'caption %r too wide (%d > %d px)' % (new_text, width, span)
        start = self.x_lo + (span - width) // 2
        for i, ch in enumerate(new_text):
            if ch == ' ':
                continue
            for dy, row in enumerate(f[ch]):
                for k, v in enumerate(row):
                    if v:
                        setpx(img, self.base, start + self.pitch * i + k, self.y0 + dy, 1)
