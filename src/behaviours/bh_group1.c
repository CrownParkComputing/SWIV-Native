/* bh_group1.c -- behaviour handlers, group 1 (re/handlers_group1.asm, $2132C8..$213BF8)
 * AIRMINE#0, SKYEYEA#0, SKYEYEB#8, EDGE#0, BUNNY#2, XEVIOUS#5, _AIRPORT#14, MILL#0, DADA#0,
 * BLACKJET#0, BOS#0, MAMA#0, TILT#0.  Ported literally from the listing; see re/PORTING_GUIDE.md. */
#include "../engine/engine.h"


/* ------------------------------------------------------------------------------------------
 * Local verbs not exported by engine.h
 * ------------------------------------------------------------------------------------------ */

/* LAB_0680 @ $215102: start the slot-B "rotor" animation (LAB_0527 with a fixed script):
 * rate 1, loop forever over frames 5..8 of set 0, toggling render flag $80 each frame. */
static const int16_t ROTOR_ANIM[] = {
    A_RATE(1), A_SETLOOP(0),
    0x0A00, A_FLAGS_SET(0x80), 0x0A00, A_FLAGS_CLR(0x80),
    0x0C00, A_FLAGS_SET(0x80), 0x0C00, A_FLAGS_CLR(0x80),
    0x0E00, A_FLAGS_SET(0x80), 0x0E00, A_FLAGS_CLR(0x80),
    0x1000, A_FLAGS_SET(0x80), 0x1000, A_FLAGS_CLR(0x80),
    A_LOOP, A_END
};
static void rotor_anim(Obj *o) { anim_start_b(o, ROTOR_ANIM); }

/* LAB_0619 @ $21430A: muzzle flash (copy of effects.c flash_script, which is static there) */
static const int16_t FLASH_ANIM[] = { A_RATE(1), 0x0401, A_END_SIGNAL, A_END };
static void lab_0619_flash(Obj *o) {
    enemy_init(o, 7, 0x8000, -16, 0, 0, 1); o->z = PX(33); o->flags367 |= F_NO_SHADOW; o->margin = 0; stop(o);
    anim_start(o, FLASH_ANIM); set_frame(o, 0x0401); wait_signal(o);
}

/* ------------------------------------------------------------------------------------------
 * AIRMINE.LIN#0  @ $2132C8
 * ------------------------------------------------------------------------------------------ */
static const int16_t AIRMINE_ANIM[] = { A_RATE(7), A_SETLOOP(0), 0x0012, 0x0212, A_LOOP, A_END };
void bh_airmine_0(Obj *o) {
    enemy_init(o, 0x0012, 34, -48, 3, 20, 7);
    o->z = PX(0x20);
    anim_start(o, AIRMINE_ANIM);
    /* vx = (int16)rng >> 2, sign-extended to long (a sub-pixel drift) */
    o->vx = (int32_t)((int16_t)rng() >> 2);
    o->vz = (int32_t)0xffffd000;
    for (;;) {                                   /* LAB_05A4: bob up/down every 10 ticks */
        o->vz = -o->vz;
        if (wait_ticks(o, 10)) break;
    }
    wait_signal(o);
}

/* ------------------------------------------------------------------------------------------
 * SKYEYEA.LIN#0  @ $213318  -- six planes entering from the left in a line
 * ------------------------------------------------------------------------------------------ */
static const uint16_t SKYEYEA_TBL[16] = {   /* LAB_05AD */
    0x1009, 0x2200, 0x2200, 0x2200, 0x2200, 0x2200, 0x2200, 0x2200,
    0x0009, 0x0209, 0x0409, 0x0609, 0x0809, 0x0A09, 0x0C09, 0x0E09
};
static void skyeyea_frame(Obj *o) { set_frame_table16(o, SKYEYEA_TBL); }   /* LAB_05AC */
static void cont_skyeyea(Obj *o) {
    enemy_init(o, 0x0009, 34, 0xc0, 1, 30, 10);
    o->cb538_disabled = 1;                        /* ST 538(A5) */
    o->flags367 |= F_SCREEN_LOCKED;
    o->z = PX(0x20);
    int tx, ty; prefer_heli(&tx, &ty);            /* LAB_0581 */
    if ((uint16_t)ty >= (uint16_t)(o->y >> 16)) o->y = PX((int16_t)ty);
    int d1 = -16, d2 = 0;                         /* LAB_05A6 */
    if (tx < 0xa0) { o->x = PX(0x140 - (int16_t)(o->x >> 16)); d1 = 16; d2 = 0x80; }
    o->w[1] = (int16_t)d1; o->angle = (uint8_t)d2; o->speed = 0x300;
    set_velocity_from_angle(o); skyeyea_frame(o);
    for (;;) {                                    /* LAB_05A8: fly until x in (0x90,0xB0) */
        if (step(o)) return;
        int x = (int16_t)(o->x >> 16);
        if (x <= 0x90) continue;
        if (x >= 0xb0) continue;
        break;
    }
    o->cb538_disabled = 0;                        /* SF 538(A5) */
    if (wait_vbls(o, 10)) return;
    do {                                          /* LAB_05A9: turn w[0] times */
        if (wait_vbls(o, 4)) return;
        o->angle += (uint8_t)o->w[1];
        set_velocity_from_angle(o); skyeyea_frame(o);
    } while (--o->w[0] != 0);
    if (g.difficulty182 >= 4) fire_missile_aimed(o);
    wait_signal(o);
}
void bh_skyeyea_0(Obj *o) {
    o->x = PX((int16_t)0xfff0);
    o->w[0] = 7;
    formation(o, -40, 0, 6, -1, cont_skyeyea);
    cont_skyeyea(o);
}

/* ------------------------------------------------------------------------------------------
 * SKYEYEB.LIN#8  @ $21340C
 * ------------------------------------------------------------------------------------------ */
static void cont_skyeyeb(Obj *o) {
    enemy_init(o, 0x000a, 34, 24, 1, 30, 10);
    o->cb538_disabled = 1;
    o->flags367 |= F_SCREEN_LOCKED;
    o->z = PX(0x20);
    int tx, ty; prefer_heli(&tx, &ty);
    int d1 = 16, d2 = 0;
    if (tx < 0xa0) { o->x = PX(0x140 - (int16_t)(o->x >> 16)); d1 = -16; d2 = 0x80; }   /* LAB_05AE */
    o->w[1] = (int16_t)d1; o->angle = (uint8_t)d2; o->speed = 0x380;
    set_velocity_from_angle(o); set_frame_dir16(o, 0x000a);
    for (;;) {                                    /* LAB_05AF */
        if ((uint16_t)(o->x >> 16) <= 0x140) {
            uint16_t r = rng() & 0x7f;
            if (r < (uint16_t)g.difficulty182) fire_missile_aimed(o);   /* SUB + BCC */
        }
        if (step(o)) return;                      /* LAB_05B0 */
        int x = (int16_t)(o->x >> 16);
        if (x <= 0x90) continue;
        if (x >= 0xb0) continue;
        break;
    }
    o->cb538_disabled = 0;
    if (wait_vbls(o, 10)) return;
    do {                                          /* LAB_05B1 */
        if (wait_vbls(o, 4)) return;
        o->angle += (uint8_t)o->w[1];
        set_velocity_from_angle(o); set_frame_dir16(o, 0x000a);
    } while (--o->w[0] != 0);
    wait_signal(o);
}
void bh_skyeyeb_8(Obj *o) {
    o->x = PX((int16_t)0xfff0);
    o->w[0] = 7;
    formation(o, -40, 0, 6, -1, cont_skyeyeb);
    cont_skyeyeb(o);
}

/* ------------------------------------------------------------------------------------------
 * EDGE.LIN#0  @ $2134E6  -- column of 6, dives down the screen weaving
 * ------------------------------------------------------------------------------------------ */
static void cont_edge(Obj *o) {
    enemy_init(o, 0x0008, 34, -48, 1, 50, 10);
    /* LAB_05B4 */
    o->flags367 |= F_SCREEN_LOCKED;
    o->z = PX(0x20);
    o->speed = 0x300; o->angle = 0x40;
    set_velocity_from_angle(o); set_frame_dir8(o, 0x0008);
    if (wait_onscreen(o, 32)) return;
    if (g.difficulty182 >= 4) fire_missile_aimed(o);
    if (wait_onscreen(o, 0x9c)) return;           /* LAB_05B5 */
    for (int n = 7; n; n--) {                     /* LAB_05B6 (stack counter) */
        int d0 = ((int16_t)(o->x >> 16) <= 0xa0) ? 32 : -32;
        o->angle += (uint8_t)d0;
        set_velocity_from_angle(o); set_frame_dir8(o, 0x0008);
        wait_vbls(o, 5);                          /* result NOT tested in the original */
    }
    fire_missile_aimed(o);
    wait_signal(o);
}
void bh_edge_0(Obj *o) {
    int x = 64;
    if ((int16_t)(g.rng11172 >> 16) < 0) x = 0x100;   /* TST.W 11172(A6): high word of the RNG state */
    o->x = PX(x);
    formation(o, 0, -4, 6, 0, cont_edge);
    cont_edge(o);
}

/* ------------------------------------------------------------------------------------------
 * BUNNY.LIN#2  @ $21358E  -- three fan out, accelerate, fire
 * ------------------------------------------------------------------------------------------ */
static const uint16_t BUNNY_TBL[16] = {     /* LAB_05BC */
    0x2200, 0x2200, 0x004F, 0x024F, 0x044F, 0x064F, 0x084F, 0x2200,
    0x2200, 0x2200, 0x2200, 0x2200, 0x2200, 0x2200, 0x2200, 0x2200
};
static void bunny_frame(Obj *o) { set_frame_table16(o, BUNNY_TBL); }   /* LAB_05BB */
static int bunny_turn(Obj *o) {                 /* LAB_05BA */
    o->angle += 0x10; o->speed += 0x80;
    set_velocity_from_angle(o); bunny_frame(o);
    fire_missile_aimed(o);
    return wait_vbls(o, 8);
}
static void cont_bunny(Obj *o) {
    enemy_init(o, 0x004f, 34, -48, 1, 90, 10);
    if (!threat_ok()) return;
    o->flags367 |= F_SCREEN_LOCKED;
    o->z = PX(0x20);
    o->speed = 0x280; o->angle = (uint8_t)(0x40 - (o->w[0] << 4));
    set_velocity_from_angle(o); bunny_frame(o);
    if (wait_vbls(o, 70)) return;
    bunny_turn(o);                                /* results not tested in the original */
    bunny_turn(o);
    wait_signal(o);
}
void bh_bunny_2(Obj *o) {
    g.flag3616 = 0;                                /* SF 3616(A6) */
    o->w[0] = 0;
    formation(o, -48, 6, 3, 1, cont_bunny);
    cont_bunny(o);
}

/* ------------------------------------------------------------------------------------------
 * XEVIOUS.LIN#5  @ $21363A  -- indestructible drifting thing; pings when shot
 * ------------------------------------------------------------------------------------------ */
static const int16_t XEVIOUS5_ANIM[] = { A_RATE(7), A_SETLOOP(0), 0x062E, 0x082E, 0x0A2E, 0x0C2E, 0x0E2E, 0x102E, 0x0E2E, A_LOOP, A_END };
static void xevious5_shot(Obj *o) { sfx(SFX_HIT, o->x >> 16); }   /* LAB_05BD -> LAB_041F (ricochet sound) */
void bh_xevious_5(Obj *o) {
    enemy_init(o, 0x062e, 34, -16, 0, 0, 13);
    o->flags367 |= F_NO_SHADOW;
    o->z = PX(0x20);
    on_event(o, EV_BULLET, xevious5_shot);        /* LAB_0505 */
    o->vy = 0x00008000;
    anim_start(o, XEVIOUS5_ANIM);
    wait_signal(o);
}

/* ------------------------------------------------------------------------------------------
 * _AIRPORT.LIN#14  @ $213690  -- two planes taxi then take off
 * ------------------------------------------------------------------------------------------ */
static const int16_t AIRPORT_ANIM[] = { A_RATE(3), A_SETLOOP(0), 0x1E3C, 0x1C3C, A_LOOP, A_END };
static void airport_body(Obj *o, int margin) {    /* LAB_05BF */
    enemy_init(o, 0x1c3c, 34, margin, 2, 25, 3);
    g.flag3615 = 0;                                /* SF 3615(A6) */
    o->animA.flags |= 1;
    o->z = PX(0x0c);
    o->x -= PX(0x28);
    o->vx = 0x00008000;
    if (wait_vbls(o, 80)) return;
    anim_start(o, AIRPORT_ANIM);
    o->ax = (o->ax & (int32_t)0xffff0000) | 0x1000;   /* MOVE.W #$1000,346(A5): low word of ax */
    wait_signal(o);
}
static void airport_child(Obj *o) { airport_body(o, 127); }   /* LAB_05BE */
void bh__airport_14(Obj *o) {
    spawn(airport_child);
    airport_body(o, 0xb0);
}

/* ------------------------------------------------------------------------------------------
 * MILL.LIN#0  @ $2136F4  -- big chopper, ring of 8 missiles every 100 ticks
 * ------------------------------------------------------------------------------------------ */
void bh_mill_0(Obj *o) {
    enemy_init(o, 0x0058, 34, -48, 10, 70, 15);
    o->z = PX(0x20);
    rotor_anim(o);
    o->vy = 0x00008000;
    wait_onscreen(o, 24);                          /* result not tested */
    o->flags367 |= F_SCREEN_LOCKED;
    for (;;) {                                     /* LAB_05C1 */
        if (wait_vbls(o, 100)) return;
        uint32_t r = rng();
        o->angle = (uint8_t)(16 & (r >> 16));      /* AND.W 11172(A6): high word of the new RNG state */
        do {                                       /* LAB_05C2: 8 missiles, 32 apart */
            fire_missile_ahead(o);
            int a = o->angle + 0x20; o->angle = (uint8_t)a;
            if (a > 0xff) break;                   /* ADDI.B ... BCC */
        } while (1);
    }
}

/* ------------------------------------------------------------------------------------------
 * DADA.LIN#0  @ $21374C  -- drifts in, curves sideways, fires homing pairs
 * ------------------------------------------------------------------------------------------ */
static void dada_fire(Obj *o, int dx) { fire_homing(o, dx, 20, 0x40); }   /* LAB_05C7 */
void bh_dada_0(Obj *o) {
    enemy_init(o, 0x0059, 34, -48, 12, 70, 15);
    o->death376 = fx_ring8;                        /* LAB_062F */
    o->z = PX(0x20);
    o->vy = PX(1);
    wait_onscreen(o, 0);                           /* result not tested */
    int d0 = 0x800;
    if ((int16_t)(o->x >> 16) > 0xa0) d0 = -d0;   /* LAB_05C4 */
    o->ax = d0;
    o->flags367 |= F_SCREEN_LOCKED;
    for (;;) {                                     /* LAB_05C5 */
        if (wait_vbls(o, 2 * (14 - g.difficulty182))) return;
        dada_fire(o, -22);
        dada_fire(o, 22);
    }
}

/* ------------------------------------------------------------------------------------------
 * BLACKJET.LIN#0  @ $2137B8  -- (difficulty/2 + 5) jets scattered across the screen
 * ------------------------------------------------------------------------------------------ */
static void cont_blackjet(Obj *o) {
    enemy_init(o, 0x0020, 34, -48, 1, 25, 15);
    if (!threat_ok()) return;
    o->z = PX(0x20);
    o->x = PX((rng() & 0xff) + 0x20);
    sfx(SFX_MISSILE, o->x >> 16);                  /* LAB_0401: jet whoosh -- closest SFX_ */
    o->ay = (o->ay & (int32_t)0xffff0000) | 0x1800;   /* MOVE.W #$1800,350(A5): low word of ay */
    wait_signal(o);
}
void bh_blackjet_0(Obj *o) {
    formation(o, 0, -4, (g.difficulty182 >> 1) + 5, 0, cont_blackjet);
    cont_blackjet(o);
}

/* ------------------------------------------------------------------------------------------
 * BOS.LIN#0  @ $21380A  -- bomber with an attached bomb bay that drops LAB_0759 bombs
 * ------------------------------------------------------------------------------------------ */
static void bos_bomb(Obj *o) {                   /* LAB_0759 @ $2166C0 */
    o->y += PX(0x10);
    sfx(SFX_MISSILE, o->x >> 16);                  /* LAB_0411: bomb whistle -- closest SFX_ */
    enemy_init(o, 0x7001, 6, 0, 0, 0, 5);
    o->flags367 |= F_NO_SHADOW;
    o->z += PX(1);
    spawn(lab_0619_flash);
    o->vy = PX(6);
    wait_signal(o);
}
static const int16_t BOS_BAY_ANIM[] = { A_RATE(8), A_SETLOOP(0), 0x0C50, 0x0E50, A_END };
static void bos_bay(Obj *o) {                    /* LAB_05CA */
    enemy_init(o, 0x0850, 34, -48, 0, 0, 0);
    o->cb538_disabled = 1;
    o->flags367 |= F_FLASH_WITH_PARENT | F_ATTACHED;
    wait_onscreen(o, 0xc0);                        /* result not tested */
    anim_start(o, BOS_BAY_ANIM);
    for (;;) {                                     /* LAB_05CB */
        if (wait_vbls(o, 20)) return;
        if ((int16_t)((o->y >> 16) - g.scroll3542) > 0x20) spawn(bos_bomb);
    }
}
static const int16_t BOS_ANIM[] = { A_RATE(1), A_SETLOOP(0), 0x0050, 0x0250, 0x0450, 0x0650, A_LOOP, A_END };
void bh_bos_0(Obj *o) {
    enemy_init(o, 0x0050, 34, -48, 4, 100, 35);
    if (!threat_ok()) return;
    o->flags367 |= F_SCREEN_LOCKED;
    o->z = PX(0x20);
    spawn_attached(bos_bay);
    anim_start(o, BOS_ANIM);
    o->vy = PX(2);
    wait_onscreen(o, 0xc8);                        /* result not tested */
    o->vy = PX(-3);
    o->ax = 0x00001000;
    wait_signal(o);
}

/* ------------------------------------------------------------------------------------------
 * MAMA.LIN#0  @ $2138CA  -- mother ship with attached hatch spawning little fliers
 * ------------------------------------------------------------------------------------------ */
static const int16_t MAMA_KID_ANIM0[] = { A_RATE(3), A_SETLOOP(0), 0x0625, 0x0825, 0x0A25, 0x0825, A_LOOP, A_END };   /* LAB_05D6 */
static const int16_t MAMA_KID_ANIM1[] = { A_RATE(4), A_SETLOOP(0), 0x0C25, 0x0E25, 0x1025, 0x0E25, A_LOOP, A_END };   /* LAB_05D7 */
static const int16_t MAMA_KID_ANIM2[] = { A_RATE(1), A_SETLOOP(0), 0x1225, 0x1425, 0x1625, 0x1425, A_LOOP, A_END };   /* LAB_05D8 */
static const int16_t MAMA_KID_ANIM3[] = { A_RATE(3), A_SETLOOP(0), 0x1825, 0x1A25, 0x1C25, A_LOOP, A_END };           /* LAB_05D9 */
static void mama_kid_orphaned(Obj *o) { o->w[0] = -1; }   /* LAB_05DA: ST 276(A5) */
static void mama_kid(Obj *o) {                   /* LAB_05D1 */
    enemy_init(o, 0x0625, 34, -48, 1, 13, 9);
    if (!threat_ok()) return;
    o->flags367 |= F_SCREEN_LOCKED;
    o->z = PX(0x18);
    switch (rng() & 3) {
    case 0: anim_start(o, MAMA_KID_ANIM0); break;
    case 1: anim_start(o, MAMA_KID_ANIM1); break;
    case 2: anim_start(o, MAMA_KID_ANIM2); break;
    default: anim_start(o, MAMA_KID_ANIM3); break;
    }
    /* LAB_05D2 */
    o->w[0] = 0;                                   /* SF 276(A5) */
    o->cb542 = mama_kid_orphaned;
    o->angle = 0xc0; o->speed = 0x280;
    o->w[3] = (int16_t)~(int16_t)(o->vx >> 16);
    for (;;) {                                     /* LAB_05D3 */
        int16_t d0 = (int16_t)(o->vx >> 16), d1 = o->w[3];
        if ((int16_t)(d0 ^ d1) < 0) {              /* vx changed sign since last time */
            o->w[2] = (int16_t)((rng() & 7) + 2);
            o->w[1] = 0;
            d1 = (int16_t)(127 + g.scroll3542 - (int16_t)(o->y >> 16));
            int16_t d2 = (int16_t)(o->vy >> 16);
            if ((int16_t)(d1 ^ d2) < 0) {
                o->w[3] = (int16_t)(o->vx >> 16);
                o->w[1] = 10;
                d0 = (int16_t)((int16_t)(o->x >> 16) - 0xa0);
                if ((int16_t)(d0 ^ d1) >= 0) o->w[1] = -10;
            }
        }
        /* LAB_05D4 */
        o->angle += (uint8_t)o->w[1];
        set_velocity_from_angle(o);
        if (wait_vbls(o, o->w[2])) return;
        if (o->w[0] == 0) continue;                /* TST.B 276(A5) */
        break;
    }
    stop(o);                                       /* orphaned: drop */
    o->vy = PX(-2);
    o->ay = (o->ay & (int32_t)0xffff0000) | 0x4000;   /* MOVE.W #$4000,350(A5) */
    wait_signal(o);
}
static void mama_hatch(Obj *o) {                 /* LAB_05CE */
    enemy_init(o, 0x0225, 34, -48, 0, 0, 0);
    o->cb538_disabled = 1;
    rotor_anim(o);
    o->flags367 |= F_FLASH_WITH_PARENT | F_ATTACHED;
    o->vy = PX((int16_t)0xffe2);                   /* -30 px offset from the parent (attached) */
    wait_onscreen(o, 48);                          /* result not tested */
    set_frame(o, 0x0425);
    for (;;) {                                     /* LAB_05CF */
        if (wait_ticks(o, 4)) return;
        spawn_attached(mama_kid);
        if ((uint16_t)((o->y >> 16) - g.scroll3542) < 0xd0) continue;
        break;
    }
    set_frame(o, 0x0225);
    wait_signal(o);
}
void bh_mama_0(Obj *o) {
    enemy_init(o, 0x0025, 34, -48, 70, 0x12c, 35);
    o->flags367 |= F_SCREEN_LOCKED;
    o->z = PX(0x20);
    rotor_anim(o);
    spawn_attached(mama_hatch);
    o->vy = 0x00006000;
    wait_signal(o);
}

/* ------------------------------------------------------------------------------------------
 * TILT.LIN#0  @ $213B08  -- chopper sweeping left/right, banking, fires at each turn
 * ------------------------------------------------------------------------------------------ */
static const int16_t TILT_ANIM_L0[] = { A_RATE(6), 0x0426, 0x0226, 0x0026, A_END };
static const int16_t TILT_ANIM_R0[] = { A_RATE(6), 0x0826, 0x0A26, 0x0C26, A_END };
static const int16_t TILT_ANIM_R[]  = { A_RATE(6), 0x0226, 0x0426, 0x0626, 0x0826, 0x0A26, 0x0C26, A_END };
static const int16_t TILT_ANIM_L[]  = { A_RATE(6), 0x0A26, 0x0826, 0x0626, 0x0426, 0x0226, 0x0026, A_END };
void bh_tilt_0(Obj *o) {
    enemy_init(o, 0x0626, 34, -48, 2, 40, 12);
    o->flags367 |= F_SCREEN_LOCKED;
    o->z = PX(0x20);
    rotor_anim(o);
    o->vy = PX(1);
    if (wait_onscreen(o, 64)) return;
    o->vy = 0;
    o->ay = 0x00000800;
    o->vx = PX(4);
    o->w[0] = 0;                                   /* SF 276(A5): 0 = heading right */
    anim_start(o, TILT_ANIM_L0);
    if ((int16_t)(o->x >> 16) >= 0xa0) {
        o->vx = -o->vx;
        o->w[0] = -1;
        anim_start(o, TILT_ANIM_R0);
    }
    fire_missile_aimed(o);                         /* LAB_05DD */
    for (;;) {                                     /* LAB_05DE */
        o->ax = 0;
        int16_t x = (int16_t)(o->x >> 16);
        if (x < 0x40) {                            /* LAB_05DF */
            o->ax = 0x00003000;
            if (o->w[0]) { o->w[0] = 0; anim_start(o, TILT_ANIM_L); fire_missile_aimed(o); }
        } else if (x > 0x100) {
            o->ax = (int32_t)0xffffd000;
            if (!o->w[0]) { o->w[0] = -1; anim_start(o, TILT_ANIM_R); fire_missile_aimed(o); }
        }
        if (step(o)) return;                       /* LAB_05E0 */
    }
}
