/* Headless native test harness for the emulator core.
 * usage: native <disk.d64> <frames> [shots=f1,f2,...] [input=frame:j2=bits;frame:k60=1;...] [out=dir]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/c64.c"

static int trace_frame;
static void trace_line(void) {
  if ((int)frame_count == trace_frame && vic.raster >= 44 && vic.raster < 170)
    printf("line %3d D011=%02X D018=%02X bad=%d disp=%d rc=%d act=%02X Y=%d\n", vic.raster, vic.reg[0x11], vic.reg[0x18], vic.badline, vic.display, vic.rc, vic.sp_active, vic.reg[1]);
}

void host_log(const char *s, int len) { printf("[frame %u] %.*s\n", frame_count, len, s); }

static void shot(const char *dir, int f) {
  char path[512];
  snprintf(path, sizeof path, "%s/f%05d.ppm", dir, f);
  FILE *o = fopen(path, "wb");
  fprintf(o, "P6 %d %d 255\n", SCR_W, SCR_H);
  for (int i = 0; i < SCR_W * SCR_H; i++) {
    u32 c = framebuffer[i];
    fputc(c & 255, o); fputc((c >> 8) & 255, o); fputc((c >> 16) & 255, o);
  }
  fclose(o);
}

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage\n"); return 1; }
  FILE *f = fopen(argv[1], "rb");
  int n = fread(filebuf, 1, sizeof filebuf, f);
  fclose(f);
  int frames = atoi(argv[2]);
  const char *shots = "", *input = "", *dir = ".";
  int pcdump = 0, dumpframe = 0;
  for (int i = 3; i < argc; i++) {
    if (!strncmp(argv[i], "shots=", 6)) shots = argv[i] + 6;
    if (!strncmp(argv[i], "input=", 6)) input = argv[i] + 6;
    if (!strncmp(argv[i], "out=", 4)) dir = argv[i] + 4;
    if (!strncmp(argv[i], "pc=", 3)) pcdump = atoi(argv[i] + 3);
    if (!strncmp(argv[i], "dump=", 5)) dumpframe = atoi(argv[i] + 5);
    if (!strncmp(argv[i], "trace=", 6)) { trace_frame = atoi(argv[i] + 6); line_hook = trace_line; }
  }
  c64_init(44100);
  int isprg = strlen(argv[1]) > 4 && !strcmp(argv[1] + strlen(argv[1]) - 4, ".prg");
  printf("load: %d\n", isprg ? c64_load_prg(n) : c64_load_d64(n));
  FILE *wav = fopen("/dev/null", "wb");
  double peak = 0;
  for (int fr = 1; fr <= frames; fr++) {
    /* apply input events for this frame */
    const char *p = input;
    while (*p) {
      int ef = atoi(p);
      const char *c = strchr(p, ':');
      if (!c) break;
      if (ef == fr) {
        if (c[1] == 'j') c64_joy(c[2] - '0', atoi(c + 4));
        else if (c[1] == 'k') { int code = atoi(c + 2); const char *e = strchr(c, '='); c64_key(code, atoi(e + 1)); }
      }
      const char *nx = strchr(p, ';');
      if (!nx) break;
      p = nx + 1;
    }
    c64_frame();
    if (fr == dumpframe) { char pth[512]; snprintf(pth, sizeof pth, "%s/mem.bin", dir); FILE *m = fopen(pth, "wb"); fwrite(ram, 1, 65536, m); fclose(m); printf("dumped ram, PC=%04X SP=%02X\n", PC, SP); }
    for (int i = 0; i < audio_count; i++) { double a = audio_buf[i] < 0 ? -audio_buf[i] : audio_buf[i]; if (a > peak) peak = a; }
    audio_count = 0;
    char key[16];
    snprintf(key, sizeof key, "%d", fr);
    const char *s = shots;
    while (*s) {
      if (atoi(s) == fr) shot(dir, fr);
      s = strchr(s, ',');
      if (!s) break;
      s++;
    }
    if (pcdump && fr % pcdump == 0) printf("frame %d PC=%04X $01=%02X D011=%02X D018=%02X DD00=%02X D015=%02X jam=%d\n", fr, PC, port_dat, vic.reg[0x11], vic.reg[0x18], cia2.pra, vic.reg[0x15], jammed);
  }
  fclose(wav);
  printf("audio peak %.3f\n", peak);
  return 0;
}
