/* frontend.h -- SWIV title / attract / level-start / game-over presentation, ported from the
 * 68000 front end in AMPROG.OBJ (main loop LAB_00B2 @ $20ca84 and the screens it calls).
 * The frontend owns a 320x256 indexed screen + palette (the original's draw buffers at
 * 256/260/264(A6), palette 11134(A6), darkness 11170(A6), whiteness 11166(A6), copper colour
 * gradients) and runs its sequence as one coroutine that yields once per VBL. */
#ifndef SWIV_FRONTEND_H
#define SWIV_FRONTEND_H
#include "swivdata.h"
#include "raylib.h"

enum { FE_ATTRACT = 0,      /* frontend owns the screen: title / attract / stats / hi-score / ending */
       FE_START_GAME,       /* one-shot: fire accepted (LAB_01B5 entry) -> caller starts the engine, then fe_level_intro(0) */
       FE_LEVEL_INTRO,      /* black screen + "GET READY!" HUD (the original's level-load wait) */
       FE_PLAY,             /* gameplay: caller renders the engine; apply fe_play_darkness() */
       FE_OPTIONS };        /* the game's own controls/options screen (LAB_01D2) is showing (still frontend-rendered) */

enum { FE_KEY_ESC = 0x1b, FE_KEY_RETURN = '\r', FE_KEY_BACKSPACE = 8, FE_KEY_HELP = 0x100, FE_KEY_F1 = 0x101 /* ..F10 = 0x10a */ };

typedef struct { int completed; long bullets, destroyed, spawned, tokens; int percent; long heli_score, jeep_score; } FeStats;
typedef struct { int lives, power, score; int mode; } FeHud;   /* mode: 0 idle (alternate status / PRESS FIRE), 1 GET READY!, 2 status (in game) */
enum { FE_HUD_IDLE = 0, FE_HUD_READY = 1, FE_HUD_STATUS = 2 };

void fe_init(SwivDisk *d);
void fe_start_title(void);                       /* (re)start the attract loop from the title (LAB_00B2) */
int  fe_update(int fire_pressed, int joy_bits);  /* one VBL.  joy_bits: 0 up,1 down,2 left,3 right,5 fire (heli), 6 fire (jeep) */
void fe_render(Color *buf);                      /* 320x256 RGB out (palette, fades and copper gradients applied) + HUD line */
void fe_level_intro(int level);                  /* -> FE_LEVEL_INTRO ("GET READY!") then FE_PLAY with a 16-VBL fade-in */
void fe_game_over(const FeStats *s);             /* from FE_PLAY: 16-VBL fade-out, stats screen, hi-score entry, back to attract */
void fe_game_completed(void);                    /* from FE_PLAY after the last level: ending, then the stats screen */
void fe_key(int ch);                             /* keyboard: ASCII, FE_KEY_* (hi-score name entry, options screen, Esc) */
int  fe_play_darkness(void);                     /* 0..256 (11170(A6)) to apply to the play frame while FE_PLAY */
int  fe_join_allowed(void);                      /* credits left and not loading (LAB_055A join gate) */
void fe_player_joined(int player);               /* a player pressed fire in play (LAB_055A: credits -= 1, 10798 = 0) */
void fe_draw_hud(Color *buf, const FeHud *heli, const FeHud *jeep);   /* the plane-5 status line (LAB_0216 + LAB_0593/0599) */
const char *fe_music(void);                      /* "AMTITUNE.MOD" / "AMHITUNE.MOD" / NULL (10798(A6)) */
int  fe_control_method(int player);            /* 0 = heli, 1 = jeep -> 0 joystick port 1, 1 joystick port 2, 2 keyboard (66(A0), set on the options screen) */
void fe_request_options(void);                   /* open the options screen from the attract loop (same as the HELP key) */
void fe_text_strip(uint8_t *dst, int w, const char *text);
void fe_status_line(char *dst, size_t n, int player, const FeHud *h);   /* the LAB_0593/LAB_0599 text for one player ("HELI 3[ 2* 0123450" / "PRESS FIRE" / "GET READY!") */   /* render a line of game-font text (with _x/_a escapes, 320-wide coords) into a w x 7 0/1 strip */
void fe_debug_goto(const char *screen);          /* start the sequence at a given screen: title, publisher, helibp, hiscore, jeepbp, credits, controls, stats, entry, ending */
#endif
