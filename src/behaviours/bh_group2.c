/* bh_group2.c -- GROUP 2 behaviour handlers, ported from re/handlers_group2.asm
 * (AMPROG listing addresses in the comments; see re/PORTING_GUIDE.md).
 *
 *   XEVIOUS#9  @213bf8   FODDERA#2 @213d68   _ONERIG#0 @213e86   _CORN#7 @213f2c
 *   TRILO#4    @213f8a   VTOL#0    @214064   FROG#0    @2140fc   EGGS#2  @214198
 *   BIRD#0     @21434e   YELLOW#0  @2143dc   GOOSE#7   @2144b4   ROTOBASE#12 @21566e
 *   TAP#0      @215700
 */
#include "../engine/engine.h"

/* ---- globals of the original that the engine does not expose ----------------------------- */
static int16_t g146_fodder_count;   /* 146(A6): FODDERA record counter (CLR at level start + LAB_05EB) */
static int16_t g138_roto_toggle;    /* 138(A6): ROTOBASE spin-direction toggle (NOT.W each spawn) */

/* 68k `MOVE.W #n,off(A5)` on a 16.16 long: writes the integer word, keeps the fraction */
#define SETW(field, n) ((field) = (int32_t)(((uint32_t)(field) & 0xffff) | ((uint32_t)(uint16_t)(n) << 16)))
#define WORD(field)    ((int16_t)((field) >> 16))
#define UWORD(field)   ((uint16_t)((field) >> 16))

/* =========================================================================================
 * LAB_05E9 @213cba -- XEVIOUS bomb (dropped by XEVIOUS#9, also spawned by other groups)
 * ========================================================================================= */
static const int16_t ANIM_BOMB[] = { A_RATE(4), 0x0601, 0x0801, A_LOOP, A_END };
void bh_xevious_bomb_05e9(Obj *o) {
    sfx(SFX_MISSILE, o->x >> 16);                       /* LAB_03CC: bomb whistle (closest SFX) */
    enemy_init(o, 0x0601, 6, -16, 0, 0, 3);
    stop(o);
    o->flags367 |= F_NO_SHADOW | F_SCREEN_LOCKED;       /* ORI.B #$11,367 */
    o->margin = 0;
    anim_start(o, ANIM_BOMB);
    o->speed = 0x200;
    int tx, ty; alternate_player(&tx, &ty);
    turn_towards(o, tx, ty, 0);
    o->angle += (uint8_t)((rng() & 0x1f) - 0x10);
    set_velocity_from_angle(o);
    for (;;) {                                          /* LAB_05EA */
        SETW(o->z, UWORD(o->y) >> 1);                   /* z = y.w >> 1 (draw-order trick) */
        if (step(o)) return;
    }
}

/* =========================================================================================
 * XEVIOUS.LIN#9 @213bf8 -- column of 10 planes 3 px apart, each scatters in x, then bombs
 * ========================================================================================= */
static const int16_t ANIM_XEV_R[] = { A_RATE(4), A_SETLOOP(0), 0x182e, 0x162e, 0x142e, 0x122e, A_LOOP, A_END };
static const int16_t ANIM_XEV_L[] = { A_RATE(4), A_SETLOOP(0), 0x142e, 0x162e, 0x182e, 0x122e, A_LOOP, A_END };
static void xevious9_body(Obj *o) {                     /* LAB_05E3 @213c0e */
    enemy_init(o, 0x122e, 34, -48, 1, 20, 10);
    if (!threat_ok()) return;
    o->flags367 |= F_SCREEN_LOCKED;
    int d0 = (int16_t)((rng() & 0x7f) - 64 + WORD(o->x));
    if (d0 <= 0x20) d0 += 64;                           /* LAB_05E4 */
    if (d0 > 0x120) d0 -= 64;                           /* LAB_05E5 */
    SETW(o->x, d0);
    SETW(o->z, 0x20);
    SETW(o->vy, 2);
    if (wait_onscreen(o, 40)) return;
    SETW(o->vy, 1);
    if (WORD(o->x) >= 0xa0) { SETW(o->vx, 3);  anim_start(o, ANIM_XEV_R); }
    else                    { SETW(o->vx, -2); anim_start(o, ANIM_XEV_L); }   /* LAB_05E6 */
    spawn(bh_xevious_bomb_05e9);                        /* LAB_05E7 */
    wait_signal(o);
}
void bh_xevious_9(Obj *o) {
    for (int n = 9; n > 0; n--) {                       /* LAB_05E2 */
        spawn(xevious9_body);
        o->y -= 3 << 16;
    }
    xevious9_body(o);
}

/* =========================================================================================
 * FODDERA.LIN#2 @213d68 -- the common fodder plane.  The map record's x is IGNORED: LAB_05F9
 * picks x = (rng&255)+32 and a tiny random vx, and formation() then spawns 4 (5 at
 * difficulty>=2) clones at that same x, 4 px apart in y.  Every 4th record (146(A6)&3==0)
 * each clone re-randomises its own x instead (scattered group).
 * ========================================================================================= */
static void fodder_rand_x(Obj *o) {                     /* LAB_05F9 @213e5a */
    uint32_t r = rng();
    SETW(o->x, (r & 0xff) + 0x20);
    o->vx = (int32_t)(int16_t)(r >> 16) * 2;            /* SWAP; EXT.L; ADD.L D0,D0 */
    o->vy = 0; o->ax = 0; o->ay = 0x800;
}
static const int16_t ANIM_FOD_0[] = { A_RATE(1), A_SETLOOP(0), 0x0004, 0x0204, A_LOOP, A_END };
static const int16_t ANIM_FOD_1[] = { A_RATE(1), A_SETLOOP(0), 0x0404, 0x0604, 0x0404, 0x0804, 0x0404, 0x0a04, 0x0404, 0x0c04, A_LOOP, A_END };
static void foddera_cont(Obj *o) {                      /* LAB_05EF @213d86 */
    if ((g146_fodder_count & 3) == 0) fodder_rand_x(o);
    enemy_init(o, 0x0404, 34, -48, 1, 12, 10);
    if (!threat_ok()) return;
    o->flags367 |= F_SCREEN_LOCKED;
    if (o->w[0] - 1 == 0) anim_start(o, ANIM_FOD_1);     /* LAB_05F1 */
    else                  anim_start(o, ANIM_FOD_0);
    SETW(o->z, 0x20);                                   /* LAB_05F2 */
    o->w[0] = -1;
    uint32_t r = rng();
    if ((uint16_t)((r & 3) + g.difficulty182) >= 5)
        o->w[0] = (int16_t)(((r >> 16) & 0x3f) + 0x20); /* fire countdown (VBLs) */
    for (;;) {                                          /* LAB_05F3 */
        if (UWORD(o->vy) >= 3) o->ay = 0;
        int x = WORD(o->x);
        if (x < 0x20) o->ax = 0x800;                    /* LAB_05F5 */
        else if (x > 0x120) o->ax = -0x800;
        {   /* SUB.W -76(A6),276(A5); BCC skip */
            uint16_t w = (uint16_t)o->w[0], d = (uint16_t)g.vbl_per_tick;
            o->w[0] = (int16_t)(w - d);
            if (w < d) fire_missile_aimed(o);           /* LAB_069C */
        }
        if (step(o)) return;                            /* LAB_05F7 */
    }
}
void bh_foddera_2(Obj *o) {
    g146_fodder_count++;
    fodder_rand_x(o);
    int count = (uint16_t)g.difficulty182 < 2 ? 4 : 5;
    formation(o, 0, -4, count, 0, foddera_cont);
    foddera_cont(o);
}
/* LAB_05EB @213d28 -- fodder wave spawner used by other scripts (INST1 group): spawns
 * (4-boss140)*4 fodder planes 10 yields apart at the top of the screen, then becomes one. */
void bh_fodder_wave_05eb(Obj *o) {
    g146_fodder_count = 0;
    stop(o);
    SETW(o->y, (int16_t)(g.scroll3542 - 32));
    o->w[0] = 1;
    int n = (4 - g.boss140) * 4;
    do {                                                /* LAB_05EC */
        spawn(foddera_cont);
        yield_vbls(o, 10);                              /* LAB_049D(10) */
        if (eng_signalled(o)) break;
    } while (--n != 0);
    foddera_cont(o);
}

/* =========================================================================================
 * _ONERIG.LIN#0 @213e86 -- rig with a rotor overlay that lifts off and spirals away
 * ========================================================================================= */
static const int16_t ANIM_RIG_B_SLOW[] = { A_RATE(10), A_SETLOOP(0), 0x0a00, 0x0c00, 0x0e00, 0x1000, A_LOOP, A_END };
/* LAB_0680 @215102: fast rotor with flag $80 flicker */
static const int16_t ANIM_ROTOR_0680[] = { A_RATE(1), A_SETLOOP(0),
    0x0a00, A_FLAGS_SET(0x80), 0x0a00, A_FLAGS_CLR(0x80), 0x0c00, A_FLAGS_SET(0x80), 0x0c00, A_FLAGS_CLR(0x80),
    0x0e00, A_FLAGS_SET(0x80), 0x0e00, A_FLAGS_CLR(0x80), 0x1000, A_FLAGS_SET(0x80), 0x1000, A_FLAGS_CLR(0x80),
    A_LOOP, A_END };
void bh__onerig_0(Obj *o) {
    enemy_init(o, 0x003d, 34, -16, 6, 45, 15);
    o->z = 0;
    anim_start_b(o, ANIM_RIG_B_SLOW);                   /* LAB_0527 (slot B) */
    if (wait_onscreen(o, 64)) return;
    anim_start_b(o, ANIM_ROTOR_0680);                   /* LAB_0680 */
    o->vz = 0x8000;
    do { if (step(o)) return; } while (UWORD(o->z) < 0x20);   /* LAB_05FA */
    o->vz = 0; o->speed = 0; o->angle = 0xc0;
    o->w[0] = UWORD(o->x) < 0xa0 ? 12 : -12;            /* LAB_05FB */
    o->w[1] = 20;
    do {                                                /* LAB_05FC */
        if (wait_vbls(o, 14)) return;             /* LAB_04DE(14) */
        o->angle += (uint8_t)o->w[0];
        if ((uint16_t)o->speed >= 0x300) fire_missile_aimed(o);   /* LAB_069C */
        else o->speed += 0x120;                         /* LAB_05FD */
        set_velocity_from_angle(o);                     /* LAB_05FE */
    } while (--o->w[1] != 0);
    wait_signal(o);
}

/* =========================================================================================
 * _CORN.LIN#7 @213f2c -- crop plane: waits 80 px into the screen, takes off (not shootable)
 * ========================================================================================= */
void bh__corn_7(Obj *o) {
    o->margin = -90;
    enemy_init(o, 0x0e41, 36, -80, 0, 0, 25);
    o->z = 0;
    off_event(o, EV_TOUCH_JEEP); off_event(o, EV_TOUCH_HELI);   /* LAB_050E */
    if (wait_onscreen(o, 80)) return;
    o->box.mask = 0x22;
    o->vz = 0x4000;
    sfx(SFX_MISSILE, o->x >> 16);                       /* LAB_0416: take-off drone (closest SFX) */
    do { if (step(o)) return; } while (UWORD(o->z) < 0x20);   /* LAB_0600 */
    o->vz = 0;
    o->ay = 0x800;
    wait_signal(o);
}

/* =========================================================================================
 * TRILO.LIN#4 @213f8a -- parked plane: unfolds, takes off, then wanders towards the player
 * ========================================================================================= */
static const int16_t ANIM_TRILO[] = { A_RATE(8), A_SETLOOP(0), 0x0222, 0x0422, 0x0622, 0x0822, A_END };
void bh_trilo_4(Obj *o) {
    enemy_init(o, 0x0022, 36, -16, 4, 35, 10);
    o->animA.flags |= 1;
    o->z = 0;
    if (wait_onscreen(o, 8)) return;
    o->vy = 0x8000;
    if (wait_onscreen(o, 64)) return;
    anim_start(o, ANIM_TRILO);
    if (wait_vbls(o, 20)) return;                      /* LAB_04DE(20) */
    o->animA.flags &= ~1;
    off_event(o, EV_TOUCH_HELI);                        /* LAB_050F */
    on_event(o, EV_TOUCH_JEEP, on_bullet_hit);          /* LAB_0509 with LAB_0728 */
    o->box.mask = 0x22;
    o->vz = 0x8000;
    do { if (step(o)) return; } while (UWORD(o->z) < 0x20);   /* LAB_0602 */
    o->vz = 0; o->speed = 0;
    for (;;) {                                          /* LAB_0603 */
        if (wait_vbls(o, 10)) return;                  /* LAB_04DE(10) */
        int tx, ty; nearest_player(&tx, &ty);
        turn_towards(o, tx, ty, 0);
        if (o->angle >= 0xa0 && o->angle <= 0xe0) o->angle = 0x40;
        uint32_t r = rng();
        o->angle += (uint8_t)(((int16_t)r < 0) ? -16 : 16);  /* LAB_0604/05 */
        if ((uint16_t)o->speed < 0x300) o->speed += 0x30;
        set_velocity_from_angle(o);                     /* LAB_0606 */
    }
}

/* =========================================================================================
 * VTOL.LIN#0 @214064 -- hovers up, fires one aimed missile once well on screen
 * ========================================================================================= */
static const int16_t ANIM_VTOL_IDLE[] = { A_RATE(4), A_SETLOOP(0), 0x0023, 0x0223, A_LOOP, A_END };
static const int16_t ANIM_VTOL_FLY[]  = { A_RATE(8), A_SETLOOP(0), 0x0423, 0x0623, 0x0823, A_END };
void bh_vtol_0(Obj *o) {
    enemy_init(o, 0x0023, 36, -16, 8, 35, 10);
    anim_start(o, ANIM_VTOL_IDLE);
    o->z = 0;
    if (wait_onscreen(o, (int)(rng() & 0x1f) + 0x10)) return;
    o->animA.flags &= ~1;
    off_event(o, EV_TOUCH_HELI);                        /* LAB_050F */
    on_event(o, EV_TOUCH_JEEP, on_bullet_hit);          /* LAB_0509 */
    o->box.mask = 0x22;
    o->vz = 0x8000;
    do { if (step(o)) return; } while (UWORD(o->z) < 0x20);   /* LAB_0608 */
    o->vz = 0;
    anim_start(o, ANIM_VTOL_FLY);
    o->ay = 0x1000;
    wait_onscreen(o, 0xc0);                             /* result not tested in the original */
    fire_missile_aimed(o);                              /* LAB_069C */
    wait_signal(o);
}

/* =========================================================================================
 * FROG.LIN#0 @2140fc -- hops up, then chases the nearest player, accelerating
 * ========================================================================================= */
static const int16_t ANIM_FROG[] = { A_RATE(4), A_SETLOOP(0), 0x0053, 0x0253, 0x0453, 0x0653, A_LOOP, A_END };
void bh_frog_0(Obj *o) {
    enemy_init(o, 0x0053, 36, -8, 1, 55, 10);
    anim_start(o, ANIM_FROG);
    o->z = 0;
    if (wait_onscreen(o, 8)) return;
    o->margin = -16;
    o->angle = 0x40; o->speed = 0x180; set_velocity_from_angle(o);
    o->animA.flags &= ~1;
    off_event(o, EV_TOUCH_HELI);                        /* LAB_050F */
    on_event(o, EV_TOUCH_JEEP, on_bullet_hit);          /* LAB_0509 */
    o->box.mask = 0x22;
    o->vz = 0x8000;                                     /* LAB_060A */
    do { if (step(o)) return; } while (UWORD(o->z) < 0x20);   /* LAB_060B */
    o->vz = 0;
    for (;;) {                                          /* LAB_060C */
        int tx, ty; nearest_player(&tx, &ty);
        turn_towards(o, tx, ty, 7);
        o->speed += 0x20;
        set_velocity_from_angle(o);
        if (wait_vbls(o, 5)) return;                   /* LAB_04DE(5) */
    }
}

/* =========================================================================================
 * EGGS.LIN#2 @214198 -- egg: hatches, rises, drifts off; when shot 15 times bursts into a
 * ring of 16 missiles
 * ========================================================================================= */
static const int16_t ANIM_EGG[] = { A_RATE(10), A_SETLOOP(0), 0x061d, 0x081d, 0x0a1d, 0x0c1d, 0x0e1d, A_END };
static void eggs_hit(Obj *o) {                          /* LAB_0610 @214230 */
    if (--o->hp > 0) { o->flags367 |= F_HIT_FLASH; return; }   /* LAB_0727 */
    o->angle = 0;
    do { fire_missile_ahead(o); o->angle += 0x10; } while (o->angle != 0);   /* LAB_0611 */
    kill(o);                                            /* LAB_0729 */
}
void bh_eggs_2(Obj *o) {
    enemy_init(o, 0x041d, 32, -16, 0, 0xc8, 13);
    o->z = 0;
    if (wait_onscreen(o, 0x80)) return;
    anim_start(o, ANIM_EGG);
    if (wait_vbls(o, 50)) return;                      /* LAB_04DE(50) */
    o->vz = 0x8000;
    do { if (step(o)) return; } while (UWORD(o->z) < 0x20);   /* LAB_060E */
    o->vz = 0;
    o->hp = 15;
    on_event(o, EV_BULLET, eggs_hit);                   /* LAB_0505 */
    on_event(o, EV_TOUCH_JEEP, eggs_hit);               /* LAB_0509 */
    o->box.mask = 0x22;
    o->speed = 0x80;
    int tx = (int)(rng() & 0x7f) + 0x60, ty = (int16_t)g.scroll3530;
    turn_towards(o, tx, ty, 0);
    set_velocity_from_angle(o);
    wait_signal(o);
}

/* =========================================================================================
 * BIRD.LIN#0 @21434e -- 4 birds 32 px apart; each hit makes it hop back and drop a missile
 * ========================================================================================= */
static const int16_t ANIM_BIRD[] = { A_RATE(4), 0x0015, 0x0215, 0x0415, 0x0615, 0x0415, 0x0215, A_LOOP, A_END };
static void bird_hit(Obj *o) {                          /* LAB_061A @2143b6 */
    if (--o->hp <= 0) { kill(o); return; }              /* BLE LAB_0729 */
    o->y -= 6 << 16;
    o->angle = (uint8_t)((rng() & 0xf) + 0x38);
    fire_missile_ahead(o);                              /* LAB_069B */
    o->flags367 |= F_HIT_FLASH;                         /* LAB_0727 */
}
static void bird_cont(Obj *o) {
    enemy_init(o, 0x0015, 34, -48, 2, 55, 10);
    o->flags367 |= F_SCREEN_LOCKED;
    SETW(o->z, 0x20);
    o->vy = (int32_t)(rng() & 0x7fff) + 0x10000;
    on_event(o, EV_BULLET, bird_hit);                   /* LAB_0505 */
    on_event(o, EV_TOUCH_JEEP, bird_hit);               /* LAB_0509 */
    anim_start(o, ANIM_BIRD);
    wait_signal(o);
}
void bh_bird_0(Obj *o) {
    formation(o, 32, -4, 4, 0, bird_cont);              /* LAB_071D */
    bird_cont(o);
}

/* =========================================================================================
 * YELLOW.LIN#0 @2143dc -- 6-plane formation 8 px apart; each picks a random x on the
 * heli's side, dives, and homes on the heli until it has passed it
 * ========================================================================================= */
static const int16_t ANIM_YELLOW[] = { A_RATE(1), A_SETLOOP(0), 0x0014, 0x0214, 0x0014, 0x0414, 0x0014, 0x0614, 0x0014, 0x0814, A_LOOP, A_END };
static void yellow_cont(Obj *o) {
    enemy_init(o, 0x0014, 34, -48, g.difficulty182 + 1, 35, 10);
    if (!threat_ok()) return;
    o->flags367 |= F_SCREEN_LOCKED;
    SETW(o->z, 0x20);
    anim_start(o, ANIM_YELLOW);
    int tx, ty; prefer_heli(&tx, &ty);                  /* LAB_0581 */
    int d1 = (int16_t)tx >= 0xa0 ? 0 : 0x100;           /* LAB_061B */
    SETW(o->x, (rng() & 0x3f) + d1);
    o->angle = 0x40; o->speed = 0x200; set_velocity_from_angle(o);
    for (;;) {                                          /* LAB_061C */
        prefer_heli(&tx, &ty);
        int d1w = (int16_t)(ty - 0x18);
        if ((uint16_t)d1w < UWORD(o->y)) {              /* passed the player: straight down (LAB_061F) */
            o->speed = 0x200; o->angle = 0x40; set_velocity_from_angle(o);
        } else {
            int d2 = (int16_t)(WORD(o->x) - tx); if (d2 < 0) d2 = -d2;   /* LAB_061D */
            int d3 = (int16_t)(WORD(o->y) - d1w); if (d3 < 0) d3 = -d3;  /* LAB_061E */
            o->speed = (int16_t)(((d2 + d3) * 2) & 0x7ff);
            turn_towards(o, tx, d1w + 8, 12);
            set_velocity_from_angle(o);
        }
        if (wait_vbls(o, 15)) return;                   /* LAB_0620: LAB_04DE(15) */
    }
}
void bh_yellow_0(Obj *o) {
    formation(o, 0, -8, 6, 0, yellow_cont);             /* LAB_071D */
    yellow_cont(o);
}

/* =========================================================================================
 * GOOSE.LIN#7 @2144b4 -- 6 geese 8 px apart: drift sideways towards the player's side,
 * slow down, then rush and fire aimed missiles every 20 ticks
 * ========================================================================================= */
static void goose7_cont(Obj *o) {
    enemy_init(o, 0x0e17, 34, -48, 2, 35, 10);
    if (!threat_ok()) return;
    o->flags367 |= F_SCREEN_LOCKED;
    SETW(o->z, 0x20);
    int tx, ty; alternate_player(&tx, &ty);             /* LAB_0587 */
    int d1 = 0; int32_t d2 = 0x8000;
    if ((int16_t)tx < 0xa0) { d1 = 0x100; d2 = -0x8000; }
    o->vx = d2;                                         /* LAB_0622 */
    SETW(o->x, (rng() & 0x3f) + d1);
    SETW(o->vy, 1);
    if (wait_vbls(o, 50)) return;                      /* LAB_04DE(50) */
    o->vy = 0x8000;
    if (wait_vbls(o, 70)) return;                      /* LAB_04DE(70) */
    o->vy += 4 << 16;                                   /* ADDQ.W #4,336 */
    for (;;) {                                          /* LAB_0623 */
        fire_missile_aimed(o);                          /* LAB_069C */
        if (wait_vbls(o, 20)) return;                  /* LAB_04DE(20) */
    }
}
void bh_goose_7(Obj *o) {
    formation(o, 0, -8, 6, 0, goose7_cont);             /* LAB_071D */
    goose7_cont(o);
}

/* =========================================================================================
 * ROTOBASE.LIN#12 @21566e -- rotating gun base: spins (direction alternates per spawn),
 * every 200..327 ticks fires 4 missiles 90 degrees apart from a random heading
 * ========================================================================================= */
static const int16_t ANIM_ROTO_FWD[] = { A_RATE(2), 0x080d, 0x0a0d, 0x0c0d, 0x0e0d, 0x100d, 0x120d, 0x140d, 0x160d, A_LOOP, A_END };
static const int16_t ANIM_ROTO_REV[] = { A_RATE(2), 0x160d, 0x140d, 0x120d, 0x100d, 0x0e0d, 0x0c0d, 0x0a0d, 0x080d, A_LOOP, A_END };
void bh_rotobase_12(Obj *o) {
    enemy_init(o, 0x080d, 36, -16, 4, 35, 10);
    g138_roto_toggle = (int16_t)~g138_roto_toggle;      /* NOT.W 138(A6); BMI */
    if (g138_roto_toggle < 0) anim_start(o, ANIM_ROTO_REV);   /* LAB_06C5 */
    else                      anim_start(o, ANIM_ROTO_FWD);
    for (;;) {                                          /* LAB_06C6 */
        uint32_t r = rng();
        o->angle = (uint8_t)r;
        if (wait_vbls(o, (int)(r & 0x7f) + 0xc8)) return;   /* LAB_04DE */
        fire_missile_ahead(o); o->angle += 0x40;        /* LAB_069B x4 */
        fire_missile_ahead(o); o->angle += 0x40;
        fire_missile_ahead(o); o->angle += 0x40;
        fire_missile_ahead(o); o->angle += 0x40;
    }
}

/* =========================================================================================
 * TAP.LIN#0 @215700 -- turret that wanders its heading, then sprays 4 shots 90 degrees apart
 * ========================================================================================= */
static const uint16_t TAB_TAP_06CC[16] = {
    0x004e, 0x024e, 0x044e, 0x064e, 0x004e, 0x024e, 0x044e, 0x064e,
    0x004e, 0x024e, 0x044e, 0x064e, 0x004e, 0x024e, 0x044e, 0x064e };
static const int16_t ANIM_TAP_SHOT[] = { A_RATE(3), 0x084e, 0x0a4e, A_LOOP, A_END };
static void tap_shot(Obj *o) {                          /* LAB_06CD @21579e */
    enemy_init(o, 0x084e, 6, -16, 0, 0, 1);
    o->flags367 |= F_NO_SHADOW;
    o->margin = 0;
    anim_start(o, ANIM_TAP_SHOT);
    o->speed = 0x380;
    do {                                                /* LAB_06CE */
        set_velocity_from_angle(o);
        if (wait_vbls(o, 4)) return;                   /* LAB_04DE(4) */
        o->speed -= 0x40;
    } while (o->speed != 0);
}
void bh_tap_0(Obj *o) {
    enemy_init(o, 0x004e, 36, -16, 8, 60, 10);
    for (;;) {                                          /* LAB_06C8 */
        uint32_t r = rng();
        o->w[1] = (int16_t)((r & 31) - 0x10);
        o->w[0] = (int16_t)(((r >> 16) & 0x1f) + 8);
        do {                                            /* LAB_06C9 */
            o->angle += (uint8_t)o->w[1];
            set_frame_table16(o, TAB_TAP_06CC);          /* LAB_071A */
            if (step(o)) return;
        } while (--o->w[0] != 0);
        o->angle = (uint8_t)((o->angle + 8) & 0xf0);
        o->w[0] = 4;
        do {                                            /* LAB_06CA */
            spawn(tap_shot);                            /* child inherits the current angle */
            o->angle += 0x40;
        } while (--o->w[0] != 0);
        wait_vbls(o, 10);                               /* LAB_04DE(10); result not tested in the original */
    }
}
