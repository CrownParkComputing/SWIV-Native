/* sfxdump: render every sfx_bank entry to DIR/NN_NAME.wav (22050 Hz mono 16-bit)
 * so the synthesised bank can be measured against the original's Paula output.
 * usage: sfxdump ADF DIR */
#include "../src/sfx_bank.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void u32(FILE *f, uint32_t v) { for (int i = 0; i < 4; i++) fputc((v >> (8 * i)) & 0xff, f); }
static void u16(FILE *f, unsigned v) { fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f); }
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: sfxdump ADF DIR\n"); return 2; }
    SwivDisk d; if (swiv_open(&d, argv[1])) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    for (int i = 0; i < sfx_bank_count(); i++) {
        int n = 0; int16_t *pcm = sfx_bank_render(i, &d, &n);
        char name[64], path[512]; snprintf(name, sizeof name, "%s", sfx_bank_desc(i)->name);
        for (char *p = name; *p; p++) if (*p == '$' || *p == '+') *p = '_';
        snprintf(path, sizeof path, "%s/%02d_%s.wav", argv[2], i, name);
        FILE *f = fopen(path, "wb"); if (!f) { perror(path); return 1; }
        fputs("RIFF", f); u32(f, 36 + n * 2); fputs("WAVEfmt ", f); u32(f, 16); u16(f, 1); u16(f, 1);
        u32(f, 22050); u32(f, 44100); u16(f, 2); u16(f, 16); fputs("data", f); u32(f, n * 2);
        fwrite(pcm, 2, n, f); fclose(f); free(pcm);
        printf("%2d %-10s %6d samples %.2fs\n", i, sfx_bank_desc(i)->name, n, n / 22050.0);
    }
    return 0;
}
