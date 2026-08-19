/* effects.c -- death effects and firing verbs (VERBS.md §3, §5) */
#include "engine.h"
#include <stdlib.h>

/* LAB_0635: explosion gfx set 5, rate 4, frames 7..13 then signal */
static const int16_t EXPL_ANIM[] = { A_RATE(4), 0x0E05, 0x1005, 0x1205, 0x1405, 0x1605, 0x1805, 0x1A05, A_END_SIGNAL, A_END };
void fx_explosion_silent(Obj *o) {
    gfx_acquire(o, 5); o->flags367 |= F_NO_SHADOW; o->z += 1 << 16; stop(o);
    anim_start(o, EXPL_ANIM); set_frame(o, 0x0E05);
    wait_signal(o); gfx_release(o, 5);
}
void fx_explosion(Obj *o) { sfx(SFX_EXPL2, o->x >> 16); fx_explosion_silent(o); }

/* LAB_062D: burning wreck: set 6 anim then spawn small explosions every 15 ticks until off-screen */
static const int16_t WRECK_ANIM[] = { A_RATE(6), 0x0006, 0x0206, 0x0406, 0x0606, 0x0806, 0x0A06, 0x0C06, A_END_SIGNAL, A_END };
void fx_wreck(Obj *o) {
    sfx(SFX_EXPL1, o->x >> 16); gfx_acquire(o, 6); o->flags367 |= F_NO_SHADOW; stop(o);
    anim_start(o, WRECK_ANIM); set_frame(o, 0x0006);
    if (!wait_signal(o)) return;
    /* after the burn animation the object keeps burning: explosions at random offsets until off-screen */
    o->time12 = *o->clock;      /* un-signal: the anim signal only ends the first phase */
    for (;;) {
        Obj *c = eng_spawn(o, fx_explosion_silent, 100);
        if (c) { c->x += (int32_t)(((int)(rng() & 63)) - 31) << 16; c->y += (int32_t)(((int)(rng() & 63)) - 31) << 16; }
        if (wait_ticks(o, 15)) break;
    }
    gfx_release(o, 6);
}
static void ring(Obj *o, int n, int speed) {
    for (int i = 0; i < n; i++) {
        Obj *c = eng_spawn(o, fx_explosion_silent, 100);
        if (c) { c->speed = speed; c->angle = (uint8_t)(i * 100); set_velocity_from_angle(c); }
    }
}
void fx_ring8(Obj *o)  { sfx(SFX_BIGEXPL, o->x >> 16); ring(o, 8, 0x600); }
void fx_ring16(Obj *o) { sfx(SFX_BIGEXPL, o->x >> 16); ring(o, 16, 0x300); }
/* LAB_0636: popup: show gfx w[0] one tick, then jump 320 px up, one tick, free */
void fx_popup(Obj *o) {
    o->animA.flags |= 0x41; stop(o); o->z = 0; set_frame(o, (uint16_t)o->w[0]); o->flags367 |= F_NO_SHADOW;
    if (step(o)) return; o->y -= 320 << 16; step(o);
}
void explode_at(Obj *o, int dx, int dy) {
    Obj *c = eng_spawn(o, fx_wreck, 100); if (c) { c->x += dx << 16; c->y += dy << 16; }
    yield_n(o, 20);
}
void boss_smoke(Obj *o) {
    o->w[3] = 0;
    if (o->hp <= 50) {
        uint32_t r = rng(); o->w[3] = (int16_t)((r & 3) - 2);
        if ((r & 15) == 0) { Obj *c = eng_spawn(o, fx_explosion, 100); if (c) c->x += (int32_t)((int)(rng() & 31) - 15) << 16; }
    }
}

/* ---------- firing (LAB_0613 / LAB_0809 / LAB_069x) ---------- */
/* LAB_0616: homing bullet */
static void bullet_script(Obj *o) {
    enemy_init(o, 0x0802, 38, -16, 1, 7, 5);
    o->flags367 |= F_SCREEN_LOCKED | F_NO_SHADOW; stop(o);
    int tx, ty; alternate_player(&tx, &ty); o->w[0] = g.alternate168;   /* remember which player (w[0] = player choice) */
    o->z = 32 << 16; o->margin = 0; o->speed = 0x300; o->angle = (uint8_t)o->w[8];
    set_velocity_from_angle(o); set_frame_dir16(o, 0x0002);
    if (wait_ticks(o, 20)) return;
    for (int n = 2 * (5 + g.difficulty182); n-- > 0; ) {
        struct Player *p = (o->w[0] & 1) ? &g.heli : &g.jeep; if (!p->alive) p = p == &g.heli ? &g.jeep : &g.heli;
        turn_towards(o, p->x >> 16, p->y >> 16, 14); set_velocity_from_angle(o); set_frame_dir16(o, 0x0002);
        if (wait_ticks(o, 8)) return;
    }
    wait_signal(o);
}
static const int16_t FLASH_ANIM[] = { A_RATE(1), 0x0401, A_END_SIGNAL, A_END };
static void flash_script(Obj *o) {
    enemy_init(o, 7, 0x8000, -16, 0, 0, 1); o->z = 33 << 16; o->flags367 |= F_NO_SHADOW; o->margin = 0; stop(o);
    anim_start(o, FLASH_ANIM); set_frame(o, 0x0401); wait_signal(o);
}
void fire_homing(Obj *o, int dx, int dy, int angle) {
    Obj *b = eng_spawn(o, bullet_script, 100);
    if (b) { b->x += dx << 16; b->y += dy << 16; b->w[8] = (int16_t)angle; }
    Obj *f = eng_spawn(o, flash_script, 100);
    if (f) { f->x += dx << 16; f->y += dy << 16; }
}
extern const uint8_t swiv_fire_patterns[60];
void fire_pattern(Obj *o, int idx) {
    const uint8_t *t = swiv_fire_patterns + idx * 12;
    uint16_t gfx = (t[0] << 8) | t[1]; int ang = (int16_t)((t[2] << 8) | t[3]);
    set_frame(o, gfx); o->angle = (uint8_t)ang;
    fire_homing(o, (int8_t)t[5], (int8_t)t[7], ang); if (wait_ticks(o, 5)) return;
    fire_homing(o, (int8_t)t[9], (int8_t)t[11], ang); wait_ticks(o, 5);
}
/* LAB_06A1 missile body: w[4]=accelerate flag, w[5]=aim flag */
static void missile_body(Obj *o) {
    g.missile_budget206--; sfx(SFX_MISSILE, o->x >> 16);
    box_register(o, 0x8006); o->margin = 0; o->flags367 |= F_NO_SHADOW;
    if (o->w[5]) { int tx, ty; alternate_player(&tx, &ty); turn_towards(o, tx, ty - 8, 0); }
    o->speed = 0x1000; set_velocity_from_angle(o); o->x += o->vx; o->y += o->vy;
    on_touch_any_player(o, (Callback)eng_signal);
    int toggle = 0;
    if (o->w[4]) o->speed = 0x80; else o->speed = 0x500;
    set_velocity_from_angle(o);
    for (int k = 0; ; k++) {
        set_frame(o, (uint16_t)(0x3001 + ((((o->angle + 8) & 0xF0) << 5)) + (toggle ? 0x200 : 0))); toggle ^= 1;
        o->z = ((o->y >> 16) / 2) << 16;
        if (o->w[4] && k < 20 * 5 && k % 5 == 4) { o->speed += 0x80; set_velocity_from_angle(o); }
        if (step(o)) break;
    }
    box_unlink(o); g.missile_budget206++;
}
void fire_missile_aimed(Obj *o) { if (g.missile_budget206 < 0) return; Obj *m = eng_spawn(o, missile_body, 100); if (m) { m->w[4] = 1; m->w[5] = 1; } eng_spawn(o, flash_script, 100); }
void fire_missile_ahead(Obj *o) { Obj *m = eng_spawn(o, missile_body, 100); if (m) { m->w[4] = 1; m->w[5] = 0; } }
void fire_missile_fast(Obj *o)  { Obj *m = eng_spawn(o, missile_body, 100); if (m) { m->w[4] = 0; m->w[5] = 0; } }
