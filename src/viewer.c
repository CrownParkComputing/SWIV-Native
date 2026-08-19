/* swivview -- native SWIV map scroller + sprite browser (raylib, no 68000).
 *
 *   TAB          switch Map / Sprites
 *   Map:   1-7 level, SPACE pause, UP/DOWN scrub, +/- speed, O objects on/off, HOME restart
 *   Sprites: LEFT/RIGHT file, UP/DOWN frame, PGUP/PGDN palette source (level), A animate
 *   F2 screenshot, ESC quit
 */
#include "swivdata.h"
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIEW_W 320
#define VIEW_H 256          /* PAL window: HUD rows aside, the game shows 256 rows */
#define SCALE 3

static SwivDisk disk;
static SwivMap map; static SwivCanvas canvas; static int map_lv = -1, map_objs = 0;
static double scroll_pos;   /* scroll counter in px, counts DOWN like the game */
static float speed = 0.25f; static int paused = 0;

static Texture2D tex; static Image img;

static void load_level(int lv) {
    if (map_lv >= 0) { swiv_map_free(&map); swiv_canvas_free(&canvas); }
    swiv_map_load(&disk, lv, &map);
    swiv_map_render(&disk, &map, &canvas, map_objs);
    map_lv = lv; scroll_pos = 0;
}

/* scroll_pos = how far the level has advanced; image row of the window top */
static int window_top(void) {
    int bottom_row = map.height + SWIV_MARGIN - (int)scroll_pos;   /* map y=scroll at screen bottom */
    return bottom_row - VIEW_H;
}

static void draw_map_frame(Color *out) {
    int top = window_top();
    uint16_t pal[16]; swiv_map_palette_at(&map, (int)scroll_pos, pal);   /* one palette per frame, as the game */
    Color cols[16];
    for (int i = 0; i < 16; i++) { swiv_rgb12(pal[i], &cols[i].r, &cols[i].g, &cols[i].b); cols[i].a = 255; }
    for (int y = 0; y < VIEW_H; y++) {
        int sy = top + y;
        for (int x = 0; x < VIEW_W; x++)
            out[y * VIEW_W + x] = (sy >= 0 && sy < canvas.h) ? cols[canvas.px[(size_t)sy * canvas.w + x] & 15] : BLACK;
    }
}

/* ---- sprite browser ---- */
static int sp_file = 0, sp_frame = 0, sp_pal_lv = 0, sp_anim = 0; static double sp_t;
static int is_lin(int i) { const char *n = disk.files[i].name; size_t l = strlen(n); return l > 4 && !strcasecmp(n + l - 4, ".LIN"); }
static int next_lin(int i, int dir) { for (int k = 0; k < disk.nfiles; k++) { i = (i + dir + disk.nfiles) % disk.nfiles; if (is_lin(i)) return i; } return i; }

static void draw_sprite_frame(Color *out) {
    uint16_t pal[16]; SwivMap pm; swiv_map_load(&disk, sp_pal_lv, &pm); swiv_map_palette_at(&pm, 0, pal); swiv_map_free(&pm);
    Color cols[16];
    for (int i = 0; i < 16; i++) { swiv_rgb12(pal[i], &cols[i].r, &cols[i].g, &cols[i].b); cols[i].a = 255; }
    SwivCanvas c; swiv_canvas_init(&c, VIEW_W, VIEW_H, 255);
    const SwivLin *L = swiv_lin(&disk, sp_file);
    if (L->nframes) {
        if (sp_frame >= L->nframes) sp_frame = 0;
        const SwivFrame *F = &L->frames[sp_frame];
        for (int i = 0; i < F->nparts; i++) swiv_blit_part(&c, &F->parts[i], VIEW_W / 2 - F->parts[i].cx, VIEW_H / 2 - F->parts[i].cy);
        /* strip of all frames along the bottom */
        int x = 2;
        for (int f = 0; f < L->nframes && x < VIEW_W; f++) {
            const SwivPart *p = &L->frames[f].parts[0];
            swiv_blit_part(&c, p, x, VIEW_H - 4 - p->h);
            x += p->w + 2;
        }
    }
    for (int i = 0; i < VIEW_W * VIEW_H; i++) out[i] = c.px[i] == 255 ? (Color){34, 34, 44, 255} : cols[c.px[i] & 15];
    swiv_canvas_free(&c);
}

int main(int argc, char **argv) {
    const char *adf = "/home/jon/swiv-amiga-re/SWIVFIX.ADF";
    const char *shot = NULL; int shot_mode = 0; double shot_scroll = 0; int shot_frames = 10;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--adf")) adf = argv[++i];
        else if (!strcmp(argv[i], "--shot")) shot = argv[++i];          /* write screenshot and exit */
        else if (!strcmp(argv[i], "--sprites")) shot_mode = 1;
        else if (!strcmp(argv[i], "--scroll")) shot_scroll = atof(argv[++i]);
    }
    if (swiv_open(&disk, adf)) { fprintf(stderr, "cannot open %s\n", adf); return 1; }
    InitWindow(VIEW_W * SCALE, VIEW_H * SCALE + 24, "SWIV native viewer");
    SetTargetFPS(50);
    img = GenImageColor(VIEW_W, VIEW_H, BLACK); tex = LoadTextureFromImage(img);
    Color *buf = malloc(sizeof(Color) * VIEW_W * VIEW_H);
    int mode = shot_mode; load_level(0); sp_file = next_lin(0, 1); scroll_pos = shot_scroll;
    if (shot) { map_objs = 1; load_level(0); scroll_pos = shot_scroll; }
    char status[256];
    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_TAB)) mode ^= 1;
        if (IsKeyPressed(KEY_F2)) TakeScreenshot("swivview.png");
        if (mode == 0) {
            for (int k = 0; k < 7; k++) if (IsKeyPressed(KEY_ONE + k)) load_level(k);
            if (IsKeyPressed(KEY_SPACE)) paused ^= 1;
            if (IsKeyPressed(KEY_O)) { map_objs ^= 1; double s = scroll_pos; load_level(map_lv); scroll_pos = s; }
            if (IsKeyPressed(KEY_HOME)) scroll_pos = 0;
            if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) speed *= 2;
            if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) speed /= 2;
            if (IsKeyDown(KEY_UP)) scroll_pos += 4; if (IsKeyDown(KEY_DOWN)) scroll_pos -= 4;
            if (!paused) scroll_pos += speed;
            if (scroll_pos < 0) scroll_pos = 0;
            if (scroll_pos > map.height) scroll_pos = map.height;
            draw_map_frame(buf);
            snprintf(status, sizeof status, "MAP %d %s  scroll %.0f/%d  speed %.3g px/f  %s  objects:%s   [TAB sprites]",
                     map_lv + 1, map.pam_name, scroll_pos, map.height, speed, paused ? "PAUSED" : "", map_objs ? "on" : "off");
        } else {
            if (IsKeyPressed(KEY_RIGHT)) { sp_file = next_lin(sp_file, 1); sp_frame = 0; }
            if (IsKeyPressed(KEY_LEFT)) { sp_file = next_lin(sp_file, -1); sp_frame = 0; }
            const SwivLin *L = swiv_lin(&disk, sp_file);
            if (IsKeyPressed(KEY_UP) && L->nframes) sp_frame = (sp_frame + 1) % L->nframes;
            if (IsKeyPressed(KEY_DOWN) && L->nframes) sp_frame = (sp_frame + L->nframes - 1) % L->nframes;
            if (IsKeyPressed(KEY_PAGE_UP)) sp_pal_lv = (sp_pal_lv + 1) % 7;
            if (IsKeyPressed(KEY_PAGE_DOWN)) sp_pal_lv = (sp_pal_lv + 6) % 7;
            if (IsKeyPressed(KEY_A)) sp_anim ^= 1;
            if (sp_anim && L->nframes && (sp_t += GetFrameTime()) > 0.1) { sp_t = 0; sp_frame = (sp_frame + 1) % L->nframes; }
            draw_sprite_frame(buf);
            const SwivPart *p = L->nframes ? &L->frames[sp_frame].parts[0] : NULL;
            snprintf(status, sizeof status, "SPRITE %s  frame %d/%d  %dx%d c(%d,%d) trans %d parts %d  pal:level %d   [TAB map]",
                     disk.files[sp_file].name, sp_frame + 1, L->nframes, p ? p->w : 0, p ? p->h : 0, p ? p->cx : 0, p ? p->cy : 0,
                     p ? p->trans : 0, L->nframes ? L->frames[sp_frame].nparts : 0, sp_pal_lv + 1);
        }
        UpdateTexture(tex, buf);
        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexturePro(tex, (Rectangle){0, 0, VIEW_W, VIEW_H}, (Rectangle){0, 0, VIEW_W * SCALE, VIEW_H * SCALE}, (Vector2){0, 0}, 0, WHITE);
        DrawText(status, 4, VIEW_H * SCALE + 4, 16, RAYWHITE);
        EndDrawing();
        if (shot && --shot_frames == 0) { TakeScreenshot(shot); break; }
    }
    CloseWindow();
    return 0;
}
