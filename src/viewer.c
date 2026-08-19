/* swivview -- native SWIV map scroller + sprite browser (raylib, no 68000).
 * All controls are on-screen buttons (mouse or touch; keys are optional extras). */
#include "swivdata.h"
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIEW_W 320
#define VIEW_H 256
#define SCALE 3
#define BAR_H 120                 /* control bar below the view */
#define WIN_W (VIEW_W * SCALE)
#define WIN_H (VIEW_H * SCALE + BAR_H)
#define AMPROG_PAL 0x299C
#define N_AMPROG_PAL 11

static SwivDisk disk;
static SwivMap map; static SwivCanvas canvas; static int map_lv = -1, map_objs = 1;
static double scroll_pos; static float speed = 0.25f; static int paused = 0;
static Texture2D tex; static Image img;
static int mode = 0;              /* 0 map, 1 sprites */

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
    int fs = 20; int tw = MeasureText(label, fs);
    while (tw > r.width - 6 && fs > 10) { fs -= 2; tw = MeasureText(label, fs); }
    DrawText(label, r.x + (r.width - tw) / 2, r.y + (r.height - fs) / 2, fs, RAYWHITE);
    return hot && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}
static int held(Rectangle r) { return ui_hit(r) && IsMouseButtonDown(MOUSE_BUTTON_LEFT); }

/* ---- map ---- */
static void load_level(int lv) {
    if (map_lv >= 0) { swiv_map_free(&map); swiv_canvas_free(&canvas); }
    swiv_map_load(&disk, lv, &map);
    swiv_map_render(&disk, &map, &canvas, map_objs);
    map_lv = lv; scroll_pos = 0;
}
static void draw_map_frame(Color *out) {
    int top = map.height + SWIV_MARGIN - (int)scroll_pos - VIEW_H;
    uint16_t pal[16]; swiv_map_palette_at(&map, (int)scroll_pos, pal);
    Color cols[16];
    for (int i = 0; i < 16; i++) { swiv_rgb12(pal[i], &cols[i].r, &cols[i].g, &cols[i].b); cols[i].a = 255; }
    for (int y = 0; y < VIEW_H; y++) {
        int sy = top + y;
        for (int x = 0; x < VIEW_W; x++)
            out[y * VIEW_W + x] = (sy >= 0 && sy < canvas.h) ? cols[canvas.px[(size_t)sy * canvas.w + x] & 15] : BLACK;
    }
}

/* ---- sprites ---- */
static int sp_file = 0, sp_frame = 0, sp_pal = N_AMPROG_PAL, sp_anim = 1, sp_zoom = 2; static double sp_t;
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
    const char *shot = NULL; double shot_scroll = 0; int shot_frames = 10; const char *shot_file = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--adf")) adf = argv[++i];
        else if (!strcmp(argv[i], "--shot")) shot = argv[++i];
        else if (!strcmp(argv[i], "--sprites")) mode = 1;
        else if (!strcmp(argv[i], "--file")) shot_file = argv[++i];
        else if (!strcmp(argv[i], "--pal")) sp_pal = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scroll")) shot_scroll = atof(argv[++i]);
    }
    if (swiv_open(&disk, adf)) { fprintf(stderr, "cannot open %s\n", adf); return 1; }
    InitWindow(WIN_W, WIN_H, "SWIV native viewer");
    SetTargetFPS(50);
    img = GenImageColor(VIEW_W, VIEW_H, BLACK); tex = LoadTextureFromImage(img);
    Color *buf = malloc(sizeof(Color) * VIEW_W * VIEW_H);
    load_level(0); scroll_pos = shot_scroll; sp_file = next_lin(0, 1);
    if (shot_file) { int f = swiv_find(&disk, shot_file); if (f >= 0) sp_file = f; }
    char status[256], palname[64] = "";
    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_F2)) TakeScreenshot("swivview.png");
        if (mode == 0) {
            if (!paused) scroll_pos += speed;
            if (scroll_pos < 0) scroll_pos = 0;
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
        DrawText(status, 8, by + 4, 16, RAYWHITE);
        float r1 = by + 26, r2 = by + 72, bh = 40;
        /* left: mode toggle */
        if (button((Rectangle){8, r1, 100, bh}, mode ? "MAP" : "SPRITES", 0)) mode ^= 1;
        if (mode == 0) {
            for (int k = 0; k < 7; k++) {
                char l[4]; snprintf(l, 4, "%d", k + 1);
                if (button((Rectangle){120 + k * 52, r1, 48, bh}, l, map_lv == k)) load_level(k);
            }
            if (button((Rectangle){500, r1, 90, bh}, paused ? "PLAY" : "PAUSE", paused)) paused ^= 1;
            if (button((Rectangle){600, r1, 90, bh}, "OBJECTS", map_objs)) { double s = scroll_pos; map_objs ^= 1; load_level(map_lv); scroll_pos = s; }
            if (button((Rectangle){700, r1, 90, bh}, "RESTART", 0)) scroll_pos = 0;
            /* row 2: speed + scrub */
            DrawText("SPEED", 120, r2 + 12, 16, LIGHTGRAY);
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
            Rectangle pb = {800, r1, 150, bh * 2 + 6};
            DrawRectangleRec(pb, (Color){50, 50, 58, 255});
            DrawRectangle(pb.x, pb.y + pb.height * (1 - scroll_pos / (map.height ? map.height : 1)), pb.width, 3, SKYBLUE);
            if (held(pb)) { Vector2 p = GetMousePosition(); scroll_pos = (1 - (p.y - pb.y) / pb.height) * map.height; }
        } else {
            if (button((Rectangle){120, r1, 90, bh}, "< FILE", 0)) { sp_file = next_lin(sp_file, -1); sp_frame = 0; }
            if (button((Rectangle){216, r1, 90, bh}, "FILE >", 0)) { sp_file = next_lin(sp_file, 1); sp_frame = 0; }
            const SwivLin *L = swiv_lin(&disk, sp_file);
            if (button((Rectangle){330, r1, 90, bh}, "< FRAME", 0) && L->nframes) sp_frame = (sp_frame + L->nframes - 1) % L->nframes;
            if (button((Rectangle){426, r1, 90, bh}, "FRAME >", 0) && L->nframes) sp_frame = (sp_frame + 1) % L->nframes;
            if (button((Rectangle){540, r1, 90, bh}, "ANIM", sp_anim)) sp_anim ^= 1;
            if (button((Rectangle){640, r1, 70, bh}, sp_zoom == 1 ? "x1" : sp_zoom == 2 ? "x2" : "x4", 0)) sp_zoom = sp_zoom == 4 ? 1 : sp_zoom * 2;
            DrawText("PALETTE", 120, r2 + 12, 16, LIGHTGRAY);
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
