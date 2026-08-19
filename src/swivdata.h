/* swivdata.h -- native decoder for the SWIV (Amiga, 1991) game disk.
 * Catalogue + stream-C depacker + .LIN sprites + .PAM maps + level table.
 * Everything is a straight C port of the measured formats in
 * ~/swiv-amiga-re/docs (CATALOG.md, GRAPHICS.md, MAPS.md). */
#ifndef SWIVDATA_H
#define SWIVDATA_H
#include <stdint.h>
#include <stddef.h>

#define SWIV_MAX_FILES 256
#define SWIV_TRACK 5632           /* file area starts at track 1 */
#define SWIV_LEVTAB 0x384C        /* level table in AMPROG.OBJ */
#define SWIV_FIELD_W 320          /* visible playfield width */
#define SWIV_MARGIN 160           /* room above the map crown for tall tiles */

typedef struct {
    char name[32];
    uint32_t disk_off, stored, size;
    uint8_t *data;                /* lazily unpacked */
} SwivFile;

typedef struct {                  /* one .LIN part (a chained piece of a frame) */
    int w, h, cx, cy, trans, flags;
    const uint8_t *data; uint32_t dsz;
} SwivPart;

typedef struct {
    int nparts; SwivPart *parts;  /* parts[0] is the frame's own header */
} SwivFrame;

typedef struct {
    int nframes; SwivFrame *frames;
} SwivLin;

typedef struct {                  /* map record: y grows DOWN the image */
    int y, x, gfx, layer, type, seq;
} SwivRec;

typedef struct { int y; uint16_t pal[16]; } SwivPalCheck;

typedef struct {
    SwivRec *tiles, *objs; int ntiles, nobjs;
    SwivPalCheck *checks; int nchecks;
    int height;                   /* scroll height in px */
    char pam_name[32];
    int scroll_speed;
} SwivMap;

typedef struct {
    uint8_t *adf; size_t adf_len;
    SwivFile files[SWIV_MAX_FILES]; int nfiles;
    const uint8_t *prog; uint32_t prog_len;
    char order[512][16]; int norder;   /* game's own name table (AMPROG 0x0004) */
    SwivLin lins[SWIV_MAX_FILES];      /* parsed per file index, lazily */
    int lin_parsed[SWIV_MAX_FILES];
} SwivDisk;

int  swiv_open(SwivDisk *d, const char *adf_path);
void swiv_close(SwivDisk *d);
int  swiv_find(const SwivDisk *d, const char *name);            /* -1 if absent */
const uint8_t *swiv_load(SwivDisk *d, int idx, uint32_t *size);
const SwivLin *swiv_lin(SwivDisk *d, int idx);
int  swiv_order_index(const SwivDisk *d, int gfx_file);         /* name-table id -> file idx */

/* Level table */
int  swiv_level_count(void);
int  swiv_map_load(SwivDisk *d, int lv, SwivMap *m);
void swiv_map_free(SwivMap *m);
void swiv_map_palette_at(const SwivMap *m, int ry, uint16_t out[16]); /* ry = scroll y (bottom-up) */
int  swiv_map_palid_at(const SwivMap *m, int ry);                      /* checkpoint index, -1 if none */

/* Rendering into an 8-bit indexed canvas (index 0..15, 255 = untouched) */
typedef struct { int w, h; uint8_t *px; uint8_t *palid; int cur_palid; } SwivCanvas; /* palid: checkpoint index the pixel was drawn under (255 = background/row) */
void swiv_canvas_init(SwivCanvas *c, int w, int h, uint8_t fill);
void swiv_canvas_free(SwivCanvas *c);
void swiv_blit_part(SwivCanvas *c, const SwivPart *p, int x0, int y0);
void swiv_blit_gfx(SwivDisk *d, SwivCanvas *c, int gfx, int x, int y);     /* anchored at (x,y) */
void swiv_blit_gfx_shadow(SwivDisk *d, SwivCanvas *c, int gfx, int x, int y, uint8_t colour);  /* silhouette */
/* Full map render in image space (y down).  with_objects: also draw type!=0 */
int  swiv_map_render(SwivDisk *d, const SwivMap *m, SwivCanvas *c, int with_objects);
/* Palette for image row iy (RGB12 x16) */
void swiv_map_palette_row(const SwivMap *m, int iy, uint16_t out[16]);

static inline void swiv_rgb12(uint16_t v, uint8_t *r, uint8_t *g, uint8_t *b) {
    *r = ((v >> 8) & 15) * 17; *g = ((v >> 4) & 15) * 17; *b = (v & 15) * 17;
}
#endif
