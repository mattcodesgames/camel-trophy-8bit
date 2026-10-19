/* Z80 CPU core. Included directly by spectrum.c, which supplies:
 *   uint8_t mem_read(uint16_t), void mem_write(uint16_t, uint8_t),
 *   uint8_t port_in(uint16_t), void port_out(uint16_t, uint8_t)
 * and the T-state counter `T`.
 * Implements all documented and undocumented opcodes, including the
 * undocumented flag bits 3/5 and DDCB/FDCB register copies.
 */

typedef union { uint16_t w; struct { uint8_t l, h; } b; } reg16;

#define FC 0x01
#define FN 0x02
#define FP 0x04
#define F3 0x08
#define FH 0x10
#define F5 0x20
#define FZ 0x40
#define FS 0x80

static reg16 af, bc, de, hl, ix, iy, sp, pc, wz;
static reg16 af_, bc_, de_, hl_;
static uint8_t I, R, R7, iff1, iff2, im, halted, ei_pending;
static int idx;  /* 0 = HL, 1 = IX, 2 = IY for the current instruction */

#define A af.b.h
#define F af.b.l
#define B bc.b.h
#define C bc.b.l
#define D de.b.h
#define E de.b.l
#define H hl.b.h
#define L hl.b.l

static uint8_t sz53[256], sz53p[256];

static void z80_tables(void) {
    for (int i = 0; i < 256; i++) {
        int p = 0;
        for (int j = 0; j < 8; j++) p ^= (i >> j) & 1;
        sz53[i] = (i & (FS | F3 | F5)) | (i == 0 ? FZ : 0);
        sz53p[i] = sz53[i] | (p ? 0 : FP);
    }
}

static void z80_reset(void) {
    af.w = 0xFFFF; bc.w = de.w = hl.w = 0; ix.w = iy.w = 0; sp.w = 0xFFFF; pc.w = 0;
    af_.w = bc_.w = de_.w = hl_.w = 0; wz.w = 0;
    I = R = R7 = 0; iff1 = iff2 = 0; im = 0; halted = 0; ei_pending = 0;
}

static inline uint8_t fetch_op(void) { T += 4; R++; return mem_read(pc.w++); }
static inline uint8_t fetch8(void) { return mem_read(pc.w++); }
static inline uint16_t fetch16(void) { uint16_t v = mem_read(pc.w); v |= mem_read(pc.w + 1) << 8; pc.w += 2; return v; }
static inline uint16_t read16(uint16_t a) { return mem_read(a) | (mem_read(a + 1) << 8); }
static inline void write16(uint16_t a, uint16_t v) { mem_write(a, v & 0xFF); mem_write(a + 1, v >> 8); }
static inline void push(uint16_t v) { sp.w -= 2; write16(sp.w, v); }
static inline uint16_t pop(void) { uint16_t v = read16(sp.w); sp.w += 2; return v; }

static inline reg16 *xhl(void) { return idx == 0 ? &hl : idx == 1 ? &ix : &iy; }

/* Register by 3-bit index; 6 is (HL) and handled by callers. */
static inline uint8_t getr(int r) {
    switch (r) {
    case 0: return B; case 1: return C; case 2: return D; case 3: return E;
    case 4: return xhl()->b.h; case 5: return xhl()->b.l; case 7: return A;
    }
    return 0;
}
static inline void setr(int r, uint8_t v) {
    switch (r) {
    case 0: B = v; break; case 1: C = v; break; case 2: D = v; break; case 3: E = v; break;
    case 4: xhl()->b.h = v; break; case 5: xhl()->b.l = v; break; case 7: A = v; break;
    }
}
/* Plain registers (no IX/IY substitution) — used when (IX+d) is the other operand. */
static inline uint8_t getr_plain(int r) { int s = idx; idx = 0; uint8_t v = getr(r); idx = s; return v; }
static inline void setr_plain(int r, uint8_t v) { int s = idx; idx = 0; setr(r, v); idx = s; }

/* Effective address for (HL) / (IX+d). Adds displacement timing for indexed forms. */
static inline uint16_t addr_hl(void) {
    if (!idx) return hl.w;
    int8_t d = (int8_t)fetch8();
    T += 8;
    wz.w = xhl()->w + d;
    return wz.w;
}

static inline reg16 *rp(int p) { switch (p) { case 0: return &bc; case 1: return &de; case 2: return xhl(); } return &sp; }
static inline reg16 *rp2(int p) { switch (p) { case 0: return &bc; case 1: return &de; case 2: return xhl(); } return &af; }

static inline int cond(int y) {
    switch (y) {
    case 0: return !(F & FZ); case 1: return F & FZ; case 2: return !(F & FC); case 3: return F & FC;
    case 4: return !(F & FP); case 5: return F & FP; case 6: return !(F & FS);
    }
    return F & FS;
}

/* ---- ALU ---- */
static inline void alu_add(uint8_t v, int carry) {
    unsigned r = A + v + carry;
    F = sz53[r & 0xFF] | ((r >> 8) & FC) | ((A ^ v ^ r) & FH) | ((((A ^ ~v) & (A ^ r)) & 0x80) ? FP : 0);
    A = r;
}
static inline uint8_t alu_subf(uint8_t v, int carry) {
    unsigned r = A - v - carry;
    F = FN | sz53[r & 0xFF] | ((r >> 8) & FC) | ((A ^ v ^ r) & FH) | ((((A ^ v) & (A ^ r)) & 0x80) ? FP : 0);
    return r;
}
static void alu(int op, uint8_t v) {
    switch (op) {
    case 0: alu_add(v, 0); break;
    case 1: alu_add(v, F & FC); break;
    case 2: A = alu_subf(v, 0); break;
    case 3: A = alu_subf(v, F & FC); break;
    case 4: A &= v; F = sz53p[A] | FH; break;
    case 5: A ^= v; F = sz53p[A]; break;
    case 6: A |= v; F = sz53p[A]; break;
    case 7: alu_subf(v, 0); F = (F & ~(F3 | F5)) | (v & (F3 | F5)); break;
    }
}
static inline uint8_t inc8(uint8_t v) {
    uint8_t r = v + 1;
    F = (F & FC) | sz53[r] | (r == 0x80 ? FP : 0) | ((r & 0x0F) == 0 ? FH : 0);
    return r;
}
static inline uint8_t dec8(uint8_t v) {
    uint8_t r = v - 1;
    F = (F & FC) | FN | sz53[r] | (r == 0x7F ? FP : 0) | ((v & 0x0F) == 0 ? FH : 0);
    return r;
}
static inline uint16_t add16(uint16_t a, uint16_t v) {
    uint32_t r = a + v;
    wz.w = a + 1;
    F = (F & (FS | FZ | FP)) | ((r >> 16) & FC) | (((a ^ v ^ r) >> 8) & FH) | ((r >> 8) & (F3 | F5));
    return r;
}
static inline void adc16(uint16_t v) {
    uint32_t r = hl.w + v + (F & FC);
    wz.w = hl.w + 1;
    F = ((r >> 16) & FC) | (((hl.w ^ v ^ r) >> 8) & FH) | ((r >> 8) & (FS | F3 | F5)) |
        ((r & 0xFFFF) ? 0 : FZ) | ((((hl.w ^ ~v) & (hl.w ^ r)) & 0x8000) ? FP : 0);
    hl.w = r;
}
static inline void sbc16(uint16_t v) {
    uint32_t r = hl.w - v - (F & FC);
    wz.w = hl.w + 1;
    F = FN | ((r >> 16) & FC) | (((hl.w ^ v ^ r) >> 8) & FH) | ((r >> 8) & (FS | F3 | F5)) |
        ((r & 0xFFFF) ? 0 : FZ) | ((((hl.w ^ v) & (hl.w ^ r)) & 0x8000) ? FP : 0);
    hl.w = r;
}
static uint8_t rot(int op, uint8_t v) {
    uint8_t c;
    switch (op) {
    case 0: c = v >> 7; v = (v << 1) | c; break;
    case 1: c = v & 1; v = (v >> 1) | (c << 7); break;
    case 2: c = v >> 7; v = (v << 1) | (F & FC); break;
    case 3: c = v & 1; v = (v >> 1) | ((F & FC) << 7); break;
    case 4: c = v >> 7; v <<= 1; break;
    case 5: c = v & 1; v = (v & 0x80) | (v >> 1); break;
    case 6: c = v >> 7; v = (v << 1) | 1; break;
    default: c = v & 1; v >>= 1; break;
    }
    F = sz53p[v] | c;
    return v;
}
static inline void bit_test(int b, uint8_t v, uint8_t xy) {
    uint8_t r = v & (1 << b);
    F = (F & FC) | FH | (xy & (F3 | F5)) | (r ? 0 : (FZ | FP)) | (r & FS);
}
static void daa(void) {
    uint8_t a = A, corr = 0, c = F & FC, h;
    if ((F & FH) || (a & 0x0F) > 9) corr = 0x06;
    if (c || a > 0x99) { corr |= 0x60; c = FC; }
    if (F & FN) { h = (F & FH) && (a & 0x0F) < 6; A = a - corr; }
    else { h = (a & 0x0F) > 9; A = a + corr; }
    F = (F & FN) | c | (h ? FH : 0) | sz53p[A];
}

/* ---- Interrupts ---- */
static void z80_interrupt(void) {
    if (!iff1 || ei_pending) return;
    if (halted) halted = 0;
    iff1 = iff2 = 0;
    R++;
    push(pc.w);
    if (im == 2) { T += 19; pc.w = read16((I << 8) | 0xFF); }
    else { T += 13; pc.w = 0x0038; }
    wz.w = pc.w;
}

/* ---- CB prefix ---- */
static void exec_cb(void) {
    uint16_t a = 0;
    uint8_t op, v;
    if (idx) {
        int8_t d = (int8_t)fetch8();
        op = fetch8();
        a = xhl()->w + d; wz.w = a;
        T += 4;  /* d and op bytes; DD and CB fetches already counted */
    } else {
        op = fetch_op();
    }
    int x = op >> 6, y = (op >> 3) & 7, z = op & 7;
    if (idx) {
        v = mem_read(a);
        if (x == 1) { T += 12 - 4; bit_test(y, v, a >> 8); return; }
        T += 15 - 4;
        if (x == 0) v = rot(y, v);
        else if (x == 2) v &= ~(1 << y);
        else v |= (1 << y);
        mem_write(a, v);
        if (z != 6) setr_plain(z, v);
        return;
    }
    if (z == 6) {
        a = hl.w; v = mem_read(a);
        if (x == 1) { T += 4; bit_test(y, v, wz.b.h); return; }
        T += 7;
    } else {
        v = getr(z);
        if (x == 1) { bit_test(y, v, v); return; }
    }
    if (x == 0) v = rot(y, v);
    else if (x == 2) v &= ~(1 << y);
    else v |= (1 << y);
    if (z == 6) mem_write(a, v); else setr(z, v);
}

/* ---- ED prefix ---- */
static void exec_ed(void) {
    uint8_t op = fetch_op();
    int x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = y >> 1, q = y & 1;
    idx = 0;
    if (x == 1) {
        switch (z) {
        case 0: { uint8_t v = port_in(bc.w); T += 4; wz.w = bc.w + 1; F = (F & FC) | sz53p[v]; if (y != 6) setr(y, v); break; }
        case 1: T += 4; port_out(bc.w, y == 6 ? 0 : getr(y)); wz.w = bc.w + 1; break;
        case 2: T += 7; if (q) adc16(rp(p)->w); else sbc16(rp(p)->w); break;
        case 3: { uint16_t nn = fetch16(); T += 12; wz.w = nn + 1;
                  if (q) rp(p)->w = read16(nn); else write16(nn, rp(p)->w); break; }
        case 4: { uint8_t v = A; A = 0; A = alu_subf(v, 0); break; }
        case 5: T += 6; iff1 = iff2; pc.w = pop(); wz.w = pc.w; break;
        case 6: { static const uint8_t modes[4] = { 0, 0, 1, 2 }; im = modes[y & 3]; break; }
        case 7:
            switch (y) {
            case 0: T += 1; I = A; break;
            case 1: T += 1; R = A; R7 = A & 0x80; break;
            case 2: T += 1; A = I; F = (F & FC) | sz53[A] | (iff2 ? FP : 0); break;
            case 3: T += 1; A = (R & 0x7F) | R7; F = (F & FC) | sz53[A] | (iff2 ? FP : 0); break;
            case 4: { uint8_t v = mem_read(hl.w); T += 10; mem_write(hl.w, (A << 4) | (v >> 4)); A = (A & 0xF0) | (v & 0x0F); F = (F & FC) | sz53p[A]; wz.w = hl.w + 1; break; }
            case 5: { uint8_t v = mem_read(hl.w); T += 10; mem_write(hl.w, (v << 4) | (A & 0x0F)); A = (A & 0xF0) | (v >> 4); F = (F & FC) | sz53p[A]; wz.w = hl.w + 1; break; }
            }
            break;
        }
        return;
    }
    if (x == 2 && z <= 3 && y >= 4) {
        int dir = (y & 1) ? -1 : 1, rep = y >= 6;
        T += 8;
        switch (z) {
        case 0: { /* LDI/LDD */
            uint8_t v = mem_read(hl.w); mem_write(de.w, v);
            hl.w += dir; de.w += dir; bc.w--;
            uint8_t n = v + A;
            F = (F & (FS | FZ | FC)) | (bc.w ? FP : 0) | (n & F3) | ((n & 0x02) ? F5 : 0);
            if (rep && bc.w) { pc.w -= 2; T += 5; wz.w = pc.w + 1; }
            break; }
        case 1: { /* CPI/CPD */
            uint8_t v = mem_read(hl.w), r = A - v, h = (A ^ v ^ r) & FH;
            hl.w += dir; bc.w--; wz.w += dir;
            uint8_t n = r - (h ? 1 : 0);
            F = (F & FC) | FN | (sz53[r] & ~(F3 | F5)) | h | (bc.w ? FP : 0) | (n & F3) | ((n & 0x02) ? F5 : 0);
            if (rep && bc.w && r) { pc.w -= 2; T += 5; wz.w = pc.w + 1; }
            break; }
        case 2: { /* INI/IND */
            uint8_t v = port_in(bc.w); wz.w = bc.w + dir; mem_write(hl.w, v);
            B--; hl.w += dir;
            unsigned k = v + (uint8_t)(C + dir);
            F = sz53[B] | ((v & 0x80) ? FN : 0) | (k > 255 ? (FH | FC) : 0) | (sz53p[(k & 7) ^ B] & FP);
            if (rep && B) { pc.w -= 2; T += 5; }
            break; }
        case 3: { /* OUTI/OUTD */
            uint8_t v = mem_read(hl.w); B--; port_out(bc.w, v); hl.w += dir; wz.w = bc.w + dir;
            unsigned k = v + L;
            F = sz53[B] | ((v & 0x80) ? FN : 0) | (k > 255 ? (FH | FC) : 0) | (sz53p[(k & 7) ^ B] & FP);
            if (rep && B) { pc.w -= 2; T += 5; }
            break; }
        }
        return;
    }
    /* everything else is an 8T NOP */
}

/* ---- Main instruction step ---- */
static void z80_step(void) {
    if (halted) { T += 4; R++; return; }
    idx = 0;
    uint8_t op = fetch_op();
    while (op == 0xDD || op == 0xFD) { idx = op == 0xDD ? 1 : 2; op = fetch_op(); }
    if (op == 0xCB) { exec_cb(); return; }
    if (op == 0xED) { exec_ed(); return; }

    int x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = y >> 1, q = y & 1;
    switch (x) {
    case 0:
        switch (z) {
        case 0:
            switch (y) {
            case 0: break;
            case 1: { reg16 t = af; af = af_; af_ = t; break; }
            case 2: { T += 1; int8_t d = (int8_t)fetch8(); T += 3; if (--B) { pc.w += d; wz.w = pc.w; T += 5; } break; }
            case 3: { int8_t d = (int8_t)fetch8(); pc.w += d; wz.w = pc.w; T += 8; break; }
            default: { int8_t d = (int8_t)fetch8(); T += 3; if (cond(y - 4)) { pc.w += d; wz.w = pc.w; T += 5; } break; }
            }
            break;
        case 1:
            if (!q) { rp(p)->w = fetch16(); T += 6; }
            else { T += 7; xhl()->w = add16(xhl()->w, rp(p)->w); }
            break;
        case 2:
            switch (y) {
            case 0: T += 3; mem_write(bc.w, A); wz.b.l = bc.w + 1; wz.b.h = A; break;
            case 1: T += 3; A = mem_read(bc.w); wz.w = bc.w + 1; break;
            case 2: T += 3; mem_write(de.w, A); wz.b.l = de.w + 1; wz.b.h = A; break;
            case 3: T += 3; A = mem_read(de.w); wz.w = de.w + 1; break;
            case 4: { uint16_t nn = fetch16(); T += 12; write16(nn, xhl()->w); wz.w = nn + 1; break; }
            case 5: { uint16_t nn = fetch16(); T += 12; xhl()->w = read16(nn); wz.w = nn + 1; break; }
            case 6: { uint16_t nn = fetch16(); T += 9; mem_write(nn, A); wz.b.l = nn + 1; wz.b.h = A; break; }
            case 7: { uint16_t nn = fetch16(); T += 9; A = mem_read(nn); wz.w = nn + 1; break; }
            }
            break;
        case 3: T += 2; if (!q) rp(p)->w++; else rp(p)->w--; break;
        case 4:
            if (y == 6) { uint16_t a = addr_hl(); T += 7; mem_write(a, inc8(mem_read(a))); }
            else setr(y, inc8(getr(y)));
            break;
        case 5:
            if (y == 6) { uint16_t a = addr_hl(); T += 7; mem_write(a, dec8(mem_read(a))); }
            else setr(y, dec8(getr(y)));
            break;
        case 6:
            if (y == 6) { uint16_t a = addr_hl(); if (idx) T -= 3; T += 6; mem_write(a, fetch8()); }
            else { T += 3; setr(y, fetch8()); }
            break;
        case 7:
            switch (y) {
            case 0: A = (A << 1) | (A >> 7); F = (F & (FS | FZ | FP)) | (A & (F3 | F5 | FC)); break;
            case 1: F = (F & (FS | FZ | FP)) | (A & FC); A = (A >> 1) | (A << 7); F |= A & (F3 | F5); break;
            case 2: { uint8_t c = A >> 7; A = (A << 1) | (F & FC); F = (F & (FS | FZ | FP)) | (A & (F3 | F5)) | c; break; }
            case 3: { uint8_t c = A & 1; A = (A >> 1) | (F << 7); F = (F & (FS | FZ | FP)) | (A & (F3 | F5)) | c; break; }
            case 4: daa(); break;
            case 5: A ^= 0xFF; F = (F & (FC | FP | FZ | FS)) | (A & (F3 | F5)) | FH | FN; break;
            case 6: F = (F & (FP | FZ | FS)) | (A & (F3 | F5)) | FC; break;
            case 7: F = (F & (FP | FZ | FS)) | ((F & FC) ? FH : FC) | (A & (F3 | F5)); break;
            }
            break;
        }
        break;

    case 1:
        if (op == 0x76) { halted = 1; break; }
        if (z == 6) { uint16_t a = addr_hl(); T += 3; setr_plain(y, mem_read(a)); }
        else if (y == 6) { uint16_t a = addr_hl(); T += 3; mem_write(a, getr_plain(z)); }
        else setr(y, getr(z));
        break;

    case 2:
        if (z == 6) { uint16_t a = addr_hl(); T += 3; alu(y, mem_read(a)); }
        else alu(y, getr(z));
        break;

    case 3:
        switch (z) {
        case 0: T += 1; if (cond(y)) { pc.w = pop(); wz.w = pc.w; T += 6; } break;
        case 1:
            if (!q) { rp2(p)->w = pop(); T += 6; }
            else switch (p) {
                case 0: pc.w = pop(); wz.w = pc.w; T += 6; break;
                case 1: { reg16 t; t = bc; bc = bc_; bc_ = t; t = de; de = de_; de_ = t; t = hl; hl = hl_; hl_ = t; break; }
                case 2: pc.w = xhl()->w; break;
                case 3: T += 2; sp.w = xhl()->w; break;
            }
            break;
        case 2: { uint16_t nn = fetch16(); T += 6; wz.w = nn; if (cond(y)) pc.w = nn; break; }
        case 3:
            switch (y) {
            case 0: pc.w = fetch16(); wz.w = pc.w; T += 6; break;
            case 2: { uint8_t n = fetch8(); T += 7; port_out((A << 8) | n, A); wz.b.l = n + 1; wz.b.h = A; break; }
            case 3: { uint8_t n = fetch8(); T += 3; uint16_t port = (A << 8) | n; wz.w = port + 1; A = port_in(port); T += 4; break; }
            case 4: { uint16_t v = read16(sp.w); write16(sp.w, xhl()->w); xhl()->w = v; wz.w = v; T += 15; break; }
            case 5: { reg16 t = de; de = hl; hl = t; break; }
            case 6: iff1 = iff2 = 0; break;
            case 7: iff1 = iff2 = 1; ei_pending = 1; break;
            }
            break;
        case 4: { uint16_t nn = fetch16(); T += 6; wz.w = nn; if (cond(y)) { push(pc.w); pc.w = nn; T += 7; } break; }
        case 5:
            if (!q) { T += 7; push(rp2(p)->w); }
            else { uint16_t nn = fetch16(); T += 13; push(pc.w); pc.w = nn; wz.w = nn; }
            break;
        case 6: T += 3; alu(y, fetch8()); break;
        case 7: T += 7; push(pc.w); pc.w = y * 8; wz.w = pc.w; break;
        }
        break;
    }
}
