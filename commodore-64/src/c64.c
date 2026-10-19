/*
 * c64.c - a compact Commodore 64 (PAL) emulator core built for WebAssembly.
 *
 *   6510 CPU   : all documented + the stable undocumented opcodes, per-instruction timing
 *   VIC-II     : line-based renderer (text/bitmap/ECM/multicolor modes, sprites with
 *                priorities/collisions, raster IRQs, badline cycle stealing, border flip-flops)
 *   CIA 1/2    : timers A/B, interrupt control, keyboard matrix, joysticks, VIC bank select
 *   SID        : 3 voices, ADSR, sync/ring, combined waveforms, state-variable filter
 *   KERNAL     : no Commodore ROMs are used. A tiny replacement KERNAL provides the IRQ/NMI
 *                entry code and traps the jump table to C ("high level emulation").
 */
#include <stdint.h>
#include "chargen.h"

typedef uint8_t u8;
typedef int8_t i8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t i32;

#ifdef __wasm__
#define EXPORT(n) __attribute__((export_name(n)))
__attribute__((import_module("env"), import_name("host_log"))) void host_log(const char *s, int len);
void *memset(void *d, int c, unsigned long n) { __builtin_memset(d, c, n); return d; }
void *memcpy(void *d, const void *s, unsigned long n) { __builtin_memcpy(d, s, n); return d; }
#else
#define EXPORT(n)
void host_log(const char *s, int len);
#endif

#define CPU_HZ 985248
#define LINES 312
#define CYCLES_PER_LINE 63
#define SCR_W 384
#define SCR_H 272
#define FIRST_LINE 16

/* ------------------------------------------------------------------ logging */
static char logbuf[256];
static int logpos;
static void lput(char c) { if (logpos < 255) logbuf[logpos++] = c; }
static void lstr(const char *s) { while (*s) lput(*s++); }
static void lhex(u32 v, int digits) {
  for (int i = digits - 1; i >= 0; i--) lput("0123456789ABCDEF"[(v >> (i * 4)) & 15]);
}
static void ldec(i32 v) {
  char t[12]; int n = 0;
  if (v < 0) { lput('-'); v = -v; }
  do { t[n++] = '0' + v % 10; v /= 10; } while (v);
  while (n) lput(t[--n]);
}
static void lflush(void) { host_log(logbuf, logpos); logpos = 0; }

/* ------------------------------------------------------------------ state */
static u8 ram[65536];
static u8 kernal[8192];
static u8 basic_rom[8192];
static u8 colorram[1024];
static u8 port_ddr, port_dat, memcfg;

static u8 A, X, Y, SP, P;
static u16 PC;
static int jammed;
static int nmi_pending;
static int stall;          /* cycles the CPU is halted by VIC DMA */
static u32 frame_count;
static int log_unknown = 1;

enum { FC = 1, FZ = 2, FI = 4, FD = 8, FB = 16, FU = 32, FV = 64, FN = 128 };

/* ------------------------------------------------------------------ VIC-II */
static struct {
  u8 reg[64];
  int raster, cycle;
  u8 irq_flags;
  int den_latch, badline, display, vc, vcbase, rc;
  int vborder;
  u8 sp_active, sp_expff;
  u8 mcbase[8];
  u8 cbuf[40], colbuf[40];
  u8 coll_ss, coll_sb;
  int frame_done;
} vic;

static u32 framebuffer[SCR_W * SCR_H];
static const u32 palette[16] = {
  /* Pepto PAL palette, stored as 0xAABBGGRR (little-endian RGBA bytes) */
  0xFF000000, 0xFFFFFFFF, 0xFF2B3768, 0xFFB2A470, 0xFF863D6F, 0xFF438D58, 0xFF792835, 0xFF6FC7B8,
  0xFF254F6F, 0xFF003943, 0xFF59679A, 0xFF444444, 0xFF6C6C6C, 0xFF84D29A, 0xFFB55E6C, 0xFF959595,
};

/* ------------------------------------------------------------------ CIA */
typedef struct {
  u8 pra, prb, ddra, ddrb;
  u16 ta, tb, la, lb;
  u8 cra, crb, icr, mask, irq;
  u8 tod[4], alarm[4], sdr;
  int is_nmi;
} CIA;
static CIA cia1, cia2;
static u8 keymat[8];       /* keymat[column] = rows pressed (bit per PB line) */
static u8 joy[3];          /* joy[1], joy[2]: bits 0-4 = up,down,left,right,fire (1 = active) */
static int restore_key;

/* ------------------------------------------------------------------ SID */
typedef struct {
  u32 acc;
  u16 freq, pw;
  u8 ctrl, ad, sr;
  u32 lfsr;
  int env;
  int state;          /* 0 attack, 1 decay/sustain, 2 release */
  u32 rate_cnt;
  int exp_cnt;
  u8 msb_rose;
} Voice;
static struct {
  Voice v[3];
  u8 reg[32];
  float lp, bp;
  float sample_period, sample_frac;
  int sample_rate;
} sid;
#define AUDIO_MAX 16384
static float audio_buf[AUDIO_MAX];
static int audio_count;

/* ================================================================== memory */
static inline void update_cfg(void) { memcfg = (port_dat | ~port_ddr) & 7; }

static u8 vic_read(int r);
static void vic_write(int r, u8 v);
static u8 cia_read(CIA *c, int r);
static void cia_write(CIA *c, int r, u8 v);
static u8 sid_read(int r);
static void sid_write(int r, u8 v);

static u8 io_read(u16 a) {
  switch ((a >> 8) & 0x0F) {
  case 0: case 1: case 2: case 3: return vic_read(a & 0x3F);
  case 4: case 5: case 6: case 7: return sid_read(a & 0x1F);
  case 8: case 9: case 10: case 11: return colorram[a & 0x3FF] | 0xF0;
  case 12: return cia_read(&cia1, a & 15);
  case 13: return cia_read(&cia2, a & 15);
  default: return 0xFF;
  }
}

static void io_write(u16 a, u8 v) {
  switch ((a >> 8) & 0x0F) {
  case 0: case 1: case 2: case 3: vic_write(a & 0x3F, v); break;
  case 4: case 5: case 6: case 7: sid_write(a & 0x1F, v); break;
  case 8: case 9: case 10: case 11: colorram[a & 0x3FF] = v & 15; break;
  case 12: cia_write(&cia1, a & 15, v); break;
  case 13: cia_write(&cia2, a & 15, v); break;
  default: break;
  }
}

static inline u8 rd(u16 a) {
  if (a < 0xA000) {
    if (a >= 2) return ram[a];
    return a ? ((port_dat & port_ddr) | (~port_ddr & 0x17)) : port_ddr;
  }
  if (a < 0xC000) return ((memcfg & 3) == 3) ? basic_rom[a - 0xA000] : ram[a];
  if (a < 0xD000) return ram[a];
  if (a < 0xE000) {
    if (!(memcfg & 3)) return ram[a];
    return (memcfg & 4) ? io_read(a) : chargen_rom[a - 0xD000];
  }
  return (memcfg & 2) ? kernal[a - 0xE000] : ram[a];
}

static inline void wr(u16 a, u8 v) {
  if (a < 2) {
    ram[a] = v;
    if (a) port_dat = v; else port_ddr = v;
    update_cfg();
    return;
  }
  if (a >= 0xD000 && a < 0xE000 && (memcfg & 3) && (memcfg & 4)) { io_write(a, v); return; }
  ram[a] = v;
}

/* VIC sees 16K banks; the character ROM is visible at $1000-$1FFF in banks 0 and 2 */
static inline u16 vic_bank(void) { return (u16)((3 - ((cia2.pra | ~cia2.ddra) & 3)) << 14); }
static inline u8 vrd(u16 bank, u16 a) {
  a &= 0x3FFF;
  if ((a & 0x3000) == 0x1000 && !(bank & 0x4000)) return chargen_rom[a & 0xFFF];
  return ram[bank | a];
}

/* ================================================================== VIC-II */
static inline int vic_irq_line(void) { return (vic.irq_flags & vic.reg[0x1A] & 15) != 0; }
static inline int raster_cmp(void) { return vic.reg[0x12] | ((vic.reg[0x11] & 0x80) << 1); }

static u8 vic_read(int r) {
  switch (r) {
  case 0x11: return (vic.reg[0x11] & 0x7F) | ((vic.raster & 0x100) >> 1);
  case 0x12: return vic.raster & 0xFF;
  case 0x13: case 0x14: return 0;
  case 0x16: return vic.reg[r] | 0xC0;
  case 0x18: return vic.reg[r] | 0x01;
  case 0x19: return vic.irq_flags | 0x70 | (vic_irq_line() ? 0x80 : 0);
  case 0x1A: return vic.reg[r] | 0xF0;
  case 0x1E: { u8 v = vic.coll_ss; vic.coll_ss = 0; return v; }
  case 0x1F: { u8 v = vic.coll_sb; vic.coll_sb = 0; return v; }
  default:
    if (r >= 0x20 && r <= 0x2E) return vic.reg[r] | 0xF0;
    if (r >= 0x2F) return 0xFF;
    return vic.reg[r];
  }
}

static void vic_write(int r, u8 v) {
  switch (r) {
  case 0x11: case 0x12: {
    int old = raster_cmp();
    vic.reg[r] = v;
    if (raster_cmp() != old && raster_cmp() == vic.raster) vic.irq_flags |= 1;
    if (r == 0x11 && vic.raster == 0x30 && (v & 0x10)) vic.den_latch = 1;
    break;
  }
  case 0x19: vic.irq_flags &= ~v & 15; break;
  case 0x1A: vic.reg[r] = v & 15; break;
  case 0x1E: case 0x1F: break;
  default: vic.reg[r] = v; break;
  }
}

static void vic_check_badline(void) {
  int r = vic.raster;
  if (r == 0x30 && (vic.reg[0x11] & 0x10)) vic.den_latch = 1;
  vic.badline = vic.den_latch && r >= 0x30 && r <= 0xF7 && (r & 7) == (vic.reg[0x11] & 7);
  if (vic.badline) stall += 40;
}

static u8 linebuf[SCR_W + 64];
static u8 fgmask[SCR_W + 64];
static u8 spmask[SCR_W + 64];
static u8 spcol[SCR_W + 64];

static void vic_render_line(void) {
  const int r = vic.raster;
  const u8 *reg = vic.reg;
  const u16 bank = vic_bank();
  const u16 vm = (u16)((reg[0x18] >> 4) << 10);
  const u16 cb = (u16)(((reg[0x18] >> 1) & 7) << 11);
  const int ecm = reg[0x11] & 0x40, bmm = reg[0x11] & 0x20, mcm = reg[0x16] & 0x10;
  const int mode = (ecm ? 4 : 0) | (bmm ? 2 : 0) | (mcm ? 1 : 0);
  const int rsel = reg[0x11] & 8, csel = reg[0x16] & 8;
  const int top = rsel ? 51 : 55, bottom = rsel ? 251 : 247;
  const int visible = r >= FIRST_LINE && r < FIRST_LINE + SCR_H;

  /* border flip-flop (left-edge comparison) */
  if (r == bottom) vic.vborder = 1;
  if (r == top && (reg[0x11] & 0x10)) vic.vborder = 0;

  if (vic.badline) {
    vic.display = 1;
    vic.rc = 0;
    for (int i = 0; i < 40; i++) {
      int idx = (vic.vcbase + i) & 0x3FF;
      vic.cbuf[i] = vrd(bank, vm | idx);
      vic.colbuf[i] = colorram[idx];
    }
  }
  vic.vc = vic.vcbase;

  if (visible) {
    const u8 bg0 = reg[0x21] & 15;
    for (int i = 0; i < SCR_W + 16; i++) { linebuf[i] = bg0; fgmask[i] = 0; spmask[i] = 0; }
    const int xs = reg[0x16] & 7;
    for (int col = 0; col < 40; col++) {
      u8 c = 0, cc = 0, gd;
      if (vic.display) { c = vic.cbuf[col]; cc = vic.colbuf[col]; }
      u16 addr;
      if (!vic.display) addr = ecm ? 0x39FF : 0x3FFF;
      else if (bmm) addr = (u16)(((reg[0x18] & 8) << 10) | (((vic.vc + col) & 0x3FF) << 3) | vic.rc);
      else addr = (u16)(cb | (c << 3) | vic.rc);
      if (ecm && vic.display) addr &= 0x39FF;
      gd = vrd(bank, addr);
      u8 *o = &linebuf[32 + xs + col * 8];
      u8 *f = &fgmask[32 + xs + col * 8];
      switch (mode) {
      case 0:
        for (int b = 0; b < 8; b++) { int on = (gd >> (7 - b)) & 1; o[b] = on ? (cc & 15) : bg0; f[b] = on; }
        break;
      case 4: {
        u8 bgc = reg[0x21 + (c >> 6)] & 15;
        for (int b = 0; b < 8; b++) { int on = (gd >> (7 - b)) & 1; o[b] = on ? (cc & 15) : bgc; f[b] = on; }
        break;
      }
      case 1:
        if (cc & 8) {
          u8 cols[4] = { bg0, (u8)(reg[0x22] & 15), (u8)(reg[0x23] & 15), (u8)(cc & 7) };
          for (int b = 0; b < 8; b += 2) {
            int p = (gd >> (6 - b)) & 3;
            o[b] = o[b + 1] = cols[p]; f[b] = f[b + 1] = p >= 2;
          }
        } else {
          for (int b = 0; b < 8; b++) { int on = (gd >> (7 - b)) & 1; o[b] = on ? (cc & 7) : bg0; f[b] = on; }
        }
        break;
      case 2:
        for (int b = 0; b < 8; b++) { int on = (gd >> (7 - b)) & 1; o[b] = on ? (c >> 4) : (c & 15); f[b] = on; }
        break;
      case 3: {
        u8 cols[4] = { bg0, (u8)(c >> 4), (u8)(c & 15), (u8)(cc & 15) };
        for (int b = 0; b < 8; b += 2) {
          int p = (gd >> (6 - b)) & 3;
          o[b] = o[b + 1] = cols[p]; f[b] = f[b + 1] = p >= 2;
        }
        break;
      }
      default: /* invalid modes display black */
        if (mode & 1) {
          for (int b = 0; b < 8; b += 2) { int p = (gd >> (6 - b)) & 3; o[b] = o[b + 1] = 0; f[b] = f[b + 1] = p >= 2; }
        } else {
          for (int b = 0; b < 8; b++) { o[b] = 0; f[b] = (gd >> (7 - b)) & 1; }
        }
        break;
      }
    }

    /* sprites: drawn 7..0 so the lowest numbered sprite ends on top */
    if (vic.sp_active) {
      u8 ss = 0, sb = 0;
      for (int n = 7; n >= 0; n--) {
        if (!((vic.sp_active >> n) & 1)) continue;
        u16 ptr = (u16)(vrd(bank, vm | 0x3F8 | n) << 6);
        u8 mb = vic.mcbase[n];
        u32 d = (u32)vrd(bank, ptr + mb) << 16 | (u32)vrd(bank, ptr + mb + 1) << 8 | vrd(bank, ptr + mb + 2);
        int x = reg[n * 2] | (((reg[0x10] >> n) & 1) << 8);
        if (x >= 0x1F8) x -= 504;
        int ox = x + 8;
        int mc = (reg[0x1C] >> n) & 1, xe = (reg[0x1D] >> n) & 1;
        u8 colr = reg[0x27 + n] & 15;
        u8 cols[4] = { 0, (u8)(reg[0x25] & 15), colr, (u8)(reg[0x26] & 15) };
        for (int i = 0; i < 24; i++) {
          int p;
          if (mc) p = (d >> (22 - (i & ~1))) & 3;
          else p = ((d >> (23 - i)) & 1) ? 2 : 0;
          if (!p) continue;
          for (int k = 0; k <= xe; k++) {
            int pos = ox + i * (xe + 1) + k;
            if (pos < 0 || pos >= SCR_W) continue;
            if (spmask[pos]) ss |= spmask[pos] | (1 << n);
            if (fgmask[pos]) sb |= 1 << n;
            spmask[pos] |= 1 << n;
            spcol[pos] = cols[p];
          }
        }
      }
      for (int pos = 0; pos < SCR_W; pos++) {
        u8 m = spmask[pos];
        if (!m) continue;
        int n = __builtin_ctz(m);
        if (!((reg[0x1B] >> n) & 1) || !fgmask[pos]) linebuf[pos] = spcol[pos];
      }
      if (ss) { if (!vic.coll_ss) vic.irq_flags |= 4; vic.coll_ss |= ss; }
      if (sb) { if (!vic.coll_sb) vic.irq_flags |= 2; vic.coll_sb |= sb; }
    }

    /* borders */
    const u8 bc = reg[0x20] & 15;
    if (vic.vborder) {
      for (int i = 0; i < SCR_W; i++) linebuf[i] = bc;
    } else {
      int left = csel ? 32 : 39, right = csel ? 352 : 343;
      for (int i = 0; i < left; i++) linebuf[i] = bc;
      for (int i = right; i < SCR_W; i++) linebuf[i] = bc;
    }
    u32 *out = &framebuffer[(r - FIRST_LINE) * SCR_W];
    for (int i = 0; i < SCR_W; i++) out[i] = palette[linebuf[i]];
  }

  /* video counter / row counter update (cycle 58 logic) */
  if (vic.display) vic.vc = vic.vcbase + 40;
  if (vic.rc == 7) {
    vic.vcbase = vic.vc;
    if (!vic.badline) vic.display = 0;
  }
  if (vic.display) vic.rc = (vic.rc + 1) & 7;

  /* sprite DMA bookkeeping */
  for (int n = 0; n < 8; n++) {
    u8 bit = 1 << n;
    if (vic.sp_active & bit) {
      int adv = 1;
      if ((reg[0x17] >> n) & 1) { vic.sp_expff ^= bit; adv = (vic.sp_expff & bit) != 0; }
      if (adv) {
        vic.mcbase[n] += 3;
        if (vic.mcbase[n] >= 63) vic.sp_active &= ~bit;
      }
    }
    if (!(vic.sp_active & bit) && (reg[0x15] & bit) && reg[1 + n * 2] == (r & 0xFF)) {
      vic.sp_active |= bit;
      vic.sp_expff |= bit;
      vic.mcbase[n] = 0;
    }
  }
  if (vic.sp_active) stall += 2 * __builtin_popcount(vic.sp_active);

  /* border flip-flop (cycle 63 comparison) */
  if (r == bottom) vic.vborder = 1;
  if (r == top && (reg[0x11] & 0x10)) vic.vborder = 0;
}

static void tod_tick(CIA *c);

static void (*line_hook)(void);   /* debug: called before each raster line is drawn */

static void vic_end_line(void) {
  if (line_hook) line_hook();
  vic_render_line();
  vic.badline = 0;
  if (++vic.raster == LINES) {
    vic.raster = 0;
    vic.vcbase = 0;
    vic.den_latch = 0;
    vic.frame_done = 1;
    frame_count++;
    if (frame_count % 5 == 0) { tod_tick(&cia1); tod_tick(&cia2); }
  }
  if (vic.raster == raster_cmp()) vic.irq_flags |= 1;
}

/* ================================================================== CIA */
static void cia_update_irq(CIA *c) {
  if ((c->icr & c->mask & 0x1F) && !c->irq) {
    c->irq = 1;
    if (c->is_nmi) nmi_pending = 1;
  }
}

static inline void cia_tick(CIA *c) {
  int ta_uf = 0;
  if (c->cra & 1) {
    if (c->ta == 0) {
      ta_uf = 1;
      c->icr |= 1;
      c->ta = c->la;
      if (c->cra & 8) c->cra &= ~1;
    } else c->ta--;
  }
  if (c->crb & 1) {
    int m = (c->crb >> 5) & 3;
    if (m == 0 || (m >= 2 && ta_uf)) {
      if (c->tb == 0) {
        c->icr |= 2;
        c->tb = c->lb;
        if (c->crb & 8) c->crb &= ~1;
      } else c->tb--;
    }
  }
  if (c->icr & c->mask) cia_update_irq(c);
}

static u8 bcd_inc(u8 v) { v++; if ((v & 15) > 9) v += 6; return v; }
static void tod_tick(CIA *c) {
  /* 5 frames ~= 1/10 s */
  c->tod[0] = (c->tod[0] + 1) % 10;
  if (c->tod[0]) goto check;
  c->tod[1] = bcd_inc(c->tod[1]);
  if (c->tod[1] < 0x60) goto check;
  c->tod[1] = 0;
  c->tod[2] = bcd_inc(c->tod[2]);
  if (c->tod[2] < 0x60) goto check;
  c->tod[2] = 0;
  {
    u8 h = c->tod[3] & 0x1F, pm = c->tod[3] & 0x80;
    h = bcd_inc(h);
    if (h == 0x12) pm ^= 0x80;
    if (h == 0x13) h = 1;
    c->tod[3] = h | pm;
  }
check:
  if (c->tod[0] == c->alarm[0] && c->tod[1] == c->alarm[1] && c->tod[2] == c->alarm[2] && c->tod[3] == c->alarm[3]) {
    c->icr |= 4;
    cia_update_irq(c);
  }
}

static u8 cia_read(CIA *c, int r) {
  switch (r) {
  case 0: {
    u8 v = c->pra | ~c->ddra;
    if (c == &cia1) {
      u8 pb = c->prb | ~c->ddrb;
      for (int col = 0; col < 8; col++)
        if (keymat[col] & ~pb) v &= ~(1 << col);
      v &= ~(joy[2] & 0x1F);
    } else {
      v = (v & 0x3F) | 0xC0;
    }
    return v;
  }
  case 1: {
    u8 v = c->prb | ~c->ddrb;
    if (c == &cia1) {
      u8 pa = c->pra | ~c->ddra;
      for (int col = 0; col < 8; col++)
        if (!((pa >> col) & 1)) v &= ~keymat[col];
      v &= ~(joy[1] & 0x1F);
    }
    return v;
  }
  case 2: return c->ddra;
  case 3: return c->ddrb;
  case 4: return c->ta & 0xFF;
  case 5: return c->ta >> 8;
  case 6: return c->tb & 0xFF;
  case 7: return c->tb >> 8;
  case 8: return c->tod[0];
  case 9: return c->tod[1];
  case 10: return c->tod[2];
  case 11: return c->tod[3];
  case 12: return c->sdr;
  case 13: {
    u8 v = c->icr | (c->irq ? 0x80 : 0);
    c->icr = 0;
    c->irq = 0;
    return v;
  }
  case 14: return c->cra;
  case 15: return c->crb;
  }
  return 0xFF;
}

static void cia_write(CIA *c, int r, u8 v) {
  switch (r) {
  case 0: c->pra = v; break;
  case 1: c->prb = v; break;
  case 2: c->ddra = v; break;
  case 3: c->ddrb = v; break;
  case 4: c->la = (c->la & 0xFF00) | v; break;
  case 5: c->la = (c->la & 0x00FF) | (v << 8); if (!(c->cra & 1)) c->ta = c->la; break;
  case 6: c->lb = (c->lb & 0xFF00) | v; break;
  case 7: c->lb = (c->lb & 0x00FF) | (v << 8); if (!(c->crb & 1)) c->tb = c->lb; break;
  case 8: case 9: case 10: case 11:
    if (c->crb & 0x80) c->alarm[r - 8] = v;
    else c->tod[r - 8] = (r == 8) ? (v & 15) : v;
    break;
  case 12: c->sdr = v; break;
  case 13:
    if (v & 0x80) c->mask |= v & 0x1F;
    else c->mask &= ~v;
    cia_update_irq(c);
    break;
  case 14:
    if (v & 0x10) c->ta = c->la;
    c->cra = v & ~0x10;
    break;
  case 15:
    if (v & 0x10) c->tb = c->lb;
    c->crb = v & ~0x10;
    break;
  }
}

/* ================================================================== SID */
static const u32 adsr_rate[16] = { 9, 32, 63, 95, 149, 220, 267, 313, 392, 977, 1954, 3126, 3907, 11720, 19532, 31251 };

static u8 sid_read(int r) {
  if (r == 0x1B) {
    Voice *v = &sid.v[2];
    /* oscillator 3 output (upper 8 bits of waveform) - used by many games as RNG */
    u32 acc = v->acc;
    u32 out = 0xFFF;
    int any = 0;
    if (v->ctrl & 0x10) { out &= ((acc & 0x800000) ? ~acc : acc) >> 11; any = 1; }
    if (v->ctrl & 0x20) { out &= acc >> 12; any = 1; }
    if (v->ctrl & 0x40) { out &= ((acc >> 12) >= v->pw) ? 0xFFF : 0; any = 1; }
    if (v->ctrl & 0x80) {
      u32 l = v->lfsr;
      u32 n = ((l >> 9) & 0x800) | ((l >> 8) & 0x400) | ((l >> 5) & 0x200) | ((l >> 3) & 0x100) |
              ((l >> 2) & 0x080) | ((l << 1) & 0x040) | ((l << 3) & 0x020) | ((l << 4) & 0x010);
      out &= n; any = 1;
    }
    return any ? (out >> 4) & 0xFF : 0;
  }
  if (r == 0x1C) return (u8)sid.v[2].env;
  if (r == 0x19 || r == 0x1A) return 0xFF; /* paddles */
  return 0;
}

static void sid_write(int r, u8 val) {
  sid.reg[r] = val;
  if (r < 21) {
    Voice *v = &sid.v[r / 7];
    switch (r % 7) {
    case 0: v->freq = (v->freq & 0xFF00) | val; break;
    case 1: v->freq = (v->freq & 0x00FF) | (val << 8); break;
    case 2: v->pw = (v->pw & 0xF00) | val; break;
    case 3: v->pw = (v->pw & 0x0FF) | ((val & 15) << 8); break;
    case 4: {
      u8 old = v->ctrl;
      v->ctrl = val;
      if ((val & 1) && !(old & 1)) { v->state = 0; v->rate_cnt = 0; }
      else if (!(val & 1) && (old & 1)) { v->state = 2; }
      if (val & 8) { v->acc = 0; v->lfsr = 0x7FFFF8; }
      break;
    }
    case 5: v->ad = val; break;
    case 6: v->sr = val; break;
    }
  }
}

static inline void voice_env(Voice *v, u32 n) {
  u32 period;
  switch (v->state) {
  case 0: period = adsr_rate[v->ad >> 4]; break;
  case 1: period = adsr_rate[v->ad & 15]; break;
  default: period = adsr_rate[v->sr & 15]; break;
  }
  v->rate_cnt += n;
  while (v->rate_cnt >= period) {
    v->rate_cnt -= period;
    if (v->state == 0) {
      if (++v->env >= 255) { v->env = 255; v->state = 1; }
      continue;
    }
    int sustain = (v->sr >> 4) * 17;
    if (v->state == 1 && v->env <= sustain) { v->env = sustain < v->env ? sustain : v->env; continue; }
    if (v->env == 0) continue;
    int ep = v->env > 93 ? 1 : v->env > 54 ? 2 : v->env > 26 ? 4 : v->env > 14 ? 8 : v->env > 6 ? 16 : 30;
    if (++v->exp_cnt >= ep) {
      v->exp_cnt = 0;
      v->env--;
    }
  }
}

static inline int voice_out(int i) {
  Voice *v = &sid.v[i];
  u32 acc = v->acc;
  u32 out = 0xFFF;
  int any = 0;
  u8 c = v->ctrl;
  if (c & 0x10) {
    u32 msb = acc & 0x800000;
    if (c & 4) msb ^= sid.v[(i + 2) % 3].acc & 0x800000;
    out &= ((msb ? ~acc : acc) >> 11) & 0xFFF;
    any = 1;
  }
  if (c & 0x20) { out &= acc >> 12; any = 1; }
  if (c & 0x40) { out &= (((acc >> 12) >= v->pw) || (c & 8)) ? 0xFFF : 0; any = 1; }
  if (c & 0x80) {
    u32 l = v->lfsr;
    u32 n = ((l >> 9) & 0x800) | ((l >> 8) & 0x400) | ((l >> 5) & 0x200) | ((l >> 3) & 0x100) |
            ((l >> 2) & 0x080) | ((l << 1) & 0x040) | ((l << 3) & 0x020) | ((l << 4) & 0x010);
    out &= n;
    any = 1;
  }
  if (!any) return 0;
  return ((int)out - 0x800) * v->env;
}

static float fsin(float x) {
  float x2 = x * x;
  return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42)));
}

static void sid_clock(u32 n) {
  for (int i = 0; i < 3; i++) {
    Voice *v = &sid.v[i];
    v->msb_rose = 0;
    if (v->ctrl & 8) { voice_env(v, n); continue; }
    u32 old = v->acc;
    u32 nacc = old + (u32)v->freq * n;
    /* noise LFSR clocks on every rising edge of accumulator bit 19 */
    u32 edges = ((nacc + 0x80000) >> 20) - ((old + 0x80000) >> 20);
    if (edges > 23) edges = 23;
    while (edges--) {
      u32 l = v->lfsr;
      v->lfsr = ((l << 1) | (((l >> 22) ^ (l >> 17)) & 1)) & 0x7FFFFF;
    }
    if ((nacc & 0x1000000) || (!(old & 0x800000) && (nacc & 0x800000))) v->msb_rose = 1;
    v->acc = nacc & 0xFFFFFF;
    voice_env(v, n);
  }
  for (int i = 0; i < 3; i++) {
    Voice *v = &sid.v[i];
    if ((v->ctrl & 2) && sid.v[(i + 2) % 3].msb_rose) v->acc = 0;
  }

  sid.sample_frac += (float)n;
  while (sid.sample_frac >= sid.sample_period) {
    sid.sample_frac -= sid.sample_period;
    u8 filt = sid.reg[0x17], modevol = sid.reg[0x18];
    float direct = 0, filtered = 0;
    for (int i = 0; i < 3; i++) {
      if (i == 2 && (modevol & 0x80) && !(filt & 4)) continue;
      float o = (float)voice_out(i) * (1.0f / (2048.0f * 255.0f));
      if (filt & (1 << i)) filtered += o; else direct += o;
    }
    float outf = 0;
    if (modevol & 0x70) {
      int fc = (sid.reg[0x15] & 7) | (sid.reg[0x16] << 3);
      float hz = 30.0f + fc * 5.8f;
      float f = 2.0f * fsin(3.14159265f * hz / (float)sid.sample_rate);
      if (f > 1.2f) f = 1.2f;
      float q = 1.0f / (0.707f + (float)(filt >> 4) / 15.0f);
      float hp = filtered - sid.lp - q * sid.bp;
      sid.bp += f * hp;
      sid.lp += f * sid.bp;
      if (sid.bp > 4) sid.bp = 4; if (sid.bp < -4) sid.bp = -4;
      if (sid.lp > 4) sid.lp = 4; if (sid.lp < -4) sid.lp = -4;
      if (modevol & 0x10) outf += sid.lp;
      if (modevol & 0x20) outf += sid.bp;
      if (modevol & 0x40) outf += hp;
    }
    float s = (direct + outf) * (float)(modevol & 15) / 15.0f * 0.3f;
    if (audio_count < AUDIO_MAX) audio_buf[audio_count++] = s;
  }
}

/* ================================================================== CPU */
static inline void push(u8 v) { ram[0x100 | SP] = v; SP--; }
static inline u8 pop(void) { SP++; return ram[0x100 | SP]; }
static inline void setNZ(u8 v) { P = (P & ~(FN | FZ)) | (v & 0x80) | (v ? 0 : FZ); }

static void interrupt(u16 vec, int brk) {
  push(PC >> 8);
  push(PC & 0xFF);
  push((P & ~FB) | FU | (brk ? FB : 0));
  P |= FI;
  PC = rd(vec) | (rd(vec + 1) << 8);
}

static void adc(u8 v) {
  unsigned c = P & FC;
  if (P & FD) {
    unsigned al = (A & 15) + (v & 15) + c;
    if (al > 9) al += 6;
    unsigned ah = (A >> 4) + (v >> 4) + (al > 15);
    P &= ~(FZ | FN | FV | FC);
    if (((A + v + c) & 0xFF) == 0) P |= FZ;
    if (ah & 8) P |= FN;
    if ((((ah << 4) ^ A) & 0x80) && !((A ^ v) & 0x80)) P |= FV;
    if (ah > 9) ah += 6;
    if (ah > 15) P |= FC;
    A = (u8)((ah << 4) | (al & 15));
  } else {
    unsigned s = A + v + c;
    P &= ~(FV | FC);
    if (~(A ^ v) & (A ^ s) & 0x80) P |= FV;
    if (s > 0xFF) P |= FC;
    A = (u8)s;
    setNZ(A);
  }
}

static void sbc(u8 v) {
  unsigned c = P & FC;
  unsigned s = A - v - (1 - c);
  u8 oa = A;
  P &= ~(FV | FC);
  if ((A ^ v) & (A ^ s) & 0x80) P |= FV;
  if (s < 0x100) P |= FC;
  setNZ((u8)s);
  if (P & FD) {
    int al = (oa & 15) - (v & 15) - (int)(1 - c);
    int ah = (oa >> 4) - (v >> 4);
    if (al & 0x10) { al -= 6; ah--; }
    if (ah & 0x10) ah -= 6;
    A = (u8)((ah << 4) | (al & 15));
  } else A = (u8)s;
}

static inline void cmp(u8 r, u8 v) {
  unsigned t = r - v;
  P = (P & ~FC) | (r >= v ? FC : 0);
  setNZ((u8)t);
}
static inline u8 asl(u8 v) { P = (P & ~FC) | (v >> 7); v <<= 1; setNZ(v); return v; }
static inline u8 lsr(u8 v) { P = (P & ~FC) | (v & 1); v >>= 1; setNZ(v); return v; }
static inline u8 rol(u8 v) { u8 c = P & FC; P = (P & ~FC) | (v >> 7); v = (u8)(v << 1) | c; setNZ(v); return v; }
static inline u8 ror(u8 v) { u8 c = P & FC; P = (P & ~FC) | (v & 1); v = (v >> 1) | (c << 7); setNZ(v); return v; }

static int extra;
static inline u16 a_abs(void) { u16 a = rd(PC) | (rd(PC + 1) << 8); PC += 2; return a; }
static inline u16 a_absx(int pen) { u16 b = a_abs(), a = b + X; if (pen && ((a ^ b) & 0xFF00)) extra++; return a; }
static inline u16 a_absy(int pen) { u16 b = a_abs(), a = b + Y; if (pen && ((a ^ b) & 0xFF00)) extra++; return a; }
static inline u16 a_zp(void) { return rd(PC++); }
static inline u16 a_zpx(void) { return (u8)(rd(PC++) + X); }
static inline u16 a_zpy(void) { return (u8)(rd(PC++) + Y); }
static inline u16 a_indx(void) { u8 z = rd(PC++) + X; return rd(z) | (rd((u8)(z + 1)) << 8); }
static inline u16 a_indy(int pen) {
  u8 z = rd(PC++);
  u16 b = rd(z) | (rd((u8)(z + 1)) << 8), a = b + Y;
  if (pen && ((a ^ b) & 0xFF00)) extra++;
  return a;
}

static void trap(u16 addr);

static const u8 cycles[256] = {
  7,6,0,8,3,3,5,5,3,2,2,2,4,4,6,6, 2,5,0,8,4,4,6,6,2,4,2,7,4,4,7,7,
  6,6,0,8,3,3,5,5,4,2,2,2,4,4,6,6, 2,5,0,8,4,4,6,6,2,4,2,7,4,4,7,7,
  6,6,0,8,3,3,5,5,3,2,2,2,3,4,6,6, 2,5,0,8,4,4,6,6,2,4,2,7,4,4,7,7,
  6,6,0,8,3,3,5,5,4,2,2,2,5,4,6,6, 2,5,0,8,4,4,6,6,2,4,2,7,4,4,7,7,
  2,6,2,6,3,3,3,3,2,2,2,2,4,4,4,4, 2,6,0,6,4,4,4,4,2,5,2,5,5,5,5,5,
  2,6,2,6,3,3,3,3,2,2,2,2,4,4,4,4, 2,5,0,5,4,4,4,4,2,4,2,4,4,4,4,4,
  2,6,2,8,3,3,5,5,2,2,2,2,4,4,6,6, 2,5,0,8,4,4,6,6,2,4,2,7,4,4,7,7,
  2,6,2,8,3,3,5,5,2,2,2,2,4,4,6,6, 2,5,0,8,4,4,6,6,2,4,2,7,4,4,7,7,
};

/* read-modify-write helpers: the 6502 writes the unmodified value first */
#define RMW(addrexpr, op) { u16 a_ = (addrexpr); u8 v_ = rd(a_); wr(a_, v_); v_ = op(v_); wr(a_, v_); }
#define RMWX(addrexpr, op, then) { u16 a_ = (addrexpr); u8 v_ = rd(a_); wr(a_, v_); v_ = op(v_); wr(a_, v_); then; }

static inline u8 inc8(u8 v) { v++; setNZ(v); return v; }
static inline u8 dec8(u8 v) { v--; setNZ(v); return v; }
static inline u8 inc_raw(u8 v) { return v + 1; }
static inline u8 dec_raw(u8 v) { return v - 1; }

static inline void branch(int cond) {
  i8 off = (i8)rd(PC++);
  if (cond) {
    u16 n = PC + off;
    extra += ((n ^ PC) & 0xFF00) ? 2 : 1;
    PC = n;
  }
}

static int cpu_step(void) {
  if (nmi_pending) {
    nmi_pending = 0;
    interrupt(0xFFFA, 0);
    return 7;
  }
  if (!(P & FI) && (vic_irq_line() || cia1.irq)) {
    interrupt(0xFFFE, 0);
    return 7;
  }
  u16 opc_addr = PC;
  u8 op = rd(PC++);
  extra = 0;
  switch (op) {
  /* ---- loads/stores ---- */
  case 0xA9: A = rd(PC++); setNZ(A); break;
  case 0xA5: A = rd(a_zp()); setNZ(A); break;
  case 0xB5: A = rd(a_zpx()); setNZ(A); break;
  case 0xAD: A = rd(a_abs()); setNZ(A); break;
  case 0xBD: A = rd(a_absx(1)); setNZ(A); break;
  case 0xB9: A = rd(a_absy(1)); setNZ(A); break;
  case 0xA1: A = rd(a_indx()); setNZ(A); break;
  case 0xB1: A = rd(a_indy(1)); setNZ(A); break;
  case 0xA2: X = rd(PC++); setNZ(X); break;
  case 0xA6: X = rd(a_zp()); setNZ(X); break;
  case 0xB6: X = rd(a_zpy()); setNZ(X); break;
  case 0xAE: X = rd(a_abs()); setNZ(X); break;
  case 0xBE: X = rd(a_absy(1)); setNZ(X); break;
  case 0xA0: Y = rd(PC++); setNZ(Y); break;
  case 0xA4: Y = rd(a_zp()); setNZ(Y); break;
  case 0xB4: Y = rd(a_zpx()); setNZ(Y); break;
  case 0xAC: Y = rd(a_abs()); setNZ(Y); break;
  case 0xBC: Y = rd(a_absx(1)); setNZ(Y); break;
  case 0x85: wr(a_zp(), A); break;
  case 0x95: wr(a_zpx(), A); break;
  case 0x8D: wr(a_abs(), A); break;
  case 0x9D: wr(a_absx(0), A); break;
  case 0x99: wr(a_absy(0), A); break;
  case 0x81: wr(a_indx(), A); break;
  case 0x91: wr(a_indy(0), A); break;
  case 0x86: wr(a_zp(), X); break;
  case 0x96: wr(a_zpy(), X); break;
  case 0x8E: wr(a_abs(), X); break;
  case 0x84: wr(a_zp(), Y); break;
  case 0x94: wr(a_zpx(), Y); break;
  case 0x8C: wr(a_abs(), Y); break;
  /* ---- transfers ---- */
  case 0xAA: X = A; setNZ(X); break;
  case 0xA8: Y = A; setNZ(Y); break;
  case 0x8A: A = X; setNZ(A); break;
  case 0x98: A = Y; setNZ(A); break;
  case 0xBA: X = SP; setNZ(X); break;
  case 0x9A: SP = X; break;
  /* ---- stack ---- */
  case 0x48: push(A); break;
  case 0x68: A = pop(); setNZ(A); break;
  case 0x08: push(P | FB | FU); break;
  case 0x28: P = (pop() & ~FB) | FU; break;
  /* ---- logic/arith ---- */
#define ALU(base, OPER) \
  case base + 0x09: { u8 v = rd(PC++); OPER; } break; \
  case base + 0x05: { u8 v = rd(a_zp()); OPER; } break; \
  case base + 0x15: { u8 v = rd(a_zpx()); OPER; } break; \
  case base + 0x0D: { u8 v = rd(a_abs()); OPER; } break; \
  case base + 0x1D: { u8 v = rd(a_absx(1)); OPER; } break; \
  case base + 0x19: { u8 v = rd(a_absy(1)); OPER; } break; \
  case base + 0x01: { u8 v = rd(a_indx()); OPER; } break; \
  case base + 0x11: { u8 v = rd(a_indy(1)); OPER; } break;
  ALU(0x00, A |= v; setNZ(A))
  ALU(0x20, A &= v; setNZ(A))
  ALU(0x40, A ^= v; setNZ(A))
  ALU(0x60, adc(v))
  ALU(0xC0, cmp(A, v))
  ALU(0xE0, sbc(v))
#undef ALU
  case 0xEB: sbc(rd(PC++)); break;
  case 0xE0: cmp(X, rd(PC++)); break;
  case 0xE4: cmp(X, rd(a_zp())); break;
  case 0xEC: cmp(X, rd(a_abs())); break;
  case 0xC0: cmp(Y, rd(PC++)); break;
  case 0xC4: cmp(Y, rd(a_zp())); break;
  case 0xCC: cmp(Y, rd(a_abs())); break;
  case 0x24: { u8 v = rd(a_zp()); P = (P & ~(FN | FV | FZ)) | (v & 0xC0) | ((A & v) ? 0 : FZ); } break;
  case 0x2C: { u8 v = rd(a_abs()); P = (P & ~(FN | FV | FZ)) | (v & 0xC0) | ((A & v) ? 0 : FZ); } break;
  /* ---- shifts / inc / dec ---- */
#define SHIFT(base, fn) \
  case base + 0x0A: A = fn(A); break; \
  case base + 0x06: RMW(a_zp(), fn) break; \
  case base + 0x16: RMW(a_zpx(), fn) break; \
  case base + 0x0E: RMW(a_abs(), fn) break; \
  case base + 0x1E: RMW(a_absx(0), fn) break;
  SHIFT(0x00, asl)
  SHIFT(0x20, rol)
  SHIFT(0x40, lsr)
  SHIFT(0x60, ror)
#undef SHIFT
  case 0xE6: RMW(a_zp(), inc8) break;
  case 0xF6: RMW(a_zpx(), inc8) break;
  case 0xEE: RMW(a_abs(), inc8) break;
  case 0xFE: RMW(a_absx(0), inc8) break;
  case 0xC6: RMW(a_zp(), dec8) break;
  case 0xD6: RMW(a_zpx(), dec8) break;
  case 0xCE: RMW(a_abs(), dec8) break;
  case 0xDE: RMW(a_absx(0), dec8) break;
  case 0xE8: X++; setNZ(X); break;
  case 0xC8: Y++; setNZ(Y); break;
  case 0xCA: X--; setNZ(X); break;
  case 0x88: Y--; setNZ(Y); break;
  /* ---- flags ---- */
  case 0x18: P &= ~FC; break;
  case 0x38: P |= FC; break;
  case 0x58: P &= ~FI; break;
  case 0x78: P |= FI; break;
  case 0xB8: P &= ~FV; break;
  case 0xD8: P &= ~FD; break;
  case 0xF8: P |= FD; break;
  /* ---- branches / jumps ---- */
  case 0x10: branch(!(P & FN)); break;
  case 0x30: branch(P & FN); break;
  case 0x50: branch(!(P & FV)); break;
  case 0x70: branch(P & FV); break;
  case 0x90: branch(!(P & FC)); break;
  case 0xB0: branch(P & FC); break;
  case 0xD0: branch(!(P & FZ)); break;
  case 0xF0: branch(P & FZ); break;
  case 0x4C: PC = a_abs(); break;
  case 0x6C: { u16 p = a_abs(); PC = rd(p) | (rd((p & 0xFF00) | ((p + 1) & 0xFF)) << 8); } break;
  case 0x20: { u16 t = a_abs(); PC--; push(PC >> 8); push(PC & 0xFF); PC = t; } break;
  case 0x60: { PC = pop(); PC |= pop() << 8; PC++; } break;
  case 0x40: { P = (pop() & ~FB) | FU; PC = pop(); PC |= pop() << 8; } break;
  case 0x00: PC++; interrupt(0xFFFE, 1); break;
  case 0xEA: break;
  /* ---- undocumented ---- */
#define ILL(base, body) \
  case base + 0x03: RMWX(a_indx(), body##_m, body##_a) break; \
  case base + 0x07: RMWX(a_zp(), body##_m, body##_a) break; \
  case base + 0x0F: RMWX(a_abs(), body##_m, body##_a) break; \
  case base + 0x13: RMWX(a_indy(0), body##_m, body##_a) break; \
  case base + 0x17: RMWX(a_zpx(), body##_m, body##_a) break; \
  case base + 0x1B: RMWX(a_absy(0), body##_m, body##_a) break; \
  case base + 0x1F: RMWX(a_absx(0), body##_m, body##_a) break;
#define SLO_m asl
#define SLO_a (A |= v_, setNZ(A))
#define RLA_m rol
#define RLA_a (A &= v_, setNZ(A))
#define SRE_m lsr
#define SRE_a (A ^= v_, setNZ(A))
#define RRA_m ror
#define RRA_a adc(v_)
#define DCP_m dec_raw
#define DCP_a cmp(A, v_)
#define ISC_m inc_raw
#define ISC_a sbc(v_)
  ILL(0x00, SLO)
  ILL(0x20, RLA)
  ILL(0x40, SRE)
  ILL(0x60, RRA)
  ILL(0xC0, DCP)
  ILL(0xE0, ISC)
#undef ILL
  case 0x87: wr(a_zp(), A & X); break;
  case 0x97: wr(a_zpy(), A & X); break;
  case 0x8F: wr(a_abs(), A & X); break;
  case 0x83: wr(a_indx(), A & X); break;
  case 0xA7: A = X = rd(a_zp()); setNZ(A); break;
  case 0xB7: A = X = rd(a_zpy()); setNZ(A); break;
  case 0xAF: A = X = rd(a_abs()); setNZ(A); break;
  case 0xBF: A = X = rd(a_absy(1)); setNZ(A); break;
  case 0xA3: A = X = rd(a_indx()); setNZ(A); break;
  case 0xB3: A = X = rd(a_indy(1)); setNZ(A); break;
  case 0xAB: A = X = (A | 0xEE) & rd(PC++); setNZ(A); break;
  case 0x0B: case 0x2B: A &= rd(PC++); setNZ(A); P = (P & ~FC) | (A >> 7); break;
  case 0x4B: A &= rd(PC++); A = lsr(A); break;
  case 0x6B: {
    A &= rd(PC++);
    A = (A >> 1) | ((P & FC) << 7);
    setNZ(A);
    P &= ~(FC | FV);
    if (A & 0x40) P |= FC;
    if (((A >> 6) ^ (A >> 5)) & 1) P |= FV;
  } break;
  case 0xCB: { u8 v = rd(PC++); unsigned t = (A & X) - v; P = (P & ~FC) | (t < 0x100 ? FC : 0); X = (u8)t; setNZ(X); } break;
  case 0x8B: A = (A | 0xEE) & X & rd(PC++); setNZ(A); break;
  case 0xBB: { u8 v = rd(a_absy(1)) & SP; A = X = SP = v; setNZ(v); } break;
  case 0x9B: { u16 b = a_abs(), a = b + Y; SP = A & X; wr(a, SP & ((b >> 8) + 1)); } break;
  case 0x93: { u8 z = rd(PC++); u16 b = rd(z) | (rd((u8)(z + 1)) << 8); wr(b + Y, A & X & ((b >> 8) + 1)); } break;
  case 0x9F: { u16 b = a_abs(); wr(b + Y, A & X & ((b >> 8) + 1)); } break;
  case 0x9E: { u16 b = a_abs(); u16 a = b + Y; wr(((a & 0xFF00) != (b & 0xFF00)) ? (u16)((X & ((b >> 8) + 1)) << 8 | (a & 0xFF)) : a, X & ((b >> 8) + 1)); } break;
  case 0x9C: { u16 b = a_abs(); u16 a = b + X; wr(((a & 0xFF00) != (b & 0xFF00)) ? (u16)((Y & ((b >> 8) + 1)) << 8 | (a & 0xFF)) : a, Y & ((b >> 8) + 1)); } break;
  case 0x1A: case 0x3A: case 0x5A: case 0x7A: case 0xDA: case 0xFA: break;
  case 0x80: case 0x82: case 0x89: case 0xC2: case 0xE2: PC++; break;
  case 0x04: case 0x44: case 0x64: rd(a_zp()); break;
  case 0x14: case 0x34: case 0x54: case 0x74: case 0xD4: case 0xF4: rd(a_zpx()); break;
  case 0x0C: rd(a_abs()); break;
  case 0x1C: case 0x3C: case 0x5C: case 0x7C: case 0xDC: case 0xFC: rd(a_absx(1)); break;
  default:
    /* JAM opcodes: $02 inside our KERNAL/BASIC area is the HLE trap */
    if (op == 0x02 && opc_addr >= 0xA000 &&
        ((opc_addr >= 0xE000 && (memcfg & 2)) || (opc_addr < 0xC000 && (memcfg & 3) == 3))) {
      PC = opc_addr;
      trap(opc_addr);
      return 6;
    }
    if (!jammed) {
      lstr("CPU JAM opcode $"); lhex(op, 2); lstr(" at $"); lhex(opc_addr, 4); lflush();
    }
    jammed = 1;
    PC = opc_addr;
    return 2;
  }
  return cycles[op] + extra;
}

/* ================================================================== KERNAL (HLE) */
static void rts(void) { PC = pop(); PC |= pop() << 8; PC++; }

static u16 screen_base(void) { return ram[0x288] << 8; }

static void scr_clear(void) {
  u16 s = screen_base();
  for (int i = 0; i < 1000; i++) { ram[(u16)(s + i)] = 0x20; colorram[i] = ram[0x286] & 15; }
  ram[0xD3] = 0; ram[0xD6] = 0;
}

static void scr_sync_ptr(void) {
  u16 p = screen_base() + ram[0xD6] * 40;
  ram[0xD1] = p & 0xFF; ram[0xD2] = p >> 8;
  u16 c = 0xD800 + ram[0xD6] * 40;
  ram[0xF3] = c & 0xFF; ram[0xF4] = c >> 8;
}

static void scr_scroll(void) {
  u16 s = screen_base();
  for (int i = 0; i < 960; i++) { ram[(u16)(s + i)] = ram[(u16)(s + i + 40)]; colorram[i] = colorram[i + 40]; }
  for (int i = 960; i < 1000; i++) { ram[(u16)(s + i)] = 0x20; colorram[i] = ram[0x286] & 15; }
}

static void chrout(u8 c) {
  static const u8 colcodes[16] = { 0x90, 0x05, 0x1C, 0x9F, 0x9C, 0x1E, 0x1F, 0x9E, 0x81, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B };
  int row = ram[0xD6], col = ram[0xD3];
  for (int i = 0; i < 16; i++) if (colcodes[i] == c) { ram[0x286] = i; return; }
  switch (c) {
  case 0x0D: case 0x8D: col = 0; row++; ram[0xC7] = 0; break;
  case 0x93: scr_clear(); row = col = 0; break;
  case 0x13: row = col = 0; break;
  case 0x11: row++; break;
  case 0x91: if (row) row--; break;
  case 0x1D: if (++col == 40) { col = 0; row++; } break;
  case 0x9D: if (col) col--; else if (row) { row--; col = 39; } break;
  case 0x12: ram[0xC7] = 1; break;
  case 0x92: ram[0xC7] = 0; break;
  case 0x0E: vic.reg[0x18] |= 2; break;
  case 0x8E: vic.reg[0x18] &= ~2; break;
  case 0x14:
    if (col) col--; else if (row) { row--; col = 39; }
    ram[(u16)(screen_base() + row * 40 + col)] = 0x20;
    break;
  default: {
    u8 sc;
    if (c < 0x20 || (c >= 0x80 && c < 0xA0)) return;
    if (c < 0x40) sc = c;
    else if (c < 0x60) sc = c - 0x40;
    else if (c < 0x80) sc = c - 0x20;
    else if (c < 0xC0) sc = c - 0x40;
    else if (c < 0xFF) sc = c - 0x80;
    else sc = 0x5E;
    if (ram[0xC7]) sc |= 0x80;
    ram[(u16)(screen_base() + row * 40 + col)] = sc;
    colorram[row * 40 + col] = ram[0x286] & 15;
    if (++col == 40) { col = 0; row++; }
  }
  }
  while (row >= 25) { scr_scroll(); row--; }
  ram[0xD6] = row; ram[0xD3] = col;
  scr_sync_ptr();
}

static const u8 petscii_norm[64] = {
  0x14, 0x0D, 0x1D, 0x88, 0x85, 0x86, 0x87, 0x11, '3', 'W', 'A', '4', 'Z', 'S', 'E', 0,
  '5', 'R', 'D', '6', 'C', 'F', 'T', 'X', '7', 'Y', 'G', '8', 'B', 'H', 'U', 'V',
  '9', 'I', 'J', '0', 'M', 'K', 'O', 'N', '+', 'P', 'L', '-', '.', ':', '@', ',',
  0x5C, '*', ';', 0x13, 0, '=', 0x5E, '/', '1', 0x5F, 0, '2', ' ', 0, 'Q', 0x03,
};
static const u8 petscii_shift[64] = {
  0x94, 0x8D, 0x9D, 0x8C, 0x89, 0x8A, 0x8B, 0x91, '#', 0xD7, 0xC1, '$', 0xDA, 0xD3, 0xC5, 0,
  '%', 0xD2, 0xC4, '&', 0xC3, 0xC6, 0xD4, 0xD8, '\'', 0xD9, 0xC7, '(', 0xC2, 0xC8, 0xD5, 0xD6,
  ')', 0xC9, 0xCA, '0', 0xCD, 0xCB, 0xCF, 0xCE, 0xDB, 0xD0, 0xCC, 0xDD, '>', '[', 0xBA, '<',
  0xA9, 0xC0, ']', 0x93, 0, '=', 0xDE, '?', '!', 0x5F, 0, '"', 0xA0, 0, 0xD1, 0x83,
};

static void scnkey(void) {
  int code = 64;
  u8 shift = 0;
  for (int col = 0; col < 8; col++) {
    for (int row = 0; row < 8; row++) {
      if (!((keymat[col] >> row) & 1)) continue;
      int k = col * 8 + row;
      if (k == 15 || k == 52) { shift |= 1; continue; }
      if (k == 61) { shift |= 2; continue; }
      if (k == 58) { shift |= 4; continue; }
      if (code == 64) code = k;
    }
  }
  ram[0x28D] = shift;
  ram[0xCB] = code;
  if (code != 64 && code != ram[0xC5]) {
    u8 pet = (shift & 1) ? petscii_shift[code] : petscii_norm[code];
    if (pet && ram[0xC6] < 10) ram[0x277 + ram[0xC6]++] = pet;
  }
  ram[0xC5] = code;
  ram[0x28E] = shift;
}

static void udtim(void) {
  if (++ram[0xA2] == 0) if (++ram[0xA1] == 0) ++ram[0xA0];
  ram[0x91] = (keymat[7] & 0x80) ? 0x7F : 0xFF;
}

static void cia_defaults(void) {
  memset(&cia1, 0, sizeof cia1);
  memset(&cia2, 0, sizeof cia2);
  cia2.is_nmi = 1;
  cia1.ddra = 0xFF; cia1.pra = 0x7F; cia1.ddrb = 0x00; cia1.prb = 0xFF;
  cia1.la = cia1.ta = 0x4025; cia1.cra = 0x11; cia1.mask = 1;
  cia1.lb = cia1.tb = 0xFFFF;
  cia2.ddra = 0x3F; cia2.pra = 0x97; cia2.ddrb = 0; cia2.prb = 0xFF;
  cia2.la = cia2.ta = cia2.lb = cia2.tb = 0xFFFF;
  cia1.tod[3] = cia2.tod[3] = 1;
}

static void vic_defaults(void) {
  memset(vic.reg, 0, sizeof vic.reg);
  vic.reg[0x11] = 0x1B; vic.reg[0x16] = 0xC8; vic.reg[0x18] = 0x15;
  vic.reg[0x20] = 14; vic.reg[0x21] = 6; vic.reg[0x22] = 1; vic.reg[0x23] = 2; vic.reg[0x24] = 3;
  vic.reg[0x25] = 4; vic.reg[0x26] = 0;
  for (int i = 0; i < 8; i++) vic.reg[0x27 + i] = i + 1;
}

static void restore_vectors(void) {
  static const u16 vec[16] = { 0xEA31, 0xFE66, 0xFE47, 0xF34A, 0xF291, 0xF20E, 0xF250, 0xF333,
                               0xF157, 0xF1CA, 0xF6ED, 0xF13E, 0xF32F, 0xFE66, 0xF4A5, 0xF5ED };
  for (int i = 0; i < 16; i++) { ram[0x314 + i * 2] = vec[i] & 0xFF; ram[0x315 + i * 2] = vec[i] >> 8; }
}

/* disk image for KERNAL LOAD */
static u8 filebuf[200000];
static int disk_len;
static u8 disk[174848 + 1024];

static int d64_off(int t, int s) {
  static const u8 spt[41] = { 0, 21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21, 19,19,19,19,19,19,19, 18,18,18,18,18,18, 17,17,17,17,17,17,17,17,17,17 };
  if (t < 1 || t > 40 || s >= spt[t]) return -1;
  int o = 0;
  for (int i = 1; i < t; i++) o += spt[i];
  o = (o + s) * 256;
  return (o + 256 <= disk_len) ? o : -1;
}

/* Find a PRG by PETSCII name ("*" wildcards, empty = first file). Returns length loaded into dst. */
static int d64_load(const u8 *name, int nlen, u8 *dst, int max) {
  int t = 18, s = 1, guard = 0;
  while (t && guard++ < 40) {
    int o = d64_off(t, s);
    if (o < 0) return -1;
    for (int e = 0; e < 8; e++) {
      const u8 *de = &disk[o + e * 32];
      if ((de[2] & 7) != 2) continue;
      int match = 1;
      for (int i = 0; i < nlen && i < 16; i++) {
        if (name[i] == '*') break;
        if (name[i] == '?') continue;
        if (de[5 + i] != name[i]) { match = 0; break; }
        if (i == nlen - 1 && i < 15 && de[5 + i + 1] != 0xA0) match = 0;
      }
      if (!match) continue;
      int ft = de[3], fs = de[4], n = 0, g2 = 0;
      while (ft && g2++ < 800) {
        int fo = d64_off(ft, fs);
        if (fo < 0) return -1;
        const u8 *sec = &disk[fo];
        int cnt = sec[0] ? 254 : sec[1] - 1;
        for (int i = 0; i < cnt && n < max; i++) dst[n++] = sec[2 + i];
        ft = sec[0]; fs = sec[1];
      }
      return n;
    }
    t = disk[o]; s = disk[o + 1];
  }
  return -1;
}

static void kernal_load(void) {
  static u8 tmp[65536];
  u8 name[16];
  int nlen = ram[0xB7];
  u16 np = ram[0xBB] | (ram[0xBC] << 8);
  for (int i = 0; i < nlen && i < 16; i++) name[i] = rd(np + i);
  int n = d64_load(name, nlen, tmp, sizeof tmp);
  lstr("KERNAL LOAD \""); for (int i = 0; i < nlen && i < 16; i++) lput(name[i]); lstr("\" -> "); ldec(n); lflush();
  if (n < 2) { A = 4; P |= FC; ram[0x90] = 0x42; return; }
  u16 addr = (ram[0xB9] == 0) ? (X | (Y << 8)) : (tmp[0] | (tmp[1] << 8));
  for (int i = 2; i < n; i++) ram[(u16)(addr + i - 2)] = tmp[i];
  u16 end = addr + n - 2;
  X = end & 0xFF; Y = end >> 8;
  ram[0xAE] = X; ram[0xAF] = Y;
  ram[0x90] = 0x40;
  P &= ~FC;
}

/* Minimal BASIC "RUN": walks the program and executes POKE/SYS statements with numeric
 * arguments - enough for the one-line SYS launchers that crunchers and intros start. */
static int basic_num(u16 *p) {
  int v = 0;
  while (ram[*p] == ' ' || ram[*p] == '(') (*p)++;
  while (ram[*p] >= '0' && ram[*p] <= '9') v = v * 10 + (ram[(*p)++] - '0');
  while (ram[*p] == ' ' || ram[*p] == ')') (*p)++;
  return v;
}

static void basic_run(void) {
  u16 line = ram[0x2B] | (ram[0x2C] << 8);
  int guard = 0;
  while ((ram[line] | ram[line + 1]) && guard++ < 1000) {
    u16 next = ram[line] | (ram[line + 1] << 8);
    u16 p = line + 4;
    while (ram[p]) {
      u8 tok = ram[p++];
      if (tok == 0x97) { /* POKE */
        int a = basic_num(&p);
        if (ram[p] == ',') p++;
        wr((u16)a, (u8)basic_num(&p));
      } else if (tok == 0x9E) { /* SYS */
        int a = basic_num(&p);
        lstr("BASIC: SYS "); ldec(a); lflush();
        SP = 0xFA;
        push((0xE386 - 1) >> 8);
        push((0xE386 - 1) & 0xFF);
        PC = (u16)a;
        return;
      } else if (tok == 0x8F || tok == 0x83) { /* REM / DATA: skip line */
        break;
      } else if (tok == 0x80) { /* END */
        lstr("BASIC: END"); lflush(); jammed = 1; return;
      }
    }
    line = next;
  }
  lstr("BASIC: program ended without SYS"); lflush();
  jammed = 1;
}

static void trap(u16 addr) {
  switch (addr) {
  case 0xA659: case 0xA68E: case 0xA660: break; /* CLR / reset TXTPTR: nothing to do */
  case 0xA7AE: case 0xA871: basic_run(); return;
  case 0xE097: { /* BASIC RND: new seed in $8B-$8F, copied into FAC1 as a float in [0,1) */
    u32 s = ram[0x8C] << 24 | ram[0x8D] << 16 | ram[0x8E] << 8 | ram[0x8F];
    s ^= (u32)cia1.ta << 16 | cia1.tb | vic.raster;
    for (int i = 0; i < 3; i++) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; }
    if (!s) s = 0x9E3779B9;
    ram[0x8B] = 0x80; ram[0x8C] = (s >> 24) | 0x80; ram[0x8D] = s >> 16; ram[0x8E] = s >> 8; ram[0x8F] = s;
    for (int i = 0; i < 5; i++) ram[0x61 + i] = ram[0x8B + i];
    ram[0x66] = 0; ram[0x70] = 0;
    break;
  }
  case 0xEA31: udtim(); scnkey(); cia_read(&cia1, 13); PC = 0xEA81; return;
  case 0xEA87: case 0xFF9F: scnkey(); break;
  case 0xFFEA: udtim(); break;
  case 0xFFE4: case 0xFFCF:
    if (ram[0xC6]) {
      A = ram[0x277];
      for (int i = 0; i < 9; i++) ram[0x277 + i] = ram[0x278 + i];
      ram[0xC6]--;
    } else A = (addr == 0xFFCF) ? 0x0D : 0;
    setNZ(A); P &= ~FC;
    break;
  case 0xFFD2: case 0xE716: chrout(A); P &= ~FC; break;
  case 0xFF81: case 0xE518: vic_defaults(); scr_clear(); scr_sync_ptr(); break;
  case 0xE544: scr_clear(); scr_sync_ptr(); break;
  case 0xE566: ram[0xD3] = ram[0xD6] = 0; scr_sync_ptr(); break;
  case 0xFF84: case 0xFDA3: cia_defaults(); sid_write(0x18, 0); break;
  case 0xFF87: break;
  case 0xFF8A: restore_vectors(); break;
  case 0xFF8D: {
    u16 p = X | (Y << 8);
    for (int i = 0; i < 32; i++) {
      if (P & FC) ram[(u16)(p + i)] = ram[0x314 + i];
      else ram[0x314 + i] = rd(p + i);
    }
    break;
  }
  case 0xFF90: ram[0x9D] = A; break;
  case 0xFF99: if (P & FC) { X = ram[0x283]; Y = ram[0x284]; } else { ram[0x283] = X; ram[0x284] = Y; } break;
  case 0xFF9C: if (P & FC) { X = ram[0x281]; Y = ram[0x282]; } else { ram[0x281] = X; ram[0x282] = Y; } break;
  case 0xFFB7: A = ram[0x90]; setNZ(A); break;
  case 0xFFBA: ram[0xB8] = A; ram[0xBA] = X; ram[0xB9] = Y; break;
  case 0xFFBD: ram[0xB7] = A; ram[0xBB] = X; ram[0xBC] = Y; break;
  case 0xFFD5: kernal_load(); break;
  case 0xFFD8: P |= FC; A = 5; break;
  case 0xFFDB: ram[0xA2] = A; ram[0xA1] = X; ram[0xA0] = Y; break;
  case 0xFFDE: A = ram[0xA2]; X = ram[0xA1]; Y = ram[0xA0]; break;
  case 0xFFE1: P = (P & ~FZ) | ((keymat[7] & 0x80) ? FZ : 0); break;
  case 0xFFED: X = 40; Y = 25; break;
  case 0xFFF0:
    if (P & FC) { X = ram[0xD6]; Y = ram[0xD3]; }
    else { ram[0xD6] = X; ram[0xD3] = Y; scr_sync_ptr(); }
    break;
  case 0xFFF3: X = 0x00; Y = 0xDC; break;
  case 0xFF93: case 0xFF96: case 0xFFA2: case 0xFFA8: case 0xFFAB: case 0xFFAE: case 0xFFB1: case 0xFFB4:
  case 0xFFC0: case 0xFFC3: case 0xFFC6: case 0xFFC9: case 0xFFCC: case 0xFFE7:
    P &= ~FC; break;
  case 0xFFA5: A = 0x0D; break;
  case 0xFCE2:
    lstr("program jumped to RESET"); lflush();
    jammed = 1; return;
  case 0xE386: case 0xA474: case 0xE37B:
    lstr("program returned to BASIC"); lflush();
    jammed = 1; return;
  case 0xFE66:
    lstr("BRK executed"); lflush();
    jammed = 1; return;
  default:
    if (log_unknown) { lstr("unimplemented ROM call $"); lhex(addr, 4); lstr(" from stack $"); lhex(ram[0x101 + SP] | (ram[0x102 + SP] << 8), 4); lflush(); }
    break;
  }
  rts();
}

static void build_kernal(void) {
  memset(kernal, 0x02, sizeof kernal);
  memset(basic_rom, 0x02, sizeof basic_rom);
  static const struct { u16 at; u8 n; u8 b[20]; } code[] = {
    /* IRQ entry: save regs, dispatch through ($0314) or ($0316) for BRK */
    { 0xFF48, 19, { 0x48, 0x8A, 0x48, 0x98, 0x48, 0xBA, 0xBD, 0x04, 0x01, 0x29, 0x10, 0xF0, 0x03, 0x6C, 0x16, 0x03, 0x6C, 0x14, 0x03 } },
    /* end of IRQ: restore regs */
    { 0xEA81, 6, { 0x68, 0xA8, 0x68, 0xAA, 0x68, 0x40 } },
    /* NMI entry */
    { 0xFE43, 4, { 0x78, 0x6C, 0x18, 0x03 } },
    /* default NMI handler: save regs, ack CIA2, restore */
    { 0xFE47, 11, { 0x48, 0x8A, 0x48, 0x98, 0x48, 0xAD, 0x0D, 0xDD, 0x4C, 0xBC, 0xFE } },
    { 0xFEBC, 6, { 0x68, 0xA8, 0x68, 0xAA, 0x68, 0x40 } },
    /* hardware vectors */
    { 0xFFFA, 6, { 0x43, 0xFE, 0xE2, 0xFC, 0x48, 0xFF } },
  };
  for (unsigned i = 0; i < sizeof code / sizeof code[0]; i++)
    for (int j = 0; j < code[i].n; j++) kernal[code[i].at - 0xE000 + j] = code[i].b[j];
}

/* ================================================================== machine */
static void sid_reset(int rate) {
  memset(&sid, 0, sizeof sid);
  for (int i = 0; i < 3; i++) { sid.v[i].lfsr = 0x7FFFF8; sid.v[i].state = 2; }
  sid.sample_rate = rate;
  sid.sample_period = (float)CPU_HZ / (float)rate;
}

EXPORT("c64_init") void c64_init(int sample_rate) {
  memset(ram, 0, sizeof ram);
  for (int i = 0; i < 65536; i += 128) memset(&ram[i + 64], 0xFF, 64);
  memset(colorram, 14, sizeof colorram);
  memset(keymat, 0, sizeof keymat);
  memset(joy, 0, sizeof joy);
  memset(&vic, 0, sizeof vic);
  build_kernal();
  vic_defaults();
  cia_defaults();
  sid_reset(sample_rate > 0 ? sample_rate : 44100);
  audio_count = 0;
  port_ddr = 0x2F; port_dat = 0x37; update_cfg();
  ram[0] = 0x2F; ram[1] = 0x37;
  memset(ram + 2, 0, 0x3FE);
  restore_vectors();
  ram[0x288] = 4; ram[0x286] = 14; ram[0x289] = 10; ram[0x28A] = 0;
  ram[0x281] = 0x00; ram[0x282] = 0x08; ram[0x283] = 0x00; ram[0x284] = 0xA0;
  ram[0xC5] = ram[0xCB] = 64;
  ram[0xF5] = 0x81; ram[0xF6] = 0xEB;
  ram[0x9A] = 3; ram[0x99] = 0;
  ram[0x2B] = 0x01; ram[0x2C] = 0x08;
  ram[0x37] = ram[0x33] = 0x00; ram[0x38] = ram[0x34] = 0xA0;
  ram[0x300] = 0x8B; ram[0x301] = 0xE3;
  scr_clear();
  scr_sync_ptr();
  A = X = Y = 0;
  SP = 0xFF;
  P = FU | FI;
  PC = 0xFCE2;
  jammed = 0; stall = 0; nmi_pending = 0; frame_count = 0;
}

EXPORT("c64_filebuf") u8 *c64_filebuf(void) { return filebuf; }

static void print_str(const char *s) { while (*s) chrout((u8)*s++); }

/* Load PRG data from filebuf and RUN it (BASIC stub with SYS, or a machine-code start address). */
EXPORT("c64_load_prg") int c64_load_prg(int len) {
  if (len < 3) return -1;
  u16 load = filebuf[0] | (filebuf[1] << 8);
  for (int i = 2; i < len; i++) ram[(u16)(load + i - 2)] = filebuf[i];
  u16 end = load + len - 2;
  ram[0x2D] = ram[0x2F] = ram[0x31] = end & 0xFF;
  ram[0x2E] = ram[0x30] = ram[0x32] = end >> 8;
  ram[0xAE] = end & 0xFF; ram[0xAF] = end >> 8;

  u16 start = load;
  if (load == 0x0801) {
    /* find "SYS nnnn" in the first BASIC line */
    int i = 0x0805;
    start = 0;
    for (; i < 0x0801 + 80 && ram[i]; i++) {
      if (ram[i] == 0x9E) {
        int j = i + 1, v = 0;
        while (ram[j] == ' ' || ram[j] == '(') j++;
        while (ram[j] >= '0' && ram[j] <= '9') v = v * 10 + (ram[j++] - '0');
        start = (u16)v;
        break;
      }
    }
    if (!start) { lstr("no SYS found in BASIC stub"); lflush(); return -2; }
  }
  print_str("\x93\x9a\r    **** COMMODORE 64 BASIC V2 ****\r\r 64K RAM SYSTEM  38911 BASIC BYTES FREE\r\rREADY.\rRUN\r");
  /* return address for the SYS call lands in our BASIC trap */
  SP = 0xFA;
  push((0xE386 - 1) >> 8);
  push((0xE386 - 1) & 0xFF);
  PC = start;
  P = FU;
  jammed = 0;
  lstr("PRG $"); lhex(load, 4); lstr("-$"); lhex(end, 4); lstr(" start $"); lhex(start, 4); lflush();
  return 0;
}

/* Load a .d64 image from filebuf, then load+run its first program. */
EXPORT("c64_load_d64") int c64_load_d64(int len) {
  if (len > (int)sizeof disk) len = sizeof disk;
  memcpy(disk, filebuf, len);
  disk_len = len;
  int n = d64_load((const u8 *)"*", 1, filebuf, sizeof filebuf);
  if (n < 3) return -1;
  return c64_load_prg(n);
}

static inline void clock_cycles(int n) {
  int total = 0;
  while (n > 0 || stall > 0) {
    if (n > 0) n--; else stall--;
    total++;
    if (++vic.cycle == CYCLES_PER_LINE) { vic.cycle = 0; vic_end_line(); }
    else if (vic.cycle == 12) vic_check_badline();
    if (cia1.cra & 1 || cia1.crb & 1) cia_tick(&cia1);
    if (cia2.cra & 1 || cia2.crb & 1) cia_tick(&cia2);
  }
  sid_clock(total);
}

EXPORT("c64_frame") void c64_frame(void) {
  vic.frame_done = 0;
  if (restore_key == 1) { nmi_pending = 1; restore_key = 2; }
  int guard = 0;
  while (!vic.frame_done && guard++ < 40000) {
    if (jammed) clock_cycles(63);
    else clock_cycles(cpu_step());
  }
}

EXPORT("c64_framebuffer") u32 *c64_framebuffer(void) { return framebuffer; }
EXPORT("c64_audio") float *c64_audio(void) { return audio_buf; }
EXPORT("c64_audio_count") int c64_audio_count(void) { return audio_count; }
EXPORT("c64_audio_clear") void c64_audio_clear(void) { audio_count = 0; }
EXPORT("c64_jammed") int c64_jammed(void) { return jammed; }
EXPORT("c64_pc") int c64_pc(void) { return PC; }
EXPORT("c64_peek") int c64_peek(int a) { return ram[a & 0xFFFF]; }
EXPORT("c64_poke") void c64_poke(int a, int v) { ram[a & 0xFFFF] = (u8)v; }

/* matrix code 0..63 = column*8 + row (standard C64 key code), 64 = RESTORE */
EXPORT("c64_key") void c64_key(int code, int down) {
  if (code == 64) { if (down && !restore_key) restore_key = 1; if (!down) restore_key = 0; return; }
  if (code < 0 || code > 63) return;
  if (down) keymat[code >> 3] |= 1 << (code & 7);
  else keymat[code >> 3] &= ~(1 << (code & 7));
}

/* bits: 1 up, 2 down, 4 left, 8 right, 16 fire */
EXPORT("c64_joy") void c64_joy(int port, int bits) { if (port == 1 || port == 2) joy[port] = (u8)bits; }

/* ---- save states (everything lives in statics, so snapshot the lot) ---- */
#define STATE_ITEMS(X_) \
  X_(ram) X_(colorram) X_(port_ddr) X_(port_dat) X_(A) X_(X) X_(Y) X_(SP) X_(P) X_(PC) X_(jammed) \
  X_(stall) X_(frame_count) X_(vic) X_(cia1) X_(cia2) X_(sid) X_(disk) X_(disk_len)
#define SZ_(v) + sizeof(v)
static u8 statebuf[0 STATE_ITEMS(SZ_)];
EXPORT("c64_state_size") int c64_state_size(void) { return sizeof statebuf; }
EXPORT("c64_state_buf") u8 *c64_state_buf(void) { return statebuf; }
EXPORT("c64_save_state") void c64_save_state(void) {
  u8 *p = statebuf;
#define SAVE_(v) memcpy(p, &v, sizeof(v)); p += sizeof(v);
  STATE_ITEMS(SAVE_)
}
EXPORT("c64_load_state") void c64_load_state(void) {
  const u8 *p = statebuf;
  float rate = (float)sid.sample_rate;
#define LOAD_(v) memcpy(&v, p, sizeof(v)); p += sizeof(v);
  STATE_ITEMS(LOAD_)
  sid.sample_rate = (int)rate;
  sid.sample_period = (float)CPU_HZ / rate;
  update_cfg();
  memset(keymat, 0, sizeof keymat);
}
