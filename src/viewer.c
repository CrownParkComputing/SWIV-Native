/* swivview -- native SWIV map scroller + sprite browser (raylib, no 68000).
 * All controls are on-screen buttons (mouse or touch; keys are optional extras). */
#include "swivdata.h"
#include "engine/engine.h"
#include "frontend.h"
extern void player_start(void); extern void player_vbl(void); extern long player_shots_fired(void);
extern void audio_init(SwivDisk *d); extern void audio_update(void); extern void audio_music_play(SwivDisk *d, const char *name);
extern void audio_set_volumes(float sfxv, float musicv);
extern int audio_bank_count(void); extern const char *audio_bank_name(int i); extern const char *audio_bank_label(int i); extern void audio_bank_play(int i);
extern int audio_event_bank(int ev); extern void audio_event_set(int ev, int bank); extern void audio_map_save(void);
extern float audio_tune_get(int i, int what); extern void audio_tune_set(int i, int what, float v); extern void audio_tune_save(void); extern int audio_bank_frames(int i);
extern int player_input_dx, player_input_dy, player_input_fire;
extern int player2_input_dx, player2_input_dy, player2_input_fire;
extern RenderEntry player_bullet_render[30]; extern int player_bullet_count;
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIEW_W 320
#define VIEW_H 256
#define SCALE 4
#define MAP_START 96   /* the game starts with map y 96 at the screen bottom (first 96 px are never shown) */
#define BAR_H 120                 /* control bar below the view */
#define WIN_W (VIEW_W * SCALE)
#define WIN_H (VIEW_H * SCALE + BAR_H)
#define AMPROG_PAL 0x299C
#define N_AMPROG_PAL 11

static SwivDisk disk;
static Font ui_font; static int ui_font_ok; static Texture2D rr_logo;
static void ui_text(const char *t, int x, int y, int fs, Color c) { if (ui_font_ok) DrawTextEx(ui_font, t, (Vector2){(float)x, (float)y}, (float)fs, 1, c); else DrawText(t, x, y, fs, c); }
static int ui_measure(const char *t, int fs) { return ui_font_ok ? (int)MeasureTextEx(ui_font, t, (float)fs, 1).x : MeasureText(t, fs); }
static SwivMap map; static SwivCanvas canvas; static int map_lv = -1, show_ground = 1, show_air = 0;
static int game_on = 0; static int game_paused = 0; static int eng_level = 0; static int debug_ui = 0; static int pending_join_heli = 1, pending_join_jeep = 0;
static double scroll_pos; static float speed = 0.25f; static int paused = 1;
static Texture2D tex; static Image img;
static int mode = 3;              /* 0 map, 1 sprites, 2 play, 3 title, 4 sfx, 5 extras, 6 options */
/* ---- options (options.txt) ---- */
static struct { int sfx_vol, music_vol, fullscreen, scanlines, difficulty; } opt = { 8, 6, 0, 0, 1 };   /* difficulty 0 easy 1 normal 2 hard */
static void options_apply(void) { audio_set_volumes(opt.sfx_vol / 10.0f, opt.music_vol / 10.0f); if (opt.fullscreen != IsWindowFullscreen()) ToggleFullscreen(); eng_difficulty_mode = opt.difficulty; }
static void options_save(void) { FILE *f = fopen("options.txt", "w"); if (f) { fprintf(f, "sfx %d\nmusic %d\nfullscreen %d\nscanlines %d\ndifficulty %d\n", opt.sfx_vol, opt.music_vol, opt.fullscreen, opt.scanlines, opt.difficulty); fclose(f); } }
static void options_load(void) { FILE *f = fopen("options.txt", "r"); char k[32]; int v; if (!f) return; while (fscanf(f, "%31s %d", k, &v) == 2) { if (!strcmp(k, "sfx")) opt.sfx_vol = v; else if (!strcmp(k, "music")) opt.music_vol = v; else if (!strcmp(k, "fullscreen")) opt.fullscreen = v; else if (!strcmp(k, "scanlines")) opt.scanlines = v; else if (!strcmp(k, "difficulty")) opt.difficulty = v; } fclose(f); }
/* front end (src/frontend.c): the title/attract/level-start/game-over sequence owns mode 3 and drives the
 * engine through FE_START_GAME / FE_LEVEL_INTRO / FE_PLAY; fe_game = the current game was started by it */
static int fe_game = 0, fe_state = FE_ATTRACT, idle_vbl = 0, prev_joined_h = 0, prev_joined_j = 0, fe_req_sent = 0, level_base_px = 0;
typedef struct { int score, lives68, power102, level100, rate98, hiscore80, joined; } Carry; static Carry carry[2];
static void fe_keys(void) {      /* keyboard -> frontend (hi-score name entry, F-keys for the options screen, Esc) */
    int c; while ((c = GetCharPressed()) > 0) if (c < 0x80) fe_key(c);
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) fe_key(FE_KEY_RETURN);
    if (IsKeyPressed(KEY_BACKSPACE)) fe_key(FE_KEY_BACKSPACE);
    if (IsKeyPressed(KEY_ESCAPE)) fe_key(FE_KEY_ESC);
    for (int k = 0; k < 10; k++) if (IsKeyPressed(KEY_F1 + k) && k != 1) fe_key(FE_KEY_F1 + k);   /* F2 = screenshot */
    if (IsKeyPressed(KEY_F11)) fe_key(FE_KEY_HELP);
}
static FeHud hud_of(const struct Player *p, int intro) {
    FeHud h = { -p->lives68 / 4, p->power102, p->score, FE_HUD_IDLE };
    if (p->joined55) h.mode = intro ? FE_HUD_READY : FE_HUD_STATUS;
    return h;
}
static int fire_held_heli(void) { return IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_ENTER) || IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) || IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_RIGHT); }
static int fire_held_jeep(void) { return IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_LEFT_CONTROL) || IsGamepadButtonDown(1, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) || IsGamepadButtonDown(1, GAMEPAD_BUTTON_MIDDLE_RIGHT); }

/* ---- tiny immediate-mode button bar ---- */
static int ui_hit(Rectangle r) {
    Vector2 p = GetMousePosition();
    if (GetTouchPointCount() > 0) p = GetTouchPosition(0);
    return CheckCollisionPointRec(p, r);
}
static int button(Rectangle r, const char *label, int active) {
    int hot = ui_hit(r);
    Color bg = active ? (Color){70, 130, 200, 255} : hot ? (Color){80, 80, 90, 255} : (Color){50, 50, 58, 255};
    DrawRectangleRec(r, bg); DrawRectangleLinesEx(r, 1, (Color){120, 120, 130, 255});
    int fs = 24; int tw = ui_measure(label, fs);
    while (tw > r.width - 6 && fs > 10) { fs -= 2; tw = ui_measure(label, fs); }
    ui_text(label, r.x + (r.width - tw) / 2, r.y + (r.height - fs) / 2, fs, RAYWHITE);
    return hot && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}
static int held(Rectangle r) { return ui_hit(r) && IsMouseButtonDown(MOUSE_BUTTON_LEFT); }

/* ---- gamepads: skip keyboard/mouse receivers that GLFW reports as joysticks ---- */
static int real_pad(int nth) {
    int found = 0;
    for (int i = 0; i < 8; i++) {
        if (!IsGamepadAvailable(i)) continue;
        const char *n = GetGamepadName(i); if (!n) n = "";
        if (strcasestr(n, "mouse") || strcasestr(n, "keyboard") || strcasestr(n, "receiver") || GetGamepadAxisCount(i) < 2) continue;
        if (found++ == nth) return i;
    }
    return -1;
}
static void pad_read(int nth, int *dx, int *dy, int *fire, int *start) {
    int p = real_pad(nth); if (p < 0) return;
    float ax = GetGamepadAxisMovement(p, GAMEPAD_AXIS_LEFT_X), ay = GetGamepadAxisMovement(p, GAMEPAD_AXIS_LEFT_Y);
    if (ax < -0.4f || IsGamepadButtonDown(p, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) *dx = -1; if (ax > 0.4f || IsGamepadButtonDown(p, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) *dx = 1;
    if (ay < -0.4f || IsGamepadButtonDown(p, GAMEPAD_BUTTON_LEFT_FACE_UP)) *dy = -1; if (ay > 0.4f || IsGamepadButtonDown(p, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) *dy = 1;
    if (IsGamepadButtonDown(p, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) || IsGamepadButtonDown(p, GAMEPAD_BUTTON_RIGHT_FACE_LEFT) || IsGamepadButtonDown(p, GAMEPAD_BUTTON_RIGHT_TRIGGER_1) || IsGamepadButtonDown(p, GAMEPAD_BUTTON_RIGHT_TRIGGER_2)) *fire = 1;
    if (start && (IsGamepadButtonPressed(p, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) || IsGamepadButtonPressed(p, GAMEPAD_BUTTON_MIDDLE_RIGHT))) *start = 1;
}

/* ---- map ---- */
static int is_air_name(const char *n) {
    static const char *air[] = { "FODDERA", "BIRD", "VTOL", "BLACKJET", "SKYEYEA", "SKYEYEB", "YELLOW", "TRILO", "XEVIOUS", "BUNNY", "GOOSE",
        "MAMA", "DADA", "JETS", "SEAPLANE", "BOS", "ORB", "INSECTS", "FROG", "EDGE", "TAP", "TILT", "FLAME", "FISH", "HOMING", "AIRMINE", NULL };
    for (int i = 0; air[i]; i++) if (!strncasecmp(n, air[i], strlen(air[i]))) return 1;
    return 0;
}
extern int (*swiv_map_tile_filter)(uint16_t gfx);
static int placeholder_tile(uint16_t gfx) { int r = eng_handler_ported(gfx) || (strcmp(eng_handler_name(gfx), "DEFAULT") != 0); if (r && getenv("SWIV_DBG")) fprintf(stderr, "skip tile %04x\n", gfx); return r; }
static void load_level(int lv) {
    if (map_lv >= 0) { swiv_map_free(&map); swiv_canvas_free(&canvas); }
    swiv_map_load(&disk, lv, &map);
    swiv_map_render(&disk, &map, &canvas, 0);
    for (int i = 0; i < map.nobjs; i++) {
        const SwivRec *r = &map.objs[i]; int id = r->gfx & 0x1FF;
        const char *nm = id < disk.norder ? disk.order[id] : "_";
        int scenery = nm[0] == '_';
        int is_air = !scenery && is_air_name(nm);
        if (scenery || (is_air ? show_air : show_ground)) {
            canvas.cur_palid = swiv_map_palid_at(&map, r->y);
            swiv_blit_gfx(&disk, &canvas, r->gfx, r->x, map.height + SWIV_MARGIN - r->y);
        }
    }
    map_lv = lv; scroll_pos = MAP_START;
}
static void draw_map_frame(Color *out) {
    int top = map.height + SWIV_MARGIN - (int)scroll_pos - VIEW_H;
    /* The real game applies the map's colour commands per scanline (copper
     * split that travels with the map -- verified against oracle frames), so
     * each screen row takes the palette checkpoint of its own map row. */
    int last_id = -2; Color cols[16];
    for (int y = 0; y < VIEW_H; y++) {
        int sy = top + y;
        int id = swiv_map_palid_at(&map, map.height + SWIV_MARGIN - sy);
        if (id != last_id) {
            uint16_t pal[16]; swiv_map_palette_row(&map, sy, pal);
            for (int i = 0; i < 16; i++) { swiv_rgb12(pal[i], &cols[i].r, &cols[i].g, &cols[i].b); cols[i].a = 255; }
            last_id = id;
        }
        for (int x = 0; x < VIEW_W; x++)
            out[y * VIEW_W + x] = (sy >= 0 && sy < canvas.h) ? cols[canvas.px[(size_t)sy * canvas.w + x] & 15] : BLACK;
    }
}

/* ---- extras: Hall of Light media (extras/hol) ---- */
#include <dirent.h>
typedef struct { const char *title; const char *dir; const char *ext; } ExtraCat;
static const ExtraCat EXTRA_CATS[] = { {"Screenshots", "extras/hol/screen", ".png"}, {"Box", "extras/hol/box", ".png"}, {"Disks", "extras/hol/disk", ".png"},
    {"Scans & adverts", "extras/hol/misc", ".png"}, {"Cheats", NULL, NULL} };
#define N_EXTRA_CATS 5
static char extra_files[256][160]; static int extra_n, extra_cat = 0, extra_idx = 0; static Texture2D extra_tex; static int extra_loaded_idx = -1, extra_loaded_cat = -1;
static char cheats_text[4096];
static int cmpstr(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }
static void extras_scan(int cat) {
    extra_n = 0; extra_idx = 0;
    if (!EXTRA_CATS[cat].dir) { FILE *f = fopen("extras/hol/cheats.txt", "r"); if (f) { size_t n = fread(cheats_text, 1, sizeof cheats_text - 1, f); cheats_text[n] = 0; fclose(f); } return; }
    DIR *d = opendir(EXTRA_CATS[cat].dir); if (!d) return; struct dirent *e;
    while ((e = readdir(d)) && extra_n < 256) { const char *n = e->d_name; size_t l = strlen(n); if (cat == 2 && !strstr(n, "disk1")) continue; if (l > 4 && !strcasecmp(n + l - 4, EXTRA_CATS[cat].ext)) snprintf(extra_files[extra_n++], 160, "%s/%s", EXTRA_CATS[cat].dir, n); }
    closedir(d); qsort(extra_files, extra_n, 160, cmpstr);
    /* natural order for numbered names: sort by the number before the extension */
    for (int i = 1; i < extra_n; i++) for (int j = i; j > 0; j--) {
        const char *a = extra_files[j - 1], *b = extra_files[j]; const char *pa = strrchr(a, '_'), *pb = strrchr(b, '_');
        int na = pa ? atoi(pa + 1 + strspn(pa + 1, "abcdefghijklmnopqrstuvwxyz")) : 0, nb = pb ? atoi(pb + 1 + strspn(pb + 1, "abcdefghijklmnopqrstuvwxyz")) : 0;
        const char *da = strrchr(a, '-'), *db = strrchr(b, '-'); int pa2 = da ? atoi(da + 1) : 0, pb2 = db ? atoi(db + 1) : 0;
        if (na > nb || (na == nb && pa2 > pb2)) { char t[160]; memcpy(t, extra_files[j - 1], 160); memcpy(extra_files[j - 1], extra_files[j], 160); memcpy(extra_files[j], t, 160); } else break;
    }
}
static void extras_load(void) {
    if (extra_loaded_idx == extra_idx && extra_loaded_cat == extra_cat) return;
    if (extra_tex.id) UnloadTexture(extra_tex);
    extra_tex = (Texture2D){0};
    if (extra_n) { extra_tex = LoadTexture(extra_files[extra_idx]); SetTextureFilter(extra_tex, TEXTURE_FILTER_BILINEAR); }
    extra_loaded_idx = extra_idx; extra_loaded_cat = extra_cat;
}

/* ---- sprites ---- */
static int sp_file = 0, sp_frame = 0, sp_pal = N_AMPROG_PAL, sp_anim = 0, sp_zoom = 2; static double sp_t;
static int is_lin(int i) { const char *n = disk.files[i].name; size_t l = strlen(n); return l > 4 && !strcasecmp(n + l - 4, ".LIN"); }
static int next_lin(int i, int dir) { for (int k = 0; k < disk.nfiles; k++) { i = (i + dir + disk.nfiles) % disk.nfiles; if (is_lin(i)) return i; } return i; }
static void pal_source(int src, uint16_t pal[16], char *name, size_t n) {
    if (src < N_AMPROG_PAL) {
        for (int i = 0; i < 16; i++) pal[i] = (uint16_t)((disk.prog[AMPROG_PAL + src * 32 + 2 * i] << 8) | disk.prog[AMPROG_PAL + src * 32 + 2 * i + 1]);
        snprintf(name, n, "AMPROG $%04X", AMPROG_PAL + src * 32);
    } else {
        SwivMap pm; swiv_map_load(&disk, src - N_AMPROG_PAL, &pm);
        /* levels fade in from black: use the first fully-populated checkpoint */
        int best = 0, bestn = -1;
        for (int i = 0; i < pm.nchecks; i++) { int n = 0; for (int k = 0; k < 16; k++) n += pm.checks[i].pal[k] != 0; if (n > bestn) { bestn = n; best = i; } }
        memcpy(pal, pm.nchecks ? pm.checks[best].pal : (uint16_t[16]){0}, 32);
        snprintf(name, n, "LEVEL %d %s", src - N_AMPROG_PAL + 1, pm.pam_name); swiv_map_free(&pm);
    }
}
#define N_PAL_SRC (N_AMPROG_PAL + 7)

static void draw_sprite_frame(Color *out, char *palname, size_t pn) {
    uint16_t pal[16]; pal_source(sp_pal, pal, palname, pn);
    Color cols[16];
    for (int i = 0; i < 16; i++) { swiv_rgb12(pal[i], &cols[i].r, &cols[i].g, &cols[i].b); cols[i].a = 255; }
    SwivCanvas c; swiv_canvas_init(&c, VIEW_W, VIEW_H, 255);
    SwivCanvas big; swiv_canvas_init(&big, VIEW_W / sp_zoom + 1, 180 / sp_zoom + 1, 255);
    const SwivLin *L = swiv_lin(&disk, sp_file);
    if (L->nframes) {
        if (sp_frame >= L->nframes) sp_frame = 0;
        const SwivFrame *F = &L->frames[sp_frame];
        for (int i = 0; i < F->nparts; i++) swiv_blit_part(&big, &F->parts[i], big.w / 2 - F->parts[i].cx, big.h / 2 - F->parts[i].cy);
        /* zoomed composite in the upper area */
        for (int y = 0; y < 180; y++) for (int x = 0; x < VIEW_W; x++)
            c.px[y * VIEW_W + x] = big.px[(y / sp_zoom) * big.w + x / sp_zoom];
        /* strip of all frames along the bottom, scrolled so the current one is visible */
        int x = 2, cur_x = 0;
        for (int f = 0; f < sp_frame; f++) cur_x += L->frames[f].parts[0].w + 2;
        int shift = cur_x > VIEW_W - 40 ? cur_x - (VIEW_W - 40) : 0;
        for (int f = 0; f < L->nframes; f++) {
            const SwivPart *p = &L->frames[f].parts[0];
            int bx = x - shift;
            if (bx + p->w > 0 && bx < VIEW_W) {
                swiv_blit_part(&c, p, bx, VIEW_H - 4 - p->h);
                if (f == sp_frame) for (int k = 0; k < p->w + 2; k++) { int xx = bx - 1 + k; if (xx >= 0 && xx < VIEW_W) c.px[(VIEW_H - 2) * VIEW_W + xx] = 1; }
            }
            x += p->w + 2;
        }
    }
    /* palette swatch row */
    for (int i = 0; i < 16; i++) for (int y = 182; y < 190; y++) for (int x = i * 20; x < i * 20 + 20; x++) c.px[y * VIEW_W + x] = (uint8_t)i;
    for (int i = 0; i < VIEW_W * VIEW_H; i++) out[i] = c.px[i] == 255 ? (Color){34, 34, 44, 255} : cols[c.px[i] & 15];
    swiv_canvas_free(&c); swiv_canvas_free(&big);
}

int main(int argc, char **argv) {
    const char *adf = "/home/jon/swiv-amiga-re/SWIVFIX.ADF";
    const char *shot = NULL; double shot_scroll = 0; int shot_frames = 10; const char *shot_file = NULL; int autofire = 0, test_go = -1, frame_no = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--adf")) adf = argv[++i];
        else if (!strcmp(argv[i], "--shot")) shot = argv[++i];
        else if (!strcmp(argv[i], "--sprites")) mode = 1;
        else if (!strcmp(argv[i], "--play")) mode = 2;
        else if (!strcmp(argv[i], "--autofire")) autofire = 1;
        else if (!strcmp(argv[i], "--testgo")) test_go = atoi(argv[++i]);          /* debug: end the game at this frame */
        else if (!strcmp(argv[i], "--frames")) shot_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--file")) shot_file = argv[++i];
        else if (!strcmp(argv[i], "--pal")) sp_pal = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scroll")) shot_scroll = atof(argv[++i]);
    }
    if (swiv_open(&disk, adf)) { fprintf(stderr, "cannot open %s\n", adf); return 1; }
    InitWindow(WIN_W, WIN_H, "SWIV native viewer"); SetExitKey(KEY_NULL);
    { const char *fonts[] = { "/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/noto/NotoSans-Regular.ttf", NULL };
      if (FileExists("assets/retro_recomp_logo.png")) { rr_logo = LoadTexture("assets/retro_recomp_logo.png"); SetTextureFilter(rr_logo, TEXTURE_FILTER_BILINEAR); }
      for (int i = 0; fonts[i] && !ui_font_ok; i++) if (FileExists(fonts[i])) { ui_font = LoadFontEx(fonts[i], 40, NULL, 0); ui_font_ok = ui_font.texture.id != 0; if (ui_font_ok) SetTextureFilter(ui_font.texture, TEXTURE_FILTER_BILINEAR); } }
    if (!shot) { audio_init(&disk); options_load(); options_apply(); }
    fe_init(&disk); fe_start_title();
    SetTargetFPS(50);
    img = GenImageColor(VIEW_W, VIEW_H, BLACK); tex = LoadTextureFromImage(img);
    Color *buf = malloc(sizeof(Color) * VIEW_W * VIEW_H);
    load_level(0); scroll_pos = shot_scroll; sp_file = next_lin(0, 1);
    if (shot_file) { int f = swiv_find(&disk, shot_file); if (f >= 0) sp_file = f; }
    char status[256], palname[64] = "";
    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_F2)) TakeScreenshot("swivview.png");
        if (++frame_no == test_go && mode == 2) { g.game_over160 = 1; autofire = 0; }
        audio_update(); audio_music_play(&disk, (mode == 3 || mode == 2) ? fe_music() : NULL);
        if (mode == 6) {
            memset(buf, 0, sizeof(Color) * VIEW_W * VIEW_H);
        } else if (mode == 5) {
            memset(buf, 0, sizeof(Color) * VIEW_W * VIEW_H);
            snprintf(status, sizeof status, "EXTRAS: Hall of Light media (amiga.abime.net)");
        } else if (mode == 4) {
            memset(buf, 0, sizeof(Color) * VIEW_W * VIEW_H);
            snprintf(status, sizeof status, "SFX DEBUG: click a sound to hear it; select an event (right) then a sound to assign; SAVE writes sfxmap.txt");
        } else if (mode == 3) {
            /* FRONT END: LAB_00B2 attract loop in src/frontend.c.  Port 2 = helicopter (arrows/space/pad 0/tap right),
             * port 1 = jeep (WASD/shift/pad 1/tap left); either fire starts, the other can join any time. */
            int start_heli = fire_held_heli() || autofire, start_jeep = fire_held_jeep();
            Vector2 mp = GetMousePosition(); if (GetTouchPointCount() > 0) mp = GetTouchPosition(0);
            if ((IsMouseButtonDown(MOUSE_BUTTON_LEFT) || GetTouchPointCount() > 0) && mp.y < VIEW_H * SCALE) { if (mp.x < WIN_W / 2) start_jeep = 1; else start_heli = 1; }
            fe_keys();
            int r = fe_update(start_heli, start_jeep ? 0x40 : 0);
            if (r == FE_START_GAME) {                 /* LAB_01B5: start the engine; the joining player is the one who pressed fire */
                eng_init(&disk, 0); player_start(); eng_level = 0; map_lv = 0; game_on = 1; fe_game = 1; idle_vbl = 0;
                pending_join_heli = start_heli || !start_jeep; pending_join_jeep = start_jeep;
                g.heli.joined55 = pending_join_heli; g.jeep.joined55 = pending_join_jeep;
                if (pending_join_heli) fe_player_joined(0); if (pending_join_jeep) fe_player_joined(1);
                prev_joined_h = g.heli.joined55; prev_joined_j = g.jeep.joined55; memset(carry, 0, sizeof carry); fe_req_sent = 0; level_base_px = 0;
                fe_level_intro(0); r = FE_LEVEL_INTRO;
            }
            fe_state = r;
            fe_render(buf);
            if (r == FE_LEVEL_INTRO && fe_game) {     /* "GET READY!" (LAB_055B/LAB_0597) for the joined players */
                FeHud h = hud_of(&g.heli, 1), j = hud_of(&g.jeep, 1); fe_draw_hud(buf, &h, &j);
                /* a second player may still join during the intro */
                if (!g.heli.joined55 && start_heli && fe_join_allowed()) { g.heli.joined55 = 1; fe_player_joined(0); }
                if (!g.jeep.joined55 && start_jeep && fe_join_allowed()) { g.jeep.joined55 = 1; fe_player_joined(1); }
            } else if (r == FE_PLAY && fe_game) {
                mode = 2; game_paused = 0;
            } else {
                FeHud h = hud_of(&g.heli, 0), j = hud_of(&g.jeep, 0); h.mode = j.mode = FE_HUD_IDLE;
                if (fe_game && game_on) { game_on = 0; }
                fe_draw_hud(buf, &h, &j);
            }
            snprintf(status, sizeof status, "S.W.I.V.  (C) 1991 The Sales Curve / Storm  --  native  --  front end state %d", r);
        } else if (mode == 2) {
            if (!game_on) { eng_init(&disk, map_lv < 0 ? 0 : map_lv); player_start(); game_on = 1; eng_level = map_lv < 0 ? 0 : map_lv; g.heli.joined55 = pending_join_heli; g.jeep.joined55 = pending_join_jeep; }
            int dx = 0, dy = 0, fire = 0;
            if (IsKeyDown(KEY_LEFT)) dx = -1; if (IsKeyDown(KEY_RIGHT)) dx = 1;
            if (IsKeyDown(KEY_UP)) dy = -1; if (IsKeyDown(KEY_DOWN)) dy = 1;
            if (IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_LEFT_CONTROL) || autofire) fire = 1;
            static Vector2 stick0; static int stick_on = 0;
            int tc = GetTouchPointCount();
            for (int t = 0; t < (tc ? tc : (IsMouseButtonDown(MOUSE_BUTTON_LEFT) ? 1 : 0)); t++) {
                Vector2 p = tc ? GetTouchPosition(t) : GetMousePosition();
                if (p.y >= VIEW_H * SCALE) continue;
                if (p.x < WIN_W / 2) {
                    if (!stick_on) { stick0 = p; stick_on = 1; }
                    float ddx = p.x - stick0.x, ddy = p.y - stick0.y;
                    if (ddx < -12) dx = -1; if (ddx > 12) dx = 1; if (ddy < -12) dy = -1; if (ddy > 12) dy = 1;
                } else fire = 1;
            }
            if (!tc && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) stick_on = 0;
            player_input_dx = dx; player_input_dy = dy; player_input_fire = fire;
            /* gamepads follow the game's own controls screen (fe_control_method, LAB_01D2): joystick port 2 = first
             * real gamepad, port 1 = second, keyboard = none.  The keyboard sets (arrows+space / WASD+shift) always work. */
            { int m = fe_control_method(0); if (m == 1) pad_read(0, &player_input_dx, &player_input_dy, &player_input_fire, NULL); else if (m == 0) pad_read(1, &player_input_dx, &player_input_dy, &player_input_fire, NULL); }
            /* player 2 (jeep): WASD + left shift/ctrl, or gamepad 1 */
            int dx2 = 0, dy2 = 0, f2 = 0;
            if (IsKeyDown(KEY_A)) dx2 = -1; if (IsKeyDown(KEY_D)) dx2 = 1; if (IsKeyDown(KEY_W)) dy2 = -1; if (IsKeyDown(KEY_S)) dy2 = 1;
            if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_LEFT_CONTROL)) f2 = 1;
            { int m = fe_control_method(1); if (m == 0) pad_read(1, &dx2, &dy2, &f2, NULL); else if (m == 1) pad_read(0, &dx2, &dy2, &f2, NULL); }
            player2_input_dx = dx2; player2_input_dy = dy2; player2_input_fire = f2;
            if (!game_paused) {
                int ja = fe_join_allowed();
                eng_vbl(); player_vbl();
                if (fe_game) {
                    /* LAB_055A join gate (credits / please wait) + credit accounting */
                    if (g.heli.joined55 && !prev_joined_h) { if (ja) fe_player_joined(0); else g.heli.joined55 = 0; }
                    if (g.jeep.joined55 && !prev_joined_j) { if (ja) fe_player_joined(1); else g.jeep.joined55 = 0; }
                    prev_joined_h = g.heli.joined55; prev_joined_j = g.jeep.joined55;
                    fe_keys();
                    int r = fe_update(fire_held_heli() || player_input_fire, (fire_held_jeep() || player2_input_fire) ? 0x40 : 0);
                    /* LAB_01B7: nobody active -> 100 VBLs grace (300 with credits left) -> game over */
                    if (!g.heli.joined55 && !g.jeep.joined55) idle_vbl++; else idle_vbl = 0;
                    int level_done = (int16_t)(g.scroll3530 - (0xE9C0 - eng_map.height)) <= 0;
                    if (r == FE_PLAY && !fe_req_sent && idle_vbl >= (fe_join_allowed() ? 300 : 100)) {
                        /* LAB_00B3 statistics: 12486 shots, 12498 destroyed, 12494 spawned, 12490 tokens, ($E9C0-3534)*100/$68C1 */
                        FeStats st = { 0, player_shots_fired(), g.stat_destroyed12498, g.stat_spawned12494, g.stat_tokens12490,
                                       (int)((long)(level_base_px + (uint16_t)(0xE9C0 - g.scroll3530)) * 100 / 0x68c1), g.heli.hiscore80 > g.heli.score ? g.heli.hiscore80 : g.heli.score, g.jeep.hiscore80 > g.jeep.score ? g.jeep.hiscore80 : g.jeep.score };
                        fe_game_over(&st); fe_req_sent = 1;
                    } else if (r == FE_PLAY && !fe_req_sent && level_done && idle_vbl == 0) {
                        /* the original streams the next map seamlessly (LAB_02E1); natively each level is a fresh engine
                         * run with the player state carried over, announced with the same GET READY! screen */
                        if (eng_level < 6) fe_level_intro(eng_level + 1); else fe_game_completed();
                        fe_req_sent = 1;
                    }
                    if (r != FE_PLAY) {
                        /* the fade-out has finished: next level, ending or game-over statistics (frontend screens) */
                        if (r == FE_LEVEL_INTRO) {
                            struct Player *pl[2] = { &g.heli, &g.jeep };
                            for (int k = 0; k < 2; k++) carry[k] = (Carry){ pl[k]->score, pl[k]->lives68, pl[k]->power102, pl[k]->level100, pl[k]->rate98, pl[k]->hiscore80, pl[k]->joined55 != 0 };
                            level_base_px += eng_map.height; eng_level++; map_lv = eng_level; eng_init(&disk, eng_level); player_start();
                            g.heli.joined55 = carry[0].joined; g.jeep.joined55 = carry[1].joined;
                            /* the managers (LAB_055D) reset lives/score on their first tick: run it, then carry the game on */
                            eng_vbl(); player_vbl(); eng_vbl(); player_vbl();
                            for (int k = 0; k < 2; k++) if (carry[k].joined) { pl[k]->score = carry[k].score; pl[k]->lives68 = carry[k].lives68; pl[k]->power102 = carry[k].power102; pl[k]->level100 = carry[k].level100; pl[k]->rate98 = carry[k].rate98; pl[k]->hiscore80 = carry[k].hiscore80; }
                            prev_joined_h = g.heli.joined55; prev_joined_j = g.jeep.joined55;
                        } else game_on = 0;
                        fe_req_sent = 0; idle_vbl = 0; mode = 3;
                    }
                }
            }
            /* render: terrain window then sprites (descending key) */
            static uint8_t idx[VIEW_W * VIEW_H]; static uint16_t rowpal[VIEW_H][16];
            static SwivCanvas terrain; static int terrain_lv = -1;
            if (terrain_lv != eng_level) { if (terrain_lv >= 0) swiv_canvas_free(&terrain); swiv_map_tile_filter = placeholder_tile; swiv_map_render(&disk, &eng_map, &terrain, 0); swiv_map_tile_filter = 0; terrain_lv = eng_level; }
            int top_img = eng_map.height + SWIV_MARGIN - (int)(0xE9C0 - g.scroll3542);   /* image row of map y = scroll (screen top) */
            for (int y = 0; y < VIEW_H; y++) {
                int sy = top_img + y;
                if (sy >= 0 && sy < terrain.h) memcpy(idx + y * VIEW_W, terrain.px + (size_t)sy * terrain.w, VIEW_W); else memset(idx + y * VIEW_W, 0, VIEW_W);
                swiv_map_palette_row(&eng_map, sy, rowpal[y]);
            }
            SwivCanvas c = { VIEW_W, VIEW_H, idx, NULL, 0 };
            /* sort render list descending by key (simple insertion, small n) */
            static RenderEntry rl[1024 + 30]; int n = 0;
            for (int i = 0; i < render_count; i++) rl[n++] = render_list[i];
            for (int i = 0; i < player_bullet_count; i++) rl[n++] = player_bullet_render[i];
            for (int i = 1; i < n; i++) { RenderEntry e = rl[i]; int j = i - 1; while (j >= 0 && rl[j].key < e.key) { rl[j + 1] = rl[j]; j--; } rl[j + 1] = e; }
            for (int i = 0; i < n; i++) { if (rl[i].flags & 0x80) continue; if (rl[i].flags & 0x20) swiv_blit_gfx_shadow(&disk, &c, rl[i].gfx, rl[i].x, rl[i].y, 0); else swiv_blit_gfx(&disk, &c, rl[i].gfx, rl[i].x, rl[i].y); }
            /* hardware-sprite entries (player shots): own palette, drawn after the playfield */
            static uint8_t spr[VIEW_W * VIEW_H]; int have_spr = 0;
            for (int i = 0; i < n; i++) if (rl[i].flags & 0x80) {
                if (!have_spr) { memset(spr, 255, sizeof spr); have_spr = 1; }
                SwivCanvas sc = { VIEW_W, VIEW_H, spr, NULL, 0 };
                swiv_blit_gfx(&disk, &sc, rl[i].gfx, rl[i].x, rl[i].y);
                /* tag the pair: even pair -> indices as is, odd pair -> +16 */
                const SwivFrame *F = NULL; (void)F;
                if (rl[i].flags & 0x40) for (int y = 0; y < VIEW_H; y++) for (int x = 0; x < VIEW_W; x++) if (spr[y * VIEW_W + x] < 16 && spr[y * VIEW_W + x] != 255) spr[y * VIEW_W + x] |= 0x10;
            }
            for (int y = 0; y < VIEW_H; y++) {
                Color cols[16];
                for (int i = 0; i < 16; i++) { swiv_rgb12(rowpal[y][i], &cols[i].r, &cols[i].g, &cols[i].b); cols[i].a = 255; }
                for (int x = 0; x < VIEW_W; x++) buf[y * VIEW_W + x] = cols[idx[y * VIEW_W + x] & 15];
            }
            if (have_spr) {
                static const Color SPRPAL[2][4] = { { {0,0,0,0}, {255,255,255,255}, {153,153,153,255}, {136,0,0,255} }, { {0,0,0,0}, {255,255,255,255}, {153,153,153,255}, {255,136,0,255} } };
                for (int i = 0; i < VIEW_W * VIEW_H; i++) { uint8_t v = spr[i]; if (v == 255) continue; int pair = (v >> 4) & 1, ci = v & 15; if (ci && ci < 4) buf[i] = SPRPAL[pair][ci]; }
            }
            if (fe_game) { int dk = fe_play_darkness(); if (dk) { int k = 256 - dk; for (int i = 0; i < VIEW_W * VIEW_H; i++) { buf[i].r = (uint8_t)(buf[i].r * k >> 8); buf[i].g = (uint8_t)(buf[i].g * k >> 8); buf[i].b = (uint8_t)(buf[i].b * k >> 8); } } }
            int nobj = 0; FOR_EACH_OBJ(ob) nobj++;
            snprintf(status, sizeof status, "PLAY level %d %s   score %06d   objects %d   scroll %04x   tick %d%s",
                     eng_level + 1, eng_map.pam_name, g.heli.score, nobj, g.scroll3530, g.tick, game_paused ? "   PAUSED" : "");
        } else if (mode == 0) {
            if (!paused) scroll_pos += speed;
            if (scroll_pos < MAP_START) scroll_pos = MAP_START;
            if (scroll_pos > map.height) scroll_pos = map.height;
            draw_map_frame(buf);
            snprintf(status, sizeof status, "MAP %d %s   scroll %.0f / %d   speed %.4g px/frame%s",
                     map_lv + 1, map.pam_name, scroll_pos, map.height, speed, paused ? "   PAUSED" : "");
        } else {
            const SwivLin *L = swiv_lin(&disk, sp_file);
            if (sp_anim && L->nframes && (sp_t += GetFrameTime()) > 0.12) { sp_t = 0; sp_frame = (sp_frame + 1) % L->nframes; }
            draw_sprite_frame(buf, palname, sizeof palname);
            const SwivPart *p = L->nframes ? &L->frames[sp_frame].parts[0] : NULL;
            snprintf(status, sizeof status, "%s   frame %d/%d   %dx%d  centre (%d,%d)  trans %d  parts %d   pal %s",
                     disk.files[sp_file].name, sp_frame + 1, L->nframes, p ? p->w : 0, p ? p->h : 0, p ? p->cx : 0, p ? p->cy : 0,
                     p ? p->trans : 0, L->nframes ? L->frames[sp_frame].nparts : 0, palname);
        }
        UpdateTexture(tex, buf);
        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexturePro(tex, (Rectangle){0, 0, VIEW_W, VIEW_H}, (Rectangle){0, 0, WIN_W, VIEW_H * SCALE}, (Vector2){0, 0}, 0, WHITE);
        int by = VIEW_H * SCALE;
        DrawRectangle(0, by, WIN_W, BAR_H, (Color){28, 28, 34, 255});
                if (mode == 6) {
            int x0 = 40, y0 = 40, rh = 70; char l[64];
            ui_text("OPTIONS", x0, y0, 34, RAYWHITE);
            ui_text("Sound effects volume", x0, y0 + 70, 26, (Color){255, 238, 136, 255});
            if (button((Rectangle){x0 + 420, y0 + 60, 56, 48}, "-", 0) && opt.sfx_vol > 0) { opt.sfx_vol--; options_apply(); sfx(SFX_SHOT, 160); }
            snprintf(l, sizeof l, "%d", opt.sfx_vol); ui_text(l, x0 + 500, y0 + 70, 26, RAYWHITE);
            if (button((Rectangle){x0 + 560, y0 + 60, 56, 48}, "+", 0) && opt.sfx_vol < 10) { opt.sfx_vol++; options_apply(); sfx(SFX_SHOT, 160); }
            ui_text("Music volume", x0, y0 + 70 + rh, 26, (Color){255, 238, 136, 255});
            if (button((Rectangle){x0 + 420, y0 + 60 + rh, 56, 48}, "-", 0) && opt.music_vol > 0) { opt.music_vol--; options_apply(); }
            snprintf(l, sizeof l, "%d", opt.music_vol); ui_text(l, x0 + 500, y0 + 70 + rh, 26, RAYWHITE);
            if (button((Rectangle){x0 + 560, y0 + 60 + rh, 56, 48}, "+", 0) && opt.music_vol < 10) { opt.music_vol++; options_apply(); }
            ui_text("Fullscreen", x0, y0 + 70 + 2 * rh, 26, (Color){255, 238, 136, 255});
            if (button((Rectangle){x0 + 420, y0 + 60 + 2 * rh, 196, 48}, opt.fullscreen ? "ON" : "OFF", opt.fullscreen)) { opt.fullscreen ^= 1; options_apply(); }
            ui_text("Difficulty", x0, y0 + 70 + 3 * rh, 26, (Color){255, 238, 136, 255});
            { static const char *DN[3] = { "EASY", "NORMAL", "HARD" }; for (int d = 0; d < 3; d++) if (button((Rectangle){x0 + 420 + d * 130, y0 + 60 + 3 * rh, 124, 48}, DN[d], opt.difficulty == d)) { opt.difficulty = d; options_apply(); } }
            ui_text("easy: half the enemies    hard: 1.5x aerial, 2x ground (applies from the next game)", x0, y0 + 110 + 3 * rh, 20, LIGHTGRAY);
            ui_text("Controls", x0, y0 + 150 + 3 * rh, 26, (Color){255, 238, 136, 255});
            ui_text("Port 2 helicopter: arrows + Space (or first gamepad: stick/d-pad, A/X/trigger fire)", x0, y0 + 190 + 3 * rh, 22, LIGHTGRAY);
            ui_text("Port 1 jeep: WASD + Left Shift/Ctrl (or second gamepad)", x0, y0 + 220 + 3 * rh, 22, LIGHTGRAY);
            ui_text("P = pause, ESC (while paused) = menu; touch: left half steer, right half fire", x0, y0 + 250 + 3 * rh, 22, LIGHTGRAY);
            if (button((Rectangle){x0, y0 + 300 + 3 * rh, 170, 52}, "SAVE", 0)) options_save();
            if (button((Rectangle){x0 + 180, y0 + 300 + 3 * rh, 170, 52}, "TITLE", 0)) { options_save(); mode = 3; }
        }
        static float extra_scroll = 0;
        if (mode == 5) {
            static int extra_init = 0; if (!extra_init) { extras_scan(extra_cat); extra_init = 1; }
            int cx = 16;
            for (int c = 0; c < N_EXTRA_CATS; c++) { int w = ui_measure(EXTRA_CATS[c].title, 22) + 28; if (button((Rectangle){cx, 12, w, 44}, EXTRA_CATS[c].title, extra_cat == c)) { extra_cat = c; extras_scan(c); extra_scroll = 0; } cx += w + 8; }
            if (button((Rectangle){WIN_W - 120, 12, 104, 44}, "TITLE", 0)) mode = 3;
            int top = 70, avail_h = VIEW_H * SCALE + BAR_H - top - 16;
            if (!EXTRA_CATS[extra_cat].dir) {
                /* cheats text, wrapped */
                extra_scroll -= GetMouseWheelMove() * 40; if (extra_scroll < 0) extra_scroll = 0;
                BeginScissorMode(0, top, WIN_W, avail_h);
                float y = top - extra_scroll; const char *t = cheats_text; char line[512];
                while (*t) { const char *nl = strchr(t, '\n'); size_t len = nl ? (size_t)(nl - t) : strlen(t); if (len > 500) len = 500; memcpy(line, t, len); line[len] = 0;
                    /* wrap at ~100 chars */ char *w = line; while (*w) { size_t l2 = strlen(w); if (l2 > 105) { size_t cut = 105; while (cut > 60 && w[cut] != ' ') cut--; char sv = w[cut]; w[cut] = 0; ui_text(w, 24, y, 22, RAYWHITE); w[cut] = sv; w += cut + (sv == ' '); } else { ui_text(w, 24, y, 22, RAYWHITE); w += l2; } y += 30; }
                    if (len == 0) y += 12; t = nl ? nl + 1 : t + len; }
                EndScissorMode();
            } else {
                extras_load();
                if (extra_n == 0) ui_text("(nothing downloaded here)", 24, top + 20, 24, LIGHTGRAY);
                else {
                    float sc = (float)(WIN_W - 32) / extra_tex.width; if (extra_tex.height * sc > avail_h - 70) sc = (float)(avail_h - 70) / extra_tex.height;
                    if (0) { /* tall map: scroll vertically */ sc = (float)(WIN_W - 32) / extra_tex.width; if (sc > 3) sc = 3; extra_scroll -= GetMouseWheelMove() * 200; float maxs = extra_tex.height * sc - (avail_h - 70); if (maxs < 0) maxs = 0; if (extra_scroll < 0) extra_scroll = 0; if (extra_scroll > maxs) extra_scroll = maxs;
                        BeginScissorMode(0, top, WIN_W, avail_h - 70); DrawTextureEx(extra_tex, (Vector2){(WIN_W - extra_tex.width * sc) / 2, top - extra_scroll}, 0, sc, WHITE); EndScissorMode(); }
                    else DrawTextureEx(extra_tex, (Vector2){(WIN_W - extra_tex.width * sc) / 2, top}, 0, sc, WHITE);
                    char l[64]; snprintf(l, sizeof l, "%d / %d", extra_idx + 1, extra_n);
                    int by3 = top + avail_h - 56;
                    if (button((Rectangle){WIN_W / 2 - 200, by3, 120, 48}, "<", 0)) { extra_idx = (extra_idx + extra_n - 1) % extra_n; extra_scroll = 0; }
                    ui_text(l, WIN_W / 2 - ui_measure(l, 24) / 2, by3 + 12, 24, RAYWHITE);
                    if (button((Rectangle){WIN_W / 2 + 80, by3, 120, 48}, ">", 0)) { extra_idx = (extra_idx + 1) % extra_n; extra_scroll = 0; }
                    if (IsKeyPressed(KEY_RIGHT)) { extra_idx = (extra_idx + 1) % extra_n; extra_scroll = 0; } if (IsKeyPressed(KEY_LEFT)) { extra_idx = (extra_idx + extra_n - 1) % extra_n; extra_scroll = 0; }
                }
            }
        }
        if (mode == 4) {
            /* SFX debug: one row per game trigger: trigger name, assigned sound (short description) below, < > to change, tuning */
            static float sc = 0; int nb = audio_bank_count(); int x0 = 16, y0 = 16, row_h = 64, list_y = y0 + 44, list_h = VIEW_H * SCALE - 130;
            ui_text("SOUND EFFECTS - per game trigger: pick the sound, adjust pitch / volume / length, SAVE", x0, y0, 26, RAYWHITE);
            sc -= GetMouseWheelMove() * row_h * 2; float maxs = SFX_COUNT * row_h - list_h; if (maxs < 0) maxs = 0; if (sc < 0) sc = 0; if (sc > maxs) sc = maxs;
            BeginScissorMode(0, list_y, WIN_W, list_h);
            for (int e = 0; e < SFX_COUNT; e++) {
                float y = list_y + e * row_h - sc; if (y + row_h < list_y || y > list_y + list_h) continue;
                int b = audio_event_bank(e);
                ui_text(sfx_event_names[e], x0, y + 4, 24, (Color){255, 238, 136, 255});
                char desc[160] = "(builtin)";
                if (b >= 0) { snprintf(desc, sizeof desc, "%s", audio_bank_label(b)); char *c = strchr(desc, ':'); if (c) *c = 0; char *paren = strstr(desc, " ("); if (paren) *paren = 0; }
                ui_text(desc, x0, y + 34, 18, LIGHTGRAY);
                if (button((Rectangle){x0 + 300, y + 6, 48, 48}, "<", 0)) { b = (b <= 0 ? nb : b) - 1; audio_event_set(e, b); audio_bank_play(b); }
                if (button((Rectangle){x0 + 356, y + 6, 90, 48}, "PLAY", 0)) sfx(e, 160);
                if (button((Rectangle){x0 + 454, y + 6, 48, 48}, ">", 0)) { b = (b + 1) % (nb ? nb : 1); audio_event_set(e, b); audio_bank_play(b); }
                if (b >= 0) {
                    char l[48]; float pv = audio_tune_get(b, 0), vv = audio_tune_get(b, 1), mv = audio_tune_get(b, 2);
                    if (button((Rectangle){x0 + 530, y + 6, 44, 48}, "-", 0)) { audio_tune_set(b, 0, pv / 1.0595f); audio_bank_play(b); }
                    snprintf(l, sizeof l, "pitch %.2f", pv); ui_text(l, x0 + 582, y + 18, 20, RAYWHITE);
                    if (button((Rectangle){x0 + 700, y + 6, 44, 48}, "+", 0)) { audio_tune_set(b, 0, pv * 1.0595f); audio_bank_play(b); }
                    if (button((Rectangle){x0 + 770, y + 6, 44, 48}, "-", 0)) { audio_tune_set(b, 1, vv > 0.1f ? vv - 0.1f : 0); audio_bank_play(b); }
                    snprintf(l, sizeof l, "vol %.1f", vv); ui_text(l, x0 + 822, y + 18, 20, RAYWHITE);
                    if (button((Rectangle){x0 + 910, y + 6, 44, 48}, "+", 0)) { audio_tune_set(b, 1, vv < 2.0f ? vv + 0.1f : 2.0f); audio_bank_play(b); }
                    if (button((Rectangle){x0 + 980, y + 6, 44, 48}, "-", 0)) { audio_tune_set(b, 2, mv > 0.25f ? mv - 0.25f : 0.25f); audio_bank_play(b); }
                    snprintf(l, sizeof l, "len %.2fs", mv); ui_text(l, x0 + 1032, y + 18, 20, RAYWHITE);
                    if (button((Rectangle){x0 + 1160, y + 6, 44, 48}, "+", 0)) { audio_tune_set(b, 2, mv + 0.25f); audio_bank_play(b); }
                }
            }
            EndScissorMode();
            int by2 = list_y + list_h + 10;
            if (button((Rectangle){x0, by2, 170, 52}, "SAVE", 0)) { audio_tune_save(); audio_map_save(); }
            if (button((Rectangle){x0 + 180, by2, 170, 52}, "TITLE", 0)) mode = 3;
        }
        int dev = (debug_ui || (mode == 0 || mode == 1)) && mode != 5 && mode != 6;      /* dev bar in map/sprite modes or when DEBUG is on */
        if (dev) ui_text(status, 8, by + 4, 16, RAYWHITE);
        float r1 = by + 26, r2 = by + 72, bh = 40;
        if (mode == 2) {
            /* score panel in the bottom bar, game font (LAB_0593 status format): PLAYER 1 heli left, HI centre, PLAYER 2 jeep right */
            #define SW 426                 /* strip width: drawn at 3x (1278 px) so left status, HI and right status fit */
            static uint8_t strip[SW * 7]; static Color simg[SW * 8]; static Texture2D stex, stex2; static int stex_ok = 0;
            if (!stex_ok) { Image im = { simg, SW, 8, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 }; stex = LoadTextureFromImage(im); stex2 = LoadTextureFromImage(im); stex_ok = 1; }
            static const Color GRAD[7] = { {136,136,221,255}, {170,170,238,255}, {204,204,255,255}, {204,204,255,255}, {204,204,255,255}, {170,170,238,255}, {136,136,221,255} };
            char line[256], st[64];
            int hi = g.heli.hiscore80 > g.heli.score ? g.heli.hiscore80 : g.heli.score; if (g.jeep.score > hi) hi = g.jeep.score; if (g.jeep.hiscore80 > hi) hi = g.jeep.hiscore80;
            FeHud h = hud_of(&g.heli, 0), j = hud_of(&g.jeep, 0);
            fe_status_line(st, sizeof st, 0, &h); snprintf(line, sizeof line, "_x008_a1%s_f", st);
            fe_status_line(st, sizeof st, 1, &j); { char r2s[96]; snprintf(r2s, sizeof r2s, "_x418_a2%s_f", st); strncat(line, r2s, sizeof line - strlen(line) - 1); }
            { char hs[64]; snprintf(hs, sizeof hs, "_x213_a0HI %06d0_f", hi % 1000000); strncat(line, hs, sizeof line - strlen(line) - 1); }
            fe_text_strip(strip, SW, line);
            for (int y = 0; y < 8; y++) for (int x = 0; x < SW; x++) simg[y * SW + x] = (y < 7 && strip[y * SW + x]) ? GRAD[y] : (Color){0, 0, 0, 0};
            UpdateTexture(stex, simg);
            DrawTexturePro(stex, (Rectangle){0, 0, SW, 8}, (Rectangle){1, by + 8, SW * 3, 24}, (Vector2){0, 0}, 0, WHITE);
            /* per-player shots / hits / accuracy under each score */
            { char l2[96]; snprintf(line, sizeof line, "_x008_a1shots %d  hits %d  %d%%_f", g.stat_shots_p[0], g.stat_hits_p[0], g.stat_shots_p[0] ? g.stat_hits_p[0] * 100 / g.stat_shots_p[0] : 0);
              snprintf(l2, sizeof l2, "_x418_a2shots %d  hits %d  %d%%_f", g.stat_shots_p[1], g.stat_hits_p[1], g.stat_shots_p[1] ? g.stat_hits_p[1] * 100 / g.stat_shots_p[1] : 0); strncat(line, l2, sizeof line - strlen(line) - 1); }
            fe_text_strip(strip, SW, line);
            for (int y = 0; y < 8; y++) for (int x = 0; x < SW; x++) simg[y * SW + x] = (y < 7 && strip[y * SW + x]) ? (Color){200, 200, 200, 255} : (Color){0, 0, 0, 0};
            UpdateTexture(stex2, simg);
            DrawTexturePro(stex2, (Rectangle){0, 0, SW, 8}, (Rectangle){1, by + BAR_H - 30, SW * 3, 24}, (Vector2){0, 0}, 0, WHITE);   /* stats at the foot of the bar, gap from the score line */
            if (game_paused) { const char *t = "PAUSED"; ui_text(t, (WIN_W - ui_measure(t, 48)) / 2, VIEW_H * SCALE / 2 - 40, 48, RAYWHITE); const char *u = "P to resume   ESC for menu"; ui_text(u, (WIN_W - ui_measure(u, 24)) / 2, VIEW_H * SCALE / 2 + 20, 24, LIGHTGRAY); }
        }
        if (mode == 3) {
            if (button((Rectangle){WIN_W - 108, r1, 100, bh}, "DEBUG", debug_ui)) debug_ui ^= 1;
            if (button((Rectangle){WIN_W - 236, r1, 120, bh}, "EXTRAS", 0)) { mode = 5; }
            if (button((Rectangle){WIN_W - 364, r1, 120, bh}, "QUIT", 0)) { CloseWindow(); return 0; }
            if (button((Rectangle){WIN_W - 500, r1, 128, bh}, "OPTIONS", 0)) { mode = 6; }
            { static float mx = 0; const char *msg = "In 2026 Retro Recomps brings you SWIV Amiga, fully native.  Enjoy this all-in-one package.  See you in the next one.        PORT 1 JEEP: WASD + Shift / pad 1 / tap left  --  PORT 2 HELICOPTER: arrows + Space / pad 0 / tap right  --  press fire to start, P pauses, ESC (paused) for menu  --  F1-F10 / F11: the game's own controls screen        ";
              int lw = 0; if (rr_logo.id) { float lsc = (BAR_H - 16) / (float)rr_logo.height; lw = (int)(rr_logo.width * lsc) + 24; DrawTextureEx(rr_logo, (Vector2){8, by + 8}, 0, lsc, WHITE); }
              int fs = 26, w = ui_measure(msg, fs); mx -= 1.5f; if (mx < -w) mx += w;
              BeginScissorMode(lw, r2, WIN_W - lw, bh); ui_text(msg, lw + (int)mx, r2 + 10, fs, (Color){255, 238, 136, 255}); ui_text(msg, lw + (int)mx + w, r2 + 10, fs, (Color){255, 238, 136, 255}); EndScissorMode(); }
            if (debug_ui) {
                if (button((Rectangle){8, r1, 100, bh}, "MAP", 0)) mode = 0;
                if (button((Rectangle){116, r1, 100, bh}, "SPRITES", 0)) mode = 1;
                if (button((Rectangle){224, r1, 100, bh}, "SFX", 0)) mode = 4;
                if (button((Rectangle){332, r1, 100, bh}, "PLAY", 0)) { mode = 2; game_on = 0; fe_game = 0; }
            }
        }
        if (mode != 3 && (dev || mode == 2)) {
            if (dev) {
                if (button((Rectangle){8, r1, 100, bh}, "MAP", mode == 0)) mode = 0;
                if (button((Rectangle){8, r2, 100, bh}, "SPRITES", mode == 1)) mode = 1;
                if (button((Rectangle){WIN_W - 108, mode == 2 ? r2 : r1, 100, bh}, "PLAY", mode == 2)) { if (mode == 2) { game_on = 0; } mode = 2; fe_game = 0; }
                if (mode != 2 && button((Rectangle){WIN_W - 108, r2, 100, bh}, "TITLE", mode == 3)) mode = 3;
            }
        }
        if (mode == 2) {
            if (IsKeyPressed(KEY_P)) game_paused ^= 1;
            if (game_paused && IsKeyPressed(KEY_ESCAPE)) { mode = 3; game_on = 0; game_paused = 0; fe_game = 0; fe_start_title(); }   /* paused + ESC = straight back to the title */
            if (dev) {
                if (button((Rectangle){8, r1, 100, bh}, game_paused ? "RESUME" : "PAUSE", game_paused)) game_paused ^= 1;
                if (button((Rectangle){116, r1, 100, bh}, "DEBUG", debug_ui)) debug_ui ^= 1;
                if (button((Rectangle){224, r1, 100, bh}, "QUIT", 0)) { mode = 3; game_on = 0; if (fe_game) { fe_game = 0; fe_start_title(); } }
                for (int k = 0; k < 7; k++) {
                    char l[4]; snprintf(l, 4, "%d", k + 1);
                    if (button((Rectangle){120 + k * 52, r2, 48, bh}, l, eng_level == k)) { map_lv = k; eng_init(&disk, k); player_start(); eng_level = k; }
                }
                if (button((Rectangle){500, r2, 110, bh}, "RESTART", 0)) { eng_init(&disk, eng_level); player_start(); }
                if (button((Rectangle){620, r2, 100, bh}, "MAP", 0)) mode = 0;
                if (button((Rectangle){730, r2, 100, bh}, "SPRITES", 0)) mode = 1;
                if (button((Rectangle){840, r2, 100, bh}, "SFX", 0)) mode = 4;
            }
        } else if (mode == 0) {
            for (int k = 0; k < 7; k++) {
                char l[4]; snprintf(l, 4, "%d", k + 1);
                if (button((Rectangle){120 + k * 52, r1, 48, bh}, l, map_lv == k)) load_level(k);
            }
            if (button((Rectangle){500, r1, 90, bh}, paused ? "PLAY" : "PAUSE", paused)) paused ^= 1;

            /* row 2: speed + scrub */
            ui_text("SPEED", 120, r2 + 12, 16, LIGHTGRAY);
            if (button((Rectangle){190, r2, 48, bh}, "/2", 0)) speed /= 2;
            static const float presets[] = {0.25f, 0.5f, 1, 2, 4};
            for (int k = 0; k < 5; k++) {
                char l[8]; snprintf(l, 8, "%g", presets[k]);
                if (button((Rectangle){246 + k * 52, r2, 48, bh}, l, speed == presets[k])) speed = presets[k];
            }
            if (button((Rectangle){506, r2, 48, bh}, "x2", 0)) speed *= 2;
            if (held((Rectangle){600, r2, 90, bh})) scroll_pos -= 4; button((Rectangle){600, r2, 90, bh}, "<< BACK", 0);
            if (held((Rectangle){700, r2, 90, bh})) scroll_pos += 4; button((Rectangle){700, r2, 90, bh}, "FWD >>", 0);
            /* progress bar, tappable */
            Rectangle pb = {800, r2, 150, bh};
            DrawRectangleRec(pb, (Color){50, 50, 58, 255});
            DrawRectangle(pb.x, pb.y + pb.height * (1 - scroll_pos / (map.height ? map.height : 1)), pb.width, 3, SKYBLUE);
            if (held(pb)) { Vector2 p = GetMousePosition(); scroll_pos = (1 - (p.y - pb.y) / pb.height) * map.height; }
        } else if (mode == 1) {
            if (button((Rectangle){120, r1, 90, bh}, "< FILE", 0)) { sp_file = next_lin(sp_file, -1); sp_frame = 0; }
            if (button((Rectangle){216, r1, 90, bh}, "FILE >", 0)) { sp_file = next_lin(sp_file, 1); sp_frame = 0; }
            const SwivLin *L = swiv_lin(&disk, sp_file);
            if (button((Rectangle){330, r1, 90, bh}, "< FRAME", 0) && L->nframes) sp_frame = (sp_frame + L->nframes - 1) % L->nframes;
            if (button((Rectangle){426, r1, 90, bh}, "FRAME >", 0) && L->nframes) sp_frame = (sp_frame + 1) % L->nframes;
            if (button((Rectangle){540, r1, 90, bh}, "ANIM", sp_anim)) sp_anim ^= 1;
            if (button((Rectangle){640, r1, 70, bh}, sp_zoom == 1 ? "x1" : sp_zoom == 2 ? "x2" : "x4", 0)) sp_zoom = sp_zoom == 4 ? 1 : sp_zoom * 2;
        }
        EndDrawing();
        if (shot && --shot_frames == 0) { TakeScreenshot(shot); break; }
    }
    CloseWindow();
    return 0;
}
