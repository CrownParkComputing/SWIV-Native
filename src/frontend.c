/* frontend.c -- SWIV front end (title / attract / game over / hi-score / ending), ported from the
 * 68000 code in AMPROG.OBJ.  Every screen is annotated with the LAB it comes from (re/amprog.asm,
 * disk-image addresses; runtime addresses differ per re/OBJECT.md).
 *
 * Display model (what the kernel + game do with the copper):
 *   screen[]   320x256 4-bit indices = the draw buffers 256/260(A6)   (text is drawn into both)
 *   bg[]       the third buffer 264(A6): pictures are loaded here (LAB_048E) and copied to the screen
 *              (LAB_048C); the text renderer restores each glyph cell from it (LAB_0465)
 *   pal[16]    11134(A6), set by LAB_034D; dark11170 = 11170(A6) (0 = full, 256 = black, +-16/VBL by
 *              the fade task LAB_024B); white11166 = 11166(A6) (fade to white, step 11168(A6))
 *   copper nodes: per-row colour changes (LAB_027A lists -> LAB_038F), applied top to bottom
 *   hud[]      the 5th bitplane strip (3604/3600(A6), LAB_046B/LAB_046F): rows 8..14, COLOR16 gradient
 *              88d aae ccf ccf ccf aae 88d, COLOR17..31 = 0 (black) -> text shows only over colour 0.
 * Sequence = LAB_00B2 main loop, run as a coroutine that yields once per VBL. */
#include "frontend.h"
#include "engine/coro.h"
#include "engine/engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define W 320
#define H 256
#define ORG 0x20BD20
static SwivDisk *disk; static const uint8_t *prog;
#define A(addr) (prog + ((addr) - ORG))
#define BE16(p) (uint16_t)(((p)[0] << 8) | (p)[1])

/* ---------------------------------------------------------------- display state ---- */
static uint8_t screen[W * H], bg[W * H];
static uint16_t pal[16];                       /* 11134(A6) */
static int dark11170, white11166, step11168 = -4, fade142;   /* 142(A6): +1 fade in, -1 fade out, 0 idle */
typedef struct { int row, idx, val; } CopNode;
static CopNode cop[512]; static int ncop;      /* LAB_038F nodes (list 11106(A6)) */
static uint8_t hud[W * 7];                     /* plane-5 strip, one byte per pixel (0/1) */
static int vbl;                                /* -66/-68(A6): the VBL counter (pauses during disk loads in the original) */

static void cop_clear(void) { ncop = 0; }
static void cop_add(int row, int idx, int val) { if (ncop < 512) cop[ncop++] = (CopNode){ row, idx, val }; }
/* LAB_027A: list = count, then {drow, colour-byte, low-byte}: reg = $180 + 2*(byte>>4), value = (byte&15)<<8 | low */
static void cop_list(int row, uint32_t addr) {
    const uint8_t *p = A(addr); int n = *p++;
    for (int i = 0; i < n; i++) { row += (int8_t)p[0]; cop_add(row, p[1] >> 4, ((p[1] & 15) << 8) | p[2]); p += 3; }
}
static void set_pal(uint32_t addr) { for (int i = 0; i < 16; i++) pal[i] = BE16(A(addr) + 2 * i); }   /* LAB_034D */
static void screen_clear(void) { memset(screen, 0, sizeof screen); memset(bg, 0, sizeof bg); }      /* LAB_0470 */
static void show_bg(void) { memcpy(screen, bg, sizeof screen); }                                     /* LAB_048C */
/* LAB_048E: load a 320x256x4 .RAW into the background buffer (the palette at its end is NOT used; the
 * game keeps its own palette tables at $20e6bc..) */
static void load_raw(const char *name) {
    int i = swiv_find(disk, name); uint32_t n; const uint8_t *d;
    if (i < 0 || !(d = swiv_load(disk, i, &n)) || n < 40960) { memset(bg, 0, sizeof bg); return; }
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int v = 0; for (int pl = 0; pl < 4; pl++) if (d[pl * 40 * 256 + y * 40 + (x >> 3)] & (0x80 >> (x & 7))) v |= 1 << pl;
        bg[y * W + x] = (uint8_t)v;
    }
}
/* LAB_03A7: scale each nibble by (256-d1)/256; LAB_03A6: the same towards white */
static int scale_dark(int c, int d1) { int k = 256 - d1, r = 0; for (int s = 0; s < 12; s += 4) { int n = (c >> s) & 15; r |= ((n * k) >> 8) << s; } return r; }
static int scale_white(int c, int d1) { return (~scale_dark(~c & 0xfff, d1)) & 0xfff; }

/* ---------------------------------------------------------------- big font (LAB_08A0 @ $2190a4) ----
 * 16 bytes per char from ASCII $20: word width, 7 rows of 16-bit bitmaps (glyph right-aligned,
 * leftmost column = bit width-1).  LAB_0465 draws the glyph in colour with a 1-px black shadow to the
 * right after restoring the (width+1)-px cell from the background; LAB_046B (HUD mode) only ORs. */
#define BIGFONT 0x2190a4
static int glyph_w(int c) { if (c < 0x20 || c > 0x5f) return 0; return BE16(A(BIGFONT) + c * 16); }
static int hud_mode;                           /* 3614(A6); 2 = external strip (fe_text_strip) */
static uint8_t *hud_ext; static int hud_ext_w;
static int tx0, ty0, tcx, tcy, talign, tcol;   /* 3592, 3594, 3596, 3598, 3620, 3608 */
static char tbuf[256]; static int tlen;        /* 3626.. / 3882 */
static int tstate, tdig[3], tndig;             /* 3622: pending escape */
static void draw_glyph(int c, int x, int y) {  /* LAB_0465 */
    if (c < 0x20 || c > 0x5f) return;
    const uint8_t *g = A(BIGFONT) + c * 16; int w = BE16(g);
    if (hud_mode) {                            /* LAB_046B: OR into the plane-5 strip */
        uint8_t *dst = hud_mode == 2 ? hud_ext : hud; int dw = hud_mode == 2 ? hud_ext_w : W;
        for (int r = 0; r < 7; r++) { int bits = BE16(g + 2 + 2 * r); for (int k = 0; k < w; k++) { int px = x + k; if (px < 0 || px >= dw) continue; if ((bits >> (w - 1 - k)) & 1) dst[r * dw + px] = 1; } }
        return;
    }
    for (int r = 0; r < 7; r++) {
        int yy = y + r; if (yy < 0 || yy >= H) continue;
        int bits = BE16(g + 2 + 2 * r);
        for (int k = 0; k <= w; k++) { int px = x + k; if (px < 0 || px >= W) continue; screen[yy * W + px] = bg[yy * W + px]; }   /* cell restore */
        for (int k = 0; k <= w; k++) {
            int px = x + k; if (px < 0 || px >= W) continue;
            int on = k < w && ((bits >> (w - 1 - k)) & 1), sh = k > 0 && ((bits >> (w - k)) & 1);
            if (on) screen[yy * W + px] = (uint8_t)tcol; else if (sh) screen[yy * W + px] = 0;
        }
    }
}
static void text_flush(void) {                 /* LAB_0448 */
    tcx = tx0; tcy = ty0;
    if (talign != 1) { int wd = 0; for (int i = 0; i < tlen; i++) if ((uint8_t)tbuf[i] >= 0x20) wd += glyph_w((uint8_t)tbuf[i]) + 1; if (talign == 0) wd >>= 1; tcx -= wd; }
    for (int i = 0; i < tlen; i++) { int c = (uint8_t)tbuf[i]; if (c < 0x10) tcol = c; else { draw_glyph(c, tcx, tcy); tcx += glyph_w(c) + 1; } }
    tlen = 0;
}
static void text_char(int c) {                 /* LAB_043C -> LAB_043D state machine */
    if (c >= 'a' && c <= 'z') c -= 0x20;       /* LAB_0455 */
    switch (tstate) {
    case 0: if (c == '_') tstate = 1; else if (tlen < 255) tbuf[tlen++] = (char)c; return;
    case 1:                                    /* LAB_043E */
        tstate = 0; tndig = 0;
        if (c == 'X') tstate = 'X'; else if (c == 'Y') tstate = 'Y'; else if (c == 'C') tstate = 'C'; else if (c == 'A') tstate = 'A';
        else if (c == 'F') text_flush(); else if (c == 'N') { text_flush(); ty0 += 8; } else if (c == 'M') { text_flush(); ty0 += 12; }
        return;
    case 'X': case 'Y': tdig[tndig++] = c - '0'; if (tndig == 3) { int v = tdig[0] * 100 + tdig[1] * 10 + tdig[2]; if (tstate == 'X') tx0 = tcx = v; else ty0 = tcy = v; tstate = 0; } return;
    case 'C': tdig[tndig++] = c - '0'; if (tndig == 2) { if (tlen < 255) tbuf[tlen++] = (char)(tdig[0] * 10 + tdig[1]); tstate = 0; } return;
    case 'A': talign = c - '0'; tstate = 0; return;
    }
}
static void print(const char *s) { while (*s) text_char((uint8_t)*s++); }                /* LAB_0458 */
static void print_num(long v, int suppress) {  /* LAB_045E (suppress=5) / LAB_045C (0): 6 digits */
    long div = 100000; for (int i = 0; i < 6; i++, div /= 10) { int d = (int)((v / div) % 10); if (d == 0 && suppress > 0 && i < 5) { suppress--; continue; } suppress = 0; text_char('0' + d); }
}
static void text_at(int x, int y) { tx0 = tcx = x; ty0 = tcy = y; }                      /* LAB_0439 */
static void text_align(int a) { talign = a; }                                           /* LAB_043A */

/* ---------------------------------------------------------------- small font (LAB_0273 @ $20e9ec) ----
 * 6 bytes per char (3x5 in a 4x6 cell, bits 7..4); LAB_034F ORs it into plane 3 only -> colour 8|bg. */
static void small_char(int c, int x, int y) {
    const uint8_t *g = A(0x20e9ec) + c * 6;
    for (int r = 0; r < 6; r++) { int yy = y + r; if (yy < 0 || yy >= H) continue; for (int k = 0; k < 8; k++) { int px = x + k; if (px < 0 || px >= W) continue; if ((g[r] >> (7 - k)) & 1) screen[yy * W + px] |= 8; } }
}

/* ---------------------------------------------------------------- HUD strip ---- */
static const uint16_t HUD_GRAD[7] = { 0x88d, 0xaae, 0xccf, 0xccf, 0xccf, 0xaae, 0x88d };   /* LAB_046F copper: rows 8..14 */
static void hud_print(const char *s) { hud_mode = 1; print(s); hud_mode = 0; }

/* ---------------------------------------------------------------- input / flags ---- */
static int in_fire, in_joy, in_fire2;
static int keyq[16], keyn;
static int flag_start;        /* 12353(A6) bit0: a player joined -> start */
static int flag_options;      /* bit2: F-key/HELP pressed -> controls screen */
static int skip160;           /* 160(A6): Esc -> skip the current screen (in play: abort) */
static int please_wait158;    /* 158(A6) */
static int credits170;        /* 170(A6): -12 = 3 credits, +4 per join, 0 = NO CREDITS */
static int game_running164;   /* 164(A6) */
static int music10798;        /* 10798(A6): 1 title tune, 2 hi-score tune, 0 silent */
static int options_sel12324 = -1;   /* 12324(A6) */
/* 66(A0) control method per player record: 0 joystick port 1, 1 joystick port 2, 2 keyboard.  The original defaults
 * (LAB_0555) are heli = port 2, jeep = keyboard; natively the jeep defaults to port 1 so that the second gamepad
 * drives it without a visit to the options screen (the keyboard sets always work, see viewer.c). */
static int control_type[2] = { 1, 0 };
static int in_options;
static int pressed_key(void) { if (!keyn) return 0; int k = keyq[0]; memmove(keyq, keyq + 1, (--keyn) * sizeof keyq[0]); return k; }

/* ---------------------------------------------------------------- coroutine plumbing ---- */
static Coro *co; static int co_result; static int req_intro = -1, req_gameover, req_completed; static FeStats stats;
static int play_state;        /* 0 attract, 1 level intro, 2 play */
static void yield1(void) { coro_yield(); }
/* LAB_055A (player task, every VBL): fire + credits -> the player joins (55(A4)), latched;
 * LAB_023D: the screens test that flag (bit0 of 12353) and the options request (bit2). */
static void player_task(void) {
    int k = 0;
    for (int i = 0; i < keyn; ) {                                             /* LAB_01DD: F1..F10 -> 0..9, HELP -> 10 */
        if ((keyq[i] >= FE_KEY_F1 && keyq[i] <= FE_KEY_F1 + 9) || keyq[i] == FE_KEY_HELP) { options_sel12324 = keyq[i] == FE_KEY_HELP ? 10 : keyq[i] - FE_KEY_F1; k = 1; memmove(keyq + i, keyq + i + 1, (--keyn - i) * sizeof keyq[0]); }
        else i++;
    }
    if (k && play_state == 0 && !in_options) flag_options = 1;
    if ((in_fire || in_fire2) && !please_wait158 && credits170 != 0 && play_state == 0) flag_start = 1;
}
static int check_start(void) { return flag_start || flag_options; }
/* LAB_024B fade task, run every VBL */
static void fade_tick(void) {
    if (fade142 > 0) { dark11170 -= 16; if (dark11170 <= 0) { dark11170 = 0; fade142 = 0; } }
    else if (fade142 < 0) { dark11170 += 16; if (dark11170 >= 256) { dark11170 = 256; fade142 = 0; } }
    if (white11166) { white11166 += step11168; if (white11166 > 256) white11166 = 256; if (white11166 < 0) white11166 = 0; }
}
static void wait_vbls(int n) { while (n-- > 0) yield1(); }                         /* LAB_049D */
static void fade_out(void) { fade142 = -1; while (fade142) yield1(); }           /* LAB_0242 (16 VBLs) */
static void fade_in(void)  { fade142 = 1; while (fade142) yield1(); }            /* LAB_0243 */
static void fade_out_kill(void) { fade_out(); cop_clear(); }                     /* LAB_0241: fade + kill objects + clear lists */
/* LAB_0239: hold n*50 VBLs, cut short by fire (start) or Esc; then fade out unless an options request */
static void hold(int n50) {
    skip160 = 0; int t = n50 * 50;
    for (;;) { if (check_start() || skip160) { if (!flag_options) fade_out(); return; } yield1(); if (--t <= 0) return; }
}

/* ---------------------------------------------------------------- status line (LAB_0593 / LAB_0599) ---- */
static void status_text(char *out, const char *name, int lives, int power, int score) {   /* "HELI 3[ 2* 0123450" */
    char *p = out; p += sprintf(p, "%s ", name);
    if (lives >= 10) *p++ = (char)('0' + lives / 10);
    *p++ = (char)('0' + lives % 10); *p++ = '['; *p++ = ' ';               /* '[' = heart glyph */
    *p++ = (char)('2' + power / 5); *p++ = '*'; *p++ = ' ';                 /* '*' = star glyph */
    sprintf(p, "%06d0", score % 1000000);
}
static const char *hud_message(void) { return credits170 == 0 ? "NO CREDITS" : please_wait158 ? "PLEASE WAIT" : "PRESS FIRE"; }
void fe_draw_hud(Color *buf, const FeHud *heli, const FeHud *jeep) {
    memset(hud, 0, sizeof hud);
    char s[64]; const FeHud *p[2] = { heli, jeep }; const char *nm[2] = { "HELI", "JEEP" };
    hud_print("_x160_a0");                                                   /* LAB_0216 */
    for (int i = 0; i < 2; i++) {
        hud_print(i ? "_x312_a2" : "_x008_a1");
        if (p[i]->mode == FE_HUD_READY) hud_print("GET READY!");             /* LAB_0597 */
        else if (p[i]->mode == FE_HUD_STATUS || (vbl & 0x80)) { status_text(s, nm[i], p[i]->lives, p[i]->power, p[i]->score); hud_print(s); }   /* LAB_0593 */
        else hud_print(hud_message());                                       /* LAB_0599 */
        hud_print("_n");
    }
    for (int r = 0; r < 7; r++) for (int x = 0; x < W; x++) if (hud[r * W + x]) {
        Color *c = &buf[(8 + r) * W + x];
        int under = c->r | c->g | c->b;   /* colour index 0 under the text -> COLOR16 (gradient), else COLOR17..31 = black */
        if (!under) { uint16_t v = HUD_GRAD[r]; swiv_rgb12(v, &c->r, &c->g, &c->b); } else *c = BLACK;
    }
}

void fe_text_strip(uint8_t *dst, int w, const char *text) {
    /* temporarily point the HUD renderer at the caller's strip */
    static uint8_t *save; (void)save;
    memset(dst, 0, (size_t)w * 7);
    hud_mode = 2; hud_ext = dst; hud_ext_w = w; tlen = 0; tstate = 0; print(text); text_flush(); hud_mode = 0; hud_ext = NULL;
}
void fe_status_line(char *dst, size_t n, int player, const FeHud *h) {
    char s[64];
    if (h->mode == FE_HUD_READY) snprintf(dst, n, "GET READY!");
    else if (h->mode == FE_HUD_STATUS || (vbl & 0x80)) { status_text(s, player ? "JEEP" : "HELI", h->lives, h->power, h->score); snprintf(dst, n, "%s", s); }
    else snprintf(dst, n, "%s", hud_message());
}
int fe_control_method(int player) { return control_type[player & 1]; }
void fe_request_options(void) { if (play_state == 0 && !in_options) { options_sel12324 = 10; flag_options = 1; } }

/* ---------------------------------------------------------------- render ---- */
void fe_render(Color *buf) {
    uint16_t cur[16]; memcpy(cur, pal, sizeof cur);
    /* copper nodes are sorted by row in the original list; apply all nodes with row <= y in order */
    int order[512]; for (int i = 0; i < ncop; i++) order[i] = i;
    for (int i = 1; i < ncop; i++) { int k = order[i], j = i - 1; while (j >= 0 && cop[order[j]].row > cop[k].row) { order[j + 1] = order[j]; j--; } order[j + 1] = k; }
    int ni = 0;
    for (int y = 0; y < H; y++) {
        while (ni < ncop && cop[order[ni]].row <= y) { if (cop[order[ni]].row == y || y == 0) cur[cop[order[ni]].idx & 15] = (uint16_t)cop[order[ni]].val; ni++; }
        Color cols[16];
        for (int i = 0; i < 16; i++) { int v = cur[i]; if (dark11170) v = scale_dark(v, dark11170); if (white11166) v = scale_white(v, white11166); swiv_rgb12((uint16_t)v, &cols[i].r, &cols[i].g, &cols[i].b); cols[i].a = 255; }
        for (int x = 0; x < W; x++) buf[y * W + x] = cols[screen[y * W + x] & 15];
    }
}
int fe_play_darkness(void) { return dark11170; }
const char *fe_music(void) { return music10798 == 1 ? "AMTITUNE.MOD" : music10798 == 2 ? "AMHITUNE.MOD" : NULL; }
int fe_join_allowed(void) { return !please_wait158 && credits170 != 0; }
void fe_player_joined(int player) { (void)player; credits170 += 4; music10798 = 0; }   /* LAB_055A */

/* ================================================================ screens ================ */
/* ---- LAB_0203 @ $20e0b6: TITLE.  cover.raw, palette $20e6dc, fade in, title tune, 12 x 50 VBLs */
static void screen_title(void) {
    if (check_start()) return;
    load_raw("COVER.RAW"); fade_out_kill(); show_bg(); set_pal(0x20e6dc);
    if (check_start()) return;
    fade_in(); music10798 = 1; hold(12);
}
/* ---- LAB_020A @ $20e0ec: publisher / "PRESS FIRE TO START GAME" text.  Cover palette, LAB_0274 gradients */
static void screen_publisher(void) {
    if (check_start()) return;
    fade_out_kill(); screen_clear(); set_pal(0x20e6dc);
    cop_list(0x30, 0x20e91f); cop_list(0x40, 0x20e91f); cop_list(0x54, 0x20e92f); cop_list(0x68, 0x20e92f); cop_list(0x78, 0x20e92f);
    cop_list(0x88, 0x20e92f); cop_list(0x9c, 0x20e92f); cop_list(0xb0, 0x20e93f); cop_list(0xc0, 0x20e93f);   /* LAB_0274 */
    print((const char *)A(0x20e120));          /* "_x160_y048_c15_a0PUBLISHED BY THE SALES CURVE LTD_n_n..." */
    if (check_start()) return;
    fade_in(); hold(8);
}

/* ---- blueprint typewriter (LAB_018C/LAB_018E @ $20d800): one task per text block.  Mini-language:
 *   03 dx dy  move origin (dx*4, dy*6)   04 n  delay n VBLs per word/line   01 n  horizontal line (n*4 px)
 *   02 n  vertical line (n*6 px)   0a newline   00 end.  Words are wrapped at x0+130 (x0>160) / x0+162. */
typedef struct { const uint8_t *p; int x0, y0, x, y, right, delay; int alive; Coro *co; } Typer;
static Typer typers[3]; static int typer_gen;   /* 12532(A6): bumped to kill them */
static void typer_delay(Typer *t) { for (int i = 0; i < t->delay; i++) yield1(); }          /* LAB_01A9 */
static void typer_newline(Typer *t) { t->x = t->x0; t->y += 6; }                               /* LAB_01B1 */
static void typer_run(void *arg) {
    Typer *t = arg; int gen = typer_gen;
    for (;;) {
        if (typer_gen != gen || !*t->p) break;
        /* LAB_0193: word width -> wrap */
        { const uint8_t *q = t->p; int w = -4; while (*q > ' ') { w += 4; q++; } if (w + t->x >= t->right) typer_newline(t); }
        /* LAB_0195/LAB_0196: one word (up to and including a space or newline) */
        for (;;) {
            int c = *t->p++;
            if (c == 3) { int dx = (int8_t)*t->p++, dy = (int8_t)*t->p++; t->x0 += dx * 4; t->x = t->x0; t->y0 += dy * 6; t->y = t->y0; }
            else if (c == 4) t->delay = *t->p++;
            else if (c == 1) { int n = *t->p++; for (int i = 0; i < n; i++) { small_char(0x5c, t->x, t->y); small_char(0x5b, t->x + 4, t->y); t->x += 4; } typer_delay(t); }       /* LAB_01AB */
            else if (c == 2) { int n = *t->p++; for (int i = 0; i < n; i++) { small_char(0x5e, t->x, t->y); small_char(0x5d, t->x, t->y + 6); t->y += 6; } typer_delay(t); }   /* LAB_01AD */
            else if (c == ' ') { typer_delay(t); t->x += 4; break; }
            else if (c == '\n') { typer_delay(t); typer_newline(t); break; }
            else if (c == 0) { t->p--; break; }
            else { if (c >= 'a' && c <= 'z') c -= 0x20; if (t->x < t->right) small_char(c, t->x, t->y); t->x += 4; }   /* LAB_01A7 */
            if (typer_gen != gen) break;
        }
        yield1();
    }
    t->alive = 0;
}
static void typer_start(int i, const uint8_t *text, int x, int y) {                            /* LAB_018C */
    Typer *t = &typers[i]; memset(t, 0, sizeof *t); t->p = text; t->x0 = t->x = x; t->y0 = t->y = y;
    t->right = (x > 160 ? x : x + 32) + 130; t->alive = 1;
    if (t->co) coro_free(t->co);
    t->co = coro_new(typer_run, t, 32768);
}
static void typers_tick(void) { for (int i = 0; i < 3; i++) if (typers[i].alive && typers[i].co) { if (!coro_resume(typers[i].co)) typers[i].alive = 0; } }
static void typers_kill(void) { typer_gen++; for (int i = 0; i < 3; i++) typers[i].alive = 0; }

/* LAB_0177 @ $20d68c: wireframe colour sweep, white flash, merge, fade from white to the solid palette */
static const uint16_t RAMP_07DE[] = { 0xfff, 0xfff, 0xeff, 0xcfe, 0xafd, 0x8fc, 0x6fb, 0x4fa, 0x2f9, 0x0f8, 0x0e7, 0x0d6, 0x0c5, 0x0b4, 0x0a3 };   /* $20d7de */
static void blueprint_reveal(uint32_t solid_pal) {
    int pos[8], run[8]; memset(pos, 0, sizeof pos); memset(run, 0, sizeof run);
    /* LAB_0181: start a ramp task (LAB_0186) for colours 1..7, 8 VBLs apart; each steps every 2 VBLs */
    int started = 1, t = 0;
    for (;;) {
        if (started <= 7 && t % 8 == 0) { run[started] = 1; pos[started] = 0; started++; }
        int any = 0;
        for (int i = 1; i <= 7; i++) if (run[i]) {
            any = 1;
            if ((t & 1) == 0) {
                int n = (int)(sizeof RAMP_07DE / sizeof RAMP_07DE[0]);
                if (pos[i] >= n) { run[i] = 0; continue; }
                int v = RAMP_07DE[pos[i]++]; pal[i + 8] = (uint16_t)v; pal[i] = (uint16_t)((v >> 1) & 0x777);
            }
        }
        if (started > 7 && !any) break;
        yield1(); t++;
    }
    yield1(); set_pal(0x20e77c); yield1();                 /* white (colour 8 = cce) flash */
    for (int i = 0; i < W * H; i++) if (screen[i] == 8) bg[i] |= 8;   /* LAB_017C: wireframe + text into the solid's plane 3 */
    show_bg();                                              /* LAB_0179 */
    for (int d5 = 0x100; d5 >= 0; d5 -= 0x10) {            /* LAB_017A/LAB_017E: 17 steps from white */
        for (int i = 0; i < 16; i++) { int tv = BE16(A(solid_pal) + 2 * i); pal[i] = (uint16_t)(((~i) & 7) == 0 ? tv : scale_white(tv, d5)); }
        yield1();
    }
}
/* ---- LAB_0127 @ $20d2b8 (jeep) / LAB_0144 @ $20d478 (heli): blueprint screens */
static void screen_blueprint(int heli) {
    if (check_start()) return;
    load_raw(heli ? "HELIBP2.RAW" : "JEEPBP2.RAW"); fade_out_kill(); show_bg();
    set_pal(0x20e75c);                                      /* LAB_0172: all black except colour 8 = cce */
    if (check_start()) return;
    dark11170 = 0;                                          /* CLR.W 11170: no fade, the wireframe pops in */
    typer_start(0, A(0x20d626), 16, 0xc4);                  /* "Special Weapons Interdiction Vehicle" */
    typer_start(1, A(heli ? 0x20d4d4 : 0x20d314), 0xb4, 0x56);   /* description */
    typer_start(2, A(heli ? 0x20d576 : 0x20d3c2), 0xb4, 0x89);   /* spec box */
    /* the original now loads xxxxBP1.RAW from disk while the text types (about 2 s); stand-in wait: */
    for (int i = 0; i < 100; i++) { if (check_start()) { typers_kill(); return; } yield1(); }
    load_raw(heli ? "HELIBP1.RAW" : "JEEPBP1.RAW");
    if (check_start()) { typers_kill(); return; }
    blueprint_reveal(heli ? 0x20e7bc : 0x20e79c);
    hold(1);                                                /* LAB_0237 */
    typers_kill();                                          /* LAB_0128: ADDQ 12532 */
}

/* ---- hi-score tables (LAB_0284 init, LAB_02BB display, LAB_0291/LAB_0299 entry) ---- */
typedef struct { char name[32]; long score; int shots, pct; } HsEntry;
static HsEntry hs[2][7];
static void hs_save(void) { FILE *f = fopen("hiscores.txt", "w"); if (!f) return; for (int p = 0; p < 2; p++) for (int i = 0; i < 7; i++) fprintf(f, "%d %ld %d %d %s\n", p, hs[p][i].score, hs[p][i].shots, hs[p][i].pct, hs[p][i].name); fclose(f); }
static int hs_load(void) { FILE *f = fopen("hiscores.txt", "r"); if (!f) return 0; int p, sh, pc; long sc; char nm[64]; int n = 0, idx[2] = { 0, 0 };
    while (fscanf(f, "%d %ld %d %d %63[^\n]", &p, &sc, &sh, &pc, nm) == 5) { if (p < 0 || p > 1 || idx[p] >= 7) continue; HsEntry *e = &hs[p][idx[p]++]; e->score = sc; e->shots = sh; e->pct = pc; snprintf(e->name, 32, "%s", nm); n++; }
    fclose(f); return n == 14; }
static int hs_init;
static int hs_rng;
static void hs_tables_init(void) {             /* LAB_0284: one random HSn.TXT per player (heli hs1-8, jeep hs9-16), 7 names */
    if (hs_init) return;
    hs_init = 1;
    if (hs_load()) return;          /* native: persistent table with shots / accuracy */
    static const long SCORES[7] = { 70000, 60000, 50000, 40000, 30000, 20000, 10000 };   /* $20f11e */
    for (int p = 0; p < 2; p++) {
        char fn[16]; snprintf(fn, sizeof fn, "HS%d.TXT", (hs_rng = hs_rng * 1103515245 + 12345, (hs_rng >> 16) & 7) + 1 + p * 8);
        int i = swiv_find(disk, fn); uint32_t n; const uint8_t *d = i >= 0 ? swiv_load(disk, i, &n) : NULL;
        int k = 0; const uint8_t *q = d, *e = d ? d + n : NULL;
        while (q && q < e && *q != '.' && k < 7) { int l = 0; while (q < e && *q != '\n' && l < 31) hs[p][k].name[l++] = (char)*q++; hs[p][k].name[l] = 0; if (q < e) q++; hs[p][k].score = SCORES[k]; k++; }
        for (; k < 7; k++) { snprintf(hs[p][k].name, 32, "SWIV"); hs[p][k].score = SCORES[k]; }
    }
}
static void hs_entry_line(const HsEntry *e) {  /* LAB_02C1 + native shots / accuracy columns */
    char nm[16]; snprintf(nm, sizeof nm, "%.12s", e->name);
    tx0 = 0x10; text_align(1); print(nm); text_flush();
    tx0 = 0xB8; text_align(2); print_num(e->shots, 5); text_flush();
    tx0 = 0xE4; text_align(2); print_num(e->pct, 3); text_char('%'); text_flush();
    tx0 = 0x132; text_align(2); print_num(e->score / 10, 5); text_char('0'); text_flush();
    ty0 += 16;
}
static void hs_table_draw(int p) {             /* LAB_02BB @ $20f034 */
    load_raw("MUSHROOM.RAW"); fade_out_kill(); show_bg();
    set_pal(0x20e71c); cop_list(0x82, 0x20e96f); cop_list(0x9e, 0x20e997);          /* $20f0b6 */
    cop_list(0x30, 0x20e8ff); for (int r = 0x40; r <= 0xc0; r += 0x10) cop_list(r, 0x20e90f); cop_list(0xd0, 0x20e95f);   /* $20e9c8 */
    print("_x160_y048_c15_a0TODAY'S BEST "); print(p ? "JEEP " : "HELI "); print("SCORES_n");
    text_at(0x10, 0x40); tx0 = 0x10; text_align(1); tcol = 15; print("NAME"); text_flush(); tx0 = 0xB8; text_align(2); print("SHOTS"); text_flush(); tx0 = 0xE4; text_align(2); print("ACC"); text_flush(); tx0 = 0x132; text_align(2); print("SCORE"); text_flush();
    text_at(0x10, 0x50);
    for (int i = 0; i < 7; i++) hs_entry_line(&hs[p][i]);
}
/* ---- LAB_02B6/LAB_02B8: attract hi-score screens */
static void screen_hiscore(int p) {
    if (check_start()) return;
    hs_tables_init(); hs_table_draw(p); fade_in(); hold(5);
}
static int hs_qualifies(int p, long score) { return score > hs[p][6].score; }       /* LAB_028F (strictly greater than the lowest) */
/* LAB_028D: insert, show the table, prompt, name entry (LAB_0299).  Keyboard from fe_key(). */
static void screen_hiscore_entry(int p, long score) {
    hs_tables_init();
    int shots = g.stat_shots_p[p], pct = shots ? g.stat_hits_p[p] * 100 / shots : 0;
    int rank = 0; while (rank < 6 && score <= hs[p][rank].score) rank++;            /* LAB_02B2 (new entry goes above equal scores) */
    for (int i = 6; i > rank; i--) hs[p][i] = hs[p][i - 1];
    hs[p][rank].name[0] = 0; hs[p][rank].score = score; hs[p][rank].shots = shots; hs[p][rank].pct = pct;
    hs_table_draw(p);
    print("_x160_y208_c15_a0"); print(p ? "JEEP " : "HELI "); print("player please enter your name_n");   /* LAB_0291 */
    skip160 = 0; fade_in();
    /* LAB_0299: cursor blink 25 VBLs on/off, 80 blinks max (then accepted), Return/Esc end, Backspace deletes */
    char *name = hs[p][rank].name; int len = 0, x = 0x10, y = 0x50 + rank * 16, done = 0;
    for (int blink = 0; blink < 80 && !done; blink++) {
        for (int phase = 0; phase < 2 && !done; phase++) {
            /* redraw the name + cursor (LAB_02A3/LAB_02A7/LAB_02A9): "\" is the full-block glyph */
            for (int k = 0; k < 2; k++) { text_at(x, y); text_align(1); tcol = 15; print(name); print(phase == 0 && k == 1 ? "\\ _f" : "  _f"); }
            for (int t = 0; t < 25; t++) {
                int key = pressed_key();
                if (key == FE_KEY_RETURN || key == FE_KEY_ESC || key == '\n') { done = 1; break; }
                if (key == FE_KEY_BACKSPACE) { if (len) name[--len] = 0; break; }
                if (key >= ' ' && key < 0x7f) { if (key >= 'a' && key <= 'z') key -= 0x20; if (key <= 'Z' && len < 31) { name[len++] = (char)key; name[len] = 0; } break; }
                yield1();
            }
        }
    }
    if (!len) snprintf(name, 32, "%s", p ? "Lazy Jeep" : "Lazy Heli");               /* LAB_0293: default name */
    hs_save();
    text_at(x, y); text_align(1); print(name); print("   _f");
    print("_x160_y208_a0_c15"); print("                                                      _n");   /* blank the prompt */
}

/* ---- LAB_01EB @ $20df16: credits (faces.raw) */
static void screen_credits(void) {
    if (check_start()) return;
    load_raw("FACES.RAW"); fade_out_kill(); show_bg(); set_pal(0x20e6fc);
    for (int r = 0x16; r < 0xc0; r += 0x30) cop_list(r, 0x20e8c2);                  /* LAB_0278 */
    print((const char *)A(0x20df52));           /* "_x044_y022_c02_a1Amiga & ST_mProgramming_m..." */
    if (check_start()) return;
    fade_in(); hold(12);
}
/* ---- LAB_01D2 @ $20dc7e: controls / options screen (CONTROL.LIN sprites, F1-F3 / F6-F8).
 * Opened by HELP or any F-key during the attract loop (LAB_01DD -> bit2 of 12353).  Table LAB_01E7 @ $20de72
 * (14 bytes per key): F1/F2/F3 set the heli's control method 66(A0) to 0/1/2, F6/F7/F8 the jeep's; if the
 * other player already uses that method LAB_01E3 recursively applies the key's alternate entry (so the two
 * players never share a device).  F4/F5/F9/F10 only store a selection pointer (no visible effect). */
static const int FKEY_REC[10]  = { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1 };
static const int FKEY_VAL[10]  = { 0, 1, 2, -1, -1, 0, 1, 2, -1, -1 };
static const int FKEY_ALT[10]  = { 6, 7, 6, 9, 8, 1, 2, 1, 4, 3 };
static void apply_fkey_01E3(int f, int depth) {
    if (f < 0 || f > 9 || depth > 4) return;
    int rec = FKEY_REC[f], v = FKEY_VAL[f];
    if (v < 0) return;
    if (control_type[rec] == v) return;                                       /* same entry already selected */
    control_type[rec] = v;
    if (control_type[rec ^ 1] == v) apply_fkey_01E3(FKEY_ALT[f], depth + 1);  /* CMPM.W -> conflict -> alternate */
}
static void screen_controls(void) {
    if (!flag_options) return;
    flag_options = 0; in_options = 1; co_result = FE_OPTIONS;
    /* no fade-out: LAB_01D2 loads the set, clears and redraws at once */
    cop_clear(); screen_clear(); set_pal(0x20e73c); cop_list(0xd2, 0x20e92f);
    SwivCanvas c = { W, H, screen, NULL, 0 };
    swiv_blit_gfx(disk, &c, 0x0254, 0x52, 0x38); swiv_blit_gfx(disk, &c, 0x0254, 0xe8, 0x38);   /* LAB_01EA: joystick 1 / 2 / keyboard pictures */
    swiv_blit_gfx(disk, &c, 0x0454, 0x52, 0x70); swiv_blit_gfx(disk, &c, 0x0454, 0xe8, 0x70);
    swiv_blit_gfx(disk, &c, 0x0054, 0x52, 0xa8); swiv_blit_gfx(disk, &c, 0x0054, 0xe8, 0xa8);
    memcpy(bg, screen, sizeof bg);
    print((const char *)A(0x20dd32));           /* "_x084_y024_c15_a01up_f_x2342up_f_x130_y055F1_f..." */
    fade_in();
    int t = 15; options_sel12324 = -1;          /* MOVE.W #15,12502 (opened by a key); ST 12324 */
    for (;;) {
        /* LAB_01D9/LAB_01DA markers: heli $0854 at x=36, jeep $0654 at x=186, y = 66(A4)*56+50 */
        memcpy(screen, bg, sizeof screen); tcol = 15; print((const char *)A(0x20dd32));
        swiv_blit_gfx(disk, &c, 0x0854, 0x24, control_type[0] * 0x38 + 0x32);
        swiv_blit_gfx(disk, &c, 0x0654, 0xba, control_type[1] * 0x38 + 0x32);
        int hit = 0; skip160 = 0;
        for (int i = 0; i < t * 50; i++) {                                         /* LAB_0239(12502) */
            yield1();
            if (options_sel12324 >= 0) { int f = options_sel12324; options_sel12324 = -1; apply_fkey_01E3(f, 0); hit = 1; break; }   /* LAB_01D6/LAB_01E3 */
            if (flag_start || skip160) { skip160 = 0; fade_out(); in_options = 0; co_result = FE_ATTRACT; return; }
        }
        if (!hit) break;
    }
    fade_out();                                 /* LAB_0241 + LAB_0497 (unload the set) */
    in_options = 0; co_result = FE_ATTRACT;
}
/* ---- LAB_00B3 @ $20cac2: game over / game completed statistics */
static void screen_stats(void) {
    please_wait158 = 1;
    fade_out_kill(); screen_clear(); set_pal(0x20e73c);
    cop_list(0x30, 0x20e8ff); for (int r = 0x40; r <= 0xc0; r += 0x10) cop_list(r, 0x20e90f); cop_list(0xd0, 0x20e95f);
    print(stats.completed ? "_x020_y048_c15_a1Game Completed_n" : "_x020_y048_c15_a1Game Over_n");
    print("_y080Bullets fired:_n_nEnemies destroyed:_n_nEnemies escaped:_n_nTokens picked up:_n_nPercentage completed:_n_n_x230_y080_a2");
    long vals[5] = { stats.bullets, stats.destroyed, stats.spawned - stats.destroyed, stats.tokens, stats.completed ? 100 : stats.percent };
    for (int i = 0; i < 5; i++) { print_num(vals[i], 5); text_flush(); ty0 += 16; }   /* LAB_00B8 */
    fade_in();
    int hi = hs_qualifies(0, stats.heli_score) || hs_qualifies(1, stats.jeep_score);
    if (hi) wait_vbls(400); else hold(5);
}

/* ---- LAB_00CB @ $20cc62: game completed ending.  The original: congrat1.raw with a REACTOR.LIN
 * missile/smoke animation, two palette-inversion flashes, a particle mushroom cloud (LAB_010D),
 * screen shake (LAB_010B), fade to white, then congrat2.raw with the text for 1000 VBLs.
 * Ported: the pictures, flashes, white fade, text and timings; the sprite/particle animation is
 * approximated by the 200-VBL hold (TODO LAB_0111/LAB_0117/LAB_010D). */
static void screen_ending(void) {
    fade_out_kill(); load_raw("CONGRAT1.RAW"); show_bg(); set_pal(0x20e7dc);
    fade_in();
    wait_vbls(200);
    for (int f = 0; f < 2; f++) { for (int k = 0; k < 2; k++) { for (int i = 0; i < 16; i++) pal[i] ^= 0xfff; yield1(); } }   /* LAB_00D2 x2 */
    set_pal(0x20e7fc); wait_vbls(200);                        /* LAB_010B shake / LAB_010D cloud: approximated */
    white11166 = 8; step11168 = 0x10; while (white11166 < 256) yield1();   /* fade to white */
    wait_vbls(50);
    load_raw("CONGRAT2.RAW"); show_bg(); set_pal(0x20e7dc);
    print((const char *)A(0x20cd78));                         /* "_x160_y040_a0_c15Congratulations brave warrior!_n_n..." */
    cop_list(0x28, 0x20e92f); { static const int R[] = { 0x38, 0x44, 0x50, 0x5c, 0x6c, 0x78, 0x84, 0x94, 0xa0, 0xac, 0xb8 }; for (int i = 0; i < 11; i++) cop_list(R[i], 0x20e91f); }   /* LAB_0100 */
    pal[1] = 0x332;                                           /* MOVE.W #$332,11136 */
    step11168 = -0x10; while (white11166 > 0) yield1();
    wait_vbls(1000);
    step11168 = -4; fade_out();
}

/* ---- LAB_01B5 @ $20da28: the game.  In the original this routine loads the level ("GET READY!" on
 * a black screen meanwhile), fades in, runs until no player is active (+100/300 VBL grace), then
 * fades out into the statistics.  Natively the engine runs in viewer.c between FE_START_GAME and
 * fe_game_over(); this coroutine only keeps the screen state + fades in step. */
static void run_game(void) {
    music10798 = 0; fade_out_kill(); screen_clear(); please_wait158 = 0; flag_start = 0;
    play_state = 1; game_running164 = 0;
    co_result = FE_START_GAME; yield1();                      /* the caller starts the engine */
    for (;;) {
        /* level intro: black + GET READY (the original's disk load, 100..400 VBLs on a real disk) */
        play_state = 1; co_result = FE_LEVEL_INTRO; req_intro = -1;
        for (int i = 0; i < 120; i++) yield1();
        fade142 = 1; play_state = 2; game_running164 = 1; co_result = FE_PLAY;  /* LAB_0243 + ST 164 */
        while (req_intro < 0 && !req_gameover && !req_completed) yield1();
        if (req_intro >= 0) { fade_out(); continue; }           /* next level: LAB_0241-style fade */
        break;
    }
    please_wait158 = 1; game_running164 = 0; credits170 = -12;
    fade_out();                                                /* LAB_0241 */
    play_state = 0; co_result = FE_ATTRACT;
    if (req_completed) { stats.completed = 1; screen_ending(); }
    req_gameover = req_completed = 0;
    /* LAB_00C8: tune 1 (title) or 2 (hi-score) */
    music10798 = (hs_qualifies(0, stats.heli_score) || hs_qualifies(1, stats.jeep_score)) ? 2 : 1;
    screen_stats();                                            /* LAB_00B3 */
    if (hs_qualifies(0, stats.heli_score)) screen_hiscore_entry(0, stats.heli_score);   /* LAB_028C */
    if (hs_qualifies(1, stats.jeep_score)) screen_hiscore_entry(1, stats.jeep_score);
    please_wait158 = 0; flag_start = 0;                        /* SF 158 at the end of LAB_00B2; BCLR #0,12353 */
}

/* ---- LAB_00B2 @ $20ca84: the main loop */
static const char *debug_screen;
static void main_task(void *arg) {
    (void)arg;
    dark11170 = 256; credits170 = -12; please_wait158 = 0;
    if (debug_screen) {
        const char *s = debug_screen; debug_screen = NULL;
        if (!strcmp(s, "publisher")) screen_publisher(); else if (!strcmp(s, "helibp")) screen_blueprint(1); else if (!strcmp(s, "jeepbp")) screen_blueprint(0);
        else if (!strcmp(s, "hiscore")) screen_hiscore(0); else if (!strcmp(s, "credits")) screen_credits();
        else if (!strcmp(s, "controls")) { flag_options = 1; options_sel12324 = 10; screen_controls(); }
        else if (!strcmp(s, "stats")) { stats = (FeStats){ 0, 1234, 56, 78, 3, 42, 123450, 0 }; screen_stats(); }
        else if (!strcmp(s, "entry")) { stats.heli_score = 123450; screen_hiscore_entry(0, 123450); }
        else if (!strcmp(s, "ending")) { screen_ending(); }
    }
    for (;;) {
        screen_title();             /* LAB_0203 */
        screen_publisher();         /* LAB_020A */
        screen_blueprint(1);        /* LAB_0144 */
        hs_tables_init();           /* LAB_0284 */
        screen_hiscore(0);          /* LAB_02B6 */
        screen_blueprint(0);        /* LAB_0127 */
        screen_hiscore(1);          /* LAB_02B8 */
        screen_controls();          /* LAB_01D2 */
        screen_credits();           /* LAB_01EB */
        if (flag_start) run_game();                       /* LAB_01B5 .. LAB_028C */
        please_wait158 = 0;
    }
}

/* ---------------------------------------------------------------- API ---- */
void fe_init(SwivDisk *d) { disk = d; prog = d->prog; hs_rng = 0x5eed; }
void fe_start_title(void) {
    if (co) coro_free(co);
    typers_kill(); cop_clear(); screen_clear(); memset(pal, 0, sizeof pal); white11166 = 0; fade142 = 0;
    flag_start = flag_options = skip160 = 0; in_options = 0; play_state = 0; req_intro = -1; req_gameover = req_completed = 0; keyn = 0;
    co = coro_new(main_task, NULL, 1 << 16); co_result = FE_ATTRACT;
}
void fe_debug_goto(const char *screen) { debug_screen = screen; fe_start_title(); }
int fe_update(int fire_pressed, int joy_bits) {
    in_fire = fire_pressed || (joy_bits & 0x20); in_fire2 = (joy_bits >> 6) & 1; in_joy = joy_bits;
    { int k; while (keyn && (keyq[0] == FE_KEY_ESC)) { k = pressed_key(); (void)k; skip160 = 1; if (play_state == 2) req_gameover = 1; } }
    if (!co) fe_start_title();
    vbl++; fade_tick(); player_task();
    if (play_state == 0) typers_tick();
    coro_resume(co);
    int r = co_result; if (r == FE_START_GAME) co_result = FE_LEVEL_INTRO;   /* one-shot */
    return r;
}
void fe_level_intro(int level) { req_intro = level; }
void fe_game_over(const FeStats *s) { stats = *s; stats.completed = 0; req_gameover = 1; }
void fe_game_completed(void) { req_completed = 1; }
void fe_key(int ch) { if (keyn < 16) keyq[keyn++] = ch; }
