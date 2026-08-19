/* player_stub.c -- TEMPORARY player until the faithful port of the player
 * scripts (LAB_0670 family) lands in player.c.  Provides: heli object with
 * box $0050, record updates, player bullets as external boxes ($8041). */
#include "engine.h"
#include <string.h>

int player_input_dx, player_input_dy, player_input_fire;   /* set by the frontend each VBL */

typedef struct { int alive; int32_t x, y; Box *box; int age; } PBullet;
static PBullet pb[30];
RenderEntry player_bullet_render[30]; int player_bullet_count;

static void heli_die(Obj *o) { if (g.heli.invuln106) return; eng_spawn(o, fx_ring16, 100); eng_signal(o); }
static void heli_script(Obj *o) {
    g.heli.alive = 1; g.heli.obj = o;
    o->prio = 99; o->x = 160 << 16; o->y = (int32_t)(int16_t)(g.scroll3530 + 200) << 16; o->z = 32 << 16;
    o->flags367 |= F_SCREEN_LOCKED; o->margin = 0; o->cb538_disabled = 1;
    box_register(o, 0x0050); set_frame(o, 0x0000);
    on_event(o, EV_HELI_KILLER, heli_die);
    g.heli.invuln106 = 100; int cd = 0;
    for (;;) {
        o->vx = player_input_dx * (2 << 16) / 1; o->vy = player_input_dy * (2 << 16);
        /* clamp */
        int sx = o->x >> 16; if (sx < 4) o->x = 4 << 16; if (sx > 316) o->x = 316 << 16;
        int sy = (int16_t)((o->y >> 16) - g.scroll3542); if (sy < 4) o->y = (int32_t)(int16_t)(g.scroll3542 + 4) << 16; if (sy > 252) o->y = (int32_t)(int16_t)(g.scroll3542 + 252) << 16;
        set_frame(o, player_input_dx < 0 ? 0x0200 : player_input_dx > 0 ? 0x0400 : 0x0000);
        if (cd) cd--;
        if (player_input_fire && !cd) { cd = 3; for (int i = 0; i < 30; i++) if (!pb[i].alive) { pb[i].alive = 1; pb[i].x = o->x; pb[i].y = o->y - (12 << 16); pb[i].box = eng_extbox_alloc(); pb[i].age = 0; if (pb[i].box) { pb[i].box->mask = 0x8041; pb[i].box->hw = 2; pb[i].box->hh = 6; } break; } }
        if (g.heli.invuln106) g.heli.invuln106--;
        g.heli.x = o->x; g.heli.y = o->y; g.heli.z = o->z;
        if (step(o)) break;
        o->time12 = *o->clock;   /* ignore stray signals except death */
        if (!o->alive) break;
    }
    g.heli.alive = 0; g.heli.obj = NULL;
}
void player_start(void) { Obj *o = eng_spawn_at(heli_script, 99, NULL); if (o) { o->clock = &g.clock202; o->time12 = g.clock202; o->name = "heli"; } }
void player_vbl(void) {
    player_bullet_count = 0;
    for (int i = 0; i < 30; i++) if (pb[i].alive) {
        int hit = pb[i].box && (pb[i].box->hits & 0x20);
        pb[i].y -= 8 << 16;
        int sy = (int16_t)((pb[i].y >> 16) - g.scroll3542);
        if (pb[i].box) { pb[i].box->x = pb[i].x >> 16; pb[i].box->y = pb[i].y >> 16; pb[i].box->hits = 0; }
        if (sy < -8 || hit) { if (pb[i].box) eng_extbox_free(pb[i].box); pb[i].alive = 0; continue; }
        player_bullet_render[player_bullet_count++] = (RenderEntry){ 0x7f00, 0x0003, pb[i].x >> 16, sy, 0 };
    }
}
/* a player bullet that touched a solid enemy must disappear: solid = bit5 in the hit mask (done above) */
