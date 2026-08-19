/* swivview -- native SWIV map scroller + sprite browser (raylib, no 68000).
 * All controls are on-screen buttons (mouse or touch; keys are optional extras). */
#include "swivdata.h"
#include "engine/engine.h"
extern void player_start(void); extern void player_vbl(void);
extern void audio_init(SwivDisk *d); extern void audio_update(void); extern void audio_music_play(SwivDisk *d, const char *name);
extern int audio_bank_count(void); extern const char *audio_bank_name(int i); extern const char *audio_bank_label(int i); extern void audio_bank_play(int i);
extern int audio_event_bank(int ev); extern void audio_event_set(int ev, int bank); extern void audio_map_save(void);
extern float audio_tune_get(int i, int what); extern void audio_tune_set(int i, int what, float v); extern void audio_tune_save(void); extern int audio_bank_frames(int i);
extern int player_input_dx, player_input_dy, player_input_fire;
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
static Font ui_font; static int ui_font_ok;
static void ui_text(const char *t, int x, int y, int fs, Color c) { if (ui_font_ok) DrawTextEx(ui_font, t, (Vector2){(float)x, (float)y}, (float)fs, 1, c); else DrawText(t, x, y, fs, c); }
static int ui_measure(const char *t, int fs) { return ui_font_ok ? (int)MeasureTextEx(ui_font, t, (float)fs, 1).x : MeasureText(t, fs); }
static SwivMap map; static SwivCanvas canvas; static int map_lv = -1, show_ground = 1, show_air = 0;
static int game_on = 0; static int game_paused = 0; static int eng_level = 0; static int debug_ui = 0;
static double scroll_pos; static float speed = 0.25f; static int paused = 1;
static Texture2D tex; static Image img;
static int mode = 3;              /* 0 map, 1 sprites, 2 play, 3 title */

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

/* ---- title (COVER.RAW: 320x256, 4 planes sequential, 16 x RGB12 palette at the end) ---- */
static Color *cover;
static void decode_cover(void) {
    int i = swiv_find(&disk, "COVER.RAW"); uint32_t n; const uint8_t *d; if (i < 0 || !(d = swiv_load(&disk, i, &n)) || n < 40992) return;
    cover = malloc(sizeof(Color) * VIEW_W * VIEW_H); Color pal[16];
    for (int k = 0; k < 16; k++) { uint16_t v = (d[n - 32 + 2 * k] << 8) | d[n - 32 + 2 * k + 1]; swiv_rgb12(v, &pal[k].r, &pal[k].g, &pal[k].b); pal[k].a = 255; }
    for (int y = 0; y < VIEW_H; y++) for (int x = 0; x < VIEW_W; x++) {
        int v = 0; for (int pl = 0; pl < 4; pl++) if (d[pl * 40 * 256 + y * 40 + (x >> 3)] & (0x80 >> (x & 7))) v |= 1 << pl;
        cover[y * VIEW_W + x] = pal[v];
    }
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
    const char *shot = NULL; double shot_scroll = 0; int shot_frames = 10; const char *shot_file = NULL; int autofire = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--adf")) adf = argv[++i];
        else if (!strcmp(argv[i], "--shot")) shot = argv[++i];
        else if (!strcmp(argv[i], "--sprites")) mode = 1;
        else if (!strcmp(argv[i], "--play")) mode = 2;
        else if (!strcmp(argv[i], "--autofire")) autofire = 1;
        else if (!strcmp(argv[i], "--frames")) shot_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--file")) shot_file = argv[++i];
        else if (!strcmp(argv[i], "--pal")) sp_pal = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scroll")) shot_scroll = atof(argv[++i]);
    }
    if (swiv_open(&disk, adf)) { fprintf(stderr, "cannot open %s\n", adf); return 1; }
    InitWindow(WIN_W, WIN_H, "SWIV native viewer"); SetExitKey(KEY_NULL);
    { const char *fonts[] = { "/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/noto/NotoSans-Regular.ttf", NULL };
      for (int i = 0; fonts[i] && !ui_font_ok; i++) if (FileExists(fonts[i])) { ui_font = LoadFontEx(fonts[i], 40, NULL, 0); ui_font_ok = ui_font.texture.id != 0; if (ui_font_ok) SetTextureFilter(ui_font.texture, TEXTURE_FILTER_BILINEAR); } }
    if (!shot) audio_init(&disk);
    SetTargetFPS(50);
    img = GenImageColor(VIEW_W, VIEW_H, BLACK); tex = LoadTextureFromImage(img);
    Color *buf = malloc(sizeof(Color) * VIEW_W * VIEW_H);
    load_level(0); scroll_pos = shot_scroll; sp_file = next_lin(0, 1);
    if (shot_file) { int f = swiv_find(&disk, shot_file); if (f >= 0) sp_file = f; }
    char status[256], palname[64] = "";
    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_F2)) TakeScreenshot("swivview.png");
        audio_update(); audio_music_play(&disk, mode == 3 ? "AMTITUNE.MOD" : NULL);
        if (mode == 4) {
            memset(buf, 0, sizeof(Color) * VIEW_W * VIEW_H);
            snprintf(status, sizeof status, "SFX DEBUG: click a sound to hear it; select an event (right) then a sound to assign; SAVE writes sfxmap.txt");
        } else if (mode == 3) {
            if (!cover) decode_cover();
            if (cover) memcpy(buf, cover, sizeof(Color) * VIEW_W * VIEW_H); else memset(buf, 0, sizeof(Color) * VIEW_W * VIEW_H);
            int start = IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_LEFT_CONTROL) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
            Vector2 mp = GetMousePosition(); if (GetTouchPointCount() > 0) mp = GetTouchPosition(0);
            if ((IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || GetTouchPointCount() > 0) && mp.y < VIEW_H * SCALE) start = 1;
            if (start) { mode = 2; game_on = 0; }
            snprintf(status, sizeof status, "S.W.I.V.  (C) 1991 The Sales Curve / Storm  --  native  --  press fire / tap to start");
        } else if (mode == 2) {
            if (!game_on) { eng_init(&disk, map_lv < 0 ? 0 : map_lv); player_start(); game_on = 1; eng_level = map_lv < 0 ? 0 : map_lv; }
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
            if (!game_paused) { eng_vbl(); player_vbl(); }
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
        int dev = debug_ui || (mode == 0 || mode == 1);      /* dev bar in map/sprite modes or when DEBUG is on */
        if (dev) ui_text(status, 8, by + 4, 16, RAYWHITE);
        if (mode == 3 && (g.vbl / 25) % 2 == 0) { const char *t = "PRESS FIRE"; int fs = 40; ui_text(t, (WIN_W - ui_measure(t, fs)) / 2 + 2, VIEW_H * SCALE - 100 + 2, fs, BLACK); ui_text(t, (WIN_W - ui_measure(t, fs)) / 2, VIEW_H * SCALE - 100, fs, RAYWHITE); }
        if (mode == 3) g.vbl++;
        float r1 = by + 26, r2 = by + 72, bh = 40;
        if (mode == 2) {
            /* HUD over the top band of the playfield (the original's panel rows) */
            DrawRectangle(0, 0, WIN_W, 36 * SCALE / 2, (Color){0, 0, 0, 160});
            char h[128];
            snprintf(h, sizeof h, "HELI %06d", g.heli.score); ui_text(h, 16, 10, 30, (Color){255, 238, 136, 255});
            for (int i = 0; i < g.heli.lives68; i++) DrawRectangle(16 + i * 18, 44, 12, 6, (Color){255, 238, 136, 255});
            snprintf(h, sizeof h, "HI %06d", g.heli.hiscore80 > g.heli.score ? g.heli.hiscore80 : g.heli.score); ui_text(h, (WIN_W - ui_measure(h, 30)) / 2, 10, 30, RAYWHITE);
            snprintf(h, sizeof h, "JEEP %06d", g.jeep.score); ui_text(h, WIN_W - 16 - ui_measure(h, 30), 10, 30, (Color){136, 221, 255, 255});
            if (game_paused) { const char *t = "PAUSED"; ui_text(t, (WIN_W - ui_measure(t, 48)) / 2, VIEW_H * SCALE / 2 - 40, 48, RAYWHITE); const char *u = "P to resume   ESC for menu"; ui_text(u, (WIN_W - ui_measure(u, 24)) / 2, VIEW_H * SCALE / 2 + 20, 24, LIGHTGRAY); }
            if (g.heli.lives68 <= 0 && !g.heli.alive) { const char *t = "GAME OVER"; ui_text(t, (WIN_W - ui_measure(t, 48)) / 2, VIEW_H * SCALE / 2, 48, RAYWHITE); }
        }
        if (mode == 3) {
            if (button((Rectangle){WIN_W - 108, r1, 100, bh}, "DEBUG", debug_ui)) debug_ui ^= 1;
            if (debug_ui) {
                if (button((Rectangle){8, r1, 100, bh}, "MAP", 0)) mode = 0;
                if (button((Rectangle){116, r1, 100, bh}, "SPRITES", 0)) mode = 1;
                if (button((Rectangle){224, r1, 100, bh}, "SFX", 0)) mode = 4;
                if (button((Rectangle){332, r1, 100, bh}, "PLAY", 0)) { mode = 2; game_on = 0; }
            }
        }
        if (mode != 3 && (dev || mode == 2)) {
            if (dev) {
                if (button((Rectangle){8, r1, 100, bh}, "MAP", mode == 0)) mode = 0;
                if (button((Rectangle){8, r2, 100, bh}, "SPRITES", mode == 1)) mode = 1;
                if (button((Rectangle){WIN_W - 108, mode == 2 ? r2 : r1, 100, bh}, "PLAY", mode == 2)) { if (mode == 2) { game_on = 0; } mode = 2; }
                if (mode != 2 && button((Rectangle){WIN_W - 108, r2, 100, bh}, "TITLE", mode == 3)) mode = 3;
            }
        }
        if (mode == 2) {
            if (IsKeyPressed(KEY_P)) game_paused ^= 1;
            if (game_paused && IsKeyPressed(KEY_ESCAPE)) { mode = 3; game_on = 0; game_paused = 0; }
            if (dev) {
                if (button((Rectangle){8, r1, 100, bh}, game_paused ? "RESUME" : "PAUSE", game_paused)) game_paused ^= 1;
                if (button((Rectangle){116, r1, 100, bh}, "DEBUG", debug_ui)) debug_ui ^= 1;
                if (button((Rectangle){224, r1, 100, bh}, "QUIT", 0)) { mode = 3; game_on = 0; }
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
            ui_text("PALETTE", 120, r2 + 12, 16, LIGHTGRAY);
            if (button((Rectangle){216, r2, 60, bh}, "<", 0)) sp_pal = (sp_pal + N_PAL_SRC - 1) % N_PAL_SRC;
            if (button((Rectangle){282, r2, 60, bh}, ">", 0)) sp_pal = (sp_pal + 1) % N_PAL_SRC;
            for (int k = 0; k < 7; k++) {
                char l[4]; snprintf(l, 4, "L%d", k + 1);
                if (button((Rectangle){360 + k * 52, r2, 48, bh}, l, sp_pal == N_AMPROG_PAL + k)) sp_pal = N_AMPROG_PAL + k;
            }
            if (button((Rectangle){740, r2, 100, bh}, "GAME PAL", sp_pal == 2)) sp_pal = 2;
        }
        EndDrawing();
        if (shot && --shot_frames == 0) { TakeScreenshot(shot); break; }
    }
    CloseWindow();
    return 0;
}
