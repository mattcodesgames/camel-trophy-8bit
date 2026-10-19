"""The route-map pictures in part 2 (drawn by the routine at $D996).

Format at `src`: word run_table, then literal bytes. The decoder walks screen addresses
$4000..$5AFF; when the address equals the next run-table entry (word addr, byte count,
byte value) it writes the run, otherwise it copies the next literal."""


def decode(m, src):
    table = m[src] | m[src + 1] << 8
    lit, run, scr = src + 2, table, bytearray(6912)
    d = 0x4000
    while d < 0x5B00:
        if (m[run] | m[run + 1] << 8) == d:
            n, v = m[run + 2], m[run + 3]
            run += 4
            n = n or 256
            for _ in range(n):
                scr[d - 0x4000] = v
                d += 1
        else:
            scr[d - 0x4000] = m[lit]
            lit += 1
            d += 1
    end = max(lit, run)
    return scr, end


def encode(scr, src, min_run=5):
    """Return bytes to place at src. Runs of >= min_run equal bytes go in the run table."""
    lits, runs, i = bytearray(), [], 0
    while i < 6912:
        j = i
        while j < 6912 and scr[j] == scr[i] and j - i < 256:  # count byte 0 means 256
            j += 1
        if j - i >= min_run:
            runs.append((0x4000 + i, j - i, scr[i]))
            i = j
        else:
            lits.append(scr[i])
            i += 1
    table = src + 2 + len(lits)
    out = bytearray([table & 0xFF, table >> 8]) + lits
    for a, n, v in runs:
        out += bytes([a & 0xFF, a >> 8, n & 0xFF, v])
    return bytes(out)  # caller zero-fills after the table so no stray entry can match
