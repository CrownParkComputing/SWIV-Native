/* bh_group5.c -- behaviour group 5: _LAVA#20, ORB#0, FISH#0, _RIGS#4, LAKEGUN#0/#7, LAKESUB#0,
 * HOVER#4, SEAPLANE#0, INST1#14/#11/#9, INST2#2.  Ported from re/handlers_group5.asm (addresses in
 * comments are the disk-listing addresses, see OBJECT.md for the runtime shift). */
#include "../engine/engine.h"
#include <stdlib.h>

#ifndef PX
#define PX(v)  ((int32_t)(v) * 65536)
#endif
#define XI(o)  ((int16_t)((o)->x >> 16))
#define YI(o)  ((int16_t)((o)->y >> 16))
#define ZI(o)  ((int16_t)((o)->z >> 16))
#define SCRY(o) ((int16_t)(YI(o) - g.scroll3542))      /* screen y */

/* ---- globals not in the engine's Globals (file-static stand-ins) ---- */
static int8_t  flag3615;        /* 3615(A6): set by FISH, cleared by INST4#0 (write-only here) */
static int16_t fodder_count146; /* 146(A6): fodder wave counter (LAB_05EB/05EF) */

/* ---- verbs missing from engine.h, implemented here ---- */
/* LAB_04FB: off-screen check outside step() (used after plain yields) */
static void offscreen_check(Obj *o) {
    if (o->cb538_disabled) return;
    int m = o->margin, sy = SCRY(o), sx = XI(o);
    if (sy <= m || sx <= m || sy - 256 >= -m || sx - 320 >= -m) { if (o->cb538) o->cb538(o); else eng_signal(o); }
}
/* LAB_06DA: if not yet signalled, step until signalled */
static void run_until_signalled(Obj *o) { if (!eng_signalled(o)) while (!step(o)) ; }

/* LAB_067C @ $215078: water splash (gfx set $42), decor only */
static const int16_t SPLASH_ANIM[] = { A_RATE(4), 0x4200, 0x4400, 0x4600, 0x4800, 0x4a00, A_END_SIGNAL, A_END };
static void fx_splash(Obj *o) {
    enemy_init(o, 0x4200, 0, -16, 0, 0, 0);
    o->cb534 = NULL; stop(o); o->z = 0; o->margin = 0; o->animA.flags |= 1;
    anim_start(o, SPLASH_ANIM); wait_signal(o);
}

/* LAB_07C8 @ $2175ea: boss hit handler -- on death both live players get the score, smart bomb if last boss */
static void boss_hit(Obj *o) {
    if (--o->hp > 0) { sfx(SFX_HIT, XI(o)); o->flags367 |= F_HIT_FLASH; return; }   /* LAB_03D6 + LAB_0727 */
    if (g.heli.alive) g.heli.score += o->score;
    if (g.jeep.alive) g.jeep.score += o->score;
    if (g.boss140 <= 1) smart_bomb(o);
    kill(o);
}

/* ================================================================== _LAVA.LIN#20 @ $216cbc */
static const int16_t LAVA_ROCK_ANIM[] = { A_RATE(8), 0x284c, 0x2a4c, A_LOOP, A_END };
/* LAB_0787: flying lava rock */
static void lava_rock(Obj *o) {
    enemy_init(o, 0x284c, 38, -16, 1, 30, 10);
    if (!threat_ok()) return;
    anim_start(o, LAVA_ROCK_ANIM);
    o->speed = (rng() & 0x7f) + 0x140; set_velocity_from_angle(o);
    o->z = PX(1);
    o->vz = (int32_t)(rng() & 0x1ffff) + PX(2);      /* ADDQ.W #2 hits the high word */
    o->az = -0x1800;
    for (;;) { if (step(o)) return; if (ZI(o) == 0) break; }
    kill(o);
}
void bh__lava_20(Obj *o) {
    g.render_gate155 = 0;                              /* SF 155(A6) */
    enemy_init(o, 0x284c, 0x8000, 32, 0, 0, 0);
    o->margin = 0;
    for (;;) {                                          /* LAB_0785 */
        spawn(fx_explosion); sfx(SFX_EXPL1, XI(o));    /* LAB_0405 */
        o->w[0] = (int16_t)((g.rng11172 & 7) + 3);
        do {                                            /* LAB_0786 */
            o->angle += (rng() & 0x1f) + 0x80;
            spawn(lava_rock);
            yield_n(o, 10 - o->w[0]);
        } while (--o->w[0]);
        yield_n(o, g.rng11172 & 127);
        offscreen_check(o);
        if (eng_signalled(o)) return;
    }
}

/* ================================================================== ORB.LIN#0 @ $216da4 */
static const uint16_t ORB_GFX[6] = { 0x0252, 0x0452, 0x0652, 0x0852, 0x0a52, 0x0c52 };   /* LAB_078C */
static const int16_t ORB_GUN_ANIM[] = { A_RATE(8), 0x1e52, 0x2052, 0x2252, 0x2452, 0x1a52, A_END };
/* LAB_078D: the gun that the orb drops (homing, accelerating) */
static void orb_gun(Obj *o) {
    o->x -= PX(4); o->y += PX(2);
    enemy_init(o, 0x0e52, 34, -32, 1, 70, 10);
    yield_n(o, (o->w[0] + 1) << 6);
    o->margin = -10;
    anim_start(o, ORB_GUN_ANIM);
    wait_ticks(o, 50);                                  /* result not checked in the original */
    o->z = PX(0x20); o->speed = 0x80; o->angle = 0xc0;
    for (;;) {                                          /* LAB_078E */
        int tx, ty; nearest_player(&tx, &ty); turn_towards(o, tx, ty, 33);
        o->angle = (o->angle + 0x10) & 0xe0; set_velocity_from_angle(o);
        set_frame_dir8(o, 0x0e52);                      /* LAB_078F */
        o->speed += 0x40;
        if (wait_ticks(o, 15)) break;
    }
    wait_signal(o);
}
/* LAB_078B: one orb */
static void orb_child(Obj *o) {
    enemy_init(o, ORB_GFX[o->w[0] % 6], 0x8000, -16, 0, 0, 7);
    o->flags367 |= F_NO_SHADOW;
    wait_onscreen(o, o->w[0] + 0x40);
    o->angle = (uint8_t)g.rng11172;
    spawn(orb_gun);
    o->z = PX(0x20); o->speed = 0x600; set_velocity_from_angle(o);
    wait_ticks(o, 15);
}
void bh_orb_0(Obj *o) {
    enemy_init(o, 0x0252, 0x8000, -16, 0, 0, 0);
    g.render_gate155 = 1;                               /* ST 155(A6) */
    o->w[0] = 5;
    do { spawn(orb_child); } while (--o->w[0] >= 0);    /* 6 orbs, w[0] = 5..0 */
}

/* ================================================================== FISH.LIN#0 @ $216ec8 */
static void cont_fish(Obj *o) {
    flag3615 = -1;                                      /* ST 3615(A6) */
    enemy_init(o, 0x001e, 34, -48, 1, 40, 10);
    if (!threat_ok()) return;
    o->z = PX(rng() & 0x1f); o->az = -0x1000; o->vy = PX(2);
    for (;;) {                                          /* LAB_0790 */
        if (ZI(o) == 0) {
            o->vz = PX(2); o->az = -0x1000; spawn(fx_splash);
            if (o->w[0] == 2) { o->vx = (int32_t)(int16_t)rng(); fire_missile_aimed(o); }
        }
        if (step(o)) return;
    }
}
void bh_fish_0(Obj *o) { formation(o, 0, -5, 6, 0, cont_fish); cont_fish(o); }

/* ================================================================== _RIGS.LIN#4 @ $216f4a */
void bh__rigs_4(Obj *o) {
    enemy_init(o, 0x083e, 36, -16, 20, 40, 10);
    wait_onscreen(o, 24);
    o->w[0] = 5;
    do {                                                /* LAB_0793 */
        if (wait_ticks(o, 20)) return;
        o->angle = 0x40; fire_missile_fast(o);          /* LAB_069A */
    } while (--o->w[0]);
    wait_signal(o);
}

/* ================================================================== LAKEGUN.LIN#0 @ $216f8c / #7 @ $216ffc */
static const int16_t LAKEGUN0_OPEN[]  = { A_RATE(6), 0x0228, 0x0428, 0x0628, 0x0828, 0x0a28, 0x0c28, A_END };
static const int16_t LAKEGUN0_CLOSE[] = { A_RATE(6), 0x0a28, 0x0828, 0x0628, 0x0428, 0x0228, A_END_SIGNAL, A_END };
static const int16_t LAKEGUN7_OPEN[]  = { A_RATE(6), 0x1028, 0x1228, 0x1428, 0x1628, 0x1828, 0x1a28, A_END };
static const int16_t LAKEGUN7_CLOSE[] = { A_RATE(6), 0x1828, 0x1628, 0x1428, 0x1228, 0x1028, A_END_SIGNAL, A_END };
static void lakegun(Obj *o, uint16_t gfx, const int16_t *open, const int16_t *close, int dx, int ang) {
    enemy_init(o, gfx, 36, 100, 4, 75, 10);
    o->popup374 = 5;
    anim_start(o, open);
    if (wait_ticks(o, 50)) return;
    o->w[0] = 6;
    do {                                                /* LAB_0795 / LAB_0797 */
        if (wait_ticks(o, 30)) return;
        fire_homing(o, dx, 0, ang);
    } while (--o->w[0]);
    anim_start(o, close);
    wait_signal(o);
}
void bh_lakegun_0(Obj *o) { lakegun(o, 0x0028, LAKEGUN0_OPEN, LAKEGUN0_CLOSE, -8, 0x80); }
void bh_lakegun_7(Obj *o) { lakegun(o, 0x0e28, LAKEGUN7_OPEN, LAKEGUN7_CLOSE,  8, 0x00); }

/* ================================================================== LAKESUB.LIN#0 @ $21706a */
static const int16_t LAKESUB_SURFACE[] = { A_RATE(6), 0x0229, 0x0429, 0x0629, 0x0829, A_RATE(12), A_SETLOOP(0), 0x0a29, 0x0829, A_LOOP, A_END };
static const int16_t LAKESUB_DIVE[]    = { A_RATE(6), 0x0829, 0x0629, 0x0429, 0x0229, A_END_SIGNAL, A_END };
static const int16_t LAKESUB_HATCH[]   = { A_RATE(6), 0x0e29, 0x1029, 0x1229, 0x1429, 0x1429, 0x1229, 0x1029, 0x0e29, 0x0c29, A_END_SIGNAL, A_END };
/* LAB_079C: torpedo */
static void sub_torpedo(Obj *o) {
    enemy_init(o, 0x1629, 6, -16, 0, 0, 1);
    o->margin = 0; o->speed = 0x180; set_velocity_from_angle(o);
    o->z &= 0xffff;                                     /* CLR.W 328(A5) */
    for (;;) {                                          /* LAB_079D */
        if (ZI(o) == 0) { o->vz = PX(2); o->az = -0x1800; spawn(fx_splash); }
        if (step(o)) return;
    }
}
/* LAB_0799: hatch/turret on the sub (attached) */
static void sub_hatch(Obj *o) {
    enemy_init(o, 0x0029, 0, 0, 0, 0, 5);
    stop(o); o->flags367 |= F_NO_SHADOW | F_FLASH_WITH_PARENT | F_ATTACHED; o->vz = PX(1);
    anim_start(o, LAKESUB_HATCH);
    if (wait_ticks(o, 26)) return;
    int tx, ty; nearest_player(&tx, &ty); turn_towards(o, tx, ty, 0);
    o->w[0] = 5;
    do { spawn(sub_torpedo); o->angle += 0x33; } while (--o->w[0]);   /* LAB_079A */
    wait_signal(o);
}
void bh_lakesub_0(Obj *o) {
    enemy_init(o, 0x0029, 36, 64, 6, 80, 17);
    anim_start(o, LAKESUB_SURFACE);
    if (wait_ticks(o, 40)) return;
    spawn_attached(sub_hatch);                          /* LAB_04CA try-once */
    if (wait_ticks(o, 70)) return;
    anim_start(o, LAKESUB_DIVE);
    wait_signal(o);
}

/* ================================================================== HOVER.LIN#4 @ $217186 */
static const int16_t HOVER_ANIM[] = { A_RATE(5), 0x022a, 0x042a, 0x062a, 0x082a, 0x0a2a, A_END };
/* LAB_07A1: hovercraft skirt/shadow sprite (attached, flashes with parent) */
static void hover_skirt(Obj *o) {
    enemy_init(o, 0x0c2a, 36, -48, 0, 0, 0);
    o->flags367 |= F_FLASH_WITH_PARENT | F_ATTACHED;
    wait_signal(o);
}
/* LAB_07A2: thing launched from the hovercraft: rises to z=32, drifts down, sprays missiles */
static void hover_launcher(Obj *o) {
    enemy_init(o, 0x0e2a, 34, -48, 10, 50, 10);
    o->margin = -8; stop(o); o->vz = PX(1);
    for (;;) { if (step(o)) return; if ((uint16_t)ZI(o) >= 0x20) break; }     /* LAB_07A3 */
    o->vz = 0; o->vy = 0x4000;
    if (wait_onscreen(o, 0xe0)) return;
    int8_t b = 0;                                       /* CLR.B 276(A5): low byte of w[0] */
    for (;;) {                                          /* LAB_07A4 */
        int8_t d = b; if (d >= 0) d = -d;
        o->angle = (uint8_t)d;
        fire_missile_ahead(o);                          /* LAB_069B */
        b += 0x10;
        if (step(o)) return;
    }
}
void bh_hover_4(Obj *o) {
    enemy_init(o, 0x002a, 36, -48, 10, 90, 15);
    o->death376 = fx_ring8;
    o->z = PX(2); o->flags367 |= F_NO_SHADOW;
    spawn_attached(hover_skirt);
    if (o->w[0] - 1 != 0) {
        o->vy = PX(1); o->vx = 0x8000;
        if (XI(o) >= 0xa0) o->vx = -o->vx;
    }
    if (wait_onscreen(o, 100)) return;                  /* LAB_079F */
    anim_start(o, HOVER_ANIM);
    o->ay = -0x800;
    if (wait_ticks(o, 23)) return;
    spawn(hover_launcher);
    if (wait_ticks(o, 25)) return;
    o->ay = 0x1000;
    if (wait_ticks(o, 50)) return;
    o->vx = 0; o->ay = 0;
    wait_signal(o);
}

/* ================================================================== SEAPLANE.LIN#0 @ $2172bc */
static const int16_t SEABOMB_ANIM[] = { A_RATE(2), 0x022f, 0x042f, A_LOOP, A_END };
/* LAB_07AF: dropped bomb */
static void sea_bomb(Obj *o) {
    enemy_init(o, 0x022f, 34, -16, 1, 10, 5);
    o->z = 0x00200000; stop(o); o->vy = 0x8000;
    anim_start(o, SEABOMB_ANIM);
    if (wait_ticks(o, 20)) return;
    o->az = -0x800;
    for (;;) {                                          /* LAB_07B0 */
        if (ZI(o) == 0) { spawn(fx_explosion); eng_signal(o); }
        if (step(o)) return;
    }
}
/* LAB_07AB: taxi across the water with a random drift until y >= scroll + d; splashes */
static int seaplane_taxi(Obj *o, int d) {
    uint32_t r = rng();
    o->vx = ((int32_t)(int16_t)(r >> 16)) >> 1;
    o->vy = ((int32_t)(r & 0xffff)) >> 1;
    for (;;) {                                          /* LAB_07AC */
        if (step(o)) return 1;
        if ((rng() & 7) == 0) spawn(fx_splash);
        if (!((uint16_t)(d + g.scroll3542) > (uint16_t)YI(o))) return 0;
    }
}
void bh_seaplane_0(Obj *o) {
    enemy_init(o, 0x002f, 36, -16, 8, 45, 20);
    if (o->w[0] - 1 != 0) seaplane_taxi(o, 48);        /* result not checked */
    o->animA.flags &= ~1;                               /* LAB_07A7 */
    off_event(o, EV_TOUCH_HELI);                        /* LAB_050F */
    on_event(o, EV_TOUCH_JEEP, on_bullet_hit);          /* LAB_0509 */
    o->box.mask = 0x22;
    o->vz = 0x8000;
    for (;;) { if (step(o)) return; if ((uint16_t)ZI(o) >= 0x20) break; }     /* LAB_07A8 */
    o->vz = 0; o->ay = 0x800;
    for (;;) { spawn(sea_bomb); if (wait_ticks(o, 10)) return; }             /* LAB_07A9 */
}

/* ================================================================== INST1.LIN#14 @ $2173ee (hangar door + plane launcher) */
/* LAB_07C0: plane that leaves the hangar */
static void hangar_plane(Obj *o) {
    enemy_init(o, 0x061c, 4, -32, 7, 70, 8);
    o->z = 0;
    wait_ticks(o, 70);
    for (;;) {                                          /* LAB_07C1 */
        o->vy = 0x8000;
        if (wait_ticks(o, (rng() & 0x3f) + 0x32)) return;
        if (g.boss140) {
            o->vy = 0;
            if (wait_ticks(o, 10)) return;
            int tx, ty; alternate_player(&tx, &ty); turn_towards(o, tx, ty, 0);
            fire_homing(o, 0, -4, o->angle);
        }
        if (wait_ticks(o, 30)) return;
    }
}
/* LAB_07BA: the door (attached child of the frame) -- slides up 27 px, releases a plane, slides back */
static void hangar_door(Obj *o) {
    o->y += PX(0x39);
    enemy_init(o, o->w[0] == 1 ? 0x201c : 0x221c, 36, -48, 0, 0, 15);
    o->flags367 |= F_NO_SHADOW; o->z = PX(1);
    for (;;) {                                          /* LAB_07BC */
        if (wait_ticks(o, (rng() & 0x3c) + 0x96)) return;
        if (g.boss140 == 0) continue;
        spawn(hangar_plane);
        o->w[0] = 0x1b;
        do { if (step(o)) return; o->y -= PX(1); } while (--o->w[0]);     /* LAB_07BD */
        if (wait_ticks(o, 30)) return;
        o->w[0] = 0x1b;
        do { if (step(o)) return; o->y += PX(1); } while (--o->w[0]);     /* LAB_07BE */
    }
}
void bh_inst1_14(Obj *o) {
    spawn_attached(hangar_door);
    enemy_init(o, 0x1c1c, 0x8000, -60, 0, 0, 10);
    o->flags367 |= F_NO_SHADOW; o->z = PX(2);
    for (;;) {                                          /* LAB_07B7 */
        Obj *c = o->child;
        if (c) o->animA.frame = (YI(c) & 2) ? 0x1e1c : 0x1c1c;     /* frame written directly (398), no box update */
        if (step(o)) return;
    }
}

/* ================================================================== INST1.LIN#11 @ $217530 (installation 1 boss) */
static const int16_t INST1_FIRE_ANIM[]  = { A_RATE(5), 0x1a1c, 0x181c, 0x161c, A_END };
static const int16_t INST1_SHELL_ANIM[] = { A_RATE(1), 0x081c, 0x0a1c, 0x0c1c, 0x0c1c, 0x0c1c, 0x0e1c, 0x0e1c, 0x101c, 0x101c, A_END_SIGNAL, A_END };
/* LAB_07CD: boss shell landing 118 px below */
static void inst1_shell(Obj *o) {
    enemy_init(o, 0x081c, 6, -63, 0, 0, 20);
    o->z = 0; o->y += PX(0x76); o->x += PX(1);
    anim_start(o, INST1_SHELL_ANIM);
    wait_ticks(o, 5);
    o->box.mask = 0;
    run_until_signalled(o);
}
/* LAB_07CE: boss death: popup + 16 wrecks */
static void inst1_death(Obj *o) {
    o->x += PX(2); o->w[0] = 0x041c;
    spawn(fx_popup);                                    /* LAB_0637 (popup without the |1 flag) */
    for (int n = 4; n; n--) {                           /* LAB_07CF */
        explode_at(o, 0, 0); explode_at(o, 48, -48); explode_at(o, -48, -48); explode_at(o, 24, -32);
    }
    eng_free(o);
}
/* LAB_07C7: fire animation + shell, then 40 ticks */
static int inst1_fire(Obj *o) {
    anim_start(o, INST1_FIRE_ANIM);
    sfx(SFX_SHOT, XI(o));                               /* LAB_040F */
    spawn_prio(inst1_shell, 100);                       /* LAB_04D2 prio 100 */
    return wait_ticks(o, 40);
}
void bh_inst1_11(Obj *o) {
    gfx_acquire(o, 6);
    enemy_init(o, 0x161c, 38, -63, 90, 0x9c4, 20);
    o->death376 = inst1_death;
    on_event(o, EV_BULLET, boss_hit); on_touch_any_player(o, boss_hit);
    o->z = PX(0x10);
    wait_onscreen_noevents(o, 84);
    boss_enter();
    for (;;) {                                          /* LAB_07C4 */
        if (wait_ticks(o, 100)) break;
        o->w[0]--;
        if ((o->w[0] & 3) == 0) {
            Obj *c = spawn(eng_handler_for_gfx(0x0003));    /* LAB_06F5 = MEDTANK.LIN#0 */
            if (c) { c->y = PX((int16_t)(g.scroll3542 - 16)); c->x = PX((rng() & 0x3f) + 0xec); c->w[0] = 3; }
        }
        if (inst1_fire(o)) break;
        if (inst1_fire(o)) break;
        if (inst1_fire(o)) break;
    }
    gfx_release(o, 6);                                  /* LAB_07C6 */
    boss_leave(o);
}

/* ================================================================== INST1.LIN#9 @ $217674 (blinking light) */
static const int16_t INST1_LIGHT_ANIM[] = { A_RATE(4), 0x121c, 0x141c, A_LOOP, A_END };
void bh_inst1_9(Obj *o) {
    enemy_init(o, 0x121c, 0, -24, 0, 0, 2);
    anim_start(o, INST1_LIGHT_ANIM);
    wait_signal(o);
}

/* ================================================================== INST2.LIN#2 @ $2176fa (installation 2 boss gun) */
/* LAB_05F9: random x, small random vx, fall with ay */
static void fodder_place(Obj *o) {
    uint32_t r = rng();
    o->x = PX((r & 0xff) + 0x20);
    o->vx = ((int32_t)(int16_t)(r >> 16)) * 2;
    o->vy = 0; o->ax = 0; o->ay = 0x800;
}
static const int16_t FODDER_ANIM_A[] = { A_RATE(1), A_SETLOOP(0), 0x0004, 0x0204, A_LOOP, A_END };
static const int16_t FODDER_ANIM_B[] = { A_RATE(1), A_SETLOOP(0), 0x0404, 0x0604, 0x0404, 0x0804, 0x0404, 0x0a04, 0x0404, 0x0c04, A_LOOP, A_END };
/* LAB_05EF @ $213d86: the fodder plane body (shared with FODDERA.LIN#2) */
static void fodder_body(Obj *o) {
    if ((fodder_count146 & 3) == 0) fodder_place(o);
    enemy_init(o, 0x0404, 34, -48, 1, 12, 10);
    if (!threat_ok()) return;
    o->flags367 |= F_SCREEN_LOCKED;
    anim_start(o, (o->w[0] - 1 == 0) ? FODDER_ANIM_A : FODDER_ANIM_B);
    o->z = PX(0x20);
    uint16_t timer = 0xffff;                            /* 276(A5) reused as a missile timer */
    uint32_t r = rng();
    if ((r & 3) + g.difficulty182 >= 5) timer = ((r >> 16) & 0x3f) + 0x20;
    for (;;) {                                          /* LAB_05F3 */
        if ((uint16_t)(o->vy >> 16) >= 3) o->ay = 0;
        int x = XI(o);
        if (x < 0x20) o->ax = 0x800; else if (x > 0x120) o->ax = -0x800;
        uint16_t t2 = timer - g.vbl_per_tick; if (timer < (uint16_t)g.vbl_per_tick) fire_missile_aimed(o); timer = t2;
        if (step(o)) return;
    }
}
/* LAB_05EB @ $213d28: fodder wave: spawn (4-boss)*4 fodder planes 10 ticks apart, then become one */
static void fodder_wave(Obj *o) {
    fodder_count146 = 0;
    stop(o); o->y = PX((int16_t)(g.scroll3542 - 32)); o->w[0] = 1;
    int n = (4 - g.boss140) * 4;
    do {                                                /* LAB_05EC */
        spawn(fodder_body);
        yield_n(o, 10);
        if (eng_signalled(o)) break;
    } while (--n);
    fodder_body(o);
}
static const int16_t INST2_OPEN[]  = { A_RATE(5), 0x062c, 0x082c, 0x0a2c, 0x0c2c, A_END };
static const int16_t INST2_CLOSE[] = { A_RATE(5), 0x0a2c, 0x082c, 0x062c, 0x042c, A_END };
static const int16_t INST2_SHELL_ANIM[] = { A_RATE(1), 0x102c, 0x122c, 0x142c, 0x162c, 0x182c, A_END_SIGNAL, A_END };
/* LAB_07D5: shell landing 105 px below */
static void inst2_shell(Obj *o) {
    enemy_init(o, 0x102c, 6, -63, 0, 0, 20);
    o->flags367 |= F_NO_SHADOW; o->z = PX(1); o->y += PX(0x69);
    anim_start(o, INST2_SHELL_ANIM);
    wait_ticks(o, 5);
    o->box.mask = 0;
    run_until_signalled(o);
}
void bh_inst2_2(Obj *o) {
    gfx_acquire(o, 6);
    enemy_init(o, 0x042c, 36, -32, 50, 2000, 40);
    off_event(o, EV_BULLET);
    o->death376 = fx_wreck;
    wait_onscreen(o, o->w[0] == 1 ? 83 : 57);
    boss_enter();
    wait_ticks(o, 100);
    for (;;) {                                          /* LAB_07D2 */
        spawn(fodder_wave);
        anim_start(o, INST2_OPEN);
        if (wait_ticks(o, 20)) break;
        on_event(o, EV_BULLET, boss_hit);
        if (wait_ticks(o, 50)) break;
        for (;;) {                                      /* LAB_07D3 */
            sfx(SFX_SHOT, XI(o));                       /* LAB_040F */
            spawn_prio(inst2_shell, 100);
            if (wait_ticks(o, 20)) goto out;
            if ((int16_t)rng() < 0) break;
        }
        anim_start(o, INST2_CLOSE);
        off_event(o, EV_BULLET);
        if (wait_ticks(o, ((4 - g.boss140) << 5) + 0x14)) break;
    }
out:
    gfx_release(o, 6);                                  /* LAB_07D4 */
    boss_leave(o);
}
