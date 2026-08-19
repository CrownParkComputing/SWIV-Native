#include "swivdata.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

/* ---- stream C depacker (loader 0x966/0xa14/0xb52/0xaee/0xb1e) ---- */
typedef struct { const uint8_t *d; size_t pos; int d6, d5; } StreamC;
static int sc_byte(StreamC *s) { return s->d[s->pos++]; }
static int sc_bit(StreamC *s) {
    if (--s->d5 < 0) { s->d6 = sc_byte(s); s->d5 = 7; }
    int b = (s->d6 >> 7) & 1; s->d6 = (s->d6 << 1) & 0xFF; return b;
}
static int sc_bits(StreamC *s, int n) { int v = 0; while (n--) v = (v << 1) | sc_bit(s); return v; }
static int sc_code(StreamC *s) {
    if (!sc_bit(s)) return 0;
    int v = sc_bits(s, 2) + 1; if (v != 4) return v;
    v = (1 << 2) | sc_bits(s, 2); if (v != 7) return v;
    return sc_bits(s, 11);
}
static uint8_t *sc_unpack(const uint8_t *d, size_t pos, uint32_t size) {
    StreamC s = { d, pos, 0, 0 };
    uint8_t *out = malloc(size ? size : 1);
    uint8_t ring[1024]; memset(ring, 0, sizeof ring);
    uint32_t n = 0; int rpos = 0, literal = 1, rem = 0, off = 0;
    while (n < size) {
        if (rem == 0) {
            if (literal) rem = sc_code(&s);
            else { off = sc_bits(&s, 10); int k = sc_code(&s) + 2; rem = k <= 63 ? k : 0; }
        }
        uint32_t left = size - n; int take = (int)(rem < (int)left ? rem : left);
        int flip = rem <= (int)left;
        if (literal) {
            for (int i = 0; i < take; i++) { uint8_t b = sc_byte(&s); out[n++] = b; ring[rpos] = b; rpos = (rpos + 1) & 1023; }
        } else {
            int src = (rpos - off) & 1023;
            for (int i = 0; i < take; i++) { uint8_t b = ring[src]; out[n++] = b; ring[rpos] = b; src = (src + 1) & 1023; rpos = (rpos + 1) & 1023; }
        }
        rem -= take;
        if (flip) literal = !literal;
    }
    return out;
}

/* ---- disk ---- */
int swiv_open(SwivDisk *d, const char *path) {
    memset(d, 0, sizeof *d);
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    fseek(f, 0, SEEK_END); d->adf_len = ftell(f); fseek(f, 0, SEEK_SET);
    d->adf = malloc(d->adf_len);
    if (fread(d->adf, 1, d->adf_len, f) != d->adf_len) { fclose(f); return -1; }
    fclose(f);
    const uint8_t *a = d->adf;
    int cnt = be16(a + SWIV_TRACK); if (cnt > SWIV_MAX_FILES) cnt = SWIV_MAX_FILES;
    d->nfiles = cnt;
    uint32_t p = SWIV_TRACK + 2 + 4 * cnt;
    for (int i = 0; i < cnt; i++) {
        d->files[i].stored = be32(a + SWIV_TRACK + 2 + 4 * i);
        d->files[i].disk_off = p;
        d->files[i].size = be32(a + p);
        p += d->files[i].stored;
    }
    /* file 0 = name table, \n separated */
    uint32_t nsz; const uint8_t *names = swiv_load(d, 0, &nsz);
    uint32_t s = 0; int i = 0;
    for (uint32_t k = 0; k <= nsz && i < cnt; k++) {
        if (k == nsz || names[k] == '\n') {
            uint32_t len = k - s; while (len && isspace(names[s + len - 1])) len--;
            while (len && isspace(names[s])) { s++; len--; }
            if (len >= sizeof d->files[i].name) len = sizeof d->files[i].name - 1;
            if (len) memcpy(d->files[i].name, names + s, len);
            else snprintf(d->files[i].name, sizeof d->files[i].name, "FILE%03d", i);
            d->files[i].name[len ? len : strlen(d->files[i].name)] = 0;
            i++; s = k + 1;
        }
    }
    for (; i < cnt; i++) snprintf(d->files[i].name, sizeof d->files[i].name, "FILE%03d", i);
    /* AMPROG.OBJ and its internal name table (0x0004..0x0537, \0 separated) */
    int pi = swiv_find(d, "AMPROG.OBJ"); if (pi < 0) return -2;
    d->prog = swiv_load(d, pi, &d->prog_len);
    for (uint32_t o = 4; o < 0x537 && d->norder < 512;) {
        if (!d->prog[o]) { o++; continue; }
        uint32_t e = o; while (e < 0x537 && d->prog[e]) e++;
        uint32_t len = e - o; if (len > 15) len = 15;
        for (uint32_t k = 0; k < len; k++) d->order[d->norder][k] = (char)toupper(d->prog[o + k]);
        d->order[d->norder][len] = 0; d->norder++;
        o = e;
    }
    return 0;
}

void swiv_close(SwivDisk *d) {
    for (int i = 0; i < d->nfiles; i++) free(d->files[i].data);
    for (int i = 0; i < SWIV_MAX_FILES; i++) if (d->lin_parsed[i]) {
        for (int f = 0; f < d->lins[i].nframes; f++) free(d->lins[i].frames[f].parts);
        free(d->lins[i].frames);
    }
    free(d->adf); memset(d, 0, sizeof *d);
}

int swiv_find(const SwivDisk *d, const char *name) {
    for (int i = 0; i < d->nfiles; i++) if (!strcasecmp(d->files[i].name, name)) return i;
    return -1;
}

const uint8_t *swiv_load(SwivDisk *d, int idx, uint32_t *size) {
    if (idx < 0 || idx >= d->nfiles) return NULL;
    SwivFile *f = &d->files[idx];
    if (!f->data) f->data = sc_unpack(d->adf, f->disk_off + 4, f->size);
    if (size) *size = f->size;
    return f->data;
}

int swiv_order_index(const SwivDisk *d, int id) {
    if (id < 0 || id >= d->norder) return -1;
    return swiv_find(d, d->order[id]);
}

/* ---- .LIN ---- */
static int s8(uint8_t v) { return v > 127 ? v - 256 : v; }
const SwivLin *swiv_lin(SwivDisk *d, int idx) {
    if (idx < 0 || idx >= d->nfiles) return NULL;
    if (d->lin_parsed[idx]) return &d->lins[idx];
    uint32_t sz; const uint8_t *b = swiv_load(d, idx, &sz);
    SwivLin *L = &d->lins[idx]; memset(L, 0, sizeof *L);
    d->lin_parsed[idx] = 1;
    if (sz < 2) return L;
    int cnt = be16(b); uint32_t p = 2;
    L->frames = calloc(cnt ? cnt : 1, sizeof(SwivFrame));
    for (int i = 0; i < cnt; i++) {
        if (p + 10 > sz) break;
        SwivFrame *F = &L->frames[L->nframes];
        int cap = 4; F->parts = malloc(cap * sizeof(SwivPart)); F->nparts = 0;
        for (;;) {
            if (F->nparts == cap) { cap *= 2; F->parts = realloc(F->parts, cap * sizeof(SwivPart)); }
            SwivPart *P = &F->parts[F->nparts++];
            P->dsz = be16(b + p); P->w = b[p + 2]; P->h = b[p + 3];
            P->cx = s8(b[p + 4]); P->cy = s8(b[p + 5]); P->trans = b[p + 6]; P->flags = b[p + 7];
            P->data = b + p + 10;
            if (p + 10 + P->dsz > sz) P->dsz = sz - p - 10;
            p += 10 + P->dsz;
            if (!(P->flags & 1) || p + 10 > sz) break;
        }
        L->nframes++;
    }
    return L;
}

/* ---- canvas + blit ---- */
void swiv_canvas_init(SwivCanvas *c, int w, int h, uint8_t fill) {
    c->w = w; c->h = h; c->px = malloc((size_t)w * h); memset(c->px, fill, (size_t)w * h);
    c->palid = malloc((size_t)w * h); memset(c->palid, 255, (size_t)w * h); c->cur_palid = 255;
}
void swiv_canvas_free(SwivCanvas *c) { free(c->px); free(c->palid); c->px = c->palid = NULL; }

void swiv_blit_part(SwivCanvas *c, const SwivPart *p, int x0, int y0) {
    if (p->flags & 16) return;      /* part flag bit4 = collision-only marker (_STOP, jeep stop markers): never drawn */
    int wd = (p->w + 15) / 16;
    if (p->dsz < (uint32_t)(wd * 8 * p->h)) return;
    for (int y = 0; y < p->h; y++) {
        int ty = y0 + y; if (ty < 0 || ty >= c->h) continue;
        const uint8_t *row = p->data + (size_t)y * 4 * wd * 2;
        for (int x = 0; x < p->w; x++) {
            int tx = x0 + x; if (tx < 0 || tx >= c->w) continue;
            int j = x >> 4, bit = 0x8000 >> (x & 15), v = 0;
            for (int k = 0; k < 4; k++) if (be16(row + (k * wd + j) * 2) & bit) v |= 1 << k;
            if (v != p->trans) { c->px[(size_t)ty * c->w + tx] = (uint8_t)v; if (c->palid) c->palid[(size_t)ty * c->w + tx] = (uint8_t)c->cur_palid; }
        }
    }
}

void swiv_blit_gfx(SwivDisk *d, SwivCanvas *c, int gfx, int x, int y) {
    int fi = swiv_order_index(d, gfx & 0x1FF); if (fi < 0) return;
    const SwivLin *L = swiv_lin(d, fi); int fr = gfx >> 9;
    if (fr >= L->nframes) return;
    const SwivFrame *F = &L->frames[fr];
    for (int i = 0; i < F->nparts; i++) swiv_blit_part(c, &F->parts[i], x - F->parts[i].cx, y - F->parts[i].cy);
}

static void blit_part_shadow(SwivCanvas *c, const SwivPart *p, int x0, int y0, uint8_t colour) {
    int wd = (p->w + 15) / 16;
    if (p->dsz < (uint32_t)(wd * 8 * p->h)) return;
    for (int y = 0; y < p->h; y++) {
        int ty = y0 + y; if (ty < 0 || ty >= c->h) continue;
        const uint8_t *row = p->data + (size_t)y * 4 * wd * 2;
        for (int x = 0; x < p->w; x++) {
            int tx = x0 + x; if (tx < 0 || tx >= c->w) continue;
            int j = x >> 4, bit = 0x8000 >> (x & 15), v = 0;
            for (int k = 0; k < 4; k++) if (be16(row + (k * wd + j) * 2) & bit) v |= 1 << k;
            if (v != p->trans) c->px[(size_t)ty * c->w + tx] = colour;
        }
    }
}
void swiv_blit_gfx_shadow(SwivDisk *d, SwivCanvas *c, int gfx, int x, int y, uint8_t colour) {
    int fi = swiv_order_index(d, gfx & 0x1FF); if (fi < 0) return;
    const SwivLin *L = swiv_lin(d, fi); int fr = gfx >> 9;
    if (fr >= L->nframes) return;
    const SwivFrame *F = &L->frames[fr];
    for (int i = 0; i < F->nparts; i++) blit_part_shadow(c, &F->parts[i], x - F->parts[i].cx, y - F->parts[i].cy, colour);
}

/* ---- level table / maps ---- */
int swiv_level_count(void) { return 7; }

int swiv_map_load(SwivDisk *d, int lv, SwivMap *m) {
    memset(m, 0, sizeof *m);
    const uint8_t *p = d->prog;
    int fid = be16(p + SWIV_LEVTAB + lv * 6);
    uint32_t tab = SWIV_LEVTAB + be16(p + SWIV_LEVTAB + lv * 6 + 2);
    uint32_t end = lv < 6 ? SWIV_LEVTAB + be16(p + SWIV_LEVTAB + (lv + 1) * 6 + 2) : tab + 512;
    m->scroll_speed = be16(p + SWIV_LEVTAB + lv * 6 + 4);
    int ndico = (end - tab) / 2; uint16_t *dico = malloc(ndico * 2);
    for (int i = 0; i < ndico; i++) dico[i] = be16(p + tab + 2 * i);
    if (fid >= d->norder) { free(dico); return -1; }
    snprintf(m->pam_name, sizeof m->pam_name, "%s", d->order[fid]);
    int fi = swiv_find(d, m->pam_name); if (fi < 0) { free(dico); return -1; }
    uint32_t sz; const uint8_t *pam = swiv_load(d, fi, &sz);
    int ct = 0, co = 0, cc = 0;
    m->tiles = malloc(sizeof(SwivRec) * 4096); m->objs = malloc(sizeof(SwivRec) * 1024);
    m->checks = malloc(sizeof(SwivPalCheck) * 256);
    uint16_t pal[16] = {0}; int y = 0, seq = 0;
    for (uint32_t o = 0; o + 4 <= sz; o += 4) {
        uint32_t D = be32(pam + o); if (!D) break;
        if (D & 0x80000000u) {
            y += (D >> 16) & 0xFF; uint16_t w = D & 0xFFFF;
            pal[w & 15] = w >> 4;
            if (cc && m->checks[cc - 1].y == y) memcpy(m->checks[cc - 1].pal, pal, sizeof pal);
            else if (cc < 256) { m->checks[cc].y = y; memcpy(m->checks[cc].pal, pal, sizeof pal); cc++; }
            continue;
        }
        int typ = D & 15, loc = (D >> 4) & 0xFF; y += (D >> 12) & 0xFF;
        int x = D >> 20, layer = 4; while (x >= 416) { x -= 512; layer--; }
        SwivRec r = { y, x, loc < ndico ? dico[loc] : 0, layer, typ, seq++ };
        if (typ == 0) { if (ct < 4096) m->tiles[ct++] = r; } else { if (co < 1024) m->objs[co++] = r; }
    }
    m->ntiles = ct; m->nobjs = co; m->nchecks = cc; m->height = y;
    free(dico);
    return 0;
}

void swiv_map_free(SwivMap *m) { free(m->tiles); free(m->objs); free(m->checks); memset(m, 0, sizeof *m); }

int swiv_map_palid_at(const SwivMap *m, int ry) {
    int cur = m->nchecks ? 0 : -1;
    for (int i = 0; i < m->nchecks; i++) { if (m->checks[i].y > ry) break; cur = i; }
    return cur;
}
void swiv_map_palette_at(const SwivMap *m, int ry, uint16_t out[16]) {
    int id = swiv_map_palid_at(m, ry);
    if (id < 0) memset(out, 0, 32); else memcpy(out, m->checks[id].pal, 32);
}
void swiv_map_palette_row(const SwivMap *m, int iy, uint16_t out[16]) {
    swiv_map_palette_at(m, m->height + SWIV_MARGIN - iy, out);
}

static int cmp_draw(const void *a, const void *b) {
    const SwivRec *r = a, *s = b;
    int ka = r->layer == 0 ? 5 : r->layer, kb = s->layer == 0 ? 5 : s->layer;
    if (ka != kb) return kb - ka;           /* descending layer key */
    return s->seq - r->seq;                 /* descending seq within layer */
}

int (*swiv_map_tile_filter)(uint16_t gfx) = 0;   /* return 1 to skip a tile (play mode: tiles carrying an object handler gfx word are placeholders) */
int swiv_map_render(SwivDisk *d, const SwivMap *m, SwivCanvas *c, int with_objects) {
    int H = m->height + 2 * SWIV_MARGIN;
    swiv_canvas_init(c, SWIV_FIELD_W, H, 10);   /* background = colour 10 */
    SwivRec *order = malloc(sizeof(SwivRec) * m->ntiles);
    memcpy(order, m->tiles, sizeof(SwivRec) * m->ntiles);
    qsort(order, m->ntiles, sizeof(SwivRec), cmp_draw);
    for (int i = 0; i < m->ntiles; i++) {
        if (swiv_map_tile_filter && swiv_map_tile_filter((uint16_t)order[i].gfx)) continue;
        c->cur_palid = swiv_map_palid_at(m, order[i].y);
        swiv_blit_gfx(d, c, order[i].gfx, order[i].x, m->height + SWIV_MARGIN - order[i].y);
    }
    free(order);
    if (with_objects)
        for (int i = 0; i < m->nobjs; i++) {
            c->cur_palid = swiv_map_palid_at(m, m->objs[i].y);
            swiv_blit_gfx(d, c, m->objs[i].gfx, m->objs[i].x, m->height + SWIV_MARGIN - m->objs[i].y);
        }
    c->cur_palid = 255;
    return 0;
}
