#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define VW 320
#define VH 256
#define LOOKAHEAD 256          /* records within 256 px ahead of the screen edge are consumed (0x365e) */

/* Sprite class table -- measured from the level data (who appears where) and
 * the sprites themselves.  Grows as behaviours get read out of AMPROG. */
static const struct { const char *name; int cls; int hp; int score; } CLASS_TAB[] = {
    {"MEDTANK", CLS_GROUND_MOVER, 3, 200}, {"TRAIN", CLS_GROUND_MOVER, 6, 500}, {"DESTRAIN", CLS_GROUND_MOVER, 6, 500},
    {"TRUCK", CLS_GROUND_MOVER, 2, 150}, {"TINYTRUK", CLS_GROUND_MOVER, 1, 100}, {"FLATTANK", CLS_GROUND_MOVER, 4, 300},
    {"JUNTANK", CLS_GROUND_MOVER, 4, 300}, {"HOVER", CLS_GROUND_MOVER, 3, 250}, {"LAKESUB", CLS_GROUND_MOVER, 4, 400},
    {"CAMOGUN", CLS_GROUND_STATIC, 4, 300}, {"DIAGUN", CLS_GROUND_STATIC, 4, 300}, {"LAKEGUN", CLS_GROUND_STATIC, 3, 300},
    {"ROTOBASE", CLS_GROUND_STATIC, 5, 400}, {"INST1", CLS_GROUND_STATIC, 8, 1000}, {"INST2", CLS_GROUND_STATIC, 8, 1000},
    {"INST3", CLS_GROUND_STATIC, 8, 1000}, {"INST4", CLS_GROUND_STATIC, 8, 1000}, {"INST5", CLS_GROUND_STATIC, 8, 1000},
    {"MIDINST", CLS_GROUND_STATIC, 8, 1000}, {"EGGS", CLS_GROUND_STATIC, 3, 200}, {"MILL", CLS_GROUND_STATIC, 2, 100},
    {"FODDERA", CLS_AIR, 1, 100}, {"BIRD", CLS_AIR, 2, 150}, {"VTOL", CLS_AIR, 2, 200}, {"BLACKJET", CLS_AIR, 3, 300},
    {"SKYEYEA", CLS_AIR, 4, 400}, {"SKYEYEB", CLS_AIR, 4, 400}, {"YELLOW", CLS_AIR, 2, 200}, {"TRILO", CLS_AIR, 2, 200},
    {"XEVIOUS", CLS_AIR, 1, 100}, {"BUNNY", CLS_AIR, 2, 200}, {"GOOSE", CLS_AIR, 1, 100}, {"MAMA", CLS_AIR, 3, 300},
    {"DADA", CLS_AIR, 6, 800}, {"JETS", CLS_AIR, 3, 300}, {"SEAPLANE", CLS_AIR, 2, 200}, {"BOS", CLS_AIR, 3, 300},
    {"ORB", CLS_AIR, 2, 200}, {"INSECTS", CLS_AIR, 1, 100}, {"FROG", CLS_AIR, 2, 200}, {"EDGE", CLS_AIR, 2, 200},
    {"TAP", CLS_AIR, 3, 300}, {"TILT", CLS_AIR, 2, 200}, {"FLAME", CLS_AIR, 1, 100}, {"FISH", CLS_AIR, 2, 200},
    {"SKI", CLS_GROUND_MOVER, 1, 100}, {"HOMING", CLS_AIR, 1, 50},
    {"PROXMINE", CLS_MINE, 1, 50}, {"MINE", CLS_MINE, 1, 50}, {"AIRMINE", CLS_MINE, 1, 50},
    {"TOKEN", CLS_PICKUP, 1, 0}, {"SWAP", CLS_PICKUP, 1, 0}, {"POPUP", CLS_PICKUP, 1, 0}, {"MARK", CLS_PICKUP, 1, 0},
    {"PYRAMID", CLS_SCENERY, 0, 0}, {"REACTOR", CLS_SCENERY, 0, 0},
    {NULL, 0, 0, 0}
};

int game_class_of(const char *n) {
    if (n[0] == '_') return CLS_SCENERY;
    for (int i = 0; CLASS_TAB[i].name; i++) if (!strcasecmp(CLASS_TAB[i].name, n)) return CLASS_TAB[i].cls;
    return CLS_GROUND_STATIC;
}
static int class_hp(const char *n) { for (int i = 0; CLASS_TAB[i].name; i++) if (!strcasecmp(CLASS_TAB[i].name, n)) return CLASS_TAB[i].hp; return 2; }
static int class_score(const char *n) { for (int i = 0; CLASS_TAB[i].name; i++) if (!strcasecmp(CLASS_TAB[i].name, n)) return CLASS_TAB[i].score; return 100; }

static const char *lin_name(Game *g, int gfx) { int id = gfx & 0x1FF; return id < g->d->norder ? g->d->order[id] : ""; }
static int lin_file(Game *g, int gfx) { return swiv_order_index(g->d, gfx & 0x1FF); }
static const SwivFrame *frame_of(Game *g, int file, int fr) {
    const SwivLin *L = swiv_lin(g->d, file); if (!L || !L->nframes) return NULL;
    return &L->frames[fr % L->nframes];
}

static int cmp_rec(const void *a, const void *b) { const SwivRec *r = a, *s = b; return r->y != s->y ? r->y - s->y : r->seq - s->seq; }

void game_init(Game *g, SwivDisk *d, int level) {
    memset(g, 0, sizeof *g);
    g->d = d; g->level = level;
    swiv_map_load(d, level, &g->map);
    swiv_map_render(d, &g->map, &g->canvas, 0);     /* terrain only; objects are live entities */
    g->recs = malloc(sizeof(SwivRec) * g->map.nobjs); g->nrecs = g->map.nobjs;
    memcpy(g->recs, g->map.objs, sizeof(SwivRec) * g->nrecs);
    qsort(g->recs, g->nrecs, sizeof(SwivRec), cmp_rec);
    g->speed = 0.25f;
    g->px = 160; g->py = 200; g->plives = 3; g->pinv = 100; g->ppower = 1;
    g->f_jeepheli = swiv_find(d, "JEEPHELI.LIN"); g->f_bullet = swiv_find(d, "BULLET.LIN");
    g->f_expl1 = swiv_find(d, "EXPL1.LIN"); g->f_expl2 = swiv_find(d, "EXPL2.LIN"); g->f_plop = swiv_find(d, "PLOP.LIN");
}
void game_free(Game *g) { swiv_map_free(&g->map); swiv_canvas_free(&g->canvas); free(g->recs); }

/* map y -> screen y:  screen bottom (row 256) shows map y == scroll */
static float map_to_screen(Game *g, float map_y) { return VH - (map_y - (float)g->scroll); }

static Ent *new_ent(Game *g) { for (int i = 0; i < MAX_ENT; i++) if (!g->ents[i].alive) { memset(&g->ents[i], 0, sizeof(Ent)); g->ents[i].alive = 1; return &g->ents[i]; } return NULL; }
static void spawn_fx(Game *g, float x, float y, int big) {
    for (int i = 0; i < MAX_FX; i++) if (!g->fx[i].alive) {
        g->fx[i] = (Fx){1, x, y, big == 2 ? g->f_plop : big ? g->f_expl2 : g->f_expl1, 0, 0, big}; return;
    }
}
static void spawn_shot(Game *g, float x, float y, float vx, float vy, int enemy) {
    for (int i = 0; i < MAX_SHOT; i++) if (!g->shots[i].alive) { g->shots[i] = (Shot){1, x, y, vx, vy, enemy, enemy ? 8 : 0}; return; }
}

static void spawn_record(Game *g, const SwivRec *r) {
    const char *name = lin_name(g, r->gfx); int cls = game_class_of(name);
    if (cls == CLS_SCENERY) { /* decorative object: bake into terrain */ swiv_blit_gfx(g->d, &g->canvas, r->gfx, r->x, g->map.height + SWIV_MARGIN - r->y); return; }
    Ent *e = new_ent(g); if (!e) return;
    e->cls = cls; e->gfx_file = lin_file(g, r->gfx); e->frame = r->gfx >> 9;
    const SwivLin *L = swiv_lin(g->d, e->gfx_file); e->nframes = L ? L->nframes : 1;
    e->map_x = r->x; e->map_y = r->y; e->hp = class_hp(name); e->score = class_score(name);
    const SwivFrame *F = frame_of(g, e->gfx_file, e->frame);
    e->w = F ? F->parts[0].w : 16; e->h = F ? F->parts[0].h : 16;
    e->x = (float)r->x;
    if (cls == CLS_AIR) {
        /* aerial: enters from the top edge at the record's x, flies down */
        e->y = -e->h; e->vy = 1.0f + (r->seq % 3) * 0.25f; e->vx = 0; e->timer = 40 + (r->seq % 5) * 10;
    } else {
        e->y = (float)r->y;    /* ground: map-space y */
        if (cls == CLS_GROUND_MOVER) e->vx = (r->seq & 1) ? 0.5f : -0.5f;
        e->timer = 60 + (r->seq % 7) * 9;
    }
}

static float ent_sy(Game *g, Ent *e) { return e->cls == CLS_AIR ? e->y : map_to_screen(g, e->y); }

static void kill_ent(Game *g, Ent *e) {
    float sy = ent_sy(g, e);
    spawn_fx(g, e->x, sy, e->cls == CLS_AIR ? 0 : 1);
    g->pscore += e->score;
    if (e->cls == CLS_PICKUP) g->ppower = g->ppower < 4 ? g->ppower + 1 : 4;
    e->alive = 0;
}

static int overlap(float ax, float ay, int aw, int ah, float bx, float by, int bw, int bh) {
    return fabsf(ax - bx) * 2 < aw + bw && fabsf(ay - by) * 2 < ah + bh;
}

void game_step(Game *g) {
    g->frame++;
    if (g->game_over) return;
    g->scroll += g->speed;
    if (g->scroll >= g->map.height) { g->level_done = 1; return; }
    /* spawn records within LOOKAHEAD px above the screen top */
    int top_map = (int)g->scroll + VH;
    while (g->next_rec < g->nrecs && g->recs[g->next_rec].y <= top_map + LOOKAHEAD) spawn_record(g, &g->recs[g->next_rec++]);

    /* player */
    if (!g->pdead) {
        g->px += g->input_dx * 2.0f; g->py += g->input_dy * 2.0f;
        if (g->px < 12) g->px = 12; if (g->px > VW - 12) g->px = VW - 12;
        if (g->py < 16) g->py = 16; if (g->py > VH - 16) g->py = VH - 16;
        g->pframe = g->input_dx < 0 ? 1 : g->input_dx > 0 ? 2 : 0;
        if (g->pfire_cd) g->pfire_cd--;
        if (g->input_fire && !g->pfire_cd) {
            g->pfire_cd = 6;
            spawn_shot(g, g->px, g->py - 12, 0, -6, 0);
            if (g->ppower >= 2) { spawn_shot(g, g->px - 6, g->py - 8, 0, -6, 0); spawn_shot(g, g->px + 6, g->py - 8, 0, -6, 0); }
            if (g->ppower >= 3) { spawn_shot(g, g->px - 10, g->py - 4, -1, -6, 0); spawn_shot(g, g->px + 10, g->py - 4, 1, -6, 0); }
        }
        if (g->pinv) g->pinv--;
    } else if (--g->pdead == 0) {
        if (--g->plives < 0) { g->game_over = 1; return; }
        g->px = 160; g->py = 200; g->pinv = 120; g->ppower = 1;
    }

    /* entities */
    for (int i = 0; i < MAX_ENT; i++) {
        Ent *e = &g->ents[i]; if (!e->alive) continue;
        float sy;
        switch (e->cls) {
        case CLS_AIR:
            e->y += e->vy;
            e->x += e->vx + sinf((g->frame + e->seq * 17) * 0.05f) * 0.6f;
            if (e->y > VH + 40) { e->alive = 0; continue; }
            if (--e->timer <= 0 && e->y > 0 && e->y < VH - 40 && !g->pdead) {
                e->timer = 70; float dx = g->px - e->x, dy = g->py - e->y, l = sqrtf(dx * dx + dy * dy) + 0.01f;
                spawn_shot(g, e->x, e->y, dx / l * 2.5f, dy / l * 2.5f, 1);
            }
            if (e->nframes > 1 && (g->frame & 3) == 0) e->anim = (e->anim + 1) % (e->nframes > 4 ? 4 : e->nframes);
            break;
        case CLS_GROUND_MOVER:
            e->x += e->vx; if (e->x < 10 || e->x > VW - 10) e->vx = -e->vx;
            /* fallthrough */
        case CLS_GROUND_STATIC:
            sy = map_to_screen(g, e->y);
            if (sy > VH + 64) { e->alive = 0; continue; }
            if (--e->timer <= 0 && sy > 0 && sy < VH && !g->pdead) {
                e->timer = 90; float dx = g->px - e->x, dy = g->py - sy, l = sqrtf(dx * dx + dy * dy) + 0.01f;
                spawn_shot(g, e->x, sy, dx / l * 2.0f, dy / l * 2.0f, 1);
            }
            break;
        case CLS_MINE:
            sy = map_to_screen(g, e->y);
            if (sy > VH + 64) { e->alive = 0; continue; }
            if (!g->pdead && !g->pinv && overlap(e->x, sy, e->w + 8, e->h + 8, g->px, g->py, 16, 16)) { kill_ent(g, e); g->pdead = 60; spawn_fx(g, g->px, g->py, 1); continue; }
            if (e->nframes > 1 && (g->frame & 7) == 0) e->anim = (e->anim + 1) % e->nframes;
            break;
        case CLS_PICKUP:
            sy = map_to_screen(g, e->y);
            if (sy > VH + 64) { e->alive = 0; continue; }
            if (!g->pdead && overlap(e->x, sy, e->w, e->h, g->px, g->py, 16, 16)) { kill_ent(g, e); continue; }
            if (e->nframes > 1 && (g->frame & 7) == 0) e->anim = (e->anim + 1) % e->nframes;
            break;
        }
    }

    /* shots */
    for (int i = 0; i < MAX_SHOT; i++) {
        Shot *s = &g->shots[i]; if (!s->alive) continue;
        s->x += s->vx; s->y += s->vy;
        if (s->x < -8 || s->x > VW + 8 || s->y < -16 || s->y > VH + 16) { s->alive = 0; continue; }
        if (s->enemy) {
            if (!g->pdead && !g->pinv && overlap(s->x, s->y, 4, 4, g->px, g->py, 12, 12)) { s->alive = 0; g->pdead = 60; spawn_fx(g, g->px, g->py, 1); }
        } else {
            for (int k = 0; k < MAX_ENT; k++) {
                Ent *e = &g->ents[k]; if (!e->alive || e->cls == CLS_PICKUP) continue;
                float sy = ent_sy(g, e);
                if (sy < -8 || sy > VH) continue;
                if (overlap(s->x, s->y, 4, 8, e->x, sy, e->w, e->h)) {
                    s->alive = 0; spawn_fx(g, s->x, s->y, 2);
                    if (--e->hp <= 0) kill_ent(g, e);
                    break;
                }
            }
        }
    }
    /* player vs air collision */
    if (!g->pdead && !g->pinv) for (int k = 0; k < MAX_ENT; k++) {
        Ent *e = &g->ents[k]; if (!e->alive || e->cls != CLS_AIR) continue;
        if (overlap(e->x, e->y, e->w - 4, e->h - 4, g->px, g->py, 14, 14)) { kill_ent(g, e); g->pdead = 60; spawn_fx(g, g->px, g->py, 1); break; }
    }
    /* fx */
    for (int i = 0; i < MAX_FX; i++) {
        Fx *f = &g->fx[i]; if (!f->alive) continue;
        const SwivLin *L = swiv_lin(g->d, f->gfx_file); int n = L ? L->nframes : 1;
        if (++f->t % 3 == 0) f->frame++;
        if (f->frame >= n) f->alive = 0;
    }
}

static void blit_frame(Game *g, SwivCanvas *c, int file, int fr, int x, int y) {
    const SwivFrame *F = frame_of(g, file, fr); if (!F) return;
    for (int i = 0; i < F->nparts; i++) swiv_blit_part(c, &F->parts[i], x - F->parts[i].cx, y - F->parts[i].cy);
}

void game_render(Game *g, uint8_t *idx, uint16_t (*rowpal)[16]) {
    int top = g->map.height + SWIV_MARGIN - (int)g->scroll - VH;
    for (int y = 0; y < VH; y++) {
        int sy = top + y;
        if (sy >= 0 && sy < g->canvas.h) memcpy(idx + y * VW, g->canvas.px + (size_t)sy * g->canvas.w, VW);
        else memset(idx + y * VW, 0, VW);
        swiv_map_palette_row(&g->map, sy, rowpal[y]);
    }
    SwivCanvas c = { VW, VH, idx, NULL, 0 };
    /* ground entities first, then air, shots, player, fx */
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < MAX_ENT; i++) {
            Ent *e = &g->ents[i]; if (!e->alive) continue;
            if ((e->cls == CLS_AIR) != pass) continue;
            blit_frame(g, &c, e->gfx_file, e->frame + e->anim, (int)e->x, (int)ent_sy(g, e));
        }
    for (int i = 0; i < MAX_SHOT; i++) if (g->shots[i].alive) blit_frame(g, &c, g->f_bullet, g->shots[i].frame, (int)g->shots[i].x, (int)g->shots[i].y);
    if (!g->pdead && (!g->pinv || (g->frame & 2))) blit_frame(g, &c, g->f_jeepheli, g->pframe, (int)g->px, (int)g->py);
    for (int i = 0; i < MAX_FX; i++) if (g->fx[i].alive) blit_frame(g, &c, g->fx[i].gfx_file, g->fx[i].frame, (int)g->fx[i].x, (int)g->fx[i].y);
}
