/* ZX Spectrum 48K machine compiled to WebAssembly (freestanding, no libc).
 * Exposes a small C API that the JavaScript frontend drives once per frame.
 */
#include <stdint.h>

#define EXPORT __attribute__((visibility("default")))

#define FRAME_T     69888   /* T-states per 50.08 Hz frame */
#define LINE_T      224
#define CPU_HZ      3500000

#define SCR_W 320           /* 256 + 32px border each side */
#define SCR_H 256           /* 192 + 32 lines border top/bottom */
#define FIRST_LINE  32      /* first ULA line shown (screen starts at line 64) */

#define TAPE_MAX_PULSES (4u << 20)
#define TAPE_MAX_BYTES  (1u << 20)
#define TAPE_MAX_BLOCKS 1024

static uint32_t T;
static uint8_t mem[65536];

/* ---- I/O state ---- */
static uint8_t keys[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static uint8_t kempston;
static uint8_t border, out_fe;
static uint8_t border_line[312];
static uint32_t border_upto;
static uint32_t frames;

/* ---- tape ---- */
static uint8_t tape_data[TAPE_MAX_BYTES];
static uint32_t tape_len;
static uint32_t pulses[TAPE_MAX_PULSES];
static uint32_t npulses;
static uint32_t block_start[TAPE_MAX_BLOCKS];   /* pulse index at start of each TZX block */
static uint32_t block_offset[TAPE_MAX_BLOCKS];  /* byte offset of each block's id byte */
static uint32_t nblocks;
static uint32_t tape_pos;       /* index of current pulse */
static uint32_t tape_remain;    /* T-states left in current pulse */
static uint8_t tape_level, tape_playing, tape_autoplay = 1;

#define P_EDGE  0u          /* toggle level then hold for duration */
#define P_LOW   1u          /* force low for duration (pause) */
#define P_STOP  2u          /* stop the tape */
#define P_KIND(p) ((p) >> 28)
#define P_LEN(p)  ((p) & 0x0FFFFFFF)

/* ---- video / audio ---- */
static uint32_t fb[SCR_W * SCR_H];
static uint32_t palette[16];
#define AUDIO_MAX 4096
static float abuf[AUDIO_MAX];
static uint32_t an;
static double a_tps = (double)CPU_HZ / 44100.0, a_next, a_last, a_acc;
static float a_hp_x, a_hp_y;
static uint8_t muted;

static uint8_t rom_buf[16384];

static void *memset_(void *d, int c, uint32_t n) { uint8_t *p = d; while (n--) *p++ = (uint8_t)c; return d; }
static void memcpy_(void *d, const void *s, uint32_t n) { uint8_t *a = d; const uint8_t *b = s; while (n--) *a++ = *b++; }

#ifdef __wasm__
/* clang may emit calls to these for struct copies / large inits */
void *memset(void *d, int c, unsigned long n) { return memset_(d, c, (uint32_t)n); }
void *memcpy(void *d, const void *s, unsigned long n) { memcpy_(d, s, (uint32_t)n); return d; }
#endif

#ifndef MEM_HOOKS
#define MEM_READ_HOOK(a)
#define MEM_WRITE_HOOK(a, v)
#endif
static inline uint8_t mem_read(uint16_t a) { MEM_READ_HOOK(a); return mem[a]; }
static inline void mem_write(uint16_t a, uint8_t v) { MEM_WRITE_HOOK(a, v); if (a >= 0x4000) mem[a] = v; }

/* ---- tape playback ---- */
static void tape_advance(uint32_t t);
static uint32_t tape_t;  /* frame T up to which the tape has been advanced */

static void tape_next_pulse(void) {
    for (;;) {
        if (tape_pos >= npulses) { tape_playing = 0; tape_level = 0; return; }
        uint32_t p = pulses[tape_pos++];
        switch (P_KIND(p)) {
        case P_EDGE: tape_level ^= 1; tape_remain = P_LEN(p); return;
        case P_LOW:  tape_level = 0; tape_remain = P_LEN(p); return;
        case P_STOP: tape_level ^= 1; tape_playing = 0; tape_remain = 0; return;  /* final edge, then stop */
        }
    }
}

static void tape_advance(uint32_t t) {
    if (t <= tape_t) return;
    uint32_t dt = t - tape_t;
    tape_t = t;
    while (tape_playing && dt) {
        if (tape_remain > dt) { tape_remain -= dt; return; }
        dt -= tape_remain;
        tape_remain = 0;
        tape_next_pulse();
    }
}

/* ---- audio: box-filtered beeper at the host sample rate ---- */
static inline float audio_level(void) {
    float v = (out_fe & 0x10) ? 0.5f : 0.0f;
    if (tape_playing) v += tape_level ? 0.12f : 0.0f;
    return v;
}
static void audio_to(uint32_t t) {
    float lvl = audio_level();
    while (a_next <= t) {
        a_acc += lvl * (a_next - a_last);
        float x = (float)(a_acc / a_tps);
        /* DC-blocking high-pass so an idle speaker is silent */
        float y = x - a_hp_x + 0.995f * a_hp_y;
        a_hp_x = x; a_hp_y = y;
        if (an < AUDIO_MAX) abuf[an++] = muted ? 0.0f : y;
        a_acc = 0;
        a_last = a_next;
        a_next += a_tps;
    }
    a_acc += lvl * (t - a_last);
    a_last = t;
}

/* ---- border tracking ---- */
static void border_to(uint32_t t) {
    uint32_t line = t / LINE_T;
    if (line > 311) line = 311;
    while (border_upto <= line) border_line[border_upto++] = border;
}

/* ---- ports ---- */
#ifndef PORT_HOOK
#define PORT_HOOK(p)
#endif
static uint8_t port_in(uint16_t port) {
    PORT_HOOK(port);
    if (!(port & 1)) {
        uint8_t v = 0x1F;
        uint8_t hi = port >> 8;
        for (int r = 0; r < 8; r++) if (!(hi & (1 << r))) v &= keys[r];
        tape_advance(T);
        uint8_t ear = tape_playing ? tape_level : ((out_fe >> 4) & 1);
        return v | 0xA0 | (ear << 6);
    }
    if ((port & 0xFF) == 0x1F) return kempston;
    return 0xFF;
}

static void port_out(uint16_t port, uint8_t v) {
    if (!(port & 1)) {
        border_to(T);
        audio_to(T);
        border = v & 7;
        out_fe = v;
    }
}

#include "z80.c"

/* ---- video ---- */
static void render(void) {
    border_to(FRAME_T - 1);
    uint32_t flash = (frames >> 4) & 1;
    for (int y = 0; y < SCR_H; y++) {
        uint32_t *row = fb + y * SCR_W;
        uint32_t bc_ = palette[border_line[y + FIRST_LINE]];
        int sy = y - 32;
        if (sy < 0 || sy >= 192) {
            for (int x = 0; x < SCR_W; x++) row[x] = bc_;
            continue;
        }
        for (int x = 0; x < 32; x++) { row[x] = bc_; row[SCR_W - 1 - x] = bc_; }
        uint16_t pix = 0x4000 | ((sy & 0xC0) << 5) | ((sy & 0x07) << 8) | ((sy & 0x38) << 2);
        uint16_t att = 0x5800 + (sy >> 3) * 32;
        for (int cx = 0; cx < 32; cx++) {
            uint8_t p = mem[pix + cx], a = mem[att + cx];
            uint8_t bright = (a & 0x40) >> 3;
            uint32_t ink = palette[(a & 7) | bright], paper = palette[((a >> 3) & 7) | bright];
            if ((a & 0x80) && flash) { uint32_t t = ink; ink = paper; paper = t; }
            uint32_t *o = row + 32 + cx * 8;
            for (int b = 0; b < 8; b++) o[b] = (p & (0x80 >> b)) ? ink : paper;
        }
    }
}

/* ---- TZX / TAP parsing into a pulse list ---- */
static int add_pulse(uint32_t kind, uint32_t len) {
    if (npulses >= TAPE_MAX_PULSES) return 0;
    pulses[npulses++] = (kind << 28) | (len & 0x0FFFFFFF);
    return 1;
}
static void add_data(const uint8_t *d, uint32_t n, uint32_t zero, uint32_t one, int last_bits) {
    for (uint32_t i = 0; i < n; i++) {
        int bits = (i == n - 1) ? last_bits : 8;
        for (int b = 0; b < bits; b++) {
            uint32_t len = (d[i] & (0x80 >> b)) ? one : zero;
            add_pulse(P_EDGE, len); add_pulse(P_EDGE, len);
        }
    }
}
/* A pause starts with an edge that terminates the last pulse, holds 1ms, then goes low. */
static void add_pause(uint32_t ms) {
    if (!ms) return;
    add_pulse(P_EDGE, CPU_HZ / 1000);
    if (ms > 1) add_pulse(P_LOW, (ms - 1) * (CPU_HZ / 1000));
}
static void add_block_turbo(const uint8_t *d, uint32_t n, uint32_t pilot, uint32_t pilot_n,
                            uint32_t s1, uint32_t s2, uint32_t zero, uint32_t one, int last_bits, uint32_t pause) {
    for (uint32_t i = 0; i < pilot_n; i++) add_pulse(P_EDGE, pilot);
    add_pulse(P_EDGE, s1); add_pulse(P_EDGE, s2);
    add_data(d, n, zero, one, last_bits);
    add_pause(pause);
}
static void add_std_block(const uint8_t *d, uint32_t n, uint32_t pause) {
    add_block_turbo(d, n, 2168, (n && d[0] < 128) ? 8063 : 3223, 667, 735, 855, 1710, 8, pause);
}
static inline uint32_t w16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static inline uint32_t w24(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16); }
static inline uint32_t w32(const uint8_t *p) { return w24(p) | ((uint32_t)p[3] << 24); }

static void mark_block(uint32_t off) {
    if (nblocks < TAPE_MAX_BLOCKS) { block_start[nblocks] = npulses; block_offset[nblocks] = off; nblocks++; }
}

static int parse_tap(void) {
    uint32_t p = 0;
    while (p + 2 <= tape_len) {
        uint32_t n = w16(tape_data + p);
        if (p + 2 + n > tape_len) break;
        mark_block(p);
        add_std_block(tape_data + p + 2, n, 1000);
        p += 2 + n;
    }
    return nblocks > 0;
}

static int parse_tzx(void) {
    const uint8_t *d = tape_data;
    uint32_t p = 10, loop_start = 0, loop_count = 0;
    while (p < tape_len) {
        uint8_t id = d[p];
        uint32_t off = p++;
        const uint8_t *b = d + p;
        mark_block(off);
        switch (id) {
        case 0x10: { uint32_t n = w16(b + 2); add_std_block(b + 4, n, w16(b)); p += 4 + n; break; }
        case 0x11: { uint32_t n = w24(b + 15);
            add_block_turbo(b + 18, n, w16(b), w16(b + 10), w16(b + 2), w16(b + 4), w16(b + 6), w16(b + 8), b[12], w16(b + 13));
            p += 18 + n; break; }
        case 0x12: for (uint32_t i = 0; i < w16(b + 2); i++) add_pulse(P_EDGE, w16(b)); p += 4; break;
        case 0x13: for (uint32_t i = 0; i < b[0]; i++) add_pulse(P_EDGE, w16(b + 1 + 2 * i)); p += 1 + 2 * b[0]; break;
        case 0x14: { uint32_t n = w24(b + 7); add_data(b + 10, n, w16(b), w16(b + 2), b[4]); add_pause(w16(b + 5)); p += 10 + n; break; }
        case 0x15: { /* direct recording */
            uint32_t tps = w16(b), n = w24(b + 5), last = b[4];
            uint8_t lvl = tape_level; uint32_t run = 0; uint8_t cur = 2;
            for (uint32_t i = 0; i < n; i++) {
                int bits = i == n - 1 ? last : 8;
                for (int k = 0; k < bits; k++) {
                    uint8_t v = (b[8 + i] >> (7 - k)) & 1;
                    if (v != cur && run) { add_pulse(P_EDGE, run); run = 0; }
                    cur = v; run += tps;
                }
            }
            if (run) add_pulse(P_EDGE, run);
            (void)lvl;
            add_pause(w16(b + 2)); p += 8 + n; break; }
        case 0x20: { uint32_t ms = w16(b); if (ms) add_pause(ms); else add_pulse(P_STOP, 0); p += 2; break; }
        case 0x21: p += 1 + b[0]; break;
        case 0x22: break;
        case 0x23: p += 2; break;
        case 0x24: loop_count = w16(b); p += 2; loop_start = p; break;
        case 0x25: if (loop_count > 1) { loop_count--; p = loop_start; } break;
        case 0x26: p += 1 + 2 * w16(b); break;
        case 0x27: break;
        case 0x28: p += 2 + w16(b); break;
        case 0x2A: p += 4; break;  /* stop if 48K: we always continue (autoplay restarts) */
        case 0x2B: p += 5; break;
        case 0x30: p += 1 + b[0]; break;
        case 0x31: p += 2 + b[1]; break;
        case 0x32: p += 2 + w16(b); break;
        case 0x33: p += 1 + 3 * b[0]; break;
        case 0x35: p += 20 + w32(b + 16); break;
        case 0x5A: p += 9; break;
        default:
            /* unknown block: extension rule says a 4-byte length follows the id */
            if (p + 4 > tape_len) return nblocks > 0;
            p += 4 + w32(b); break;
        }
    }
    return nblocks > 0;
}

/* ---- exported API ---- */
EXPORT uint8_t *rom_ptr(void) { return rom_buf; }
EXPORT uint8_t *tape_ptr(void) { return tape_data; }
EXPORT uint32_t tape_capacity(void) { return TAPE_MAX_BYTES; }
EXPORT uint32_t *fb_ptr(void) { return fb; }
EXPORT float *audio_ptr(void) { return abuf; }
EXPORT uint8_t *mem_ptr(void) { return mem; }
EXPORT uint8_t *keys_ptr(void) { return keys; }

EXPORT void set_sample_rate(uint32_t rate) { a_tps = (double)CPU_HZ / (double)rate; a_next = a_tps; a_last = 0; a_acc = 0; }
EXPORT void set_muted(uint32_t m) { muted = m ? 1 : 0; }
EXPORT void set_kempston(uint32_t v) { kempston = v & 0x1F; }

EXPORT void reset(void) {
    memcpy_(mem, rom_buf, 16384);
    memset_(mem + 16384, 0, 49152);
    for (int i = 0; i < 8; i++) keys[i] = 0xFF;
    z80_reset();
    T = 0; border = 7; out_fe = 0; border_upto = 0; tape_t = 0;
}

EXPORT void init(void) {
    static const uint8_t lv[2] = { 0xD7, 0xFF };
    for (int i = 0; i < 16; i++) {
        uint8_t v = lv[i >> 3];
        uint32_t r = (i & 2) ? v : 0, g = (i & 4) ? v : 0, b = (i & 1) ? v : 0;
        palette[i] = 0xFF000000u | (b << 16) | (g << 8) | r;
    }
    z80_tables();
    reset();
}

/* Parse the loaded tape bytes. Returns the number of blocks (0 on failure). */
EXPORT uint32_t tape_load(uint32_t len) {
    tape_len = len > TAPE_MAX_BYTES ? TAPE_MAX_BYTES : len;
    npulses = 0; nblocks = 0; tape_pos = 0; tape_remain = 0; tape_playing = 0; tape_level = 0;
    int ok;
    if (tape_len >= 10 && tape_data[0] == 'Z' && tape_data[1] == 'X' && tape_data[7] == 0x1A) ok = parse_tzx();
    else ok = parse_tap();
    return ok ? nblocks : 0;
}
EXPORT uint32_t tape_block_count(void) { return nblocks; }
EXPORT uint32_t tape_block_offset(uint32_t i) { return i < nblocks ? block_offset[i] : 0; }
EXPORT uint32_t tape_block_start(uint32_t i) { return i < nblocks ? block_start[i] : 0; }
EXPORT void tape_seek_block(uint32_t i) {
    tape_pos = i < nblocks ? block_start[i] : npulses;
    tape_remain = 0; tape_level = 0;
}
EXPORT uint32_t tape_position(void) { return tape_pos; }
EXPORT void tape_set_position(uint32_t p) { tape_pos = p < npulses ? p : npulses; tape_remain = 0; tape_level = 0; }
EXPORT uint32_t tape_length(void) { return npulses; }
EXPORT uint32_t tape_is_playing(void) { return tape_playing; }
EXPORT void tape_play(void) { if (tape_pos < npulses) { tape_playing = 1; tape_t = T; } }
EXPORT void tape_stop(void) { tape_playing = 0; }
EXPORT void tape_set_autoplay(uint32_t on) { tape_autoplay = on ? 1 : 0; }

/* Run one 50Hz frame. Returns number of audio samples produced. */
EXPORT uint32_t run_frame(void) {
    an = 0;
    border_upto = 0;
    while (T < FRAME_T) {
        if (T < 32) z80_interrupt();
        ei_pending = 0;
        /* ROM LD-BYTES entry: press play automatically */
        if (pc.w == 0x0556 && tape_autoplay && !tape_playing && tape_pos < npulses) tape_play();
        z80_step();
    }
    tape_advance(FRAME_T);
    audio_to(FRAME_T);
    render();
    T -= FRAME_T;
    tape_t = T;
    a_next -= FRAME_T; a_last -= FRAME_T;
    frames++;
    return an;
}

/* ---- snapshot state ---- */
#define STATE_SIZE (49152 + 64)
static uint8_t state_buf[STATE_SIZE];
EXPORT uint8_t *state_ptr(void) { return state_buf; }
EXPORT uint32_t state_size(void) { return STATE_SIZE; }

EXPORT void state_save(void) {
    uint8_t *s = state_buf;
    uint16_t r16[] = { af.w, bc.w, de.w, hl.w, ix.w, iy.w, sp.w, pc.w, wz.w, af_.w, bc_.w, de_.w, hl_.w };
    s[0] = 'Z'; s[1] = 'X'; s[2] = 'W'; s[3] = 1;
    for (int i = 0; i < 13; i++) { s[4 + 2 * i] = r16[i] & 0xFF; s[5 + 2 * i] = r16[i] >> 8; }
    s[30] = I; s[31] = R; s[32] = R7; s[33] = iff1; s[34] = iff2; s[35] = im; s[36] = halted;
    s[37] = border; s[38] = out_fe;
    s[39] = T & 0xFF; s[40] = (T >> 8) & 0xFF; s[41] = (T >> 16) & 0xFF;
    memcpy_(s + 64, mem + 16384, 49152);
}

EXPORT uint32_t state_load(void) {
    uint8_t *s = state_buf;
    if (s[0] != 'Z' || s[1] != 'X' || s[2] != 'W' || s[3] != 1) return 0;
    reg16 *r[] = { &af, &bc, &de, &hl, &ix, &iy, &sp, &pc, &wz, &af_, &bc_, &de_, &hl_ };
    for (int i = 0; i < 13; i++) r[i]->w = s[4 + 2 * i] | (s[5 + 2 * i] << 8);
    I = s[30]; R = s[31]; R7 = s[32]; iff1 = s[33]; iff2 = s[34]; im = s[35]; halted = s[36];
    border = s[37]; out_fe = s[38];
    T = s[39] | (s[40] << 8) | (s[41] << 16);
    ei_pending = 0;
    memcpy_(mem, rom_buf, 16384);
    memcpy_(mem + 16384, s + 64, 49152);
    tape_playing = 0;
    return 1;
}
