/* game.h -- native SWIV game core (data-driven from the real map records). */
#ifndef SWIV_GAME_H
#define SWIV_GAME_H
#include "swivdata.h"

enum { CLS_TILE = 0, CLS_GROUND_STATIC, CLS_GROUND_MOVER, CLS_AIR, CLS_MINE, CLS_PICKUP, CLS_SCENERY };

typedef struct {
    int alive, cls, gfx_file, frame, nframes;
    float x, y;              /* playfield coords; y in MAP space (scroll units) for ground, screen for air */
    float vx, vy;
    int hp, score, timer, anim, seq;
    int w, h;                /* hit box */
    int map_y, map_x;        /* spawn record */
} Ent;

#define MAX_ENT 128
#define MAX_SHOT 96
#define MAX_FX 48

typedef struct { int alive; float x, y, vx, vy; int enemy, frame; } Shot;
typedef struct { int alive; float x, y; int gfx_file, frame, t, big; } Fx;

typedef struct {
    SwivDisk *d; SwivMap map; SwivCanvas canvas;
    int level;
    double scroll;           /* scroll counter (px); screen bottom = map y scroll */
    float speed;
    int next_rec;            /* next map record (tiles+objs merged by y) */
    SwivRec *recs; int nrecs;
    /* player */
    float px, py; int pframe, pfire_cd, plives, pdead, pinv, pscore, ppower;
    Ent ents[MAX_ENT]; Shot shots[MAX_SHOT]; Fx fx[MAX_FX];
    int frame;
    int game_over, level_done;
    int f_jeepheli, f_bullet, f_expl1, f_expl2, f_plop;
    int input_dx, input_dy, input_fire;   /* -1..1, -1..1, 0/1 */
} Game;

void game_init(Game *g, SwivDisk *d, int level);
void game_free(Game *g);
void game_step(Game *g);                          /* one 50 Hz frame */
void game_render(Game *g, uint8_t *idx /*320x256*/, uint16_t (*rowpal)[16] /*256 rows*/);
int  game_class_of(const char *linname);
#endif
