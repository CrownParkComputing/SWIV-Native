/* simrun LEVEL VBLS OUT.txt [--adf PATH] -- run the native engine headless and
 * write a per-VBL object log in the SWIV-Amiga host's --objlog format:
 *   frame obj gfx x y z f367 hp type pc ang f408      /  frame SCROLL 3530 3542 tick
 * so tools/parity.py can diff it against re/trace/objlog_*.txt. */
#include "../src/engine/engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern void player_start(void); extern void player_vbl(void);
extern int player_input_dx, player_input_dy, player_input_fire;
extern int player2_input_dx, player2_input_dy, player2_input_fire;
int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: simrun LEVEL VBLS OUT [--adf PATH] [--fire] [--jeep DX DY]\n"); return 2; }
    const char *adf = "/home/jon/swiv-amiga-re/SWIVFIX.ADF"; int fire = 0, jeep = 0, jdx = 0, jdy = 0;
    for (int i = 4; i < argc; i++) { if (!strcmp(argv[i], "--adf")) adf = argv[++i]; else if (!strcmp(argv[i], "--fire")) fire = 1; else if (!strcmp(argv[i], "--jeep")) { jeep = 1; jdx = atoi(argv[++i]); jdy = atoi(argv[++i]); } }
    SwivDisk d; if (swiv_open(&d, adf)) return 1;
    eng_init(&d, atoi(argv[1])); player_start();
    FILE *f = fopen(argv[3], "w");
    int vbls = atoi(argv[2]);
    for (int v = 1; v <= vbls; v++) {
        player_input_fire = fire && ((v / 50) & 1);
        if (jeep) { player2_input_fire = v < 20; player2_input_dx = v > 40 ? jdx : 0; player2_input_dy = v > 40 ? jdy : 0; }   /* --jeep: port 1 fire joins the jeep, then holds a direction */
        eng_vbl(); player_vbl();
        FOR_EACH_OBJ(o) {
            fprintf(f, "%d %06x %04x %d %d %d %02x %d %d %s %04x %d\n", v, o->id, o->gfxset ? o->gfxset : o->animA.frame,
                    (int16_t)(o->x >> 16), (int16_t)(o->y >> 16), (int16_t)(o->z >> 16), o->flags367, o->hp, o->w[0],
                    o->name ? o->name : eng_handler_name(o->gfxset), o->angle, 0);
        }
        fprintf(f, "%d SCROLL %04x %04x %d\n", v, g.scroll3530, g.scroll3542, g.tick);
    }
    fclose(f);
    int n = 0; FOR_EACH_OBJ(o) n++;
    printf("ran %d VBLs, %d objects alive, scroll %04x, heli score %d\n", vbls, n, g.scroll3530, g.heli.score);
    return 0;
}
