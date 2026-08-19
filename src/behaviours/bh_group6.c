/* bh_group6.c -- behaviour group 6: INST2#0, INST3#3/#12, INST4#6/#0/#3, INST5#0 (final boss), GOOSE#0.
 * Ported from re/handlers_group6.asm (disk-listing addresses in the comments). */
#include "../engine/engine.h"
#include <stdlib.h>

#ifndef PX
#define PX(v)  ((int32_t)(v) * 65536)
#endif
void bh_token_06AA(Obj *o);   /* bh_tokens.c: LAB_06AA weapon token */
#define XI(o)  ((int16_t)((o)->x >> 16))
#define YI(o)  ((int16_t)((o)->y >> 16))
#define ZI(o)  ((int16_t)((o)->z >> 16))
#define SCRY(o) ((int16_t)(YI(o) - g.scroll3542))

/* ---- globals not in the engine's Globals (file-static stand-ins) ---- */
static int8_t  flag3617;          /* 3617(A6): "INST4 platform destroyed" -- set by INST4#6, read by INST4#0/#3 */
static uint8_t flag148;           /* 148(A6): INST4#3 one-shot bit */
static int16_t fade142;           /* 142(A6): palette fade request (-1 = to white) */
static int16_t flash11166, fade11170;   /* 11166/11170(A6): screen flash / fade level */
static int     boss_clock12534;   /* 12534(A6): boss-group clock (INST5 children) */
static int16_t tail_count163;     /* 163(A6) set while the goose is alive */
static uint8_t state12353;        /* 12353(A6) game-state bits (bit3 = level complete) */
static int32_t map_adj3560;       /* 3560(A6) */

/* ---- verbs missing from engine.h ---- */
/* LAB_04FB: off-screen check outside step() */
static void offscreen_check(Obj *o) {
    if (o->cb538_disabled) return;
    int m = o->margin, sy = SCRY(o), sx = XI(o);
    if (sy <= m || sx <= m || sy - 256 >= -m || sx - 320 >= -m) { if (o->cb538) o->cb538(o); else eng_signal(o); }
}
/* LAB_06DA */
static void run_until_signalled(Obj *o) { if (!eng_signalled(o)) while (!step(o)) ; }

/* LAB_05E9 @ $213cba: flame projectile (duplicated from bh_group5.c) */
static const int16_t FLAME_ANIM[] = { A_RATE(4), 0x0601, 0x0801, A_LOOP, A_END };
static void fx_flame(Obj *o) {
    sfx(SFX_FIREBALL, XI(o));   /* LAB_05E9 -> LAB_03CC */
    enemy_init(o, 0x0601, 6, -16, 0, 0, 3);
    stop(o); o->flags367 |= F_SCREEN_LOCKED | F_NO_SHADOW; o->margin = 0;
    anim_start(o, FLAME_ANIM);
    o->speed = 0x200; int tx, ty; alternate_player(&tx, &ty); turn_towards(o, tx, ty, 0);
    o->angle += (rng() & 0x1f) - 0x10; set_velocity_from_angle(o);
    for (;;) { o->z = PX(((uint16_t)YI(o)) >> 1); if (step(o)) return; }
}
/* LAB_07C8 @ $2175ea: boss hit handler (duplicated from bh_group5.c) */
static void boss_hit(Obj *o) {
    if (--o->hp > 0) { sfx(SFX_HIT, XI(o)); o->flags367 |= F_HIT_FLASH; return; }
    if (g.heli.alive) g.heli.score += o->score;
    if (g.jeep.alive) g.jeep.score += o->score;
    if (g.boss140 <= 1) smart_bomb(o);
    kill(o);
}
/* LAB_07E3 @ $21791a: boss damage tick: wobble + smoke/flame when hp <= 50, then one step.
 * (engine boss_smoke() lacks the LAB_05E9 flame spawn and the step, so it is re-done here) */
static int boss_tick(Obj *o) {
    o->w[3] = 0;
    if (o->hp <= 50) {
        uint32_t r = rng(); o->w[3] = (int16_t)((r & 3) - 2);
        if ((r & 15) == 0) {
            spawn(fx_flame);
            Obj *c = spawn(fx_explosion);
            if (c) { uint32_t r2 = rng(); c->x += PX((int)(r2 & 31) - 15); c->y += PX((int)((r2 >> 16) & 31) - 15); }
        }
    }
    o->y += PX(o->w[3]);
    int res = step(o);
    o->y -= PX(o->w[3]);
    return res;
}

/* ================================================================== INST2.LIN#0 @ $217808 (flame vent) */
void bh_inst2_0(Obj *o) {
    enemy_init(o, 0x002c, 32, -16, 80, 100, 10);
    for (;;) {                                          /* LAB_07D6 */
        if (g.boss140) {
            o->w[0] = 5;
            do { spawn(fx_flame); if (wait_vbls(o, 7)) return; } while (--o->w[0]);   /* LAB_07D7 */
        }
        if (wait_vbls(o, (g.boss140 << 7) + 1)) return;
    }
}

/* ================================================================== INST3.LIN#3 @ $21784e (installation 3 boss: sliding gun) */
static const int16_t INST3_FLASH_ANIM[] = { A_RATE(1), 0x1031, 0x1231, 0x1431, 0x1631, A_END_SIGNAL, A_END };
/* LAB_07F0: muzzle flash */
static void inst3_flash(Obj *o) {
    enemy_init(o, 0x1031, 0x8000, -16, 0, 0, 4);
    o->z = PX(0x21); o->flags367 |= F_NO_SHADOW;
    anim_start(o, INST3_FLASH_ANIM);
    wait_signal(o);
}
/* LAB_07EF: shell fired downwards */
static void inst3_shell(Obj *o) {
    enemy_init(o, 0x0e31, 6, -63, 0, 0, 10);
    o->z = 0;
    spawn(inst3_flash);
    o->y += PX(0x14); o->vy = PX(6);
    run_until_signalled(o);
}
/* LAB_07DD: spawn a shell at pos+(dx,dy) */
static void inst3_fire(Obj *o, int dx, int dy) {
    o->x += PX(dx); o->y += PX(dy); spawn(inst3_shell); o->x -= PX(dx); o->y -= PX(dy);
}
/* LAB_07DF(n) / LAB_07DE(1): n ticks of boss_tick on the absolute tick counter; returns signalled */
static int inst3_wait(Obj *o, int n) {
    int16_t t = (int16_t)(g.vblcount + n);
    while ((int16_t)(g.vblcount - t) < 0) { if (boss_tick(o)) break; }
    return eng_signalled(o);
}
/* LAB_07E5: sweep across the screen, alternating direction (w[4] toggles) */
static void inst3_sweep(Obj *o) {
    o->w[4] = ~o->w[4];
    if (o->w[4] < 0) {
        o->vx = PX(3);
        for (;;) { if (boss_tick(o)) break; if (XI(o) >= 0x128) break; }    /* LAB_07E6 */
    } else {
        o->vx = PX(-3);
        for (;;) { if (boss_tick(o)) break; if (XI(o) <= 0x18) break; }     /* LAB_07E9 */
    }
    o->vx = 0;
}
/* LAB_07EB: track the alternate player's x until within 24 px */
static void inst3_track(Obj *o) {
    int tx, ty; alternate_player(&tx, &ty);
    struct Player *p = g.alternate168 ? &g.heli : &g.jeep;   /* the record picked by LAB_0587 */
    if (!p->alive) p = (p == &g.heli) ? &g.jeep : &g.heli;
    for (;;) {                                          /* LAB_07EC */
        if (boss_tick(o)) break;
        int d = (int16_t)((p->x >> 16) - XI(o)), d1 = 2;
        if (d < 0) { d1 = -2; d = -d; }
        o->vx = PX(d1);
        if (d < 0x18) break;
    }
    o->vx = 0;
}
void bh_inst3_3(Obj *o) {
    gfx_acquire(o, 6);
    enemy_init(o, 0x0631, 38, -32, 300, 7500, 40);
    o->z = PX(4);
    on_event(o, EV_BULLET, boss_hit); on_touch_any_player(o, boss_hit);
    o->death376 = fx_wreck;
    wait_onscreen_noevents(o, 80);
    boss_enter();
    for (;;) {                                          /* LAB_07DA */
        inst3_sweep(o);
        inst3_track(o);
        if (inst3_wait(o, 4)) break;
        sfx(SFX_BOMB_WHISTLE, XI(o));                           /* LAB_0411 */
        inst3_fire(o, 19, 20); inst3_fire(o, -19, 20);
        o->y -= PX(16); o->w[2] = 16;
        do { o->y += PX(1); inst3_wait(o, 1); } while (--o->w[2]);   /* LAB_07DB recoil */
        if (inst3_wait(o, 10)) break;
    }
    gfx_release(o, 6);                                  /* LAB_07DC */
    boss_leave(o);
}

/* ================================================================== INST3.LIN#12 @ $217a5a (drone spawner) */
static const int16_t INST3_DRONE_ANIM[] = { A_RATE(1), A_SETLOOP(0), 0x1831, 0x1a31, A_LOOP, A_END };
/* LAB_07F3: drone: waits 50, then flies at the alternate player; self-destructs when the boss is gone */
static void inst3_drone(Obj *o) {
    enemy_init(o, 0x1831, 38, 16, 1, 50, 10);
    o->z = PX(3); o->flags367 |= F_NO_SHADOW;
    anim_start(o, INST3_DRONE_ANIM);
    wait_vbls(o, 50);
    o->speed = 0x200;
    int tx, ty; alternate_player(&tx, &ty); turn_towards(o, tx, ty, 0); set_velocity_from_angle(o);
    for (;;) {                                          /* LAB_07F4 */
        if (step(o)) return;
        if (g.boss140) continue;
        kill(o); return;
    }
}
void bh_inst3_12(Obj *o) {
    enemy_init(o, 0x1831, 0x8000, 32, 0, 0, 0);
    for (;;) {                                          /* LAB_07F1 */
        if (g.boss140) spawn(inst3_drone);
        yield_vbls(o, (rng() & 0x7f) + 0x32);
        offscreen_check(o);
        if (eng_signalled(o)) return;
    }
}

/* ================================================================== INST4.LIN#6 @ $217af6 (floating platform) */
void bh_inst4_6(Obj *o) {
    o->y -= PX(0x200);
    gfx_acquire(o, 6);
    flag3617 = 0;
    enemy_init(o, 0x0c32, 32, -48, 0, 10000, 0);
    o->z = PX(0x18); o->death376 = fx_wreck; o->flags367 |= F_SCREEN_LOCKED;
    o->vy = 0x8000;
    wait_onscreen_noevents(o, 0xd0);
    o->vy = -0x4000;
    for (;;) { if (step(o)) goto out; if (SCRY(o) > 0x44) continue; break; }   /* LAB_07F6 */
    o->vy = 0;
    for (;;) { if (step(o)) goto out; if (g.boss140) break; }                  /* LAB_07F7 */
    o->hp = 250;
    on_event(o, EV_BULLET, boss_hit); on_touch_any_player(o, boss_hit);
    wait_signal(o);
    flag3617 = -1;                                      /* ST 3617(A6): platform destroyed */
out:
    gfx_release(o, 6);                                  /* LAB_07F8 */
}

/* ================================================================== INST4.LIN#0 @ $217b8e (flame core; ends the level when the platform dies) */
/* LAB_07FC: 100-tick lightning/flash sequence with random wrecks */
static void inst4_storm(Obj *o) {
    int n = 100;
    do {                                                /* LAB_07FD */
        uint32_t r = rng() & 0x7f;
        if (!(r < (uint8_t)n)) {
            flash11166 = r & 0x47; yield_vbls(o, 1); flash11166 = 0;
            if (g.vbl_per_tick < 5) {
                Obj *c = spawn(fx_wreck);
                if (c) { uint32_t r2 = rng(); c->y = PX((int16_t)((r2 & 0x3f) + g.scroll3542 + 0x10)); c->x = PX(((r2 >> 16) & 0xff) + 0x20); }
            }
        }
        yield_vbls(o, 1);                               /* LAB_07FE */
    } while (--n);
}
void bh_inst4_0(Obj *o) {
    enemy_init(o, 0x0032, 38, -48, 0, 0, 60);
    o->z = PX(0x19); o->cb534 = NULL; o->flags367 |= F_NO_SHADOW;
    wait_onscreen(o, 58);
    boss_enter();
    for (;;) {                                          /* LAB_07F9 */
        o->w[0] = 5;
        do { spawn(fx_flame); if (wait_vbls(o, 7)) return; } while (--o->w[0]);   /* LAB_07FA */
        if (wait_vbls(o, 50)) return;
        if (!flag3617) continue;
        inst4_storm(o);
        g.scroll3530 -= 0x13f;
        g.flag3615 = 0;
        g.flags166 |= 2;                                /* level end */
        flash11166 = 0x100;
        return;                                         /* note: no boss_leave in the original */
    }
}

/* ================================================================== INST4.LIN#3 @ $217c62 (rising/sinking gun, fire patterns) */
/* LAB_0805: fire-pattern bursts (while the platform is alive) */
static void inst4_burst(Obj *o) {
    if (flag3617) return;
    if ((int16_t)rng() < 0) goto lab0806;
lab0807:
    fire_pattern(o, 3); fire_pattern(o, 4); fire_pattern(o, 3); fire_pattern(o, 2);
    if ((rng() & 3) == 0) goto lab0806;
    return;
lab0806:
    fire_pattern(o, 1); fire_pattern(o, 0); fire_pattern(o, 1); fire_pattern(o, 2);
    if ((rng() & 3) != 0) return;
    goto lab0807;
}
void bh_inst4_3(Obj *o) {
    gfx_acquire(o, 6);
    enemy_init(o, 0x0632, 38, -48, 0, 0, 60);
    o->cb534 = NULL;
    wait_vbls(o, 250);
    for (;;) {                                          /* LAB_07FF */
        o->w[0] = 15;
        do { o->y -= PX(2); step(o); } while (--o->w[0]);          /* LAB_0800 rise (result unchecked) */
        flag148 = 0;
        if (!flag3617) {
            for (;;) {                                  /* LAB_0801 */
                if (wait_vbls(o, 200)) goto out;
                int was = flag148 & 1; flag148 |= 1;
                if (!was) break;
            }
        }
        o->w[0] = 15;
        do { o->y += PX(2); step(o); } while (--o->w[0]);          /* LAB_0803 sink */
        rng();                                          /* (rng()&1)*2 computed, unused */
        inst4_burst(o);
        if (wait_vbls(o, 10)) break;
    }
out:
    gfx_release(o, 6);                                  /* LAB_0804 */
}

/* ================================================================== INST5.LIN#0 @ $217d88 (final boss) */
static const uint16_t EYE_GFX[6]   = { 0x0256, 0x0456, 0x0656, 0x0856, 0x0a56, 0x0c56 };   /* LAB_081B */
static const uint16_t MOUTH_GFX[5] = { 0x0e56, 0x1056, 0x1256, 0x1456, 0x1656 };           /* LAB_081E */
/* LAB_0818: eyes */
static void inst5_eyes(Obj *o) {
    eng_set_clock(o, &boss_clock12534);
    enemy_init(o, 0x0256, 0, 0, 0, 0, 0);
    o->cb534 = NULL;
    for (;;) {                                          /* LAB_0819 */
        yield_vbls(o, (rng() & 0x7f) + 100);
        for (;;) {                                      /* LAB_081A */
            int r = rng() & 7; if (r >= 6) break;
            set_frame(o, EYE_GFX[r]);
            if (wait_vbls(o, 5)) return;
        }
    }
}
/* LAB_081C: mouth */
static void inst5_mouth(Obj *o) {
    eng_set_clock(o, &boss_clock12534);
    enemy_init(o, 0x0e56, 0, 0, 0, 0, 0);
    o->cb534 = NULL;
    for (;;) {                                          /* LAB_081D */
        yield_vbls(o, (rng() & 7) + 3);
        int r = rng() & 7; if (r >= 5) continue;
        set_frame(o, MOUTH_GFX[r]);
        if (wait_vbls(o, 1)) return;
    }
}

/* ---- spawn of the three spit-hatched creatures (LAB_0825 table → sub-handlers) ---- */
static void spit_gunner(Obj *o);    /* $2180f6 gfx $0057 */
static void spit_bird(Obj *o);      /* $21814c gfx $0857 */
static void spit_goose(Obj *o);     /* $2181be gfx $0e57 */
static const Script SPIT_KIND[8] = { spit_bird, spit_bird, spit_bird, spit_gunner, spit_gunner, spit_gunner, spit_goose, spit_goose };

/* LAB_0836 @ $218280: goose wing-flap frame + one step */
static int goose_flap(Obj *o) {
    uint16_t gfx = 0x0e57;
    if ((g.rng11172 & 31) == 0) gfx = 0x1e57;
    else if (ZI(o) != 0) { o->w[1] = ~o->w[1]; if (o->w[1] < 0) gfx = 0x1e57; }
    set_frame_dir8(o, gfx);
    return step(o);
}
/* LAB_0833(n): n flap-steps, returns signalled */
static int goose_flaps(Obj *o, int n) { while (n-- > 0) goose_flap(o); return eng_signalled(o); }
/* LAB_082F: circle (angle += w[0]) until heading roughly at a player and downwards */
static void goose_circle(Obj *o) {
    for (;;) {
        o->angle += o->w[0]; set_velocity_from_angle(o);
        goose_flap(o);
        if (goose_flap(o)) return;
        if ((rng() & 3) != 0) {
            int tx, ty;
            if (ZI(o) == 0) prefer_jeep(&tx, &ty); else prefer_heli(&tx, &ty);   /* LAB_0582 / LAB_0581 */
            uint8_t old = o->angle;
            turn_towards(o, tx, ty, 0);                 /* D2 is undefined in the original; snap assumed */
            uint8_t nw = o->angle; o->angle = old;
            int d = (int16_t)(old - nw); if (d < 0) d = -d;
            if (d > 16) continue;
        }
        if (o->angle > 0x80) continue;                  /* LAB_0832 */
        return;
    }
}
/* LAB_0839: rise to z = 32, then become a jeep target (mask $22) */
static void goose_rise(Obj *o) {
    if (ZI(o) >= 0x20) return;
    off_event(o, EV_TOUCH_JEEP); off_event(o, EV_TOUCH_HELI);
    o->vz = PX(1); o->speed = 0x200; set_velocity_from_angle(o);
    for (;;) { if (goose_flap(o)) break; if (ZI(o) >= 0x20) break; }
    o->vz = 0;
    on_event(o, EV_TOUCH_JEEP, on_bullet_hit); o->box.mask = 0x22;
}
/* LAB_083D: descend to the ground, then become a heli target (mask $24) */
static void goose_land(Obj *o) {
    if (ZI(o) == 0) return;
    off_event(o, EV_TOUCH_JEEP); off_event(o, EV_TOUCH_HELI);
    o->vz = PX(-1); o->speed = 0x80; set_velocity_from_angle(o);
    for (;;) { if (goose_flap(o)) break; if (ZI(o) == 0) break; }
    o->vz = 0;
    on_event(o, EV_TOUCH_HELI, on_bullet_hit); o->box.mask = 0x24;
}
/* $2181be: hatched goose */
static void spit_goose(Obj *o) {
    enemy_init(o, 0x0e57, 36, 0, 5, 70, 15);
    stop(o); o->z = PX(0x20);
    o->w[0] = (int16_t)((rng() & 0x40) - 0x20);
    o->angle = 0x40; o->speed = 0x100;
    for (;;) {                                          /* LAB_082C */
        goose_circle(o);
        int r = rng() & 7;
        if (r == 0) goose_rise(o); else if (r < 3) goose_land(o);
        if (goose_flaps(o, 20)) return;
    }
}
/* $2180f6: hatched gunner, drops flames */
static const int16_t GUNNER_ANIM[] = { A_RATE(1), 0x0057, 0x0257, 0x0457, 0x0657, 0x0457, 0x0257, A_LOOP, A_END };
static void spit_gunner(Obj *o) {
    enemy_init(o, 0x0057, 36, 0, 5, 70, 15);
    stop(o); o->z = 0; o->vy = 0x8000;
    anim_start(o, GUNNER_ANIM);
    for (;;) { spawn(fx_flame); if (wait_vbls(o, (rng() & 0x7f) + 0x96)) return; }   /* LAB_082A */
}
/* $21814c: hatched bird, weaves towards the nearest player */
static const int16_t BIRD_ANIM[] = { A_RATE(2), 0x0857, 0x0a57, 0x0c57, 0x0a57, A_LOOP, A_END };
static void spit_bird(Obj *o) {
    enemy_init(o, 0x0857, 34, 0, 5, 70, 15);
    stop(o); o->z = PX(0x20); o->speed = 0x200;
    anim_start(o, BIRD_ANIM);
    for (;;) {                                          /* LAB_082B */
        int tx, ty; nearest_player(&tx, &ty);
        turn_towards(o, tx, ty, 0);                     /* D2 undefined in the original; snap assumed */
        o->angle += (rng() & 0x1f) - 15;
        o->angle &= 0x7f;                               /* BCLR #7,359: keep heading downwards */
        set_velocity_from_angle(o);
        if (wait_vbls(o, 8)) break;
    }
    o->vx = 0; o->vy = PX(4);
    wait_signal(o);
}
/* LAB_0826: spit egg: flies out, bounces twice, hatches the creature in w[2] */
static const int16_t EGG_FLY_ANIM[]   = { A_RATE(6), 0x2e57, 0x3057, 0x3257, 0x3457, 0x3657, A_END };
static const int16_t EGG_HATCH_ANIM[] = { A_RATE(4), 0x3857, 0x3a57, A_END_SIGNAL, A_END };
static int egg_fall(Obj *o) {                           /* LAB_0828 */
    o->az = -0x1000;
    for (;;) { if (step(o)) return 1; if (ZI(o) == 0) return 0; }
}
static void spit_egg(Obj *o) {
    enemy_init(o, 0x2e57, 38, 0, 3, 70, 10);
    int16_t sp = o->speed;
    o->speed = 0x2800; set_velocity_from_angle(o); o->x += o->vx; o->y += o->vy;
    o->speed = sp; set_velocity_from_angle(o);
    anim_start(o, EGG_FLY_ANIM);
    o->vz = PX(2); if (egg_fall(o)) return;
    stop(o); o->vz = PX(1); if (egg_fall(o)) return;
    spawn(SPIT_KIND[o->w[2] & 7]);
    anim_start(o, EGG_HATCH_ANIM);
    wait_signal(o);
}
/* LAB_0824: spawn an egg, mirror the heading */
static void inst5_spit_one(Obj *o) {
    spawn(spit_egg);                                    /* w[0..7] (incl. the kind in w[2]) are copied */
    o->angle ^= 127; o->angle += 1;
}
/* LAB_081F: spitter -- bursts of eggs in fanning directions */
static void inst5_spitter(Obj *o) {
    eng_set_clock(o, &boss_clock12534);
    gfx_acquire(o, 0x57);
    o->x += PX(4); o->y += PX(4);
    for (;;) {                                          /* LAB_0820 */
        if (threat_ok()) {
            uint32_t r = rng(); o->w[5] = (int16_t)r;
            o->w[2] = (int16_t)((r >> 16) & 7);         /* creature kind (LAB_0825 index) */
            o->angle = 0x10; o->speed = 0x80; o->w[4] = 5;
            do {                                        /* LAB_0821 */
                yield_vbls(o, 10);
                if (eng_signalled(o)) goto out;
                inst5_spit_one(o); inst5_spit_one(o);
                o->angle = (uint8_t)(((16 + o->w[5] + o->angle) & 0x1f) - 16);
                o->speed += 0x48;
            } while (--o->w[4]);
        }
        yield_vbls(o, 200);                             /* LAB_0822 */
        if (eng_signalled(o)) break;
    }
out:
    gfx_release(o, 0x57);                               /* LAB_0823 */
    eng_free(o);
}
/* LAB_0851: place at radius (w[1]+6)*12 in direction w[0]/24 of a turn, no motion */
static void spark_place(Obj *o) {
    o->angle = (uint8_t)(((o->w[0] * 0xaaa) + 0x80) >> 8);
    o->speed = 0xc00; set_velocity_from_angle(o);
    for (int n = o->w[1] + 5; n >= 0; n--) { o->x += o->vx; o->y += o->vy; }
    stop(o);
}
/* LAB_084F: intro spark (gfx $2256), popup-style two-tick flash */
static void spark_intro(Obj *o) {
    spark_place(o);
    if ((uint16_t)SCRY(o) > 0x100) { eng_free(o); return; }
    o->cb534 = NULL; o->animA.flags |= 0x40;
    set_frame(o, 0x2256);
    wait_vbls(o, 1); o->y -= PX(0x140); wait_vbls(o, 1);
    eng_free(o);
}
/* LAB_0850: ring spark (gfx $1a56), one tick */
static void spark_ring(Obj *o) {
    spark_place(o);
    o->cb534 = NULL; set_frame(o, 0x1a56);
    wait_vbls(o, 1); eng_free(o);
}
/* LAB_084A / LAB_0849: a radial line of ring sparks (w[0] = 23 or 22 downto 1/0 step 2, w[1] = ring) */
static void spark_line(Obj *o, int start, int ring) {
    for (int d = start; d >= 0; d -= 2) { o->w[0] = d; o->w[1] = ring; spawn(spark_ring); }
}
/* LAB_084D: a full ring (all 13 radii) in direction d */
static void spark_ray(Obj *o, int d) { for (int r = 12; r >= 0; r--) { o->w[0] = d; o->w[1] = r; spawn(spark_ring); } }
/* LAB_084C */
static void spark_random_ray(Obj *o) {
    int d; do { d = rng() & 0x1f; } while (d >= 24);
    spark_ray(o, d); yield_vbls(o, 5);
}
/* LAB_0840: the boss's spark/aura effect generator */
static void inst5_sparks(Obj *o) {
    eng_set_clock(o, &boss_clock12534);
    o->x += PX(4); o->y += PX(4);
    for (int ring = 12; ring >= 0; ring--) {            /* LAB_0841: intro burst */
        for (int d = 23; d >= 0; d--) { o->w[0] = d; o->w[1] = ring; spawn(spark_intro); }
        yield_once(o);
    }
    for (;;) {                                          /* LAB_0843 */
        yield_vbls(o, (rng() & 0x7f) + 8);
        if (eng_signalled(o)) { eng_free(o); return; }
        for (int ring = 12; ring >= 0; ring--) { spark_line(o, 23, ring); yield_vbls(o, 5); }   /* LAB_0844 */
        if ((rng() & 3) == 0) for (int n = 12; n; n--) spark_random_ray(o);                   /* LAB_0845 */
        rng();
        if ((rng() & 3) == 0) {                         /* LAB_0847 */
            int ring = (rng() & 7) + 1;
            for (int n = 4; n; n--) { spark_line(o, 23, ring); yield_vbls(o, 5); spark_line(o, 22, ring); yield_vbls(o, 5); }
        }
    }
}
/* LAB_0811: final boss hit handler (runs inline in step; may yield) */
static void inst5_hit(Obj *o) {
    sfx(SFX_BOSS_HIT, XI(o));                                /* LAB_07B3 -> LAB_03D6 */
    if (--o->hp > 0) { flash11166 = 0x40; yield_vbls(o, 1); flash11166 = 0; return; }
    boss_clock12534++;                                  /* kill the boss group */
    smart_bomb(o);
    gfx_acquire(o, 6);
    for (int n = 12; n; n--) {                          /* LAB_0813 */
        Obj *c = spawn(fx_wreck);
        if (c) { uint32_t r = rng(); c->x += PX((int)(r & 0x3f) - 0x20); c->x += PX((int)((r >> 16) & 0x3f) - 0x20); }   /* both on x, as in the original */
        screen_shake(o); screen_shake(o); screen_shake(o); screen_shake(o);
    }
    gfx_release(o, 6);
    fade142 = -1; fade11170 = 0;                        /* fade to white: 11170 += $10 per VBL until $100 (LAB_024C) */
    while (fade142) { screen_shake(o); fade11170 += 0x10 * 2 * g.vbl_per_tick; if (fade11170 >= 0x100) { fade11170 = 0x100; fade142 = 0; } }
    /* 11260/11440(A6).l = -1 (player-record +84), 12353 bit3 = level complete */
    state12353 |= 8;
    kill(o);
}
void bh_inst5_0(Obj *o) {
    for (;;) {                                          /* LAB_080C: wait for the boss flag */
        yield_once(o);
        if (eng_signalled(o)) { eng_free(o); return; }
        if (g.flags166 & 8) break;
    }
    gfx_acquire(o, 0x57);
    enemy_init(o, 0x0000, 38, 0, 200, 20000, 0);
    o->cb534 = NULL;
    for (;;) {                                          /* LAB_080D: strobe until the level-end flag clears */
        flash11166 = 0; fade11170 = 0x100; yield_vbls(o, 1);
        flash11166 = 0x100; fade11170 = 0; yield_vbls(o, 1);
        if (eng_signalled(o)) goto out;
        if (!(g.flags166 & 2)) break;
    }
    on_event(o, EV_BULLET, inst5_hit);
    while (g.flags166 & 2) yield_vbls(o, 1);            /* LAB_080E */
    spawn(inst5_eyes); spawn(inst5_mouth); spawn(inst5_spitter); spawn(inst5_sparks);
    o->box.hw = o->box.hh = 32;
    o->animA.flags |= 0x80;
    for (;;) { if (wait_ticks(o, 100)) break; }         /* LAB_080F */
out:
    boss_clock12534++;                                  /* LAB_0810 */
    g.game_over160 = -1;
    gfx_release(o, 0x57);
}

/* ================================================================== GOOSE.LIN#0 @ $2184aa (the big goose boss) */
static const int16_t GOOSE_FLY_ANIM[]  = { A_RATE(1), (int16_t)0xb880, 0x0017, (int16_t)0xc080, 0x0017, A_LOOP, A_END };
static const int16_t GOOSE_LAND_ANIM[] = { (int16_t)0xc080, A_RATE(10), 0x0017, 0x0017, 0x0217, 0x0417, 0x0617, 0x0817, 0x0a17, A_END };
/* LAB_0680 @ $215102: fixed slot-B script (set 0 frames 5..8, flicker flag $80) */
static const int16_t ROTOR_ANIM_B[] = { A_RATE(1), A_SETLOOP(0), 0x0a00, A_FLAGS_SET(0x80), 0x0a00, A_FLAGS_CLR(0x80), 0x0c00, A_FLAGS_SET(0x80), 0x0c00, A_FLAGS_CLR(0x80),
    0x0e00, A_FLAGS_SET(0x80), 0x0e00, A_FLAGS_CLR(0x80), 0x1000, A_FLAGS_SET(0x80), 0x1000, A_FLAGS_CLR(0x80), A_LOOP, A_END };
static const int16_t GOOSE_HEAD_ANIM[] = { A_RATE(8), A_SETLOOP(0), 0x1017, 0x1217, 0x1417, 0x1617, A_LOOP, A_END };
/* LAB_087B: child: detach, fly (vx/vy = 0) for 75 ticks, then home back to the parent's anchor
 * (w[4..6] = saved vx,vy,vz) and re-attach; decrements the parent's w[0] on arrival */
static void goose_part_return(Obj *o) {
    o->speed = 0x200; o->angle = 0x40;
    o->w[4] = (int16_t)(o->vx >> 16); o->w[5] = (int16_t)(o->vy >> 16); o->w[6] = (int16_t)(o->vz >> 16);
    o->vx = o->vy = o->vz = 0;
    if (o->parent) o->z = PX(ZI(o->parent) + o->w[6]);
    o->flags367 &= ~F_ATTACHED;
    set_velocity_from_angle(o);
    wait_vbls(o, 0x4b);
    for (;;) {                                          /* LAB_087C */
        if (!o->parent) break;
        int tx = XI(o->parent) + o->w[4], ty = YI(o->parent) + o->w[5];
        int d2 = XI(o) - tx; if (d2 < 0) d2 = -d2;
        int d3 = YI(o) - ty; if (d3 < 0) d3 = -d3;
        d3 += d2;
        if (d3 < 6) break;
        d2 = 63 - d3; if (d2 <= 2) d2 = 2;
        d2 = (d2 >> 1) * g.vbl_per_tick;
        turn_towards(o, tx, ty, d2); set_velocity_from_angle(o);
        if (step(o)) break;
    }
    o->vx += o->vx; o->vy += o->vy;                     /* LAB_0880 */
    step(o);
    o->vx = PX(o->w[4]); o->vy = PX(o->w[5]); o->vz = PX(o->w[6]);
    o->flags367 |= F_ATTACHED;
    if (o->parent) o->parent->w[0]--;
}
/* LAB_0868/0869/086A -> LAB_086C: wings / tail (attached parts that follow with a spring) */
static void goose_part(Obj *o, int dx, int dy, uint16_t gfx) {
    o->vx = PX(dx); o->vy = PX(dy); o->w[0] = dx; o->w[1] = dy;
    yield_vbls(o, rng() & 0x3f);
    o->cb538_disabled = 1;                              /* ST 538(A5) */
    enemy_init(o, gfx, 34, -32, 0, 0, 10);
    o->cb534 = NULL; o->cb542 = (Callback)eng_signal;
    o->vz = PX(1);
    o->x = PX((rng() & 0xff) + 0x20); o->y = PX((int16_t)(g.scroll3542 - 24));
    goose_part_return(o);
    for (;;) {                                          /* LAB_086D: spring back towards (w0,w1) offset, doubled when hit */
        Obj *p = o->parent;
        if (p && (p->flags367 & F_HIT_FLASH)) { o->vx = PX(o->w[0] * 2); o->vy = PX(o->w[1] * 2); }
        int d = (o->vx >> 16) - o->w[0]; if (d) o->vx -= PX(d > 0 ? 4 : -4);
        d = (o->vy >> 16) - o->w[1];      if (d) o->vy -= PX(d > 0 ? 4 : -4);
        if (step(o)) return;
    }
}
static void goose_tail(Obj *o)  { goose_part(o, 0, -44, 0x0e17); }     /* LAB_0868 */
static void goose_wing_r(Obj *o){ goose_part(o, 16, -12, 0x0c17); }    /* LAB_0869 */
static void goose_wing_l(Obj *o){ goose_part(o, -16, -12, 0x0c17); }   /* LAB_086A */
/* LAB_0873: head -- returns, then swings left/right spitting */
static int goose_head_rand_wait(Obj *o) { return wait_ticks(o, (rng() & 0x1f) + 0x0a); }    /* LAB_0875 */
static int goose_head_swing(Obj *o, int d) {           /* LAB_0876 (+8) / LAB_0877 (-8) */
    o->w[1] = d; o->w[0] = 6;
    do { o->angle += o->w[1]; set_velocity_from_angle(o); o->vy += PX(0x0c); if (step(o)) return 1; } while (--o->w[0]);
    return wait_ticks(o, 20);
}
static void goose_head(Obj *o) {
    o->cb538_disabled = 1;
    enemy_init(o, 0x1017, 34, -32, 0, 0, 10);
    o->cb534 = NULL; o->cb542 = kill;                   /* orphaned head dies with a kill() */
    o->vx = 0; o->vy = PX(0x18); o->vz = PX(1);
    anim_start(o, GOOSE_HEAD_ANIM);
    o->x = PX((rng() & 0xff) + 0x20); o->y = PX((int16_t)(g.scroll3542 - 24));
    goose_part_return(o);
    wait_ticks(o, 20);
    o->speed = 0x1200; o->angle = 0x40;
    for (;;) {                                          /* LAB_0874 */
        if (goose_head_swing(o, -8)) return;
        if (goose_head_swing(o, 8)) return;
        if (goose_head_rand_wait(o)) return;
        if (goose_head_swing(o, 8)) return;
        if (goose_head_swing(o, -8)) return;
        if (goose_head_rand_wait(o)) return;
    }
}
/* LAB_0863(n): spread n×(live players) weapon TOKENS (LAB_06AA, bh_tokens.c) around the goose,
 * evenly spaced in angle from a random start; each token sets its own speed. */
static void goose_bombs(Obj *o, int n) {
    int cnt = 0;
    if (g.heli.alive) cnt += n;                         /* 11231(A6) = heli record +55 */
    if (g.jeep.alive) cnt += n;                         /* 11411(A6) = jeep record +55 */
    o->w[2] = cnt; if (!cnt) return;
    o->w[4] = 0x100 / cnt; o->angle = (uint8_t)rng();
    do { spawn(bh_token_06AA); o->angle += o->w[4]; } while (--o->w[2]);   /* LAB_0866 */
}
/* LAB_0860: goose hit handler */
static void goose_hit(Obj *o) {
    if (--o->hp > 0) { o->vx = -o->vx; sfx(SFX_BOSS_HIT, XI(o)); o->flags367 |= F_HIT_FLASH; return; }   /* LAB_0627 */
    for (Obj *c = o->child; c; ) { Obj *n = c->sib; c->parent = NULL; c->sib = NULL; c = n; } o->child = NULL;   /* LAB_04C3 */
    int d = 2; if (!((uint16_t)o->w[2] > 0x1f4)) d++;   /* BHI skips the ADDQ */
    goose_bombs(o, d);
    sfx(SFX_ENEMY_DESTROYED, XI(o));                            /* LAB_0628 */
    kill(o);
}
void bh_goose_0(Obj *o) {
    tail_count163 = -1;
    gfx_acquire(o, 0x18);
    o->cb538_disabled = 1;
    o->vx = 0; o->vy = 0;
    enemy_init(o, 0x0017, 0, 0, 0, 0, 100);
    o->cb534 = NULL; o->flags367 |= F_SCREEN_LOCKED;
    o->y += PX(0x120); o->x = PX(0xa0); o->z = PX(0x20);
    o->w[2] = 0;
    spawn_attached(goose_head); spawn_attached(goose_wing_l); spawn_attached(goose_wing_r); spawn_attached(goose_tail);
    o->w[0] = 4;                                        /* parts still out */
    anim_start(o, GOOSE_FLY_ANIM);
    o->vy = PX(-2);
    for (;;) { if (step(o)) goto done; if ((uint16_t)SCRY(o) <= 0x48) break; }   /* LAB_0854 */
    o->vy = 0;
    anim_start(o, GOOSE_LAND_ANIM);
    o->hp = 0x19;
    on_event(o, EV_BULLET, goose_hit); on_event(o, EV_TOUCH_JEEP, goose_hit);
    o->box.mask = 0x22;
    anim_start_b(o, ROTOR_ANIM_B);                      /* LAB_0680: slot-B rotor overlay */
    for (;;) { if (step(o)) goto done; if (o->w[0] == 0) break; }               /* LAB_0855: wait for the parts */
    o->w[2] = 0x7d0; o->w[3] = 0x0a; o->vy = PX(1);
    for (;;) {                                          /* LAB_0856 */
        int tx, ty; nearest_player(&tx, &ty);
        int32_t a = 0x600;
        if (tx >= XI(o)) { if ((int16_t)(o->vx >> 16) >= 1) a = 0; }
        else { a = -a; if ((int16_t)(o->vx >> 16) <= -2) a = 0; }
        o->ax = a;
        int sy = SCRY(o);
        if (sy < 0x40) o->vy = PX(4); else if (sy > 0xc0) o->vy = -0x4000;
        if ((uint16_t)sy <= 0x80 && --o->w[3] == 0) {
            o->w[3] = (12 - g.difficulty182) * 4;
            fire_missile_aimed(o);
            fire_homing(o, 6, 0, 0); fire_homing(o, -6, 0, 0x80);
        }
        if (step(o)) goto done;                         /* LAB_085B */
        o->w[2] -= g.vbl_per_tick;
        if (o->w[2] < 0) break;
    }
    if (o->hp == 0x19) goose_bombs(o, 5);
    stop(o); o->vy = PX(-4); o->cb538_disabled = 0;     /* LAB_085C: fly off */
    wait_signal(o);
done:                                                   /* LAB_085D */
    for (Obj *c = o->child; c; ) { Obj *n = c->sib; c->parent = NULL; c->sib = NULL; c = n; } o->child = NULL;
    o->box.mask = 0;
    gfx_release(o, 0x18);
    tail_count163 = 0;
    /* the original then checksums 0x6ac1 words of program text (yielding) and adds the sum to 3560(A6) */
    map_adj3560 += 0;
}
