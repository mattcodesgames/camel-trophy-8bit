/* For a picture drawn by a decompressor, record which source byte each screen byte came from.
 * Usage: srcmap ROM TAPE FRAME_FROM FRAME_TO OUT KEYSCRIPT
 * Output: 6912 little-endian uint16 source addresses (0 = not from a literal read). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define MEM_HOOKS
static uint16_t last_read; static int tracking; static uint16_t srcof[6912];
static uint8_t mem_hook_dummy;
#define MEM_READ_HOOK(a) do { if ((a) >= 0x9000 && (a) < 0xD900) last_read = (a); } while (0)
#define MEM_WRITE_HOOK(a, v) do { if (tracking && (a) >= 0x4000 && (a) < 0x5B00) srcof[(a) - 0x4000] = last_read; } while (0)
#include "../src/spectrum.c"
static void key(int row, int bit, int down) { if (down) keys[row] &= ~(1 << bit); else keys[row] |= (1 << bit); }
int main(int argc, char **argv) {
    (void)mem_hook_dummy;
    init();
    FILE *f = fopen(argv[1], "rb"); fread(rom_ptr(), 1, 16384, f); fclose(f); reset();
    f = fopen(argv[2], "rb"); uint32_t n = fread(tape_ptr(), 1, TAPE_MAX_BYTES, f); fclose(f); tape_load(n);
    int from = atoi(argv[3]), to = atoi(argv[4]);
    struct { int f, r, b, d; } s[8192] = {{150,6,3,1},{155,6,3,0},{160,7,1,1},{162,5,0,1},{166,5,0,0},{170,5,0,1},{174,5,0,0},{176,7,1,0},{180,6,0,1},{185,6,0,0}};
    int ns = 10;
    for (char *p = argv[6]; *p && ns < 8192; ) { sscanf(p, "%d:%d:%d:%d", &s[ns].f, &s[ns].r, &s[ns].b, &s[ns].d); ns++; p = strchr(p, ','); if (!p) break; p++; }
    for (int fr = 0; fr < to; fr++) {
        for (int i = 0; i < ns; i++) if (s[i].f == fr) key(s[i].r, s[i].b, s[i].d);
        tracking = fr >= from;
        if (fr == from) memset(srcof, 0, sizeof srcof);
        run_frame();
    }
    f = fopen(argv[5], "wb"); fwrite(srcof, 2, 6912, f); fclose(f);
    f = fopen("/dev/stdout", "w"); fprintf(f, "done\n"); fclose(f);
    return 0;
}
