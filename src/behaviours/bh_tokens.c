/* bh_tokens.c -- the weapon power-up TOKEN (LAB_06AA @ $2153F8 .. LAB_06BA, re/amprog.asm).
 *
 * Spawned by the goose boss (LAB_0866, bh_group6.c) with the angle preset by the caller; the
 * token itself sets its speed.  gfx $0018 = TOKEN.LIN set $18, symbol frames $0218..$0A18.
 * Translated 1:1 from the 68000 listing; see the LAB_xxxx @ $addr comments.
 */
#include "../engine/engine.h"
#define XI(o)  ((int16_t)((o)->x >> 16))

/* LAB_06B1 @ $2154DC: symbol frame per w[0] (276): 0 straight, 1 spread, 2 rapid fire, 3 bonus, 4 star (8-way) */
static const uint16_t TOKEN_SYMBOL_06B1[5] = { 0x0218, 0x0418, 0x0618, 0x0818, 0x0A18 };

/* LAB_06B0 @ $2154CE: set_frame(TOKEN_SYMBOL[w276]) */
static void token_symbol_06B0(Obj *o) { set_frame(o, TOKEN_SYMBOL_06B1[(uint16_t)o->w[0] % 5]); }

/* LAB_06AE @ $2154A0: hit by a player bullet (event bit0).  Knock it 8 px up; when the shot
 * counter 278 has run negative, re-arm it (4) and cycle the symbol 0..3; every full cycle counts
 * down 280 (12 cycles), after which the symbol becomes the yellow star (4) and the counter gets +8. */
static void token_shot_06AE(Obj *o) {
    o->y -= PX(8);                                           /* SUBQ.W #8,324(A5) */
    if (o->w[1] >= 0) return;                                /* TST.W 278 ; BPL */
    o->w[1] = 4;
    o->w[0] = (o->w[0] + 1) & 3;
    if (o->w[0] != 0) return;
    if (--o->w[2] != 0) return;
    o->w[0] = 4;                                             /* star: super 8-way fire */
    o->w[1] += 8;
}

/* LAB_06B2 @ $2154E6: touched by a player (events bit3/bit4) -> grant the power-up, signal self. */
static void token_touched_06B2(Obj *o) {
    g.stat_tokens12490++;                                    /* ADDQ.L #1,12490(A6) */
    sfx(SFX_WEAPON_TOKEN, XI(o));                            /* LAB_0423+6: weapon-token jingle */
    struct Player *p = (o->box.hits & 0x40) ? &g.heli : &g.jeep;   /* bit6 = heli's box */
    if (++p->power102 >= 0x14) p->power102--;                /* 102 saturates at 19 */
    switch (o->w[0]) {                                       /* LAB_06B4 */
    case 1: case 0: default: {                               /* 1 -> spread (-1); 0 / 5+ -> straight (0) */
        int8_t d0 = (o->w[0] == 1) ? -1 : 0;
        int8_t old = (int8_t)p->spread104;                   /* LAB_06B6: 104 is a byte */
        p->spread104 = d0;
        if (old == d0 && (uint16_t)p->level100 < 5) p->level100++;   /* same mode again -> one more bullet */
        break; }
    case 2:                                                  /* LAB_06B8: rapid fire */
        p->rate98 -= 3; if (p->rate98 < 8) p->rate98 = 8;
        break;
    case 3:                                                  /* LAB_06B9: bonus: 500 points + 500 VBLs of flicker (108) */
        p->flicker108 += 0x1f4; p->score += 0x1f4;
        break;
    case 4:                                                  /* LAB_06BA: yellow star: 8-way fire, fastest rate, smart bomb */
        p->level100 = 6; p->rate98 = 8; smart_bomb(o);       /* LAB_062B */
        break;
    }
    eng_signal(o);                                           /* LAB_06B7: JMP -1414(A6) */
}

/* LAB_06AA @ $2153F8: the token.  Flies out (speed $140, caller's angle, 32 VBLs), then sits,
 * drifting down 0.5 px/VBL, x clamped 8..$138, flicking between its symbol and the blank frame. */
void bh_token_06AA(Obj *o) {
    enemy_init(o, 0x0018, 32, -16, 0, 0, 5);                 /* solid, shootable but harmless, no hp */
    o->flags367 |= F_NO_SHADOW | F_SCREEN_LOCKED;            /* ORI.B #$11,367 */
    o->cb534 = NULL;                                         /* ST 534: smart-bomb immune */
    o->cb538_disabled = 1;                                   /* ST 538: no off-screen kill while flying out */
    stop(o);                                                 /* LAB_053A */
    off_event(o, EV_TOUCH_JEEP); off_event(o, EV_TOUCH_HELI);   /* LAB_050E */
    off_event(o, EV_BULLET);                                 /* LAB_050B */
    o->w[0] = 3; o->w[2] = 12;
    token_symbol_06B0(o);
    o->speed = 0x140; set_velocity_from_angle(o);            /* LAB_0515 */
    o->vy -= PX(1);                                          /* SUBQ.W #1,336: vy high word */
    if (wait_vbls(o, 32)) return;                            /* LAB_04DE(32) */
    on_touch_any_player(o, token_touched_06B2);              /* LAB_0508 */
    on_event(o, EV_BULLET, token_shot_06AE);                 /* LAB_0505 */
    stop(o);
    o->vy = 0x8000;                                          /* MOVE.W #$8000,338: vy low word = 0.5 px/VBL down */
    { int x = XI(o); if (x <= 8) x = 8; if (x > 0x138) x = 0x138; o->x = PX(x); }   /* LAB_06AB/06AC */
    o->cb538_disabled = 0;                                   /* SF 538: off-screen = die */
    o->w[1] = 0;
    for (;;) {                                               /* LAB_06AD */
        o->w[1]--;
        token_symbol_06B0(o);
        step(o);                                             /* LAB_04E9 (result ignored) */
        set_frame(o, 0x0018);
        if (step(o)) return;                                 /* LAB_04E9 ; BEQ loop ; BRA LAB_0725 */
    }
}
