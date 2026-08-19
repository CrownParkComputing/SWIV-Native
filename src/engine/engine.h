/* engine.h -- native SWIV object engine: a 1:1 C rendering of the Sales Curve
 * Kernel object model + the AMPROG verb library (see re/VERBS.md, re/OBJECT.md).
 *
 * Every behaviour script runs as a coroutine on its own stack with `o` = the
 * current object (the 68000 A5).  Verbs that wait return int: 0 = completed
 * (68k EQ), nonzero = the object was signalled/killed (68k NE) -> script should
 * `return;` (the engine performs LAB_0725 cleanup when a script returns).
 *
 * Units: positions/velocities/accelerations 16.16 px; speed 8.8 px/VBL;
 * angle 0..255 (0 = right, 64 = down, 128 = left, 192 = up); y is in MAP units
 * (screen y = y - g.scroll3542); smaller y = further ahead.  A tick = one game
 * update = g.vbl_per_tick VBLs (2 in play).
 */
#ifndef SWIV_ENGINE_H
#define SWIV_ENGINE_H
#include <stdint.h>
#include "../swivdata.h"

typedef struct Obj Obj;
typedef void (*Script)(Obj *o);        /* behaviour handler; runs as a coroutine */
typedef void (*Callback)(Obj *o);      /* event / per-tick callbacks; run inline */

/* animation slot (OBJECT.md 380..399 / 422..441) */
typedef struct {
    const int16_t *loop0, *rd; int count, countdown, rate, user, active; uint8_t flags; uint16_t frame;
} Anim;

/* collision box (OBJECT.md 488..507) */
typedef struct { int x, y, hw, hh; uint16_t mask, hits; int linked; } Box;

struct Obj {
    /* kernel */
    Obj *next, *prev; int alive; int prio; int *clock; int time12; void *coro; Script script; int started;
    /* script params/locals 276..291 (copied to children) + 292..307 */
    int16_t w[16];            /* w[0] = 276 type param, w[1]=278 ... w[7]=290 copied; w[8..15] = 292..306 not copied */
    /* hierarchy */
    Obj *parent, *child, *sib;
    /* motion (16.16) */
    int32_t x, y, z, vx, vy, vz, ax, ay, az;
    int16_t speed;            /* 356, 8.8 px/VBL */
    uint8_t angle;            /* 358 */
    /* combat */
    int16_t hp, score, margin; /* 360, 362, 364 */
    uint8_t flags367;         /* NO_SHADOW 1, HIT_FLASH 2, FLASH_WITH_PARENT 4, ATTACHED 8, SCREEN_LOCKED 16 */
    uint16_t gfxset;          /* 368 graphic-set word (refcount) */
    int16_t threat;           /* 370 */
    int16_t scroll372;
    uint16_t popup374;        /* bonus popup gfx on death */
    Script death376;          /* death-effect script (default explosion) */
    Anim animA, animB;        /* animA.flags = 397 render flags, animA.frame = 398 gfx; animB.active = 438 */
    int timer486;
    Box box;                  /* 488.. ; box.mask = 504, box.hits = 506 */
    uint16_t enable508;
    Callback h510, h514, h518, h522, h526, h530;   /* event handlers bit0, bit3, bit1, bit4, bit2, bit5 */
    Callback cb534, cb538, cb542;                 /* smart-bomb, off-screen, orphan/think (NULL = none) */
    int cb538_disabled;
    /* native extras */
    int id; const char *name;
};

enum { F_NO_SHADOW = 1, F_HIT_FLASH = 2, F_FLASH_WITH_PARENT = 4, F_ATTACHED = 8, F_SCREEN_LOCKED = 16 };

/* globals (A6) */
typedef struct {
    SwivDisk *disk;
    int vbl;                 /* -72 VBL counter */
    int tick;                /* game ticks (scheduler passes) */
    int vblcount;            /* -66/-68: advances every VBL during play */
    int vbl_per_tick;        /* -76 (2) */
    int clock202, clock204;   /* generation clocks (-1418 compares obj time12 against *clock) */
    int threat156;           /* sum of live threat */
    int game_over160, paused165, alternate168, smart_bomb169, difficulty182, missile_budget206;
    long stat_spawned12494, stat_destroyed12498, stat_tokens12490;   /* game-over statistics (LAB_00B3): enemies spawned / destroyed / tokens */
    uint16_t scroll3530, scroll3542, cursor3586;  /* map scroll (counts down), as u16 like the original */
    uint32_t rng11172;
    int boss140; uint8_t flags166;
    int render_gate155;
    int32_t jeep_limit3558;
    int stat_shots, stat_hits;   /* native stats (totals) */
    int stat_shots_p[2], stat_hits_p[2];   /* per player: [0] heli, [1] jeep */
    /* respawn / bonus-zone globals used by JEEPHELI#23/#31/#43 and MEDTANK dust */
    int g3548, g3550, g3552, g3554, g3556; int zone150, zone152, zone154; int flag3615, flag3616;
    /* player records */
    struct Player { int alive; int no; int vehicle; int16_t joy; int32_t x, y, z; int score; int fire_cd; int invuln106, flicker108; Obj *obj; const char *name;
        /* player.c (LAB_0556/055A manager + LAB_057D input): 98 fire period (VBLs), 100 weapon level (bullets/shot, 6 = 8-way),
         * 102 power counter (0..19), 104 spread flag (-1 = per-bullet velocities), 68 lives (-4 units, 0 = none left), 110 time alive,
         * 55 joined, 80 hiscore, 84 next extra life, 66 raw joystick, 90/92/94 double-tap detector */
        int16_t rate98, level100, power102, spread104, lives68, time110, joined55; int32_t hiscore80, next_life84; int16_t joy66, joy90, joy92, joy94;
    } heli, jeep;
} Globals;
extern Globals g;

/* ---------- object lifetime / scheduling ---------- */
Obj *eng_spawn_at(Script s, int prio, Obj *from);   /* LAB_04D2 semantics: copies pos/vel/acc/speed/angle/w[0..7]/clock from `from` (may be NULL) */
#define spawn(s)            eng_spawn(o, (s), 100)            /* LAB_04D1 / LAB_04CD (always succeeds natively) */
#define spawn_prio(s, p)    eng_spawn(o, (s), (p))            /* LAB_04D2 */
#define spawn_attached(s)   eng_spawn_attached(o, (s))        /* LAB_04C9 / LAB_04CA */
Obj *eng_spawn(Obj *o, Script s, int prio);
Obj *eng_spawn_attached(Obj *o, Script s);
void eng_free(Obj *o);                  /* LAB_04DC: detach children, unlink, free (script must return after) */
void eng_signal(Obj *o);                /* LAB_053B / -1414 */
int  eng_signalled(const Obj *o);       /* -1418 != 0 */
void eng_set_clock(Obj *o, int *clock); /* -1422 */

/* formation: LAB_071D -- spawn count-1 clones of the CALLER's continuation.  In C the
 * continuation is `cont` (a Script that the clones start at); the caller keeps going. */
void formation(Obj *o, int dx, int dy, int count, int dparam, Script cont);

/* ---------- waits (return 0 = completed, nonzero = signalled) ---------- */
int  step(Obj *o);                      /* LAB_04E9: one tick with physics */
int  wait_ticks(Obj *o, int n);         /* LAB_04E4: n STEPS (each step = vbl_per_tick VBLs) */
int  wait_vbls(Obj *o, int n);          /* LAB_04DE/LAB_04DD: step until the VBL counter (-66) has advanced n = n VBLs = n/2 steps */
int  wait_signal(Obj *o);               /* LAB_04E8: until signalled (always returns nonzero) */
int  wait_onscreen(Obj *o, int margin); /* LAB_06D5: step until y >= scroll3530 + margin */
int  wait_onscreen_noevents(Obj *o, int margin);  /* LAB_06D4 */
int  wait_onscreen_inert(Obj *o, int margin);     /* LAB_06D0: plain yields, no physics/render */
void yield_once(Obj *o);                /* LAB_0499 */
int  yield_n(Obj *o, int n);            /* LAB_049F: n plain yields (n passes) */
int  yield_vbls(Obj *o, int n);         /* LAB_049D/049C: plain yields until the VBL counter (-68) advanced n */
#define PX(n) ((int32_t)((uint32_t)(n) << 16))   /* integer px -> 16.16 (negative-safe) */

/* ---------- enemy init / death ---------- */
void enemy_init(Obj *o, uint16_t gfx, uint16_t mask, int margin, int hp, int score, int threat);  /* LAB_0720 (may yield) */
void enemy_cleanup(Obj *o);             /* LAB_0724 */
void on_bullet_hit(Obj *o);             /* LAB_0728 (default bit0/3/4 handler installed by enemy_init) */
void eng_kill(Obj *o);                      /* LAB_0729: score, death effect, popup, signal */
/* death effect scripts (src/engine/effects.c) */
void fx_explosion(Obj *o);              /* LAB_0634 */
void fx_explosion_silent(Obj *o);       /* LAB_0635 */
void fx_wreck(Obj *o);                  /* LAB_062D burning wreck */
void fx_ring8(Obj *o);                  /* LAB_062F */
void fx_ring16(Obj *o);                 /* LAB_0630 */
void fx_popup(Obj *o);                  /* LAB_0636 (w[0] = gfx) */
void explode_at(Obj *o, int dx, int dy);/* LAB_07D0: spawn wreck at pos+(dx,dy), wait 20 ticks */
void boss_smoke(Obj *o);                /* LAB_07E3 */

/* ---------- events ---------- */
void on_event(Obj *o, int bit, Callback h);   /* bit 0,1,2,3,4,5 -> slots 510/518/526/514/522/530 (LAB_0505..050A) */
void on_touch_any_player(Obj *o, Callback h); /* LAB_0508: bits 3+4 */
void off_event(Obj *o, int bit);              /* LAB_050B.. */
#define EV_BULLET 0
#define EV_JEEP_KILLER 1
#define EV_HELI_KILLER 2
#define EV_TOUCH_JEEP 3
#define EV_TOUCH_HELI 4
#define EV_SOLID 5

/* ---------- motion / aiming ---------- */
void stop(Obj *o);                                  /* LAB_053A */
void set_velocity_from_angle(Obj *o);               /* LAB_0515 */
int  sin256(int a); int cos256(int a);              /* LAB_051F table */
int  angle_to(int tx, int ty, int sx, int sy);      /* LAB_0516 */
void turn_towards(Obj *o, int tx, int ty, int maxstep);   /* LAB_0510 (0 = snap) */
void set_frame(Obj *o, uint16_t gfx);               /* LAB_0538 */
void set_frame_dir8(Obj *o, uint16_t base);         /* LAB_071B */
void set_frame_dir16(Obj *o, uint16_t base);        /* LAB_071C */
void set_frame_table8(Obj *o, const uint16_t *t);   /* LAB_0719 */
void set_frame_table16(Obj *o, const uint16_t *t);  /* LAB_071A */
int  nearest_player(int *tx, int *ty);              /* LAB_058A: returns 1 if a player is alive */
int  alternate_player(int *tx, int *ty);            /* LAB_0587 */
int  prefer_heli(int *tx, int *ty);                 /* LAB_0581 */
int  prefer_jeep(int *tx, int *ty);                 /* LAB_0582 */
int  joy_to_angle(int joy, int *angle);             /* LAB_0697 */
int  blocked_ahead(Obj *o);                         /* LAB_067A: 2*(vx,vy) ahead, eng_terrain_test mode 0 */
int  eng_terrain_test(Obj *o, int mode);            /* LAB_030E (mode 0: flag-4 tiles) / LAB_030D (mode 1: flag-2 tiles): sprite mask vs level collision mask */
int  eng_terrain_at(int x, int y_orig, int mode);   /* one playfield pixel of the level collision mask */

/* ---------- firing ---------- */
void fire_homing(Obj *o, int dx, int dy, int angle);/* LAB_0613 */
void fire_pattern(Obj *o, int idx);                 /* LAB_0809 (waits 10 ticks total) */
void fire_missile_aimed(Obj *o);                    /* LAB_069C */
void fire_missile_ahead(Obj *o);                    /* LAB_069B (accelerating, parent's angle) */
void fire_missile_fast(Obj *o);                     /* LAB_069A */

/* ---------- graphics / anim ---------- */
void gfx_acquire(Obj *o, uint16_t set);             /* LAB_0493 (no-op wait natively) */
void gfx_release(Obj *o, uint16_t set);             /* LAB_0498 */
void anim_start(Obj *o, const int16_t *script);     /* LAB_0528 slot A */
void anim_start_b(Obj *o, const int16_t *script);   /* LAB_0527/LAB_0680 slot B (set o->animB.active) */
#define A_END 0x8000
#define A_END_SIGNAL ((int16_t)0x8800)
#define A_LOOP ((int16_t)0x9000)
#define A_SETLOOP(n) ((int16_t)(0x9800+(n)))
#define A_USER(n) ((int16_t)(0xA000+(n)))
#define A_USERADD(n) ((int16_t)(0xA800+(n)))
#define A_RATE(n) ((int16_t)(0xB000+(n)))
#define A_FLAGS_SET(n) ((int16_t)(0xB800+(n)))
#define A_FLAGS_CLR(n) ((int16_t)(0xC000+(n)))
void box_register(Obj *o, uint16_t mask);           /* LAB_053E/053D */
void box_unlink(Obj *o);                            /* LAB_0541 */

/* ---------- misc ---------- */
uint32_t rng(void);                                 /* LAB_0629 */
int  threat_ok(void);                               /* LAB_0625: 1 if threat156 <= 160 */
void smart_bomb(Obj *o);                            /* LAB_062B */
void screen_shake(Obj *o);                          /* LAB_0817 (yields twice) */
void boss_enter(void); void boss_leave(Obj *o);     /* LAB_07B4 / LAB_07B5 */
void sfx(int id, int x);                            /* LAB_03xx family: id = SFX_* */
enum { SFX_PICKUP, SFX_BIGEXPL, SFX_EXPL1, SFX_EXPL2, SFX_BOMB, SFX_HIT, SFX_SHOT, SFX_MISSILE, SFX_PLOP, SFX_JUMP, SFX_CANNON, SFX_ALARM, SFX_FLAME, SFX_EXTRALIFE,
       SFX_RICOCHET, SFX_JET, SFX_BOMB_WHISTLE, SFX_FIREBALL, SFX_BOSS_HIT, SFX_WARBLE, SFX_TURRET_SHOT, SFX_TAKEOFF, SFX_ENEMY_DESTROYED, SFX_TOKEN, SFX_HOMING, SFX_GAMESTART, SFX_WEAPON_TOKEN, SFX_COUNT };
extern const char *sfx_event_names[SFX_COUNT];

/* ---------- render list (filled by step) ---------- */
typedef struct { int key; uint16_t gfx; int x, y; uint8_t flags; } RenderEntry;
extern RenderEntry render_list[1024]; extern int render_count;   /* draw in DESCENDING key order */
Box *eng_extbox_alloc(void); void eng_extbox_free(Box *b);      /* player-bullet boxes */

/* ---------- engine driver ---------- */
void eng_init(SwivDisk *d, int level);
extern SwivMap eng_map;                 /* the level map (terrain for the renderer) */
const char *eng_handler_name(uint16_t gfx); int eng_handler_ported(uint16_t gfx);
void eng_spawn_map_object(int x, int mapy, uint16_t gfx, int type);   /* map interpreter -> LAB_02F2 */
void eng_run_tick(void);                /* one game tick: run every task until it yields, collision sweep, etc. */
void eng_vbl(void);                     /* one VBL: scroll + map interpreter; every vbl_per_tick VBLs runs a tick */
Obj *eng_first(void); Obj *eng_next(Obj *o);
#define FOR_EACH_OBJ(v) for (Obj *v = eng_first(); v; v = eng_next(v))
Script eng_handler_for_gfx(uint16_t gfx);   /* the dispatch table (behaviours/table.c) */
extern int eng_difficulty_mode;              /* native: 0 easy (half the enemies), 1 normal, 2 hard (1.5x aerial, 2x ground) */
int eng_is_air_gfx(uint16_t gfx);
void eng_register_handler(uint16_t gfx, Script s, const char *name);
#endif
