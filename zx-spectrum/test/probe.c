/* Probe which keys/joystick bits affect the screen from a given point in the game. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/spectrum.c"
static const char *names[8][5] = {{"CS","Z","X","C","V"},{"A","S","D","F","G"},{"Q","W","E","R","T"},{"1","2","3","4","5"},
                                  {"0","9","8","7","6"},{"P","O","I","U","Y"},{"EN","L","K","J","H"},{"SP","SS","M","N","B"}};
static uint32_t base[SCR_W * SCR_H];
int main(int argc, char **argv) {
    init();
    FILE *f = fopen(argv[1], "rb"); fread(rom_ptr(), 1, 16384, f); fclose(f); reset();
    f = fopen(argv[2], "rb"); uint32_t n = fread(tape_ptr(), 1, TAPE_MAX_BYTES, f); fclose(f);
    tape_load(n); tape_seek_block(8);
    int mode = atoi(argv[3]);  /* menu key row/bit: 1 = keyboard, 2 = kempston */
    struct { int f, r, b, d; } s[] = {{150,6,3,1},{155,6,3,0},{160,7,1,1},{162,5,0,1},{166,5,0,0},{170,5,0,1},{174,5,0,0},{176,7,1,0},{180,6,0,1},{185,6,0,0},
        {12000,3,mode - 1,1},{12005,3,mode - 1,0},{13000,6,0,1},{13005,6,0,0},{14000,6,0,1},{14005,6,0,0},{15000,6,0,1},{15005,6,0,0}};
    for (int fr = 0; fr < 17000; fr++) {
        for (unsigned i = 0; i < sizeof s / sizeof s[0]; i++) if (s[i].f == fr) { if (s[i].d) keys[s[i].r] &= ~(1 << s[i].b); else keys[s[i].r] |= 1 << s[i].b; }
        run_frame();
    }
    /* optional prefix: comma list of key indexes, each held 60 frames then released 30 */
    if (getenv("PREFIX")) for (char *p = getenv("PREFIX"); *p; ) {
        int k = atoi(p);
        if (k >= 40) kempston = 1 << (k - 40); else keys[k / 5] &= ~(1 << (k % 5));
        for (int i = 0; i < 60; i++) run_frame();
        for (int r = 0; r < 8; r++) keys[r] = 0xFF; kempston = 0;
        for (int i = 0; i < 30; i++) run_frame();
        p = strchr(p, ','); if (!p) break; p++;
    }
    state_save();
    static uint8_t snap[STATE_SIZE]; memcpy(snap, state_buf, STATE_SIZE);
    int hold = 150;
    for (int i = 0; i < hold; i++) run_frame();
    memcpy(base, fb, sizeof base);
    for (int k = -1; k < 45; k++) {
        memcpy(state_buf, snap, STATE_SIZE); state_load();
        if (k >= 40) kempston = 1 << (k - 40);
        else if (k >= 0) keys[k / 5] &= ~(1 << (k % 5));
        for (int i = 0; i < hold; i++) run_frame();
        for (int r = 0; r < 8; r++) keys[r] = 0xFF;
        kempston = 0;
        int diff = 0, ymin = 999, ymax = -1;
        for (int p = 0; p < SCR_W * SCR_H; p++) if (fb[p] != base[p]) { diff++; int y = p / SCR_W; if (y < ymin) ymin = y; if (y > ymax) ymax = y; }
        if (diff) printf("%-6s diff=%6d rows %d-%d\n", k < 0 ? "none" : k >= 40 ? (const char*[]){"K-R","K-L","K-D","K-U","K-F"}[k - 40] : names[k / 5][k % 5], diff, ymin, ymax);
        if (k >= 0 && diff > 0 && argc > 4) { char pth[256]; snprintf(pth, sizeof pth, "%s/k%02d.rgba", argv[4], k); FILE *o = fopen(pth, "wb"); fwrite(fb, 4, SCR_W * SCR_H, o); fclose(o); }
    }
    return 0;
}
