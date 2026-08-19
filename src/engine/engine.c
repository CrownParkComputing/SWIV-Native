/* engine.c -- Sales Curve Kernel object model + AMPROG verb library, native.
 * Semantics per re/VERBS.md / re/OBJECT.md. */
#include "engine.h"
#include "coro.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Globals g;
extern const int16_t swiv_sin256[256];
extern const uint8_t swiv_atan32[1024];

#define SCREEN_W 320
#define SCREEN_H 256
#define MAX_OBJS 512

static Obj pool[MAX_OBJS];
static Obj head;                       /* circular list sorted by prio descending (unsigned) */
static Obj *cur;                       /* running object (A5) */
static int  list_dirty;

/* ---------- render list (blit slots) ---------- */
RenderEntry render_list[1024]; int render_count;
static void render_push(int key, uint16_t gfx, int x, int y, uint8_t flags) {
    if (render_count < 1024) render_list[render_count++] = (RenderEntry){ key, gfx, x, y, flags };
}

/* ---------- list ---------- */
static void list_insert(Obj *o) {
    /* kernel LAB_00E5: walk while node.prio > o.prio (unsigned), insert before the first <= */
    Obj *n = head.next;
    while (n != &head && (uint16_t)n->prio > (uint16_t)o->prio) n = n->next;
    o->next = n; o->prev = n->prev; n->prev->next = o; n->prev = o;
}
static void list_remove(Obj *o) { o->prev->next = o->next; o->next->prev = o->prev; o->next = o->prev = NULL; }
Obj *eng_first(void) { return head.next == &head ? NULL : head.next; }
Obj *eng_next(Obj *o) { return o->next == &head ? NULL : o->next; }

static void script_main(void *arg) {
    Obj *o = arg;
    o->script(o);
    /* script returned = LAB_0725 path: enemy cleanup + free */
    enemy_cleanup(o);
    eng_free(o);
}

static Obj *alloc_obj(void) {
    for (int i = 0; i < MAX_OBJS; i++) if (!pool[i].alive) { Obj *o = &pool[i]; memset(o, 0, sizeof *o); o->alive = 1; o->id = i; return o; }
    return NULL;
}

Obj *eng_spawn_at(Script s, int prio, Obj *from) {
    Obj *c = alloc_obj(); if (!c) return NULL;
    c->prio = prio; c->script = s;
    if (from) {
        c->x = from->x; c->y = from->y; c->z = from->z; c->vx = from->vx; c->vy = from->vy; c->vz = from->vz;
        c->ax = from->ax; c->ay = from->ay; c->az = from->az; c->speed = from->speed; c->angle = from->angle;
        memcpy(c->w, from->w, 8 * sizeof(int16_t));
        c->clock = from->clock; c->time12 = from->time12;
        c->animA.flags = from->animA.flags & 1;
    } else { c->clock = &g.clock204; c->time12 = g.clock204; }
    c->margin = -64; c->cb538 = NULL; /* NULL = default signal-self */ c->cb542 = NULL; c->cb534 = NULL;
    c->death376 = fx_explosion; c->enable508 = 0;
    c->h510 = c->h514 = c->h518 = c->h522 = c->h526 = c->h530 = (Callback)eng_free;
    c->coro = coro_new(script_main, c, 24 * 1024);
    list_insert(c);
    return c;
}
Obj *eng_spawn(Obj *o, Script s, int prio) { return eng_spawn_at(s, prio, o); }
Obj *eng_spawn_attached(Obj *o, Script s) {
    Obj *c = eng_spawn(o, s, 100); if (!c) return c;
    c->parent = o; c->sib = o->child; o->child = c; c->cb542 = (Callback)eng_signal;
    return c;
}
static void detach_children(Obj *o) { for (Obj *c = o->child; c; ) { Obj *n = c->sib; c->parent = NULL; c->sib = NULL; c = n; } o->child = NULL; }
static void unlink_parent(Obj *o) {
    if (!o->parent) return;
    Obj **pp = &o->parent->child; while (*pp && *pp != o) pp = &(*pp)->sib; if (*pp) *pp = o->sib; o->parent = NULL;
}
void eng_free(Obj *o) {
    if (!o->alive) return;
    detach_children(o); unlink_parent(o); box_unlink(o);
    if (o->box.linked) box_unlink(o);
    list_remove(o); o->alive = 0;
    if (o == cur) { /* free from inside own script: finish the coroutine */ Coro *c = o->coro; o->coro = NULL; (void)c; coro_yield(); /* never resumed */ }
}
void eng_signal(Obj *o) { o->time12--; }
int  eng_signalled(const Obj *o) { return o->time12 != *o->clock; }
void eng_set_clock(Obj *o, int *clock) { o->clock = clock; o->time12 = *clock; }

void formation(Obj *o, int dx, int dy, int count, int dparam, Script cont) {
    for (int i = 0; i < count - 1; i++) {
        Obj *c = eng_spawn(o, cont, 100); (void)c;
        o->x += dx << 16; o->y += dy << 16; o->w[0] += dparam;
    }
}

/* ---------- yields ---------- */
static void do_yield(Obj *o) {
    (void)o;
    do { coro_yield(); } while (g.paused165);
}
void yield_once(Obj *o) { do_yield(o); }
int  yield_n(Obj *o, int n) { while (n-- > 0) do_yield(o); return eng_signalled(o); }

/* ---------- boxes / collision ---------- */
static Obj *boxes[MAX_OBJS]; static int nboxes;
void box_register(Obj *o, uint16_t mask) {
    o->box.x = o->x >> 16; o->box.y = o->y >> 16; o->box.hw = o->box.hh = 8; o->box.mask = mask; o->box.hits = 0;
    if (!o->box.linked) { boxes[nboxes++] = o; o->box.linked = 1; }
}
void box_unlink(Obj *o) {
    if (!o->box.linked) return;
    for (int i = 0; i < nboxes; i++) if (boxes[i] == o) { boxes[i] = boxes[--nboxes]; break; }
    o->box.linked = 0;
}
/* player bullets are not objects: they register boxes here */
typedef struct { int used; Box box; } ExtBox;
static ExtBox extboxes[64];
Box *eng_extbox_alloc(void) { for (int i = 0; i < 64; i++) if (!extboxes[i].used) { extboxes[i].used = 1; memset(&extboxes[i].box, 0, sizeof(Box)); return &extboxes[i].box; } return NULL; }
void eng_extbox_free(Box *b) { for (int i = 0; i < 64; i++) if (&extboxes[i].box == b) extboxes[i].used = 0; }

static void collision_sweep(void) {
    /* gather all boxes */
    Box *all[MAX_OBJS + 64]; int n = 0;
    for (int i = 0; i < nboxes; i++) all[n++] = &boxes[i]->box;
    for (int i = 0; i < 64; i++) if (extboxes[i].used) all[n++] = &extboxes[i].box;
    for (int i = 0; i < n; i++) {
        Box *a = all[i]; if (a->mask & 0x8000) continue;      /* passive never scans */
        for (int j = 0; j < n; j++) {
            if (i == j) continue; Box *b = all[j];
            if (b->x > a->x - a->hw && b->x < a->x + a->hw && b->y > a->y - a->hh && b->y < a->y + a->hh) {
                b->hits |= a->mask; a->hits |= b->mask;
            }
        }
    }
}

/* ---------- frames / anim ---------- */
static const SwivFrame *frame_of_gfx(uint16_t gfx) {
    int fi = swiv_order_index(g.disk, gfx & 0x1ff); if (fi < 0) return NULL;
    const SwivLin *L = swiv_lin(g.disk, fi); int fr = gfx >> 9;
    return (L && fr < L->nframes) ? &L->frames[fr] : NULL;
}
static void box_size_from_frame(Obj *o) {
    const SwivFrame *F = frame_of_gfx(o->animA.frame);
    /* frame descriptor +16/+18 = collision half extents; the .LIN header carries w,h: use half of them */
    if (F) { o->box.hw = F->parts[0].w / 2; o->box.hh = F->parts[0].h / 2; }
}
void set_frame(Obj *o, uint16_t gfx) { o->animA.frame = gfx; box_size_from_frame(o); }
void set_frame_dir8(Obj *o, uint16_t base)  { set_frame(o, base + ((((o->angle + 16) & 0xE0) << 4))); }
void set_frame_dir16(Obj *o, uint16_t base) { set_frame(o, base + ((((o->angle + 8) & 0xF0) << 5))); }
void set_frame_table8(Obj *o, const uint16_t *t)  { set_frame(o, t[((o->angle + 16) & 0xE0) >> 5]); }
void set_frame_table16(Obj *o, const uint16_t *t) { set_frame(o, t[((o->angle + 8) & 0xF0) >> 4]); }
void gfx_acquire(Obj *o, uint16_t set) { (void)o; (void)set; }
void gfx_release(Obj *o, uint16_t set) { (void)o; (void)set; }

void anim_start(Obj *o, const int16_t *s)   { Anim *a = &o->animA; a->loop0 = a->rd = s; a->count = 0; a->rate = 3; a->countdown = 1; a->user = 0; a->active = -1; }
void anim_start_b(Obj *o, const int16_t *s) { Anim *a = &o->animB; a->loop0 = a->rd = s; a->count = 0; a->rate = 3; a->countdown = 1; a->user = 0; a->active = -1; }
static int anim_step(Obj *o, Anim *a) {
    if (!a->active) return 0;
    a->countdown -= g.vbl_per_tick; if (a->countdown > 0) return 0;
    a->countdown = a->rate;
    for (;;) {
        int16_t w = *a->rd++;
        if (w >= 0) { a->frame = (uint16_t)w; return 1; }
        int op = (w >> 11) & 15, arg = w & 0x7ff;
        switch (op) {
        case 0: a->active = 0; return 0;                          /* $8000 END */
        case 1: eng_signal(o); a->active = 0; return 0;           /* $8800 */
        case 2: if (a->count == 0) a->rd = a->loop0; else if (--a->count != 0) a->rd = a->loop0; break;   /* $9000 LOOP */
        case 3: a->count = arg; a->loop0 = a->rd; break;          /* $9800 SETLOOP */
        case 4: a->user = arg; break; case 5: a->user += arg; break;
        case 6: a->countdown = a->rate = arg; break;              /* $B000 RATE */
        case 7: a->flags |= arg; break; case 8: a->flags &= ~arg; break;
        default: a->active = 0; return 0;
        }
    }
}

/* ---------- per-tick step (LAB_04E9) ---------- */
static void offscreen_check(Obj *o) {
    if (o->cb538_disabled) return;
    int m = o->margin, sy = (int16_t)((o->y >> 16) - g.scroll3542), sx = o->x >> 16;
    if (sy <= m || sx <= m || sy - SCREEN_H >= -m || sx - SCREEN_W >= -m) { if (o->cb538) o->cb538(o); else eng_signal(o); }
}
static void dispatch_events(Obj *o) {
    uint16_t w = o->box.hits & o->enable508;
    if (w & 1)  { o->h510(o); if (!o->alive) return; w = o->box.hits & o->enable508; }
    if (w & 8)  { o->h514(o); if (!o->alive) return; w = o->box.hits & o->enable508; }
    if (w & 16) { o->h522(o); if (!o->alive) return; w = o->box.hits & o->enable508; }
    if (w & 2)  { o->h518(o); if (!o->alive) return; w = o->box.hits & o->enable508; }
    if (w & 4)  { o->h526(o); if (!o->alive) return; w = o->box.hits & o->enable508; }
    if (w & 32) { o->h530(o); if (!o->alive) return; }
    o->box.hits = 0;
}
static void fill_render(Obj *o) {
    int x = o->x >> 16, y = (int16_t)((o->y >> 16) - g.scroll3542), z = o->z >> 16;
    uint8_t f = o->animA.flags | ((o->flags367 & F_HIT_FLASH) ? 0x10 : 0);
    if (!g.render_gate155) f &= ~1;
    render_push((z ^ 0x7fff) & 0xffff, o->animA.frame, x, y, f);
    if (o->animB.active) render_push(((z ^ 0x7fff) - 1) & 0xffff, o->animB.frame, x, y, o->animB.flags | 0x20);
    if (z && !(o->flags367 & F_NO_SHADOW)) render_push(0xffff, o->animA.frame, x + z / 2, y + z, (f | 0x21) & ~0x10);
}
int step(Obj *o) {
    if (eng_signalled(o)) return 1;
    int n;
    if ((o->flags367 & F_ATTACHED) && o->parent) { o->x = o->parent->x; o->y = o->parent->y; o->z = o->parent->z; n = 1; }
    else n = g.vbl_per_tick;
    for (int i = 0; i < n; i++) { o->vx += o->ax; o->vy += o->ay; o->vz += o->az; o->x += o->vx; o->y += o->vy; o->z += o->vz; }
    if (o->z < 0) { o->z = 0; o->vz = 0; o->az = 0; }
    if (o->timer486) { o->timer486 -= g.vbl_per_tick; if (o->timer486 < 0) o->timer486 = 0; }
    offscreen_check(o);
    if (anim_step(o, &o->animA)) box_size_from_frame(o);
    anim_step(o, &o->animB);
    fill_render(o);
    o->box.x = o->x >> 16; o->box.y = o->y >> 16;
    o->scroll372 = g.scroll3542;
    do_yield(o);
    if (!o->alive) return 1;
    if (o->flags367 & F_SCREEN_LOCKED) o->y += (int32_t)(int16_t)(g.scroll3542 - o->scroll372) << 16;
    o->flags367 &= ~F_HIT_FLASH;
    if (o->cb542 && !o->parent) { o->cb542(o); if (!o->alive) return 1; }
    if (o->cb534 && g.smart_bomb169) { o->cb534(o); if (!o->alive) return 1; }
    dispatch_events(o);
    if (!o->alive) return 1;
    return eng_signalled(o);
}
int wait_ticks(Obj *o, int n) { int r = 0; while (n-- > 0) { r = step(o); if (r) break; } return r; }
int wait_vbls(Obj *o, int n) { int t = g.vblcount + n; int r = 0; while ((int16_t)(g.vblcount - t) < 0) { r = step(o); if (r) break; } return r; }
int yield_vbls(Obj *o, int n) { int t = g.vblcount + n; while ((int16_t)(g.vblcount - t) < 0) { do_yield(o); if (eng_signalled(o)) return 1; } return eng_signalled(o); }
int wait_signal(Obj *o) { while (!step(o)) ; return 1; }
int wait_onscreen(Obj *o, int m) { while ((uint16_t)((o->y >> 16) - m) < g.scroll3530) { if (step(o)) return 1; } return eng_signalled(o); }
int wait_onscreen_noevents(Obj *o, int m) { uint16_t e = o->enable508; o->enable508 = 0; int r = wait_onscreen(o, m); o->enable508 = e; return r; }
int wait_onscreen_inert(Obj *o, int m) { while ((uint16_t)((o->y >> 16) - m) < g.scroll3530) { do_yield(o); if (eng_signalled(o)) return 1; } return eng_signalled(o); }

/* ---------- enemy init / death ---------- */
void enemy_init(Obj *o, uint16_t gfx, uint16_t mask, int margin, int hp, int score, int threat) {
    o->threat = threat; o->score = score; o->gfxset = gfx;
    gfx_acquire(o, gfx);
    wait_onscreen_inert(o, margin);
    box_register(o, mask);
    set_frame(o, gfx);
    o->hp = hp;
    if (hp) { on_event(o, EV_BULLET, on_bullet_hit); if (mask & 4) on_event(o, EV_TOUCH_HELI, on_bullet_hit); if (mask & 2) on_event(o, EV_TOUCH_JEEP, on_bullet_hit); }
    o->cb534 = kill;
    g.threat156 += threat;
}
void enemy_cleanup(Obj *o) { g.threat156 -= o->threat; o->threat = 0; gfx_release(o, o->gfxset); box_unlink(o); }
void on_bullet_hit(Obj *o) {
    if (--o->hp > 0) { sfx(SFX_HIT, o->x >> 16); o->flags367 |= F_HIT_FLASH; return; }
    kill(o);
}
void kill(Obj *o) {
    if (o->box.hits & 0x40) g.heli.score += o->score; else if (o->box.hits & 0x80) g.jeep.score += o->score;
    if (o->death376) eng_spawn(o, o->death376, 100);
    if (o->popup374) { Obj *c = eng_spawn(o, fx_popup, 100); if (c) c->w[0] = o->popup374; }
    eng_signal(o);
}

/* ---------- events ---------- */
void on_event(Obj *o, int bit, Callback h) {
    switch (bit) { case 0: o->h510 = h; break; case 1: o->h518 = h; break; case 2: o->h526 = h; break; case 3: o->h514 = h; break; case 4: o->h522 = h; break; case 5: o->h530 = h; break; }
    o->enable508 |= 1 << bit;
}
void on_touch_any_player(Obj *o, Callback h) { on_event(o, 3, h); on_event(o, 4, h); }
void off_event(Obj *o, int bit) { o->enable508 &= ~(1 << bit); }

/* ---------- motion / aim ---------- */
void stop(Obj *o) { o->vx = o->vy = o->vz = o->ax = o->ay = o->az = 0; o->speed = 0; }
int sin256(int a) { return swiv_sin256[a & 255]; }
int cos256(int a) { return swiv_sin256[(a + 64) & 255]; }
void set_velocity_from_angle(Obj *o) { int a = o->angle & 255; o->vx = (int32_t)o->speed * cos256(a); o->vy = (int32_t)o->speed * sin256(a); }
int angle_to(int tx, int ty, int sx, int sy) {
    int dx = (int16_t)(tx - sx), dy = (int16_t)(ty - sy);
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    while (ax >= 32 || ay >= 32) { ax >>= 1; ay >>= 1; }
    int a = swiv_atan32[ay * 32 + ax];
    if (dx < 0) a = 128 - a;
    if (dy < 0) a = -a;
    return a & 255;
}
void turn_towards(Obj *o, int tx, int ty, int maxstep) {
    int a = angle_to(tx, ty, o->x >> 16, o->y >> 16);
    if (maxstep == 0) { o->angle = a; return; }
    int d = (int8_t)(a - o->angle);
    if (d > maxstep) d = maxstep; if (d < -maxstep) d = -maxstep;
    o->angle += d;
}
static int player_pos(struct Player *p, int *tx, int *ty) { *tx = p->x >> 16; *ty = p->y >> 16; return 1; }
int nearest_player(int *tx, int *ty) {
    if (g.heli.alive && g.jeep.alive) {
        int sx = cur ? cur->x >> 16 : 0, sy = cur ? cur->y >> 16 : 0;
        int dh = abs((g.heli.x >> 16) - sx) + abs((g.heli.y >> 16) - sy), dj = abs((g.jeep.x >> 16) - sx) + abs((g.jeep.y >> 16) - sy);
        return player_pos(dh <= dj ? &g.heli : &g.jeep, tx, ty);
    }
    if (g.heli.alive) return player_pos(&g.heli, tx, ty);
    if (g.jeep.alive) return player_pos(&g.jeep, tx, ty);
    *tx = 160; *ty = (int16_t)(g.scroll3530 + 192); return 0;
}
int alternate_player(int *tx, int *ty) {
    if (g.heli.alive && g.jeep.alive) { g.alternate168 ^= 1; return player_pos(g.alternate168 ? &g.heli : &g.jeep, tx, ty); }
    if (g.heli.alive) return player_pos(&g.heli, tx, ty);
    return player_pos(&g.jeep, tx, ty);
}
static int prefer(struct Player *a, struct Player *b, int *tx, int *ty) {
    if (a->alive) return player_pos(a, tx, ty); if (b->alive) return player_pos(b, tx, ty);
    int t = abs((int8_t)g.tick); *tx = 96 + t; *ty = (int16_t)(g.scroll3542 + 128 + t); return 0;
}
int prefer_heli(int *tx, int *ty) { return prefer(&g.heli, &g.jeep, tx, ty); }
int prefer_jeep(int *tx, int *ty) { return prefer(&g.jeep, &g.heli, tx, ty); }
int joy_to_angle(int joy, int *angle) {
    static const int tab[16] = { -1, 0xC0, 0x40, -1, 0x80, 0xA0, 0x60, -1, 0x00, 0xE0, 0x20, -1, -1, -1, -1, -1 };
    int a = tab[joy & 15]; if (a < 0) return 0; *angle = a; return 1;
}
int blocked_ahead(Obj *o) { (void)o; return 0; }   /* TODO terrain pixel test */

/* ---------- misc ---------- */
uint32_t rng(void) {
    uint32_t s = g.rng11172; uint32_t carry = s >> 31; uint32_t s2 = s << 1;
    if (!(carry == 0 && s2 != 0)) s2 ^= 0x1D872B41u;
    s2 = (s2 << 16) | (s2 >> 16); g.rng11172 = s2; return s2;
}
int threat_ok(void) { return g.threat156 <= 160; }
static void bomb_script(Obj *o) { sfx(SFX_BOMB, 160); g.smart_bomb169 = 1; for (int i = 0; i < 50; i++) yield_once(o); g.smart_bomb169 = 0; }
void smart_bomb(Obj *o) { eng_spawn(o, bomb_script, 100); }
void screen_shake(Obj *o) { g.scroll3530 -= 3; yield_once(o); g.scroll3530 += 3; yield_once(o); }
void boss_enter(void) { g.boss140++; g.flags166 |= 8; }
void boss_leave(Obj *o) { if (--g.boss140 == 0) { yield_n(o, 20); g.flags166 &= ~8; } }
void sfx(int id, int x) { (void)id; (void)x; }

/* ---------- map interpreter (LAB_02EB/LAB_02F2) ----------
 * Records come from the native SwivMap (y grows with level progress); the
 * original's units: y_orig = $E9C0 - y_mine, scroll counts down. */
SwivMap eng_map; static SwivRec *recs; static int nrecs, next_rec;
static int cmp_seq(const void *a, const void *b) { const SwivRec *r = a, *s = b; return r->seq - s->seq; }
static void map_task(Obj *o) { for (;;) { yield_once(o); } }
/* LAB_02EB: the record is consumed while the cursor (BEFORE this record's dy) is within
 * 256 px ahead of the scroll; tiles and objects share the cursor walk. */
void eng_map_interpreter(void) {
    while (next_rec < nrecs) {
        SwivRec *r = &recs[next_rec];
        if (!((uint16_t)(g.scroll3530 - 0x100) <= g.cursor3586)) break;
        next_rec++;
        uint16_t yrec = (uint16_t)(0xE9C0 - r->y);
        g.cursor3586 = yrec;
        if (r->type == 0) continue;                                    /* tile: drawn by the renderer */
        if ((int16_t)(yrec - g.scroll3530) > 0) continue;             /* behind the reference: consumed, not spawned */
        eng_spawn_map_object(r->x, yrec, (uint16_t)r->gfx, r->type);
    }
}

/* ---------- driver ---------- */
static int32_t scroll_acc;            /* 16.16 px */
static void map_task(Obj *o);
void eng_init(SwivDisk *d, int level) {
    (void)level;
    memset(&g, 0, sizeof g); g.disk = d; g.vbl_per_tick = 2; g.rng11172 = 0x12345678; g.render_gate155 = 1;
    g.clock202 = 2; g.clock204 = 0; g.missile_budget206 = 4; g.difficulty182 = 0;
    memset(pool, 0, sizeof pool); head.next = head.prev = &head; nboxes = 0; memset(extboxes, 0, sizeof extboxes);
    g.cursor3586 = 0xE9C0; g.scroll3530 = 0xE860; g.scroll3542 = g.scroll3530;
    g.heli.name = "Lazy Heli"; g.heli.no = 1; g.jeep.name = "Lazy Jeep"; g.jeep.no = 0; g.jeep.vehicle = 1;
    Obj *m = eng_spawn_at(map_task, 65535, NULL); m->name = "map";
    swiv_map_load(d, level, &eng_map);
    nrecs = eng_map.ntiles + eng_map.nobjs; recs = malloc(sizeof(SwivRec) * nrecs); next_rec = 0;
    memcpy(recs, eng_map.tiles, sizeof(SwivRec) * eng_map.ntiles); memcpy(recs + eng_map.ntiles, eng_map.objs, sizeof(SwivRec) * eng_map.nobjs);
    qsort(recs, nrecs, sizeof(SwivRec), cmp_seq);   /* original record order */
    scroll_acc = 0;
}
void eng_spawn_map_object(int x, int mapy, uint16_t gfx, int type) {
    Script s = eng_handler_for_gfx(gfx); if (!s) return;
    Obj *c = eng_spawn_at(s, 100, NULL); if (!c) return;
    c->x = x << 16; c->y = (int32_t)(int16_t)mapy << 16; c->z = 0; c->w[0] = type; c->clock = &g.clock204; c->time12 = g.clock204;
    c->gfxset = gfx; c->animA.frame = gfx;
}

void eng_vbl(void) {
    g.vbl++; g.vblcount++;
    scroll_acc += 0x4000;              /* 1/4 px per VBL (measured TOWN) */
    while (scroll_acc >= 0x10000) { scroll_acc -= 0x10000; g.scroll3530--; }
    eng_map_interpreter();
    if ((g.vbl % g.vbl_per_tick) == 0) eng_run_tick();
}
void eng_run_tick(void) {
    g.tick++;
    g.scroll3542 = g.scroll3530;
    render_count = 0;
    collision_sweep();
    /* run every task once (sorted by prio descending); tasks spawned during the pass run next pass */
    Obj *o = head.next;
    while (o != &head) {
        Obj *nx = o->next;
        if (o->alive && o->coro && !o->started) o->started = 1;
        if (o->alive && o->coro) {
            cur = o;
            int live = coro_resume(o->coro);
            cur = NULL;
            if (!live || !o->alive) { if (o->coro) { coro_free(o->coro); o->coro = NULL; } if (o->alive) { enemy_cleanup(o); list_remove(o); o->alive = 0; } }
        }
        o = nx;
    }
}
