/* dumpmap LEVEL OUT.ppm [--objects]  -- native map render to PPM (for the gate). */
#include "../src/swivdata.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: dumpmap LEVEL OUT.ppm [--objects] [--bake] [--adf PATH]\n"); return 2; }
    const char *adf = "/home/jon/swiv-amiga-re/SWIVFIX.ADF"; int objs = 0, bake = 0;
    for (int i = 3; i < argc; i++) { if (!strcmp(argv[i], "--objects")) objs = 1; else if (!strcmp(argv[i], "--bake")) bake = 1; else if (!strcmp(argv[i], "--adf")) adf = argv[++i]; }
    SwivDisk d; if (swiv_open(&d, adf)) { fprintf(stderr, "cannot open %s\n", adf); return 1; }
    SwivMap m; if (swiv_map_load(&d, atoi(argv[1]), &m)) { fprintf(stderr, "bad level\n"); return 1; }
    SwivCanvas c; swiv_map_render(&d, &m, &c, objs);
    FILE *f = fopen(argv[2], "wb"); fprintf(f, "P6\n%d %d\n255\n", c.w, c.h);
    uint8_t *row = malloc(c.w * 3);
    for (int y = 0; y < c.h; y++) {
        uint16_t pal[16]; swiv_map_palette_row(&m, y, pal);
        for (int x = 0; x < c.w; x++) {
            size_t i = (size_t)y * c.w + x; const uint16_t *pp = pal;
            if (bake && c.palid[i] != 255) pp = m.checks[c.palid[i]].pal;   /* reference semantics: tile anchor palette */
            swiv_rgb12(pp[c.px[i] & 15], row + x * 3, row + x * 3 + 1, row + x * 3 + 2);
        }
        fwrite(row, 1, c.w * 3, f);
    }
    fclose(f);
    printf("%s: %d tiles, %d objects, %d palette checkpoints, %d px -> %s\n", m.pam_name, m.ntiles, m.nobjs, m.nchecks, c.h, argv[2]);
    swiv_canvas_free(&c); swiv_map_free(&m); swiv_close(&d);
    return 0;
}
