"""Shared helpers: TZX block parsing, Sinclair BASIC (de)tokenising, sysvar access."""
import struct

TOKENS = ['RND', 'INKEY$', 'PI', 'FN ', 'POINT ', 'SCREEN$ ', 'ATTR ', 'AT ', 'TAB ', 'VAL$ ', 'CODE ', 'VAL ', 'LEN ',
          'SIN ', 'COS ', 'TAN ', 'ASN ', 'ACS ', 'ATN ', 'LN ', 'EXP ', 'INT ', 'SQR ', 'SGN ', 'ABS ', 'PEEK ', 'IN ',
          'USR ', 'STR$ ', 'CHR$ ', 'NOT ', 'BIN ', ' OR ', ' AND ', '<=', '>=', '<>', ' LINE ', ' THEN ', ' TO ', ' STEP ',
          ' DEF FN ', ' CAT ', ' FORMAT ', ' MOVE ', ' ERASE ', ' OPEN #', ' CLOSE #', ' MERGE ', ' VERIFY ', ' BEEP ',
          ' CIRCLE ', ' INK ', ' PAPER ', ' FLASH ', ' BRIGHT ', ' INVERSE ', ' OVER ', ' OUT ', ' LPRINT ', ' LLIST ',
          ' STOP ', ' READ ', ' DATA ', ' RESTORE ', ' NEW ', ' BORDER ', ' CONTINUE ', ' DIM ', ' REM ', ' FOR ',
          ' GO TO ', ' GO SUB ', ' INPUT ', ' LOAD ', ' LIST ', ' LET ', ' PAUSE ', ' NEXT ', ' POKE ', ' PRINT ',
          ' PLOT ', ' RUN ', ' SAVE ', ' RANDOMIZE ', ' IF ', ' CLS ', ' DRAW ', ' CLEAR ', ' RETURN ', ' COPY ']
TOK = {0xA5 + i: t for i, t in enumerate(TOKENS)}


def read_tzx(path):
    """Return (raw bytes, list of blocks). Each block: dict(id, off, data_off, data_len)."""
    d = open(path, 'rb').read()
    p, blocks = 10, []
    while p < len(d):
        t = d[p]
        b = {'id': t, 'off': p}
        p += 1
        if t == 0x10:
            n = d[p + 2] | d[p + 3] << 8
            b.update(data_off=p + 4, data_len=n); p += 4 + n
        elif t == 0x11:
            n = d[p + 15] | d[p + 16] << 8 | d[p + 17] << 16
            b.update(data_off=p + 18, data_len=n); p += 18 + n
        elif t == 0x20:
            p += 2
        else:
            raise ValueError('unsupported block %02x' % t)
        blocks.append(b)
    return d, blocks


def checksum(data):
    x = 0
    for c in data:
        x ^= c
    return x


def w16(m, a):
    return m[a] | m[a + 1] << 8


def parse_basic(m, start, end):
    """Split a tokenised program into [(line_no, body_bytes)] where body excludes the 4-byte header."""
    lines, p = [], start
    while p + 4 <= end:
        no = m[p] << 8 | m[p + 1]
        if no > 9999:
            break
        n = m[p + 2] | m[p + 3] << 8
        lines.append((no, bytes(m[p + 4:p + 4 + n])))
        p += 4 + n
    return lines, p


def encode_basic(lines):
    out = bytearray()
    for no, body in lines:
        out += bytes([no >> 8, no & 0xFF, len(body) & 0xFF, len(body) >> 8]) + body
    return bytes(out)


def segments(body):
    """Yield ('code', bytes) and ('str', bytes) segments of a line body; 5-byte numbers stay in code."""
    i, cur, segs = 0, bytearray(), []
    while i < len(body):
        c = body[i]
        if c == 0x0E:
            cur += body[i:i + 6]; i += 6; continue
        if c == 0x22:
            j = body.index(b'"', i + 1) if b'"' in body[i + 1:] else len(body) - 1
            # "" inside a string is an escaped quote
            while j + 1 < len(body) and body[j + 1] == 0x22:
                j = body.index(b'"', j + 2)
            cur += b'"'
            segs.append(('code', bytes(cur))); cur = bytearray()
            segs.append(('str', body[i + 1:j]))
            cur += b'"'; i = j + 1; continue
        if c == 0xEA:  # REM: rest of line is literal
            cur += body[i:]; break
        cur.append(c); i += 1
    if cur:
        segs.append(('code', bytes(cur)))
    return segs


def detok(body):
    out, i = [], 0
    while i < len(body):
        c = body[i]
        if c == 0x0E:
            i += 6; continue
        if c == 0x0D:
            break
        if c >= 0xA5:
            out.append(TOK[c])
        elif c == 0xEA:
            out.append(' REM ' + ''.join(chr(x) if 32 <= x < 127 else '·' for x in body[i + 1:]))
            break
        elif 32 <= c < 127:
            out.append(chr(c))
        else:
            out.append('{%02x}' % c)
        i += 1
    return ''.join(out)


def srepr(b):
    """Readable, reversible form of string bytes: printable ASCII as-is, everything else as {xx}."""
    return ''.join(chr(c) if 32 <= c < 127 and c not in (0x7B, 0x7D) else '{%02x}' % c for c in b)


def sparse(s):
    """Inverse of srepr."""
    out, i = bytearray(), 0
    while i < len(s):
        if s[i] == '{' and i + 3 < len(s) and s[i + 3] == '}':
            out.append(int(s[i + 1:i + 3], 16)); i += 4
        else:
            c = ord(s[i])
            if c > 126:
                raise ValueError('non-ASCII character %r in %r' % (s[i], s))
            out.append(c); i += 1
    return bytes(out)
