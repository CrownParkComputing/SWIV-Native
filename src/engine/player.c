/* player.c -- the SWIV PLAYER code ported from AMPROG (re/amprog.asm):
 *   LAB_0556/LAB_055A..LAB_0569  player manager task (lives, spawn, respawn, extra lives, input)
 *   LAB_056D/LAB_057A/LAB_057D    joystick word 64(rec): dir nibble, bit7 raw fire, bit6 double-tap, bit5 fire (cooldown 96/98)
 *   LAB_0681..LAB_0687            HELICOPTER (record 11176: box $0048, death event bit1, gun child LAB_067D)
 *   LAB_065F..LAB_066F            JEEP form A (record 11356: box $0050, death event bit2, turret child LAB_0638, jump, dust)
 *   LAB_0649..LAB_065A            JEEP form B (same structure; swapped in/out at the SWAP.LIN markers 3552/3556)
 *   LAB_0670 / LAB_0676           player per-tick (shield/flicker timers, record x,y,z) / player death
 *   LAB_067A/LAB_0679/LAB_068C/LAB_0692/LAB_0697  terrain tests, clamps, joystick->angle
 *   LAB_0640 + tables LAB_0647/LAB_0648   fire: bullet patterns per weapon level
 *   LAB_04A4/LAB_04A6/LAB_04B2/LAB_04B7   player-bullet task (prio -2): 2 tables x 30 entries, move, PLOP on solid hit
 *   LAB_06C2 shield child, LAB_067C dust, LAB_0771 bonus-zone marker
 * Player bullets are NOT objects: the tables live here and their collision boxes are engine ext boxes.
 * All offsets quoted are decimal bytes from A5 (object) or the player record (A4).
 *
 * NOTE on naming: the trace proves the HELI object (record "Lazy Heli", 56=1, 58=0, z=32) registers box $0048 and enables
 * event bit1 (LAB_0506), the JEEP box $0050 / bit2 (LAB_0507) -- i.e. the engine's EV_JEEP_KILLER/EV_TOUCH_JEEP names
 * (VERBS.md §6) actually denote the HELI and vice versa.  Bit numbers are used directly below. */
#include "engine.h"
#include <string.h>

int player_input_dx, player_input_dy, player_input_fire;   /* set by the frontend each VBL (player 1 = heli) */
int player2_input_dx, player2_input_dy, player2_input_fire;  /* player 2 = jeep */
RenderEntry player_bullet_render[60]; int player_bullet_count;   /* (viewer copies up to 30: count is capped at 30) */

#define VBLS g.vbl_per_tick
#define XW(o) ((int16_t)((o)->x >> 16))
#define YW(o) ((int16_t)((o)->y >> 16))
#define ZW(o) ((int16_t)((o)->z >> 16))
/* MOVE.W #n,320(A5) etc: write the INTEGER (high) word of a 16.16 long, keep the fraction */
#define SET_HI(l, v) ((l) = (int32_t)(((uint32_t)(l) & 0xffffu) | ((uint32_t)(uint16_t)(int16_t)(v) << 16)))
#define SET_LO(l, v) ((l) = (int32_t)(((uint32_t)(l) & ~0xffffu) | (uint16_t)(v)))

/* 276(A5).l of a player object (and of its children) = the player record.  Natively w[0] = 32 (the high word of
 * 11176(A6)+A6, as the host objlog shows) and w[1] = record index (0 heli, 1 jeep); both are copied to children. */
static struct Player *REC(const Obj *o) { return o->w[1] ? &g.jeep : &g.heli; }
static struct Player *OTHER(const struct Player *p) { return p == &g.heli ? &g.jeep : &g.heli; }
static void set_rec(Obj *o, struct Player *p) { o->w[0] = 32; o->w[1] = (p == &g.jeep); }

/* autofire option byte 12484(A6): bit7 set = fire on button RELEASE (the button bit is XORed with it). Not in the engine. */
static const uint8_t g_autofire12484 = 0;
static long g_shots12486;                /* shots fired statistic (12486(A6)) */
long player_shots_fired(void) { return g_shots12486; }
static const int g_lives_step12524 = 4;  /* 12524(A6): lives are counted in -4 units */

/* ---------------------------------------------------------------------------------------------------------------------
 * Terrain pixel tests.  LAB_030E (mode 0) / LAB_030D (mode 1) test the object's sprite mask against the level's
 * collision planes (engine: eng_terrain_test; see engine.c for the exact rule). */
static int pixel_test(Obj *o, int mode) { return eng_terrain_test(o, mode); }
static int blocked_ahead_mode(Obj *o, int mode) {                            /* LAB_067B: move by 2*(vx,vy), test, restore */
    o->x += o->vx * 2; o->y += o->vy * 2;
    int r = pixel_test(o, mode);
    o->x -= o->vx * 2; o->y -= o->vy * 2;
    return r;
}
#define blocked_067A(o) blocked_ahead_mode((o), 0)   /* LAB_067A (engine: blocked_ahead) */
#define blocked_0679(o) blocked_ahead_mode((o), 1)   /* LAB_0679 */

/* =====================================================================================================================
 * Player-bullet tables (11288(A6) heli / 11468(A6) jeep): 30 entries x 48 bytes
 *   0.w state (0 free, -1 flying, 1..4 PLOP countdown), 2.w dx, 4.w dy (px/VBL), 6..25 collision box (14.w x, 16.w y,
 *   18/20 hw/hh = 8, 22 mask $8041/$8081, 24 hits), 26..47 blit node (34 key, 36 gfx, 38/40 x,y)
 * ===================================================================================================================== */
typedef struct { int16_t state, dx, dy; int16_t x, y; Box *box; uint16_t gfx; int16_t key; } PBullet;
static PBullet pb_tbl[2][30];            /* [0] = 11288 (heli, D5 = 58 = 0), [1] = 11468 (jeep) */
static const uint16_t PLOP_04B1[4] = { 0, 0x60d4 /* garbage word at LAB_04B1+2 (BRA.S opcode) */, 0x0a01, 0x0c01 };

/* LAB_04B2 @ $211d24: add a bullet at (x,y) with velocity (dx,dy) px/VBL and gfx; D5 = record 58 (vehicle) selects the mask */
static void bullet_add(PBullet *tbl, int x, int y, int dx, int dy, uint16_t gfx, int vehicle) {
    int i; for (i = 0; i < 30; i++) if (tbl[i].state == 0) break;
    if (i == 30) return;
    PBullet *b = &tbl[i];
    g_shots12486++;
    g.stat_shots++; g.stat_shots_p[vehicle ? 1 : 0]++; b->state = -1; b->gfx = gfx; b->dx = dx; b->dy = dy; b->x = x; b->y = y;
    b->box = eng_extbox_alloc();                                   /* LAB_053F: hw=hh=8, hits=0, link */
    if (b->box) { b->box->hw = b->box->hh = 8; b->box->mask = vehicle ? 0x8081 : 0x8041; b->box->hits = 0; b->box->x = x; b->box->y = y; }
}
static void bullet_unlink(PBullet *b) { if (b->box) { eng_extbox_free(b->box); b->box = NULL; } }   /* LAB_0542 */

/* LAB_04A6 @ $211c78: one tick of bullet motion for both tables (D6 = 3542, D7 = 3542+256) */
static void bullets_move(void) {
    uint16_t top = g.scroll3542, bot = g.scroll3542 + 256;
    for (int t = 0; t < 2; t++) for (int i = 0; i < 30; i++) {
        PBullet *b = &pb_tbl[t][i];
        if (b->state < 0) {                                        /* flying */
            if (b->box && (b->box->hits & 0x20)) {                 /* LAB_04AF: hit something solid -> PLOP */
                b->dx = b->dy = 0; b->state = 4; bullet_unlink(b);
                sfx(SFX_PLOP, b->x);                                   /* native event hook: bullet impact (assign in SFX debug) */
                goto plop;
            }
            for (int v = 0; v < VBLS; v++) { b->x += b->dx; b->y += b->dy; }   /* LAB_04A9: once per elapsed VBL */
            if (b->box) { b->box->x = b->x; b->box->y = b->y; }
            if ((uint16_t)(b->y - top) >= (uint16_t)(bot - top) || b->x < 0 || b->x >= 320) {   /* LAB_04AE: off screen */
                b->state = 0; bullet_unlink(b); continue;
            }
            b->key = (int16_t)((((uint16_t)b->y) >> 1) ^ 0x7fff);
            continue;
        }
        if (b->state == 0) continue;
    plop:                                                          /* LAB_04AD: PLOP countdown 4->3->2->1->0 */
        if (--b->state == 0) continue;                             /* (LAB_04AD BEQ after SUBQ: entry is free, nothing drawn) */
        b->gfx = PLOP_04B1[b->state];                              /* 3: $0c01, 2: $0a01, 1: junk (not drawn natively) */
    }
}
/* LAB_04A4 @ $211c6e: the prio -2 task.  LAB_04B7 (table allocation) is static here. */
static void bullet_task(Obj *o) {
    memset(pb_tbl, 0, sizeof pb_tbl);
    for (;;) { bullets_move(); yield_once(o); }                    /* LAB_04A5: LAB_04A6 ; LAB_049C */
}
/* per-VBL snapshot of the live bullets for the renderer (blit nodes at 26 of each entry; LAB_04AB inserts them) */
void player_vbl(void) {
    if (!g.jeep.joined55 && player2_input_fire) g.jeep.joined55 = 1;     /* LAB_055A: port-1 fire joins the jeep */
    if (!g.heli.joined55 && player_input_fire) g.heli.joined55 = 1;      /* port-2 fire joins the heli */
    player_bullet_count = 0;
    for (int t = 0; t < 2; t++) for (int i = 0; i < 30; i++) {
        PBullet *b = &pb_tbl[t][i];
        if (b->state == 0 || (b->state > 0 && b->gfx == 0x60d4)) continue;
        if (player_bullet_count >= 30) return;
        /* player shots are hardware sprites in the original: palette {1:$FFF, 2:$999, 3:$800 (even sprite pair) / $F80 (odd)} -> flag $80 (+$40 for the odd pair) */
        player_bullet_render[player_bullet_count++] = (RenderEntry){ (uint16_t)b->key, b->gfx, b->x, (int16_t)(b->y - g.scroll3542), (uint8_t)(0x80 | ((i & 1) << 6)) };
    }
}

/* ---------------------------------------------------------------------------------------------------------------------
 * LAB_0640 @ $2147c0: FIRE.  Called by the gun children (heli LAB_067D, jeep turret LAB_0638) with A5 = the child:
 * bullets start at the child's x,y and fly in the child's 8-direction angle.  gfx = BULLET.LIN frame 8+dir8 ($1001 + dir8*$200).
 * Weapon level 100(A4) 1..5: `level` bullets from pattern block LAB_0647[dir8] (9 entries x {dx,dy,xoff,yoff}; odd levels use
 * entries 0.., even levels entries 5..); 104(A4) = 0: all bullets get entry 0's velocity (straight), -1: each its own (spread).
 * Level > 5: 8-way spread LAB_0648 (8 x {dx,dy,xoff,yoff,gfx}). */
typedef struct { int16_t dx, dy, xo, yo; } Pat;
static const Pat PAT_0647[8][9] = {   /* dumped from amprog.bin $2148b6.. (blocks overlap by one entry; entry 9 unused) */
    { {9,0,8,0},{8,-1,0,-4},{8,1,0,4},{7,-2,-8,-8},{7,2,-8,8},{8,-1,4,-2},{8,1,4,2},{7,-2,-4,-6},{7,2,-4,6} },
    { {6,6,6,6},{6,5,3,-3},{5,6,-3,3},{6,4,0,-12},{4,6,-12,0},{6,5,5,2},{5,6,2,5},{6,4,2,-7},{4,6,-7,2} },
    { {0,9,0,8},{-1,8,-4,0},{1,8,4,0},{-2,7,-8,-8},{2,7,8,-8},{-1,8,-2,4},{1,8,2,4},{-2,7,-6,-4},{2,7,6,-4} },
    { {-6,6,-6,6},{-6,5,-3,-3},{-5,6,3,3},{-6,4,0,-12},{-4,6,12,0},{-6,5,-5,2},{-5,6,-2,5},{-6,4,-2,-7},{-4,6,7,2} },
    { {-9,0,-8,0},{-8,-1,0,-4},{-8,1,0,4},{-7,-2,8,-8},{-7,2,8,8},{-8,-1,-4,-2},{-8,1,-4,2},{-7,-2,4,-6},{-7,2,4,6} },
    { {-6,-6,-6,-6},{-6,-5,-3,3},{-5,-6,3,-3},{-6,-4,0,12},{-4,-6,12,0},{-6,-5,-5,-2},{-5,-6,-2,-5},{-6,-4,-2,7},{-4,-6,7,-2} },
    { {0,-9,0,-8},{-1,-8,-4,0},{1,-8,4,0},{-2,-7,-8,8},{2,-7,8,8},{-1,-8,-2,-4},{1,-8,2,-4},{-2,-7,-6,4},{2,-7,6,4} },
    { {6,-6,6,-6},{6,-5,3,3},{5,-6,-3,-3},{6,-4,0,12},{4,-6,-12,0},{6,-5,5,-2},{5,-6,2,-5},{6,-4,2,7},{4,-6,-7,-2} },
};
static const struct { int16_t dx, dy, xo, yo; uint16_t gfx; } PAT_0648[8] = {   /* $214af6 */
    {0,-9,0,-4,0x1c01},{-6,-6,-3,-3,0x1a01},{6,-6,3,-3,0x1e01},{-9,0,-4,0,0x1801},{9,0,4,0,0x1001},{-6,6,-3,3,0x1601},{6,6,3,3,0x1201},{0,9,0,4,0x1401},
};
static void fire_0640(Obj *o) {
    sfx(SFX_SHOT, XW(o));                                          /* LAB_03E2 */
    struct Player *p = REC(o);
    PBullet *tbl = pb_tbl[p->vehicle ? 1 : 0];                     /* 112(A4) = the player's table */
    int x = XW(o), y = YW(o);
    int d2 = (o->angle + 16) & 0xe0;                               /* 8-direction angle */
    int veh = p->vehicle;
    uint16_t gfx = (uint16_t)((d2 << 4) + 0x1001);
    int lvl = p->level100;
    if ((uint16_t)lvl > 5) {                                       /* LAB_0645: 8-way */
        for (int e = 0; e < 8; e++) bullet_add(tbl, x + PAT_0648[e].xo, y + PAT_0648[e].yo, PAT_0648[e].dx, PAT_0648[e].dy, PAT_0648[e].gfx, veh);
        return;
    }
    const Pat *blk = PAT_0647[d2 >> 5];
    int base = (lvl & 1) ? 0 : 5;                                  /* BTST #0,D7 ; BNE -> else A1 += 40 */
    if (lvl <= 0) return;                                          /* original: DBF with -1 would loop 65536 times; never happens (level >= 1) */
    if (p->spread104) { for (int i = 0; i < lvl; i++) bullet_add(tbl, x + blk[base + i].xo, y + blk[base + i].yo, blk[base + i].dx, blk[base + i].dy, gfx, veh); }   /* LAB_0642 */
    else             { for (int i = 0; i < lvl; i++) bullet_add(tbl, x + blk[base + i].xo, y + blk[base + i].yo, blk[0].dx, blk[0].dy, gfx, veh); }                   /* LAB_0644 */
}

/* =====================================================================================================================
 * Shared player verbs
 * ===================================================================================================================== */
/* LAB_0697 @ $2152aa: joystick nibble -> angle (up=$C0, down=$40, left=$80, right=0, diagonals); Z set if none */
static const int16_t JOY_0699[16] = { -1, 0xc0, 0x40, -1, 0x80, 0xa0, 0x60, 0x40, 0x00, 0xe0, 0x20, 0x00, -1, 0xc0, 0x40, -1 };
static int joy_angle(int joy, int *angle) { int a = JOY_0699[joy & 15]; if (a < 0) return 0; *angle = a; return 1; }

/* LAB_0692 @ $21526c: heli clamp: x to 4..316, y to [3542+4, 3542+252] (integer words only) */
static void clamp_0692(Obj *o) {
    int x = XW(o); if (x <= 4) SET_HI(o->x, 4); if (x >= 316) SET_HI(o->x, 316);
    uint16_t d1 = g.scroll3542 + 4, y = (uint16_t)YW(o);
    if (!(y > d1)) SET_HI(o->y, d1);
    d1 += 248; if (!(y < d1)) SET_HI(o->y, d1);
}
static void die_0677(Obj *o);
/* LAB_068C @ $215210: jeep clamp: x 4..316; y < 3558 (jeep limit); y to [3542+4, 3542+252]; when pushed against the
 * bottom edge and both terrain tests hit -> death (LAB_0677) */
static void clamp_068C(Obj *o) {
    int x = XW(o); if (x <= 4) SET_HI(o->x, 4); if (x >= 316) SET_HI(o->x, 316);
    uint16_t y = (uint16_t)YW(o);
    if (!(y < (uint16_t)g.jeep_limit3558)) { y = (uint16_t)g.jeep_limit3558; SET_HI(o->y, y); }
    uint16_t d1 = g.scroll3542 + 4;
    if (!(y > d1)) SET_HI(o->y, d1);
    d1 += 248;
    if (!(y < d1)) { SET_HI(o->y, d1); if (pixel_test(o, 0) && pixel_test(o, 1)) die_0677(o); }
}
/* LAB_0676 @ $215026: player death unless shielded/flickering; LAB_0677: ring of 16 explosions + signal self */
static void die_0677(Obj *o) { eng_spawn(o, fx_ring16, 100); eng_signal(o); }
static void die_0676(Obj *o) { struct Player *p = REC(o); if ((p->flicker108 | p->invuln106) == 0) die_0677(o); }

/* LAB_06C2 @ $215612: shield child (spawned by LAB_0670 when 106 < 0): bobbing $1211/$1411 sprite attached to the player
 * while 106(rec) != 0.  enemy_init with mask 0, hp 0 (cannot be shot), threat 5. */
static void shield_06C2(Obj *o) {
    sfx(SFX_PICKUP, XW(o));                                        /* LAB_03E7 */
    enemy_init(o, 0x1211, 0, -16, 0, 0, 5);
    stop(o); o->flags367 |= F_ATTACHED | F_NO_SHADOW; o->cb534 = NULL;   /* ST 534 */
    for (;;) {
        set_frame(o, 0x1211); SET_HI(o->vz, 2);  if (step(o)) return;
        set_frame(o, 0x1411); SET_HI(o->vz, -2); if (step(o)) return;
        if (REC(o)->invuln106 == 0) return;
    }
}
/* LAB_0670 @ $214fc0: player per-tick: shield timer 106 (-1 = just picked up: spawn the shield child, 500 VBLs; while
 * running keeps the flicker 108 at 100), flicker 108 (bit3 -> HIT_FLASH), record 70/72/74 = x,y,z, then step() */
static int player_tick_0670(Obj *o) {
    struct Player *p = REC(o);
    if (p->invuln106) {
        if (p->invuln106 < 0) { Obj *c = spawn_attached(shield_06C2); (void)c; p->invuln106 = 500; }   /* LAB_0672 */
        else { p->flicker108 = 100; p->invuln106 -= VBLS; if (p->invuln106 < 0) p->invuln106 = 0; }
    }
    if (p->flicker108) {                                           /* LAB_0673 */
        p->flicker108 -= VBLS; if (p->flicker108 < 0) p->flicker108 = 0;
        if (p->flicker108 & 8) o->flags367 |= F_HIT_FLASH;
    }
    p->x = o->x; p->y = o->y; p->z = o->z;                          /* 70/72/74 (words; 16.16 natively) */
    return step(o);
}
/* LAB_0687 / LAB_0669 / LAB_0654: player object end: alive = 0, unlink box, release gfx, LAB_04DC (no enemy cleanup) */
static void player_end(Obj *o, uint16_t set) { struct Player *p = REC(o); p->alive = 0; p->obj = NULL; box_unlink(o); gfx_release(o, set); eng_free(o); }

/* LAB_067C @ $215078: dust cloud (jeep): set $42 anim then die */
static const int16_t DUST_ANIM[] = { A_RATE(4), 0x4200, 0x4400, 0x4600, 0x4800, 0x4a00, A_END_SIGNAL, A_END };
static void dust_067C(Obj *o) {
    enemy_init(o, 0x4200, 0, -16, 0, 0, 0); o->cb534 = NULL;
    stop(o); o->z = 0; o->margin = 0; o->animA.flags |= 1;
    anim_start(o, DUST_ANIM); set_frame(o, 0x4200);
    wait_signal(o);
}
/* LAB_0771 @ $216AB8: bonus-zone marker sprite (copied from bh_group4.c's unused static) */
static const uint16_t ZONE_FRAMES_0773[8] = { 0x5000, 0x5200, 0x5400, 0x5600, 0x5000, 0x5200, 0x5400, 0x5600 };
static void zone_popup_0771(Obj *o) {
    o->animA.flags |= 0x41; o->flags367 |= F_NO_SHADOW; stop(o);
    set_frame_table8(o, ZONE_FRAMES_0773);
    o->cb538_disabled = 1;                                          /* ST 538(A5); MOVE.W #$140,418(A5) has no native field */
    int sy = (int16_t)((o->y >> 16) - g.scroll3542);
    if (sy >= 0 && sy < 256) { wait_ticks(o, 1); o->y -= 320 << 16; wait_ticks(o, 1); }
    eng_free(o);
}
/* LAB_065B @ $214d66: start position x=160, y=3542+192, then scan (16 columns x 12 rows of 8 px) for a spot where the
 * sprite does not overlap the background (LAB_030E); z = 0, stop */
static void start_pos_065B(Obj *o) {
    o->x = 160 << 16; o->y = (int32_t)(int16_t)(g.scroll3542 + 192) << 16;
    for (int d0 = 15; d0 >= 0; d0--) {
        int hit = 0;
        for (int d1 = 11; d1 >= 0; d1--) { if (!(hit = pixel_test(o, 0))) break; o->y -= 8 << 16; }
        if (!hit) break;
        o->y += 96 << 16; o->x += 8 << 16;
    }
    o->z = 0; stop(o);
}

/* =====================================================================================================================
 * HELICOPTER  LAB_0681 @ $215130
 * ===================================================================================================================== */
static const int16_t HELI_ANIM[] = { A_RATE(1), A_SETLOOP(0), 0x0000, 0x0200, 0x0000, 0x0400, 0x0000, 0x0600, 0x0000, 0x0800, A_LOOP, A_END };
/* LAB_067D @ $2150bc: the heli's gun -- an invisible attached child (never steps): fires straight up (angle $C0) while
 * joystick bit5 is set, follows the parent's x,y,z+1, frees itself when the parent is gone or at game over */
static void heli_gun_067D(Obj *o) {
    o->angle = 0xc0;
    for (;;) {
        if (g.game_over160) { eng_free(o); return; }
        if (REC(o)->joy & 0x20) fire_0640(o);
        Obj *par = o->parent; if (!par) { eng_free(o); return; }
        SET_HI(o->x, XW(par)); SET_HI(o->y, YW(par)); SET_HI(o->z, ZW(par) + 1);
        yield_once(o);
    }
}
static void heli_0681(Obj *o) {
    struct Player *p = REC(o); p->obj = o; o->name = "heli";
    gfx_acquire(o, 0x0000);
    start_pos_065B(o);
    SET_HI(o->z, 32);
    p->flicker108 = 200;
    box_register(o, p->vehicle == 0 ? 0x0048 : 0x0088);            /* LAB_053D inline mask (heli record: 58 = 0 -> $0048) */
    o->flags367 |= F_SCREEN_LOCKED;
    on_event(o, 1, die_0676);                                      /* LAB_0506: event bit1 = touched a heli-killer */
    Obj *c = spawn_attached(heli_gun_067D); if (c) { set_rec(c, p); c->name = "heligun"; }
    anim_start(o, HELI_ANIM); set_frame(o, 0x0000);
    o->speed = 0x300;                                              /* 3 px/VBL */
    for (;;) {                                                     /* LAB_0684 */
        clamp_0692(o);
        int a;
        if (joy_angle(p->joy, &a)) { o->angle = (uint8_t)a; set_velocity_from_angle(o); }
        else { o->vx = 0; o->vy = 0; }
        if (g.game_over160) break;
        if (player_tick_0670(o)) break;
    }
    player_end(o, 0x0000);                                         /* LAB_0687 */
}

/* =====================================================================================================================
 * JEEP  form A LAB_065F @ $214db0 (land jeep, frames $2200+dir8, speed $280) and form B LAB_0649 @ $214b46 (frames
 * $3200+dir8, speed $300); they swap into each other at the SWAP.LIN markers (LAB_0688/LAB_0689: A -> B when y-64 <= 3556,
 * B -> A when y-64 <= 3552).  w[2] = 280 (dust/zone counter), w[3] = 282 (auto-jump countdown).
 * ===================================================================================================================== */
/* LAB_0638 @ $214708: jeep turret, attached child (flags $0d), sits at an offset (LAB_063E/063F) by the jeep's dir8,
 * z = jeep z + 1, aims with the joystick when the button is up, locks while held (bit7) and fires (bit5) */
static const int16_t TURRET_DX_063E[8] = { -7, -5, 0, 5, 7, 5, 0, -5 };
static const int16_t TURRET_DY_063F[8] = { 0, -5, -7, -5, 0, 5, 7, 5 };
static void jeep_turret_0638(Obj *o) {
    o->angle = 0xc0; o->animA.flags |= 1; o->flags367 |= F_NO_SHADOW | F_FLASH_WITH_PARENT | F_ATTACHED;
    set_frame(o, 0x1e00);
    SET_HI(o->vz, 1);
    for (;;) {                                                     /* LAB_0639 */
        if (g.game_over160) { eng_free(o); return; }
        Obj *par = o->parent; if (!par) { eng_free(o); return; }   /* (MOVEA.L 308(A5) with 0 would fault; the child dies via cb542 anyway) */
        int d = ((par->angle + 16) & 0xe0) >> 5;
        SET_HI(o->vx, TURRET_DX_063E[d]); SET_HI(o->vy, TURRET_DY_063F[d]);
        int joy = REC(o)->joy;
        if (!(joy & 0x80)) {                                       /* button up: aim with the stick (or keep the jeep's heading) */
            int a; if (!joy_angle(joy, &a)) a = par->angle;
            o->angle = (uint8_t)((a + 16) & 0xe0);
            set_frame_dir8(o, 0x1200);
        } else if (joy & 0x20) {                                   /* LAB_063B: fire (vx,vy words saved/restored around it) */
            int32_t vx = o->vx, vy = o->vy; fire_0640(o); SET_HI(o->vx, vx >> 16); SET_HI(o->vy, vy >> 16);
        }
        if (step(o)) { eng_free(o); return; }                      /* LAB_063C/LAB_063D */
    }
}
/* LAB_0688 / LAB_0689 @ $2151e2/$2151f0: vehicle swap at the marker: if y-64 <= marker y (unsigned) spawn the other form
 * (prio 100, LAB_04CD copies pos/w[0..7]) and signal self */
static void jeep_A_065F(Obj *o); static void jeep_B_0649(Obj *o);
static void swap_check(Obj *o, int toB) {
    uint16_t marker = toB ? (uint16_t)g.g3556 : (uint16_t)g.g3552;
    uint16_t d2 = (uint16_t)(YW(o) - 64);
    if (d2 > marker) return;
    Obj *c = eng_spawn(o, toB ? jeep_B_0649 : jeep_A_065F, 100); (void)c;
    eng_signal(o);
}
/* the two forms differ only in a handful of constants */
typedef struct { uint16_t gfx0, frame_base; int16_t speed, jump_speed; int32_t bump_add, bump_az; int toB; int dust_while_moving; int friction_when_idle; } JeepForm;
static const JeepForm JEEP_A = { 0x2e00, 0x2200, 0x0280, 0x0380, 0x4000, (int32_t)0xfffff000, 1, 0, 0 };
static const JeepForm JEEP_B = { 0x3e00, 0x3200, 0x0300, 0x0400, 0x8000, (int32_t)0xfffff400, 0, 1, 1 };

static void jeep_common(Obj *o, const JeepForm *f) {
    struct Player *p = REC(o); p->obj = o; o->name = f->toB ? "jeep" : "jeepB";
    gfx_acquire(o, f->gfx0); set_frame(o, f->gfx0);
    start_pos_065B(o);
    {   /* LAB_0660 / LAB_064A: respawn at the marker if it is on screen (marker y - 3542 < 240) */
        uint16_t my = f->toB ? (uint16_t)g.g3552 : (uint16_t)g.g3556, mx = f->toB ? (uint16_t)g.g3550 : (uint16_t)g.g3554;
        if ((uint16_t)(my - g.scroll3542) <= 0xf0) { SET_HI(o->x, mx); SET_HI(o->y, my); g.g3548 = -1; }
    }
    o->w[2] = 1; o->w[3] = 15;
    p->flicker108 = 200;
    box_register(o, p->vehicle == 0 ? 0x0050 : 0x0090);            /* LAB_053D inline: 58(rec) = 0 -> $0050, else $0090 (jeep record 58 = 1) */
    on_event(o, 2, die_0676);                                      /* LAB_0507: event bit2 = touched a jeep-killer */
    o->animA.flags |= 1; o->angle = 0xc0;
    if (!f->toB) stop(o);                                          /* LAB_0649 only */
    Obj *c = spawn_attached(jeep_turret_0638); if (c) { set_rec(c, p); c->name = "turret"; }
    set_frame(o, f->gfx0);
    p->alive = 1;                                                  /* ST 54(A4) */
    o->speed = f->speed;
    for (;;) {                                                     /* LAB_0662 / LAB_064C */
        swap_check(o, f->toB);
        clamp_068C(o);
        int joy = p->joy;
        int jump = (joy & 0x40) || (--o->w[3] < 0);                /* double-tap, or blocked for 15 ticks */
        if (!jump) {
            int a;
            if (joy_angle(joy, &a)) {
                o->angle = (uint8_t)a; set_velocity_from_angle(o); set_frame_dir8(o, f->frame_base);
                if (f->dust_while_moving && --o->w[2] == 0) { o->w[2] += 3; spawn(dust_067C); }   /* LAB_0649: dust every 3 ticks */
                if (o->z == 0) {                                   /* bumpy ride: random upward vz fraction, small gravity */
                    SET_LO(o->vz, (rng() & 0x7fff) + f->bump_add); o->az = f->bump_az;
                }
                if (!f->dust_while_moving && g.zone154) {          /* LAB_0663 (form A only): bonus-zone markers */
                    uint16_t y = (uint16_t)YW(o);
                    if (!(y > (uint16_t)g.zone150) && !(y < (uint16_t)g.zone152)) {
                        o->w[2] -= VBLS; if (o->w[2] < 0) { o->w[2] += 3; spawn(zone_popup_0771); }
                    }
                }
            } else {                                               /* LAB_0665 / LAB_064F: no direction */
                if (f->friction_when_idle) { o->vx -= o->vx >> 3; o->vy -= o->vy >> 3; if ((g.rng11172 & 15) == 0) spawn(dust_067C); }
                else { o->vx = 0; o->vy = 0; }
                o->w[3] = 5;
            }
            /* LAB_0666 / LAB_0651 */
            if (g.game_over160) break;
            if (blocked_067A(o)) { o->vx = 0; o->vy = 0; } else o->w[3] = 15;
            if (player_tick_0670(o)) break;
            continue;
        }
        /* ---- JUMP  LAB_066A / LAB_0655 ---- */
        o->w[3] = 15;
        o->box.mask = (o->box.mask & ~0x10) | 0x08;                /* airborne: becomes a heli-class box */
        on_event(o, 1, die_0676); off_event(o, 2);                 /* LAB_0506 + LAB_050D */
        sfx(SFX_JUMP, XW(o));                                      /* LAB_03D3 jeep jump */
        o->speed = f->jump_speed; o->vz = 0x1d000; o->az = (int32_t)0xfffff000;
        for (;;) {                                                 /* LAB_066B / LAB_0656 */
            swap_check(o, f->toB);
            uint8_t saved_angle = o->angle;
            clamp_068C(o);
            int a;
            if (joy_angle(p->joy, &a)) { o->angle = (uint8_t)a; set_velocity_from_angle(o); } else { o->vx = 0; o->vy = 0; }
            o->angle = saved_angle;                                /* the sprite keeps its heading in the air */
            if (blocked_067A(o) && blocked_0679(o)) { o->vx = 0; o->vy = 0; }
            if (player_tick_0670(o)) goto dead;
            if (ZW(o) == 0) break;                                 /* landed (step() zeroes z/vz/az when z < 0) */
        }
        o->vz = 0xa000; o->az = (int32_t)0xfffff000;               /* LAB_066F / LAB_065A: small bounce */
        o->box.mask = (o->box.mask & ~0x08) | 0x10;
        on_event(o, 2, die_0676); off_event(o, 1);                 /* LAB_0507 + LAB_050C */
        if (!f->toB) spawn(dust_067C);                             /* LAB_0649: landing dust */
        if (g.game_over160) break;                                 /* -> LAB_0666 / LAB_0651 tail */
        if (blocked_067A(o)) { o->vx = 0; o->vy = 0; } else o->w[3] = 15;
        if (player_tick_0670(o)) break;
    }
dead:
    player_end(o, f->gfx0);                                        /* LAB_0669 / LAB_0654 */
}
static void jeep_A_065F(Obj *o) { jeep_common(o, &JEEP_A); }
static void jeep_B_0649(Obj *o) { jeep_common(o, &JEEP_B); }

/* =====================================================================================================================
 * Player manager LAB_0556 / LAB_055A.. (one task per record, prio 1)
 * ===================================================================================================================== */
/* LAB_056D @ $212ea4: build the joystick word 64(rec) from the raw input: dir nibble (bit0 up, 1 down, 2 left, 3 right),
 * bit7 = raw button, bit6 = double-tap (LAB_057A), bit5 = fire after the cooldown (LAB_057D) */
static int read_input_056D(struct Player *p) {
    int d0 = 0; int idx = (p == &g.jeep) ? player2_input_dx : player_input_dx, idy = (p == &g.jeep) ? player2_input_dy : player_input_dy, ifire = (p == &g.jeep) ? player2_input_fire : player_input_fire;
    if (idy < 0) d0 |= 1;
    if (idy > 0) d0 |= 2;
    if (idx < 0) d0 |= 4;
    if (idx > 0) d0 |= 8;
    if (ifire) d0 |= 0x80;
    p->joy66 = (int16_t)d0;
    /* LAB_057A: double-tap detector on the direction nibble */
    int d7 = d0 & 15;
    if (p->joy90 != d7) {
        p->joy94 = 5; int d2 = p->joy92; p->joy92 = p->joy90; p->joy90 = (int16_t)d7;
        if (d7 != 0 && p->joy92 == 0 && d2 == d7) d0 |= 0x40;
    } else if (--p->joy94 == 0) { p->joy90 = p->joy92 = -1; }
    /* LAB_057D: fire cooldown 96, period 98 (>= 10 when the other player has joined) */
    if (p->fire_cd == 0) {
        if (((g_autofire12484 ^ d0) & 0x80)) {
            d0 |= 0x20;
            int d1 = p->rate98; if (OTHER(p)->joined55 && d1 < 10) d1 = 10;
            p->fire_cd = d1;
        } else goto done;
    }
    p->fire_cd -= VBLS; if (p->fire_cd < 0) p->fire_cd = 0;
done:
    p->joy = (int16_t)d0;
    return d0;
}
static const uint8_t WEAPON_0561[4][2] = { {2, 11}, {3, 10}, {4, 10}, {5, 8} };   /* $212de0: {level cap, fire period} by 102/5 */
/* LAB_056A @ $212e76: spawn the player object (prio 99 per trace; LAB_04C6 zeroes pos/vel/acc, clock 202) */
static void spawn_player_056A(struct Player *p) {
    p->x = p->y = p->z = 0;
    Obj *o = eng_spawn_at(p->no != 0 ? heli_0681 : jeep_A_065F, 99, NULL);
    if (!o) return;
    eng_set_clock(o, &g.clock202);
    set_rec(o, p); p->obj = o;
}
static void manager_055A(Obj *o) {
    struct Player *p = REC(o);
    /* LAB_055A: wait for the join (natively: player_start() joins the heli at once; the jeep joins when its record's
     * joined55 is set by the frontend) */
    while (!p->joined55) yield_once(o);                               /* either port joins on fire (LAB_055A) */
    p->joined55 = -1;
    for (;;) {
        /* LAB_055D: new game for this player */
        p->lives68 = -16; p->score = 0; p->next_life84 = 10000;
        for (;;) {                                                 /* LAB_055E */
            if (g.game_over160 || p->lives68 == 0) break;
            p->lives68 += g_lives_step12524;
            /* LAB_0562+2 */
            spawn_player_056A(p);
            p->alive = 1;                                          /* ST 54(A4) */
            { int i = (uint16_t)p->power102 / 5; if (i > 3) i = 3;
              if (WEAPON_0561[i][0] < p->level100) p->level100 = WEAPON_0561[i][0];
              p->rate98 = WEAPON_0561[i][1]; }
            for (;;) {                                             /* LAB_0564 */
                if (p->time110 < 0x7fff) p->time110++;
                /* LAB_0593: HUD update (not ported) */
                if ((uint32_t)p->score >= (uint32_t)p->next_life84) { p->next_life84 += 30000; p->lives68 -= 4; /* LAB_0421 extra-life sfx */ }
                if (!p->alive) break;
                read_input_056D(p);                                /* LAB_056D */
                yield_once(o);
            }
            p->time110 = 0;                                        /* LAB_0568 */
            if (!g.game_over160) yield_vbls(o, 100);               /* LAB_049D(100): respawn delay */
        }
        /* LAB_055F: game over for this player */
        if ((uint32_t)p->score > (uint32_t)p->hiscore80) p->hiscore80 = p->score;
        p->joined55 = 0;
        while (!p->joined55) yield_once(o);                        /* wait for a new join (LAB_055A) */
        p->joined55 = -1;
    }
}
/* LAB_0556: record init (done by eng_init for name/no/vehicle) + manager task; LAB_0558/0559: per-level weapon reset */
static void record_init_0556(struct Player *p) {
    p->alive = 0; p->joined55 = 0; p->joy = 0; p->score = 0; p->hiscore80 = 0; p->fire_cd = 0;
    p->spread104 = 0; p->level100 = 2; p->rate98 = 12; p->power102 = 0; p->time110 = 0; p->invuln106 = 0; p->flicker108 = 0;
    p->joy90 = p->joy92 = -1; p->joy94 = 0; p->obj = NULL;
}
void player_start(void) {
    memset(pb_tbl, 0, sizeof pb_tbl);
    record_init_0556(&g.heli); record_init_0556(&g.jeep);
    Obj *t = eng_spawn_at(bullet_task, -2, NULL); if (t) t->name = "pbullets";      /* LAB_04A4, prio -2 */
    Obj *mh = eng_spawn_at(manager_055A, 1, NULL); if (mh) { set_rec(mh, &g.heli); mh->name = "mgr_heli"; }
    Obj *mj = eng_spawn_at(manager_055A, 1, NULL); if (mj) { set_rec(mj, &g.jeep); mj->name = "mgr_jeep"; }
}
