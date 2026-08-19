/* bh_group4.c -- GROUP 4 behaviour handlers (re/handlers_group4.asm, $2163CE..$216CBC)
 *
 *   POPUP#0 $2163CE, DIAGUN#2 $21648E, DIAGUN#4 $2164A8, PYRAMID#1 $216586, EGGS#12 $216604,
 *   PROXMINE#0 $216700, FLAME#0 $216830, CAMOGUN#0 $216932, JEEPHELI#23 $21698A, JEEPHELI#31 $2169D6,
 *   JEEPHELI#43 $216A50, XEVIOUS#0 $216B12, TINYTRUK#0 $216BFC
 *
 * Translated 1:1 from the 68000 listing; see the LAB_xxxx @ $addr comments.
 */
#include "../engine/engine.h"

/* ======================================================================================
 * Verbs / helpers that are not in engine.h (static here; candidates for promotion)
 * ====================================================================================== */

/* LAB_0619 @ $2142C2 -- muzzle flash (the engine has it static inside effects.c only).
 * enemy_init(7, $8000 passive, -16, 0, 0, 1); z = 33; NO_SHADOW; margin 0; anim rate 1 $0401 then signal. */
static const int16_t FLASH_ANIM[] = { A_RATE(1), 0x0401, A_END_SIGNAL, A_END };
static void flash_0619(Obj *o) {
    enemy_init(o, 7, 0x8000, -16, 0, 0, 1);
    o->z = 33 << 16; o->flags367 |= F_NO_SHADOW; o->margin = 0; stop(o);
    anim_start(o, FLASH_ANIM); set_frame(o, 0x0401);
    wait_signal(o);
}

/* LAB_05E9 @ $213CBA -- aimed fireball (hazard, mask 6, not shootable), used by XEVIOUS' attached prop.
 * sfx LAB_03CC(x); enemy_init($0601,6,-16,0,0,3); stop; flags367 |= $11; margin 0; anim rate 4 {$0601,$0801} loop;
 * speed $200; aim at alternate player (snap) + random (rng&31)-16; fly, z = y/2 (draw-order trick) until signalled. */
static const int16_t FIREBALL_ANIM[] = { A_RATE(4), 0x0601, 0x0801, A_LOOP, A_END };
static void fireball_05e9(Obj *o) {
    sfx(SFX_SHOT, o->x >> 16);                       /* LAB_03CC: unknown sound, closest guess */
    enemy_init(o, 0x0601, 6, -16, 0, 0, 3);
    stop(o); o->flags367 |= F_SCREEN_LOCKED | F_NO_SHADOW; o->margin = 0;
    anim_start(o, FIREBALL_ANIM); set_frame(o, 0x0601);
    o->speed = 0x200;
    int tx, ty; alternate_player(&tx, &ty); turn_towards(o, tx, ty, 0);
    o->angle = (uint8_t)(o->angle + (int)(rng() & 31) - 16);
    set_velocity_from_angle(o);
    for (;;) {                                       /* LAB_05EA */
        o->z = (int32_t)(((uint16_t)(o->y >> 16)) >> 1) << 16;
        if (step(o)) return;
    }
}

/* Globals used by the JEEPHELI markers that the engine's Globals struct does not carry:
 *   3548(A6).b  player-respawned-at-marker flag (set by the player handlers LAB_0649/LAB_065E)
 *   3550/3552   jeep respawn x,y      3554/3556  heli respawn x,y
 *   150/152(A6) bonus-zone y range (150 = start y, 152 = start y - 600), 154(A6).b zone active
 * (3558 = g.jeep_limit3558 exists in the engine.)  Kept static here; should be promoted. */

/* LAB_0771 @ $216AB8 -- bonus-zone marker sprite spawned (by the PLAYER handlers via the helper at
 * $216A98) while a player is inside the zone: frame by dir8 from table LAB_0773 ($5000,$5200,$5400,$5600 x2),
 * flags397 |= $41, NO_SHADOW, stop, off-screen cb disabled, 418 = $140; if on screen: 1 tick, y -= 320, 1 tick; free. */
static const uint16_t ZONE_FRAMES_0773[8] = { 0x5000, 0x5200, 0x5400, 0x5600, 0x5000, 0x5200, 0x5400, 0x5600 };
__attribute__((unused)) static void zone_popup_0771(Obj *o) {
    o->animA.flags |= 0x41; o->flags367 |= F_NO_SHADOW; stop(o);
    set_frame_table8(o, ZONE_FRAMES_0773);
    o->animA.flags |= 0x41; o->cb538_disabled = 1;   /* ST 538(A5) */
    /* MOVE.W #$0140,418(A5): renderer param of blit slot A -- no field in the native Obj (noted in report) */
    int sy = (int16_t)((o->y >> 16) - g.scroll3542);
    if (sy >= 0 && sy < 256) {
        wait_ticks(o, 1); o->y -= 320 << 16; wait_ticks(o, 1);
    }
    eng_free(o);
}
/* helper @ $216A98 (called by the players each tick): if the zone is active and y is inside it, spawn LAB_0771 */
__attribute__((unused)) static void zone_check_216a98(Obj *o) {
    if (!g.zone154) return;
    uint16_t y = (uint16_t)(o->y >> 16);
    if (y > (uint16_t)g.zone150 || y < (uint16_t)g.zone152) return;
    spawn(zone_popup_0771);
}

/* ======================================================================================
 * POPUP.LIN#0 @ $2163CE  -- pop-up gun emplacement
 * ====================================================================================== */
static const int16_t POPUP_UP[]   = { A_RATE(6), 0x020e, 0x040e, 0x060e, 0x080e, 0x0a0e, 0x0c0e, 0x0e0e, A_END };
static const int16_t POPUP_DOWN[] = { A_RATE(6), 0x0c0e, 0x0a0e, 0x080e, 0x060e, 0x040e, 0x020e, A_END_SIGNAL, A_END };
/* LAB_0748 @ $216464: one shot: frame $100e, homing bullet at (+-6,20) angle 64, 5 ticks, frame $0e0e, 20 ticks */
static void popup_shot_0748(Obj *o) {
    set_frame(o, 0x100e);
    int dx = 6; o->w[0] = (int16_t)~o->w[0]; if (o->w[0] < 0) dx = -6;
    fire_homing(o, dx, 20, 64);
    wait_ticks(o, 5);
    set_frame(o, 0x0e0e);
    wait_ticks(o, 20);              /* result not checked by the original */
}
void bh_popup_0(Obj *o) {
    int margin = (int)(rng() & 63) + 64;
    enemy_init(o, 0x000e, 36, margin, 3, 70, 14);
    if (g.difficulty182 == 0) return;                /* TST.W 182(A6); BEQ LAB_0747 (die) */
    o->animA.flags |= 1; o->popup374 = 5;
    sfx(SFX_PICKUP, o->x >> 16);                     /* LAB_03F3 */
    anim_start(o, POPUP_UP);
    o->box.mask = 0x20;
    wait_ticks(o, 50);                               /* result not checked */
    o->box.mask = 0x24;
    for (int n = (g.difficulty182 >> 1) + 1; n > 0; n--) popup_shot_0748(o);   /* LAB_0746 */
    wait_ticks(o, 50);
    anim_start(o, POPUP_DOWN);
    wait_signal(o);
}

/* ======================================================================================
 * DIAGUN.LIN#2 @ $21648E / DIAGUN.LIN#4 @ $2164A8 -- diagonal gun; common body LAB_074A @ $2164C0
 * ====================================================================================== */
/* LAB_074E @ $216524: diagonal bullet */
static const int16_t DIAGUN_BULLET_ANIM[] = { A_RATE(1), 0x0c1a, 0x0e1a, 0x101a, 0x121a, A_LOOP, A_END };
static void diagun_bullet_074e(Obj *o) {
    enemy_init(o, 0x0c1a, 38, -48, 1, 6, 5);
    o->z = 0x21 << 16;                               /* MOVE.W #$21,328(A5) */
    o->flags367 |= F_NO_SHADOW;
    o->speed = 0x280; set_velocity_from_angle(o);
    o->x += o->vx * 16; o->y += o->vy * 16;          /* start 16 VBLs ahead of the muzzle */
    anim_start(o, DIAGUN_BULLET_ANIM);
    wait_signal(o);
}
/* LAB_074D @ $216500: firing frame, spawn bullet, 5 ticks, rest frame, 30 ticks */
static int diagun_fire_074d(Obj *o) {
    set_frame(o, (uint16_t)o->w[1]);
    spawn(diagun_bullet_074e);
    wait_ticks(o, 5);                                /* result not checked */
    set_frame(o, (uint16_t)o->w[0]);
    return wait_ticks(o, 30);
}
static void diagun_body_074a(Obj *o) {
    enemy_init(o, (uint16_t)o->w[0], 36, -48, 7, 80, 18);
    o->death376 = fx_wreck;
    wait_ticks(o, 100);                              /* result not checked */
    o->w[2] = 15;
    for (;;) {                                       /* LAB_074B */
        if (diagun_fire_074d(o)) break;
        if (--o->w[2] == 0) { wait_signal(o); break; }
    }
    gfx_release(o, 6);
}
void bh_diagun_2(Obj *o) {
    gfx_acquire(o, 6); o->w[0] = 0x041a; o->w[1] = 0x061a; o->angle = 0x20;
    diagun_body_074a(o);
}
void bh_diagun_4(Obj *o) {
    gfx_acquire(o, 6); o->w[0] = 0x081a; o->w[1] = 0x0a1a; o->angle = 0x60;
    diagun_body_074a(o);
}

/* ======================================================================================
 * PYRAMID.LIN#1 @ $216586
 * ====================================================================================== */
static const int16_t PYRAMID_ANIM[] = { A_RATE(8), 0x0421, 0x0621, 0x0821, 0x0a21, 0x0c21, 0x0e21, 0x1021, A_END };
/* LAB_0751 @ $2165EA: homing bullet at (+-6,20) angle 64 then 20 ticks */
static void pyramid_shot_0751(Obj *o) {
    int dx = 6; o->w[0] = (int16_t)~o->w[0]; if (o->w[0] < 0) dx = -6;
    fire_homing(o, dx, 20, 64);
    wait_ticks(o, 20);                               /* result not checked */
}
void bh_pyramid_1(Obj *o) {
    gfx_acquire(o, 6);
    enemy_init(o, 0x0221, 36, -32, 10, 75, 15);
    wait_onscreen_noevents(o, 64);                   /* result not checked */
    o->death376 = fx_wreck;
    anim_start(o, PYRAMID_ANIM);
    if (!wait_ticks(o, 100)) {
        for (int n = 2 + g.difficulty182; n > 0; n--) pyramid_shot_0751(o);   /* LAB_074F */
        wait_signal(o);
    }
    gfx_release(o, 6);                               /* LAB_0750 */
}

/* ======================================================================================
 * EGGS.LIN#12 @ $216604 -- egg nest with three attached egg turrets dropping bombs
 * ====================================================================================== */
/* LAB_0759 @ $2166C0: falling egg-bomb (hazard, both players), with a muzzle flash */
static void egg_bomb_0759(Obj *o) {
    o->y += 0x10 << 16;
    sfx(SFX_SHOT, o->x >> 16);                       /* LAB_0411: unknown sound (closest guess) */
    enemy_init(o, 0x7001, 6, 0, 0, 0, 5);
    o->flags367 |= F_NO_SHADOW;
    o->z += 1 << 16;                                 /* ADDQ.W #1,328(A5) */
    spawn(flash_0619);
    o->vy = 6 << 16;                                 /* MOVE.W #6,336(A5) */
    wait_signal(o);
}
/* LAB_0756 @ $216678: common egg body (gfx in w[8]) */
static void egg_body_0756(Obj *o, uint16_t gfx) {
    enemy_init(o, gfx, 36, -20, 8, 75, 10);
    o->flags367 |= F_FLASH_WITH_PARENT;
    o->cb542 = kill;                                 /* orphaned (nest destroyed) -> explode */
    if (wait_onscreen(o, 8)) return;
    o->w[0] = 16;
    for (;;) {                                       /* LAB_0757 */
        spawn(egg_bomb_0759);
        if (wait_ticks(o, 100)) return;
        if (--o->w[0] == 0) break;
    }
    wait_signal(o);
}
static void egg_left_0753(Obj *o)   { o->x -= 0x33 << 16; o->y -= 0x0d << 16; egg_body_0756(o, 0x121d); }
static void egg_bottom_0754(Obj *o) { o->y += 0x41 << 16;                      egg_body_0756(o, 0x141d); }
static void egg_right_0755(Obj *o)  { o->x += 0x33 << 16; o->y -= 0x0d << 16; egg_body_0756(o, 0x161d); }
void bh_eggs_12(Obj *o) {
    spawn_attached(egg_left_0753);                   /* LAB_04CA (try once) */
    spawn_attached(egg_bottom_0754);
    spawn_attached(egg_right_0755);
    gfx_acquire(o, 6);
    enemy_init(o, 0x181d, 36, -32, 18, 75, 20);
    o->death376 = fx_wreck;
    wait_signal(o);
    gfx_release(o, 6);
}

/* ======================================================================================
 * PROXMINE.LIN#0 @ $216700 -- proximity mine: arms, then bursts into 6 fragments when a player is near
 * ====================================================================================== */
static const int16_t PROXMINE_ARM[]  = { A_RATE(6), 0x000f, 0x020f, 0x040f, 0x060f, 0x080f, 0x0a0f, A_END };
static const int16_t PROXFRAG_A[]    = { A_RATE(2), 0x0c0f, 0x0e0f, 0x100f, 0x120f, 0x140f, 0x160f, A_LOOP, A_END };
static const int16_t PROXFRAG_B[]    = { A_RATE(3), 0x180f, 0x1a0f, 0x1c0f, 0x1e0f, A_LOOP, A_END };
/* LAB_075F @ $2167BC: fragment */
static void proxmine_frag_075f(Obj *o) {
    enemy_init(o, 0x0c0f, 0x26, -32, 1, 5, 5);
    o->animA.flags &= ~1;
    o->z = 0x20 << 16; o->margin = 0; o->flags367 |= F_NO_SHADOW;
    if (g.rng11172 & 0x80000000u) anim_start(o, PROXFRAG_B);   /* TST.W 11172(A6) = high word of the RNG state */
    else                           anim_start(o, PROXFRAG_A);
    on_event(o, EV_TOUCH_JEEP, (Callback)eng_signal);         /* LAB_0509 -> LAB_053B */
    o->speed = 0x280; set_velocity_from_angle(o);
    wait_signal(o);
}
void bh_proxmine_0(Obj *o) {
    int margin = (int)(rng() & 31) + 0x55;
    enemy_init(o, 0x000f, 36, margin, 4, 30, 10);
    if (g.difficulty182 == 0) return;                /* BEQ LAB_075E (die) */
    o->animA.flags |= 1; o->popup374 = 5;
    sfx(SFX_PICKUP, o->x >> 16);                     /* LAB_03F3 */
    anim_start(o, PROXMINE_ARM);
    o->box.mask = 0x20;
    wait_ticks(o, 100);                              /* result not checked */
    o->box.mask = 0x24;
    for (;;) {                                       /* LAB_075A */
        if (wait_ticks(o, 10)) return;
        int tx, ty;
        if (!nearest_player(&tx, &ty)) continue;
        int dx = (int16_t)(tx - (o->x >> 16)); if (dx < 0) dx = -dx;
        int dy = (int16_t)(ty - (o->y >> 16)); if (dy < 0) dy = -dy;
        if ((uint16_t)(dx + dy) > 0x78) continue;
        spawn(fx_explosion);                         /* LAB_0634 */
        turn_towards(o, tx, ty, 0);
        o->angle += 0x15;
        o->w[0] = 6;
        do {                                         /* LAB_075D */
            spawn(proxmine_frag_075f);
            o->angle += 0x2a;
        } while (--o->w[0]);
        return;                                      /* LAB_075E: die without kill() (no score) */
    }
}

/* ======================================================================================
 * FLAME.LIN#0 @ $216830 -- flame pit (TOWN); activation margin +128
 * ====================================================================================== */
static const int16_t FLAME_RISE[]  = { A_RATE(6), 0x0013, 0x0213, 0x0413, 0x0613, 0x0813, A_END };
static const int16_t FLAME_FLICKER[] = { A_RATE(2), 0x1813, 0x1a13, 0x1813, 0x1c13, 0x1813, 0x1a13, 0x1c13, 0x1a13,
                                         0x1813, 0x1a13, 0x1813, 0x1a13, 0x1c13, 0x1a13, A_LOOP, A_END };
static const int16_t FLAME_PUFF[]  = { A_RATE(5), 0x0a13, 0x0c13, 0x0e13, 0x1013, 0x1213, 0x1413, 0x1613, A_END };
/* LAB_0765 @ $2168F2: drifting flame puff (heli-only hazard, mask 4), 35 ticks */
static void flame_puff_0765(Obj *o) {
    sfx(SFX_SHOT, 0);                                /* LAB_03EE(D0=0): flame hiss (closest guess) */
    enemy_init(o, 0x0013, 4, 0, 0, 0, 10);
    o->vx = 0x28000;
    anim_start(o, FLAME_PUFF);
    wait_ticks(o, 35);
}
/* LAB_0762 @ $216898: attached flickering flame column, spawns a puff every 100 ticks */
static void flame_column_0762(Obj *o) {
    enemy_init(o, 0x0013, 36, 0, 0, 0, 1);
    o->animA.flags |= 1;
    o->x += 0x0c << 16;
    anim_start(o, FLAME_FLICKER);
    for (;;) {                                       /* LAB_0763 */
        if (wait_ticks(o, 100)) return;
        spawn(flame_puff_0765);
    }
}
void bh_flame_0(Obj *o) {
    enemy_init(o, 0x0013, 36, 0x80, 3, 40, 10);
    o->margin = -8;
    o->animA.flags |= 1; o->popup374 = 5;
    sfx(SFX_PICKUP, o->x >> 16);                     /* LAB_03F3 */
    anim_start(o, FLAME_RISE);
    o->box.mask = 0x20;
    wait_ticks(o, 24);                               /* result not checked */
    spawn_attached(flame_column_0762);               /* LAB_04C9 */
    wait_ticks(o, 50);
    o->box.mask = 0x24;
    wait_signal(o);
}

/* ======================================================================================
 * CAMOGUN.LIN#0 @ $216932 -- camouflaged turret (TOWN): fires a fast missile down every 100 ticks
 * ====================================================================================== */
static const int16_t CAMOGUN_RECOIL[] = { A_RATE(3), 0x0216, 0x0016, A_END };
void bh_camogun_0(Obj *o) {
    enemy_init(o, 0x0016, 36, -16, 2, 40, 10);
    o->animA.flags |= 1;
    for (;;) {                                       /* LAB_0766 */
        if (wait_ticks(o, 100)) return;
        o->angle = 0x40;
        fire_missile_fast(o);                        /* LAB_069A (D0=0,D1=8 are ignored by it) */
        o->y -= 8 << 16;                             /* recoil */
        anim_start(o, CAMOGUN_RECOIL);
        o->w[0] = 8;
        do { o->y += 1 << 16; step(o); } while (--o->w[0]);   /* LAB_0767: step result not checked */
    }
}

/* ======================================================================================
 * JEEPHELI.LIN#23 @ $21698A / #31 @ $2169D6 -- map-placed RESPAWN MARKERS (jeep / heli).
 * Invisible-ish markers (hp 0, mask 0, immune to the smart bomb) that publish their position as the
 * respawn point (3550/3552 jeep, 3554/3556 heli), clear the "respawned" flag 3548 and wait until a
 * player respawns there (the player handler sets 3548); then 3558 (jeep y limit) = their y.
 * ====================================================================================== */
static void respawn_marker(Obj *o, uint16_t gfx, int heli) {
    enemy_init(o, gfx, 0, -16, 0, 0, 10);
    o->cb534 = NULL;                                 /* ST 534(A5): immune to smart bomb */
    o->animA.flags |= 1;
    wait_onscreen(o, 32);                            /* result not checked */
    if (!heli) { g.g3550 = o->x >> 16; g.g3552 = o->y >> 16; g.g3554 = g.g3556 = 0; }
    else       { g.g3554 = o->x >> 16; g.g3556 = o->y >> 16; g.g3550 = g.g3552 = 0; }
    g.g3548 = 0;
    for (;;) {                                       /* LAB_0769 / LAB_076B */
        if (step(o)) return;
        if (g.g3548) break;
    }
    g.jeep_limit3558 = (int16_t)(o->y >> 16);        /* MOVE.W 324(A5),3558(A6) */
}
void bh_jeepheli_23(Obj *o) { respawn_marker(o, 0x000c, 0); }
void bh_jeepheli_31(Obj *o) { respawn_marker(o, 0x020c, 1); }

/* ======================================================================================
 * JEEPHELI.LIN#43 @ $216A50 -- BONUS-ZONE marker: defines a 600 px map band [y-600, y] (150/152(A6)),
 * sets 154(A6) while the band is within 256 px of the scroll, then frees itself (LAB_04DC, no enemy cleanup).
 * While 154 is set the PLAYER handlers spawn LAB_0771 markers (see helper @ $216A98 above).
 * ====================================================================================== */
void bh_jeepheli_43(Obj *o) {
    gfx_acquire(o, 0x5000);
    if (!wait_onscreen_inert(o, 0)) {
        int16_t y = (int16_t)(o->y >> 16);
        g.zone150 = y; g.zone152 = (int16_t)(y - 0x258); g.zone154 = 1;
        for (;;) {                                   /* LAB_076E */
            if ((uint16_t)(g.scroll3530 + 0x100) < (uint16_t)g.zone152) break;
            yield_once(o);                           /* BEQ LAB_076E after LAB_0499: treated as loop-unless-signalled */
            if (eng_signalled(o)) break;
        }
    }
    g.zone154 = 0;                                   /* LAB_076F */
    gfx_release(o, 0x5000);
    eng_free(o);                                     /* BRA.W LAB_04DC */
}

/* ======================================================================================
 * XEVIOUS.LIN#0 @ $216B12 -- big ground tank following a scripted path (by type nibble w[0])
 * ====================================================================================== */
/* path byte pairs {angle, ticks/2}; $FF count = end.  Literal bytes from $216B88.. (note: path 1 runs
 * straight on into path 2's bytes; path 3 ($00,$FE) runs on into the code bytes of LAB_077C -- ported
 * literally for the first pairs; the object is long off-screen by then). */
static const uint8_t XEV_PATH1_0778[] = { 0x7f,0x9d, 0x70,0x0a, 0x60,0x0a, 0x51,0x0a, 0x41,0xfe,
                                          0x80,0x27, 0x90,0x0a, 0x9f,0x0a, 0xaf,0x0a, 0xbe,0x64, 0x00,0xff };
static const uint8_t XEV_PATH2_0779[] = { 0x80,0x27, 0x90,0x0a, 0x9f,0x0a, 0xaf,0x0a, 0xbe,0x64, 0x00,0xff };
static const uint8_t XEV_PATH3_077b[] = { 0xc0,0x30, 0xd0,0x0a, 0xe0,0x0a, 0xf0,0x0a, 0x00,0xfe,
                                          /* code bytes of LAB_077C that the original would keep reading: */
                                          0x30,0x3c, 0x02,0x2e, 0x32,0x3c, 0x80,0x00, 0x74,0x00, 0x76,0x00, 0x78,0x00,
                                          0x7a,0x03, 0x61,0x00, 0xf4,0x2c, 0x61,0x00, 0xbe,0xf8, 0x00,0xff /* synthetic end */ };
/* LAB_077C @ $216BA8: attached prop/turret overlay that lobs a fireball every ~108 ticks */
static const int16_t XEV_PROP_ANIM[] = { A_FLAGS_CLR(0x80), A_RATE(4), 0x022e, 0x042e, 0x042e, 0x022e, A_FLAGS_SET(0x80), A_END };
static void xevious_prop_077c(Obj *o) {
    enemy_init(o, 0x022e, 0x8000, 0, 0, 0, 3);
    stop(o);
    o->flags367 |= F_NO_SHADOW | F_FLASH_WITH_PARENT | F_ATTACHED;
    o->vz = 1 << 16;                                 /* MOVE.W #1,340(A5) */
    for (;;) {                                       /* LAB_077D */
        anim_start(o, XEV_PROP_ANIM);
        if (wait_ticks(o, 8)) return;
        spawn(fireball_05e9);
        if (wait_ticks(o, 100)) return;
    }
}
void bh_xevious_0(Obj *o) {
    enemy_init(o, 0x002e, 36, -16, 30, 95, 13);
    if (wait_onscreen(o, 64)) return;
    spawn_attached(xevious_prop_077c);               /* LAB_04C9 */
    const uint8_t *p = XEV_PATH3_077b;
    if (o->w[0] == 1) p = XEV_PATH1_0778; else if (o->w[0] == 2) p = XEV_PATH2_0779;
    o->speed = 0x80;
    for (;;) {                                       /* LAB_0775 */
        o->angle = *p++;
        set_velocity_from_angle(o);
        int n = *p++;
        if (n == 0xff) break;
        if (wait_ticks(o, n * 2)) break;
    }
    stop(o);                                         /* LAB_0776 */
    wait_signal(o);
}

/* ======================================================================================
 * TINYTRUK.LIN#0 @ $216BFC -- small truck driving up the screen, fires homing bullets by difficulty
 * ====================================================================================== */
static const int16_t TINYTRUK_ANIM[] = { A_RATE(1), A_SETLOOP(0), 0x000b, 0x020b, 0x040b, A_LOOP, A_END };
/* LAB_0781 @ $216C68 */
static void tinytruk_fire_0781(Obj *o) {
    int d = g.difficulty182; if (d > 3) d = 3;
    if (d & 1) { fire_homing(o, 0, -2, 0xc0); wait_ticks(o, 8); }
    if (d & 2) {
        fire_homing(o, -4, 2, 0xa0); wait_ticks(o, 8);
        fire_homing(o,  4, 2, 0xe0); wait_ticks(o, 8);
    }
}
void bh_tinytruk_0(Obj *o) {
    enemy_init(o, 0x000b, 36, 0x110, 8, 45, 10);
    o->popup374 = 5;
    o->box.mask = 0x20;
    o->vy = (int32_t)0xffff4000;                     /* -0.75 px/VBL, up the screen */
    anim_start(o, TINYTRUK_ANIM);
    wait_ticks(o, 100);                              /* result not checked */
    o->box.mask = 0x24;
    o->w[2] = 6;
    for (;;) {                                       /* LAB_077F */
        if (wait_ticks(o, 20)) return;
        tinytruk_fire_0781(o);
        if (--o->w[2] == 0) break;
    }
    o->vy = 0; o->animA.active = 0;                  /* stop, freeze animation */
    wait_signal(o);
}
