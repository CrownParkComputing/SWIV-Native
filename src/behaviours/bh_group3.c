/* bh_group3.c -- SWIV behaviour handlers, group 3 ($215836..$2163ce of amprog.asm):
 * MINE#0, TRAIN#0, JETS#0, JETS#1, TRUCK#0, FLATTANK#0, SKI#0, MEDTANK#0, JUNTANK#1, DESTRAIN#3,
 * _PLAT#9, _PLAT#10, JUNTANK#2.  Translated 1:1 from re/handlers_group3.asm (see LAB_/addr comments).
 */
#include "../engine/engine.h"

/* ---- globals the engine does not model (weak so another group / the engine may own them) ----
 * 150/152/154(A6): "splash zone" set by JEEPHELI.LIN#43 ($216a50): zone150 = y top, zone152 = y top-600,
 * zone154 = active flag.  The MEDTANK turret emits dust puffs (LAB_0771) while its y is inside it.
 * 3616(A6): flag set by SKI#0 (ST), cleared by BUNNY#2; read by the map/tile renderer (LAB_0265). */


#define XW(o)  ((int16_t)((o)->x >> 16))
#define YW(o)  ((int16_t)((o)->y >> 16))
#define SETXW(o, v) ((o)->x = (int32_t)(int16_t)(v) * 65536)
#define SETYW(o, v) ((o)->y = (int32_t)(int16_t)(v) * 65536)
/* MOVE.W #n,332(A5) etc: write the INTEGER (high) word of a 16.16 long, keep the fraction */
#define SET_HI(l, v) ((l) = ((l) & 0xffff) | ((int32_t)(int16_t)(v) * 65536))
/* MOVE.W #n,338(A5): write the FRACTION (low) word */
#define SET_LO(l, v) ((l) = ((l) & ~0xffff) | ((uint16_t)(v)))

/* =====================================================================================
 * LAB_06BB @ $215580 -- smart-bomb pickup dropped by a MINE (outside this group's range,
 * but only reachable from MINE's custom hit handler, so it lives here as a static).
 * ===================================================================================== */
static const int16_t BOMB_PICKUP_ANIM[] = { A_RATE(1), 0x1211, 0x1411, A_LOOP, A_END };
static void bomb_pickup_shot(Obj *o) {            /* LAB_06BC: hp--; at 0 -> smart bomb + kill */
    if (--o->hp != 0) return;
    smart_bomb(o); kill(o);
}
static void bomb_pickup_touched(Obj *o) {         /* LAB_06BE: give the toucher a shield, or bomb if it has one */
    struct Player *p = (o->box.hits & 0x40) ? &g.heli : &g.jeep;
    if (p->invuln106 != 0) { smart_bomb(o); eng_signal(o); return; }   /* LAB_06C1 */
    p->invuln106 = -1;
    yield_vbls(o, 10);                             /* LAB_049D */
    eng_signal(o);                                 /* LAB_06C0 */
}
static void bomb_pickup(Obj *o) {                 /* LAB_06BB */
    enemy_init(o, 0x1211, 32, -16, 10, 30, 5);
    o->z = 32 << 16;
    o->flags367 |= F_NO_SHADOW | F_SCREEN_LOCKED;   /* ORI.B #$11,367 */
    o->animA.flags &= ~1;
    on_touch_any_player(o, bomb_pickup_touched);     /* LAB_0508 */
    on_event(o, EV_BULLET, bomb_pickup_shot);        /* LAB_0505 */
    anim_start(o, BOMB_PICKUP_ANIM); set_frame(o, 0x1211);
    stop(o);
    SET_LO(o->vy, 0x8000);                           /* MOVE.W #$8000,338 = vy fraction: drifts down 0.5 px/VBL */
    wait_signal(o);
}

/* =====================================================================================
 * MINE.LIN#0 @ $215836
 * ===================================================================================== */
static const int16_t MINE_ANIM[] = { A_RATE(2), 0x0011, 0x0211, 0x0411, 0x0611, 0x0811, 0x0A11, 0x0C11, 0x0E11, A_LOOP, A_END };
static void mine_hit(Obj *o) {                    /* LAB_06D9 @ $215882: drop a bomb pickup (prio 101) then die */
    spawn_prio(bomb_pickup, 101);
    kill(o);
}
void bh_mine_0(Obj *o) {
    enemy_init(o, 0x0011, 36, -16, 10, 25, 7);
    o->popup374 = 0x1011;
    o->animA.flags |= 1;
    on_event(o, EV_BULLET, mine_hit);               /* LAB_0505 */
    on_event(o, EV_TOUCH_HELI, mine_hit);           /* LAB_050A */
    anim_start(o, MINE_ANIM); set_frame(o, 0x0011);
    wait_signal(o);
}

/* =====================================================================================
 * TRAIN.LIN#0 @ $21589e (LAB_06DD) -- locomotive + chain of attached carriages (LAB_06E3+2)
 * ===================================================================================== */
static void train_carriage(Obj *o);
void bh_train_0(Obj *o) {
    enemy_init(o, 0x0010, 36, 100, 10, 75, 15);
    o->animA.flags |= 1;
    o->y -= 2 << 16;
    o->cb538_disabled = 1;                          /* ST 538 */
    int x = -48, vx = 1;
    if (XW(o) >= 0xa0) { x = 0x170; vx = -1; }
    SETXW(o, x); SET_HI(o->vx, vx);
    spawn_attached(train_carriage);                 /* LAB_04CA (try once) */
    for (;;) {                                      /* LAB_06DF */
        if (step(o)) break;
        if ((uint16_t)(YW(o) - g.scroll3542) >= 0x110) break;
    }
    /* LAB_06E0 @ $2158f4: box mask cleared, then a code-checksum loop (copy protection: sums $1ff
     * words at LAB_06DD-$9b7e with ROR and patches the word at LAB_06E3+$1d54).  It plain-yields
     * every 512 of its $6ac2 iterations = 53 yields.  Ported as the equivalent delay only. */
    o->box.mask = 0;
    yield_n(o, 53);
}
static void train_carriage(Obj *o) {                /* LAB_06E3+2 @ $215936 */
    uint16_t gfx = (rng() & 0x8000) ? 0x0210 : 0x0410;   /* ADD.W D1,D1 ; BMI -> bit 15 of rng */
    enemy_init(o, gfx, 36, 100, 2, 50, 15);
    if (--o->w[0] != 0) spawn_attached(train_carriage);  /* LAB_04CA */
    o->cb542 = NULL;                                /* MOVE.W #-1,542: no orphan callback -> survives the loco */
    o->animA.flags |= 1;
    int d = (XW(o) < 0xa0) ? -48 : 48;
    if (o->parent) SETXW(o, XW(o->parent) + d); else SETXW(o, d);   /* original reads 308 unguarded */
    o->cb538_disabled = 1;
    for (;;) {                                      /* LAB_06E8 */
        if (step(o)) break;
        if ((uint16_t)(YW(o) - g.scroll3542) >= 0x110) break;
        if (o->parent) o->vx = o->parent->vx;       /* LAB_06E9 follow the loco's speed */
        else o->vx -= o->vx >> 4;                   /* orphaned: coast to a halt */
    }
}

/* =====================================================================================
 * JETS.LIN#0 @ $2159c0 / JETS.LIN#1 @ $215a3e
 * ===================================================================================== */
static void jets_landed(Obj *o) {                   /* LAB_06ED @ $2159fe */
    enemy_init(o, 0x041f, 34, 0x120, 2, 90, 15);
    yield_vbls(o, 100);                             /* LAB_049D */
    SETYW(o, (int16_t)(g.scroll3542 + 0x120));
    SET_HI(o->z, 0x20);
    o->vy = -4 * 65536; o->vx = 0;
    wait_signal(o);
}
void bh_jets_0(Obj *o) {
    enemy_init(o, 0x001f, 36, -32, 18, 90, 15);
    wait_onscreen(o, 100);                          /* result not tested in the original */
    o->vx = -0x8000; o->vy = 0x8000;
    if (wait_onscreen(o, 0x120)) return;
    spawn(jets_landed);
}
void bh_jets_1(Obj *o) {
    enemy_init(o, 0x021f, 36, -32, 18, 90, 15);
    o->cb538_disabled = 1;
    /* ADD.W #100 / SUB.W #100 to 320 = no-op */
    o->vx = -0x4000; o->vy = 0x4000;
    wait_vbls(o, 200);                              /* LAB_04DE; result not tested in the original */
    stop(o);
    o->cb538_disabled = 0;                          /* SF 538 */
    wait_signal(o);
}

/* =====================================================================================
 * TRUCK.LIN#0 @ $215a84 -- drives in from the left, drops soldiers (LAB_06F0)
 * ===================================================================================== */
static const int16_t TRUCK_SOLDIER_ANIM[] = { A_RATE(4), 0x0224, 0x0424, 0x0624, A_LOOP, A_END };
static void truck_soldier(Obj *o) {                 /* LAB_06F0 @ $215ae0 */
    enemy_init(o, 0x0224, 36, -16, 3, 10, 5);
    anim_start(o, TRUCK_SOLDIER_ANIM); set_frame(o, 0x0224);
    o->speed = 0x200; set_velocity_from_angle(o);
    if (wait_vbls(o, 20)) return;
    o->vx = 0; o->vy = 0;
    wait_signal(o);
}
void bh_truck_0(Obj *o) {
    enemy_init(o, 0x0024, 36, -48, 30, 50, 15);
    o->flags367 |= F_NO_SHADOW;
    SETXW(o, -48);
    SET_HI(o->z, 1);
    for (;;) {                                      /* LAB_06EE */
        o->vx = 0x8000;
        if (wait_vbls(o, 70)) return;
        o->angle = (uint8_t)((rng() & 0x1f) + 0x70);
        spawn(truck_soldier);
        o->vx = 0;
        if (wait_vbls(o, 20)) return;
    }
}

/* =====================================================================================
 * LAB_06FF @ $215cca -- rotating turret attached to FLATTANK / MEDTANK (fires LAB_069B missiles)
 * ===================================================================================== */
static const uint16_t DUST_TABLE8[8] = { 0x5000, 0x5200, 0x5400, 0x5600, 0x5000, 0x5200, 0x5400, 0x5600 };   /* LAB_0773 */
static void dust_puff(Obj *o) {                     /* LAB_0771 @ $216ab8 */
    o->animA.flags |= 0x41; o->flags367 |= F_NO_SHADOW; stop(o);
    set_frame_table8(o, DUST_TABLE8);
    o->animA.flags |= 0x41;
    o->cb538_disabled = 1;
    /* MOVE.W #$140,418(A5): renderer param of blit slot A -- not modelled by the engine */
    int16_t sy = (int16_t)(YW(o) - g.scroll3542);
    if (sy >= 0 && sy < 0x100) {
        if (!step(o)) { o->y -= 0x140 << 16; step(o); }
    }
    eng_free(o);                                    /* LAB_04DC */
}
/* LAB_0707 @ $215db6: rotate the turret towards angle `target` in 16-unit steps, 6 ticks apart.
 * Returns nonzero if the object was signalled meanwhile (-> die). Uses w[1]/w[2] like the original. */
static int turret_rotate(Obj *o, int target) {
    int d1 = 16;
    int d0 = (int8_t)(target - o->angle);
    if (d0 < 0) { d1 = -16; d0 = -d0; }
    d0 = (d0 + 8) >> 4;
    if (d0 == 0) return 0;
    o->w[1] = (int16_t)d1; o->w[2] = (int16_t)d0;
    do {                                            /* LAB_0709 */
        o->angle = (uint8_t)(o->angle + o->w[1]);
        set_frame_dir16(o, 0x0803);
        if (wait_vbls(o, 6)) return 1;
    } while (--o->w[2] != 0);
    return 0;
}
static void tank_turret(Obj *o) {                   /* LAB_06FF */
    enemy_init(o, 0x0803, 0x8000, -16, 0, 0, 4);
    if (!(o->w[0] & 8)) o->animA.flags |= 1;
    o->flags367 |= F_NO_SHADOW | F_FLASH_WITH_PARENT | F_ATTACHED;   /* ORI.B #$0d,367 */
    SET_HI(o->vz, 1);                               /* sits 1 px above the hull (draw order) */
    o->w[0] = 1;
    o->speed = 0;
    if (o->parent) o->angle = o->parent->angle;
    set_frame_dir16(o, 0x0803);
    for (;;) {
        o->w[2] = (int16_t)g.vbl;                   /* LAB_0702: -66(A6) = VBL counter */
        for (;;) {                                  /* LAB_0703 */
            if (g.zone154 && (uint16_t)YW(o) <= (uint16_t)g.zone150 && (uint16_t)YW(o) >= (uint16_t)g.zone152) {
                o->w[0] -= (int16_t)g.vbl_per_tick;
                if (o->w[0] < 0) {
                    o->w[0] = 20;
                    Obj *c = spawn(dust_puff);
                    if (c && o->parent) c->angle = o->parent->angle;
                }
            }
            if (step(o)) return;                    /* LAB_0706 */
            if ((uint16_t)((int16_t)g.vbl - o->w[2]) < (uint16_t)((12 - g.difficulty182) << 4)) continue;
            break;
        }
        int tx, ty; nearest_player(&tx, &ty);
        uint8_t saved = o->angle;
        turn_towards(o, tx, ty, 0);
        int target = o->angle; o->angle = saved;
        if (turret_rotate(o, target)) return;
        if (wait_vbls(o, 16)) return;
        fire_missile_ahead(o);                      /* LAB_069B */
        if (wait_vbls(o, 8)) return;
        int back = o->parent ? o->parent->angle : 0;   /* D0 = result of LAB_04DE (0 = not signalled) if no parent */
        if (turret_rotate(o, back)) return;
    }
}

/* =====================================================================================
 * FLATTANK.LIN#0 @ $215b24
 * ===================================================================================== */
static const int16_t FLATTANK_ANIM[] = { A_RATE(2), A_SETLOOP(0), 0x0027, 0x0227, 0x0427, 0x0627, A_LOOP, A_END };
void bh_flattank_0(Obj *o) {
    Obj *t = spawn_attached(tank_turret);
    if (t) SET_HI(t->vy, 4);                        /* turret drawn 4 px below the hull */
    enemy_init(o, 0x0027, 36, -16, g.difficulty182 + 5, 50, 12);
    o->popup374 = 5;
    o->animA.flags |= 1;
    anim_start(o, FLATTANK_ANIM); set_frame(o, 0x0027);
    o->angle = 0x40; o->speed = 0x20; set_velocity_from_angle(o);
    wait_signal(o);
}

/* =====================================================================================
 * SKI.LIN#0 @ $215b80
 * ===================================================================================== */
void bh_ski_0(Obj *o) {
    g.flag3616 = -1;                                /* ST 3616(A6) */
    enemy_init(o, 0x0030, 36, 0xbe, 4, 15, 10);
    o->popup374 = 5;
    o->angle = 0x20;
    o->x -= 32 << 16;
    SET_HI(o->vx, 2); SET_HI(o->vy, -2);
    o->ax = -0x600; o->ay = 0x600;
    for (;;) {                                      /* LAB_06F2: slide until vx drops below 1 px/VBL */
        if (step(o)) return;
        if ((int16_t)(o->vx >> 16) == 0) break;
    }
    stop(o);
    for (;;) {                                      /* LAB_06F3 */
        fire_missile_ahead(o);                      /* LAB_069B */
        if (wait_ticks(o, 10)) break;
    }
    wait_signal(o);
}

/* =====================================================================================
 * MEDTANK.LIN#0 @ $215bea (LAB_06F5) -- ground tank with LAB_06FF turret; type nibble selects entry
 * ===================================================================================== */
void bh_medtank_0(Obj *o) {
    o->z = 0;
    uint16_t mask = (o->w[0] & 8) ? 32 : 36;
    enemy_init(o, 0x0003, mask, -16, g.difficulty182 + 1, 50, 10);
    o->popup374 = 5;
    int d0 = o->w[0];
    if (!(d0 & 8)) o->animA.flags |= 1;
    d0 &= ~8;
    uint16_t gfx; int ang, spd;
    switch (d0) {
    case 1:  SETXW(o, 0x160); gfx = 0x0403; ang = 0x80; spd = 0x80; break;           /* LAB_06F9 */
    case 2:  SETXW(o, -32);   gfx = 0x0003; ang = 0;    spd = 0x80; break;           /* LAB_06FA */
    case 3:  gfx = 0x0203; ang = 64; spd = 0x80; break;                               /* LAB_06F8 */
    case 4:  wait_onscreen_inert(o, 0x120); gfx = 0x0603; ang = 0xc0; spd = 0x80; break;   /* $215c36 */
    default: gfx = 0x0203; ang = 64; spd = 0; break;                                  /* LAB_06FB */
    }
    o->angle = (uint8_t)ang; o->speed = (int16_t)spd;                                /* LAB_06FC */
    set_frame(o, gfx);
    spawn_attached(tank_turret);
    for (int n = 3; ; ) {                           /* LAB_06FD */
        set_velocity_from_angle(o);
        if (wait_vbls(o, 0x12c)) return;
        o->vx = 0; o->vy = 0;
        if (wait_vbls(o, 100)) return;
        if (--n == 0) break;
    }
    wait_signal(o);
}

/* =====================================================================================
 * JUNTANK.LIN#1 @ $215df2 -- hull + attached gun (LAB_0710)
 * ===================================================================================== */
static void juntank_gun(Obj *o) {                   /* LAB_0710 @ $215e4e */
    enemy_init(o, 0x282b, 36, -48, 12, 90, 10);
    o->cb538_disabled = 1;
    o->animA.flags |= 1;                            /* LAB_0711 */
    o->flags367 |= F_NO_SHADOW | F_FLASH_WITH_PARENT | F_ATTACHED;
    SET_HI(o->vz, 1); SET_HI(o->vy, -18); SET_HI(o->vx, -2);
    o->speed = 0; o->angle = 0x40; o->w[0] = 100;
    set_frame_dir8(o, 0x282b);
    for (;;) {                                      /* LAB_0712 */
        if (--o->w[0] == 0) {                       /* LAB_0713 */
            fire_missile_ahead(o); o->w[0] = 0x32;
        } else if ((uint16_t)o->w[0] <= 10) {
            int tx, ty; nearest_player(&tx, &ty);
            turn_towards(o, tx, ty, 16);
            set_frame_dir8(o, 0x282b);
        }
        if (step(o)) return;                        /* LAB_0714 */
    }
}
void bh_juntank_1(Obj *o) {
    enemy_init(o, 0x022b, 36, -48, 15, 90, 18);
    o->animA.flags |= 1;
    o->death376 = fx_ring8;                         /* LAB_062F */
    spawn_attached(juntank_gun);
    o->vy = 0x4000;
    o->w[0] = 0x180;                                /* LAB_070B */
    for (;;) {                                      /* LAB_070C */
        if (!o->child) o->vy = 0;
        if (step(o)) return;
        o->w[0] -= (int16_t)g.vbl_per_tick;
        if (o->w[0] < 0) break;
    }
    o->vy = 0;                                      /* LAB_070E */
    wait_signal(o);
}

/* =====================================================================================
 * DESTRAIN.LIN#3 @ $215ed0 -- flies across firing homing bullets left/right
 * ===================================================================================== */
static const int16_t DESTRAIN_ANIM[] = { A_RATE(1), 0x0819, 0x0A19, 0x0C19, 0x0A19, 0x0819, 0x0619, A_END };
void bh_destrain_3(Obj *o) {
    enemy_init(o, 0x0619, 36, 0, 12, 65, 7);
    o->flags367 |= F_NO_SHADOW;
    int32_t v = 0x8000;
    switch (o->w[0]) {
    case 1:  SETXW(o, -32);   o->vx = v;  o->vy = 0; break;               /* LAB_0716 */
    case 2:  SETXW(o, 0x160); o->vx = -v; o->vy = 0; break;               /* LAB_0715 */
    default: SETXW(o, -32);   o->vx = v;  o->vy = -(v >> 5); break;       /* $215efa */
    }
    for (;;) {                                      /* LAB_0717 */
        if (wait_ticks(o, 50)) return;
        anim_start(o, DESTRAIN_ANIM); set_frame(o, 0x0819);
        if (wait_ticks(o, 10)) return;
        fire_homing(o, 6, 0, 0);
        step(o);                                    /* LAB_04DD (result not tested) */
        fire_homing(o, -6, 0, 0x80);
    }
}

/* =====================================================================================
 * _PLAT.LIN#9 @ $2160d2 / _PLAT.LIN#10 @ $2160d8 -- launch platform: the high byte of w[0] is the
 * side flag (ST/SF 276(A5) writes the BYTE at 276): $ff = #9 (right-handed), 0 = #10.
 * ===================================================================================== */
#define PLAT_SIDE(o) (((o)->w[0] >> 8) & 0xff)
static const int16_t PLAT_GUN_ANIM[]   = { A_RATE(8), 0x2842, 0x2A42, 0x2C42, 0x2C42, 0x2A42, 0x2842, 0x2642, A_END };
static const int16_t PLAT_SHELL_ANIM[] = { A_RATE(1), 0x2E42, 0x3042, 0x3242, 0x3042, A_LOOP, A_END };
static void plat_shell(Obj *o) {                    /* LAB_073F @ $216268 */
    enemy_init(o, 0x2e42, 6, -16, 0, 0, 3);
    o->flags367 |= F_NO_SHADOW;
    o->margin = 0;
    anim_start(o, PLAT_SHELL_ANIM); set_frame(o, 0x2e42);
    o->speed = 0x100;
    int tx, ty; nearest_player(&tx, &ty);
    turn_towards(o, tx, ty, 0);
    set_velocity_from_angle(o);
    wait_signal(o);
}
static void plat_gun(Obj *o) {                      /* LAB_073B @ $2161f6 */
    enemy_init(o, 0x2642, 0x8000, -16, 0, 0, 4);
    o->animA.flags |= 1;
    o->flags367 |= F_FLASH_WITH_PARENT | F_ATTACHED;   /* ORI.B #$0c,367 */
    SET_HI(o->vx, PLAT_SIDE(o) ? 11 : -11);
    if (wait_vbls(o, 0x12c)) return;
    for (;;) {                                      /* LAB_073D */
        anim_start(o, PLAT_GUN_ANIM); set_frame(o, 0x2842);
        if (wait_vbls(o, 24)) return;
        sfx(SFX_CANNON, XW(o));                     /* LAB_03D0 cannon */
        spawn_prio(plat_shell, 100);                /* LAB_04D2 with D0 = sound routine's return (unknown) */
        if (wait_vbls(o, 120)) return;
    }
}
static void plat_vehicle(Obj *o) {                  /* LAB_0736 @ $216182 */
    enemy_init(o, PLAT_SIDE(o) ? 0x2442 : 0x2242, 36, -16, 10, 60, 10);
    o->animA.flags |= 1;
    Obj *c = spawn_attached(plat_gun);
    if (c) c->w[0] = (int16_t)((c->w[0] & 0xff) | (PLAT_SIDE(o) << 8));
    SETXW(o, PLAT_SIDE(o) ? 0x11c : 36);
    o->y -= 2 << 16;
    if (wait_vbls(o, 50)) return;
    o->vx = PLAT_SIDE(o) ? -0x8000 : 0x8000;
    if (wait_vbls(o, 0xbe)) return;
    o->vx = 0;
    wait_signal(o);
}
static void plat_common(Obj *o) {                   /* LAB_072D @ $2160dc */
    enemy_init(o, PLAT_SIDE(o) ? 0x1242 : 0x1442, 36, -16, 0, 0, 5);
    o->animA.flags |= 1;
    wait_onscreen(o, 16);                           /* result not tested in the original */
    o->margin = -16;
    for (;;) {                                      /* LAB_072F */
        if (wait_vbls(o, 100)) return;
        Obj *c = spawn_attached(plat_vehicle);
        if (c) c->w[0] = (int16_t)((c->w[0] & 0xff) | (PLAT_SIDE(o) << 8));
        sfx(SFX_PICKUP, XW(o));                     /* LAB_03F3 */
        int d = PLAT_SIDE(o) ? 1 : -1;
        for (o->w[1] = 0x12; ; ) {                  /* LAB_0730: slide out */
            if (step(o)) return;
            o->x += d << 16;
            if (--o->w[1] == 0) break;
        }
        if (wait_vbls(o, 100)) return;
        for (o->w[1] = 0x12; ; ) {                  /* LAB_0732: slide back */
            if (step(o)) return;
            o->x -= d << 16;
            if (--o->w[1] == 0) break;
        }
        for (;;) {                                  /* LAB_0734: wait until the vehicle is gone */
            if (step(o)) return;
            if (!o->child) break;
        }
    }
}
void bh__plat_9(Obj *o)  { o->w[0] = (int16_t)((o->w[0] & 0xff) | 0xff00); plat_common(o); }
void bh__plat_10(Obj *o) { o->w[0] = (int16_t)(o->w[0] & 0xff); plat_common(o); }

/* =====================================================================================
 * JUNTANK.LIN#2 @ $2162b2 -- silo that opens and launches a homing missile (LAB_0742)
 * ===================================================================================== */
static const int16_t SILO_OPEN_ANIM[]  = { A_RATE(4), 0x062B, 0x082B, 0x0A2B, 0x0C2B, 0x0E2B, 0x102B, A_END };
static const int16_t SILO_CLOSE_ANIM[] = { A_RATE(4), 0x0E2B, 0x0C2B, 0x0A2B, 0x082B, 0x062B, 0x042B, A_END };
static const int16_t SILO_MISSILE_ANIM[] = { A_RATE(8), 0x122B, 0x142B, 0x162B, 0x1C2B, A_END };
static void silo_missile(Obj *o) {                  /* LAB_0742 @ $21632e */
    enemy_init(o, 0x182b, 34, -32, 4, 70, 10);
    o->margin = -10;
    o->animA.flags |= 1;
    anim_start(o, SILO_MISSILE_ANIM); set_frame(o, 0x122b);
    o->vz = 1 << 16;
    for (;;) {                                      /* LAB_0743: rise to z = 32 */
        if (step(o)) return;
        if ((uint16_t)(o->z >> 16) >= 0x20) break;
    }
    o->animA.flags &= ~1;
    o->vz = 0;
    o->angle = 0x40; o->speed = 0x300; set_velocity_from_angle(o);
    o->w[0] = 10;
    for (;;) {                                      /* LAB_0744 */
        int tx, ty; nearest_player(&tx, &ty);
        turn_towards(o, tx, ty, 33);
        o->angle = (uint8_t)((o->angle + 0x10) & 0xe0);
        set_velocity_from_angle(o);
        set_frame_dir8(o, 0x182b);
        int n = o->w[0]; o->w[0]++;
        if (wait_vbls(o, n)) return;
    }
}
void bh_juntank_2(Obj *o) {
    enemy_init(o, 0x042b, 0, -32, 0, 0, 5);
    o->animA.flags |= 1;
    wait_onscreen(o, 48);                           /* result not tested in the original */
    for (;;) {                                      /* LAB_0740 */
        anim_start(o, SILO_OPEN_ANIM); set_frame(o, 0x062b);
        if (wait_vbls(o, 30)) return;
        spawn(silo_missile);
        if (wait_vbls(o, 50)) return;
        anim_start(o, SILO_CLOSE_ANIM); set_frame(o, 0x0e2b);
        if (wait_vbls(o, 100)) return;
        if ((int16_t)(YW(o) - g.scroll3542) > 0xc0) break;
    }
    wait_signal(o);
}
