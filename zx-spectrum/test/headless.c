/* Native test harness: boots the ROM, types LOAD "", plays the tape and
 * dumps raw RGBA frames so the core can be checked without a browser.
 * Usage: headless ROM TAPE OUTDIR FRAMES [dump_every] [keyscript]
 * keyscript: comma list of frame:row:bit:down e.g. "3000:7:0:1,3010:7:0:0"
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void port_hook(uint16_t port);
#define PORT_HOOK(p) port_hook(p)
#include "../src/spectrum.c"
static unsigned port_hist[65536]; static int port_logging;
static void port_hook(uint16_t port) { if (port_logging) port_hist[port]++; }

static void key(int row, int bit, int down) { if (row == 8) { if (down) kempston |= 1 << bit; else kempston &= ~(1 << bit); return; } if (down) keys[row] &= ~(1 << bit); else keys[row] |= (1 << bit); }

int main(int argc, char **argv) {
    init();
    FILE *f = fopen(argv[1], "rb"); fread(rom_ptr(), 1, 16384, f); fclose(f);
    reset();
    f = fopen(argv[2], "rb"); uint32_t n = fread(tape_ptr(), 1, TAPE_MAX_BYTES, f); fclose(f);
    printf("blocks %u pulses %u\n", tape_load(n), tape_length());
    if (getenv("TAPEBLOCK")) tape_seek_block(atoi(getenv("TAPEBLOCK")));
    int frames_n = atoi(argv[4]), every = argc > 5 ? atoi(argv[5]) : 250;
    /* LOAD "" : J, SS+P, SS+P, ENTER */
    struct { int f, r, b, d; } s[8192] = {
        {150,6,3,1},{155,6,3,0},
        {160,7,1,1},{162,5,0,1},{166,5,0,0},{170,5,0,1},{174,5,0,0},{176,7,1,0},
        {180,6,0,1},{185,6,0,0}, };
    int ns = 10;
    if (argc > 6) { char *p = argv[6]; while (*p && ns < 8192) { sscanf(p, "%d:%d:%d:%d", &s[ns].f, &s[ns].r, &s[ns].b, &s[ns].d); ns++; p = strchr(p, ','); if (!p) break; p++; } }
    for (int fr = 0; fr < frames_n; fr++) {
        for (int i = 0; i < ns; i++) if (s[i].f == fr) key(s[i].r, s[i].b, s[i].d);
        if (getenv("RSTLOG") && fr >= atoi(getenv("RSTLOG")) && fr < atoi(getenv("RSTLOG")) + 6000) {
            /* step manually to log ROM calls */
            an = 0; border_upto = 0;
            while (T < FRAME_T) {
                if (T < 32) z80_interrupt();
                ei_pending = 0;
                if (pc.w == 0x0556 && tape_autoplay && !tape_playing && tape_pos < npulses) tape_play();
                if (pc.w == 0x0556) printf("[LD-BYTES fr%d A=%02x IX=%04x DE=%04x F=%02x from %04x tape %u]\n", fr, A, ix.w, de.w, F, mem[sp.w]|mem[sp.w+1]<<8, tape_pos);
                if (pc.w == 0x0771) { printf("[LD-BYTES ret fr%d carry=%d IX=%04x DE=%04x tape %u]", fr, F & 1, ix.w, de.w, tape_pos); for (int q = 0xaa76; q < 0xaa76 + 17; q++) printf(" %02x", mem[q]); printf("\n"); }
                if (pc.w == 0x0010) printf("<%02x>", A);
                z80_step();
            }
            tape_advance(FRAME_T); audio_to(FRAME_T); render(); T -= FRAME_T; tape_t = T; a_next -= FRAME_T; a_last -= FRAME_T; frames++;
        } else
        run_frame();
        if (getenv("TRACE") && fr >= atoi(getenv("TRACE")) && fr % 50 == 0) printf("  fr %d pc=%04x sp=%04x tape %u playing=%d iff=%d im=%d PROG=%04x ERRSP=%04x\n", fr, pc.w, sp.w, tape_position(), tape_is_playing(), iff1, im, mem[23635]|mem[23636]<<8, mem[23613]|mem[23614]<<8);
        if (getenv("PORTLOG") && fr == atoi(getenv("PORTLOG"))) port_logging = 1;
        if (getenv("PORTLOG") && fr == atoi(getenv("PORTLOG")) + 100) { port_logging = 0; for (int q = 0; q < 65536; q++) if (port_hist[q]) printf("port %04x: %u\n", q, port_hist[q]); }
        if (getenv("DUMPMEM") && fr + 1 == atoi(getenv("DUMPMEM"))) { char pth[512]; snprintf(pth, sizeof pth, "%s/mem%06d.bin", argv[3], fr + 1); FILE *o = fopen(pth, "wb"); fwrite(mem, 1, 65536, o); fclose(o); }
        if (fr % 500 == 0) printf("frame %d pc=%04x tape %u/%u playing=%d\n", fr, pc.w, tape_position(), tape_length(), tape_is_playing());
        if ((fr + 1) % every == 0) {
            char path[512]; snprintf(path, sizeof path, "%s/f%06d.rgba", argv[3], fr + 1);
            FILE *o = fopen(path, "wb"); fwrite(fb, 4, SCR_W * SCR_H, o); fclose(o);
        }
    }
    return 0;
}
