/* sfx_bank.c -- SWIV sound effects synthesised from the original Paula driver.
 *
 * Source: re/amprog.asm, the INT6 sound driver at LAB_03AD ($2107DC):
 *   - 4 voice records of 268 bytes at 10786(A6): 0.w D1 arg, 2.w D2 arg, 4.w priority*4
 *     (decremented every tick), 6.w DMACON channel mask ($8008/$8004/$8002/$8001 = voice
 *     0..3 = AUD3..AUD0), 8.l saved SP, 12.. scratch waveform / coroutine stack.
 *   - A4 = $DFF030 - 16*voice, so 160(A4)=AUDxLC 164(A4)=AUDxLEN 166(A4)=AUDxPER
 *     168(A4)=AUDxVOL, 150(A3)=DMACON.  Voice 0 = AUD3, voice 3 = AUD0.
 *   - LAB_03B5 starter: D0 prio, D1/D2 -> (A5)/2(A5), A0 body; tries the voice pair
 *     (1,2) [AUD2/AUD1 = right] then (0,3) [AUD3/AUD0 = left]; LAB_03BA tries (0,3) first.
 *     In a pair the lower-priority voice is taken if new prio*4 > it (unsigned).
 *     LAB_03BF picks LAB_03BA for x < 160 else LAB_03B5 (stereo by position).
 *   - LAB_03AF = yield one tick (LAB_03AC/AB/AA = 2/3/4), LAB_03BD = voice end
 *     (DMA off, vol 0, prio 0), LAB_042D(D0) = wait D0 frames, LAB_0430 = noise
 *     generator (LFSR over the scratch), LAB_0629 = game RNG.
 *
 * TIME BASE: the driver is NOT a VBL routine.  LAB_03A9 arms CIA-B timer A with
 * $0D88 = 3464 E-clocks in one-shot mode and LAB_03AD re-arms it at the end of every
 * interrupt, so the coroutines step at 709379/(3464+latency) = ~202 Hz, about four
 * ticks per PAL frame.  Measured on the Musashi host (SWIV-Amiga --wav + SWIV_SFXLOG):
 * 3.98 ticks/frame; the player shot LAB_03E2 (32 ticks) lasts 0.16 s, not 0.64 s.
 * Everything below that says "frame" means one driver tick of 1/202 s = 109 samples.
 * Only the jingle task LAB_0426 waits in VBLs (kernel -1426(A6)), converted at jingle().
 *
 * Each body below is a line-by-line transliteration of the 68000 coroutine; wait()
 * renders one tick (109 samples) of the voice through the Paula model.  A driver-level
 * allocator reproduces the voice stealing (matters for the jingles LAB_0421/0423+6). */
#include "sfx_bank.h"
#include "engine/engine.h"           /* rng() = LAB_0629 */
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#define OUT_RATE      22050
/* Driver tick: CIA-B TA one-shot 3464 E-clocks + re-arm latency = ~200 Hz (measured on the host).  A 50 Hz
 * variant (the earlier assumption: one tick per VBL) is kept selectable per entry for A/B listening. */
static double TICK_HZ = 200.0;
static int FRAME_SAMPLES = 110;            /* 22050 / TICK_HZ, one driver tick */
#define PAULA_CLK     3546895.0
#define MIN_PERIOD    113.75         /* one data word (2 samples) per 227.5-clock scanline: below this
                                        the word is replayed, so the audible rate pins here (LAB_041D hits 72) */
#define MAX_FRAMES    2048           /* cap for bodies that never end (~10 s) */
#define VOICE_GAIN    1              /* 127*64 = 8128 per voice = one Paula channel at full volume */

/* ------------------------------------------------------------------ Paula voice */
typedef struct {
    int8_t scratch[256];             /* 12(A5).. */
    const int8_t *lc; int len; uint16_t per; int vol; int dma_pending; int dma;   /* registers */
    const int8_t *cur; int cur_len; int idx; double acc; int passes;              /* DMA engine */
    int16_t *out; int nframes;       /* rendered VBLs so far */
    int16_t a0, a2;                  /* (A5), 2(A5) */
    const int8_t *big; int bign; const int8_t *smart; int smartn;
    jmp_buf jb;
} Voice;

static const int8_t ZERO_WORD[2] = { 0, 0 };

static void dma_on(Voice *v)  { v->dma_pending = 1; }           /* MOVE.W 6(A5),150(A3) */
static void dma_off(Voice *v) { v->dma = 0; v->dma_pending = 0; }

/* LAB_03AF: one driver tick.  All register writes issued since the last wait happen "at
 * once" (they are a few cycles apart on the 68000), then Paula runs for 1/202 s. */
static void wait(Voice *v) {
    if (v->nframes >= MAX_FRAMES) longjmp(v->jb, 1);
    if (v->dma_pending) {            /* DMACON set: latch AUDxLC/LEN, start fetching */
        v->dma_pending = 0; v->dma = 1; v->cur = v->lc; v->cur_len = v->len ? v->len * 2 : 131072;
        v->idx = 0; v->acc = (v->per < MIN_PERIOD ? MIN_PERIOD : v->per) / PAULA_CLK; v->passes = 0;
    }
    int16_t *o = v->out + v->nframes * FRAME_SAMPLES;
    int vol = v->vol & 0x7f; if (vol > 64) vol = 64;
    double per = v->per < MIN_PERIOD ? MIN_PERIOD : v->per;     /* DMA can't fetch faster */
    double tsamp = per / PAULA_CLK, tout = 1.0 / OUT_RATE;
    for (int i = 0; i < FRAME_SAMPLES; i++) {
        if (!v->dma || !v->cur) { o[i] = 0; continue; }
        double rem = tout, sum = 0;
        while (rem > 0) {                                        /* box-filter the Paula output */
            double seg = v->acc < rem ? v->acc : rem;
            sum += (double)v->cur[v->idx] * seg; rem -= seg; v->acc -= seg;
            if (v->acc <= 1e-12) {
                v->acc += tsamp;
                if (++v->idx >= v->cur_len) {                    /* end of block: reload LC/LEN */
                    v->idx = 0; v->cur = v->lc; v->cur_len = v->len ? v->len * 2 : 131072; v->passes++;
                }
            }
        }
        int s = (int)(sum * OUT_RATE * vol * VOICE_GAIN);
        o[i] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
    }
    v->nframes++;
}
static void wait2(Voice *v) { wait(v); wait(v); }                /* LAB_03AC */
static void wait3(Voice *v) { wait(v); wait(v); wait(v); }       /* LAB_03AB */
static void wait4(Voice *v) { wait(v); wait(v); wait(v); wait(v); }   /* LAB_03AA */
static void waitn(Voice *v, int n) { for (int i = 0; i < n; i++) wait(v); }   /* LAB_042D */
static void voice_end(Voice *v) { dma_off(v); v->vol = 0; longjmp(v->jb, 1); }   /* LAB_03BD */
/* LAB_042D with D0 = -1 on a one-shot sample: the voice parks on a silent 1-word loop for
 * 65535 frames; render until the sample has been played through once. */
static void wait_sample_done(Voice *v) { while (v->passes == 0) wait(v); wait(v); }

static void set_scratch(Voice *v, const uint8_t *w, int n) { memcpy(v->scratch, w, n); }
static uint32_t rd32(const int8_t *p) { return ((uint32_t)(uint8_t)p[0] << 24) | ((uint32_t)(uint8_t)p[1] << 16) | ((uint32_t)(uint8_t)p[2] << 8) | (uint8_t)p[3]; }
static void wr32(int8_t *p, uint32_t x) { p[0] = (int8_t)(x >> 24); p[1] = (int8_t)(x >> 16); p[2] = (int8_t)(x >> 8); p[3] = (int8_t)x; }
/* LAB_0430: D0 = number of longs, A0 = buffer.  D6 = ~first long; per long: D6 <<= 1,
 * unless (carry clear && result != 0) D6 ^= $1D872B41; SWAP; store. */
static void noise(int8_t *p, int nlongs) {
    uint32_t d7 = 0x1d872b41u, d6 = ~rd32(p);
    for (int i = 0; i < nlongs; i++) {
        uint32_t carry = d6 >> 31; d6 <<= 1;
        if (!(carry == 0 && d6 != 0)) d6 ^= d7;
        d6 = (d6 << 16) | (d6 >> 16); wr32(p + 4 * i, d6);
    }
}
static int16_t W(int x) { return (int16_t)x; }
static uint16_t rngw(void) { return (uint16_t)rng(); }

/* ------------------------------------------------------------------ waveforms */
static const uint8_t WAVE_SHOT[16]  = { 0x00,0x10,0x20,0x40,0x7f,0x40,0x20,0x10,0x00,0xc0,0xa0,0x90,0x81,0x90,0xa0,0xc0 };   /* LAB_03E3 */
static const uint8_t WAVE_HIT[16]   = { 0x00,0x20,0x40,0x60,0x7f,0x60,0x40,0x20,0x00,0xe0,0xc0,0xa0,0x80,0xa0,0xc0,0xe0 };   /* LAB_03CD.. */
static const uint8_t WAVE_PICK[16]  = { 0x00,0x40,0x7f,0x40,0x00,0xc0,0x81,0xc0,0x00,0x20,0x40,0x20,0x00,0xe0,0xc0,0xe0 };   /* LAB_03F5 */
static const uint8_t WAVE_SQ8[8]    = { 0x7f,0x80,0x7f,0x80,0x7f,0x7f,0x80,0x80 };                                           /* LAB_0414 */
static const uint8_t WAVE_TRI8[8]   = { 0x00,0x40,0x7f,0x40,0x00,0xc0,0x80,0xc0 };                                           /* LAB_041D */
static const uint8_t WAVE_PLUCK[64] = {                                                                                     /* LAB_03E0 */
    0x00,0x40,0x7f,0x40,0x00,0xc0,0x80,0xc0, 0x00,0x30,0x60,0x30,0x00,0xd0,0xa0,0xd0,
    0x00,0x28,0x50,0x28,0x00,0xd8,0xb0,0xd8, 0x00,0x20,0x40,0x20,0x00,0xe0,0xc0,0xe0,
    0x00,0x18,0x30,0x18,0x00,0xe8,0xd0,0xe8, 0x00,0x10,0x20,0x10,0x00,0xf0,0xe0,0xf0,
    0x00,0x08,0x10,0x08,0x00,0xf8,0xf0,0xf8, 0x00,0x04,0x08,0x04,0x00,0xfc,0xf8,0xfc };
static const uint8_t TBL_042C[16]   = { 0x08,0x18,0x28,0x38,0xc8,0xd8,0xe8,0xf8,0x08,0x18,0x28,0x38,0xc8,0xd8,0xe8,0xf8 };   /* LAB_042C */
/* LAB_0408 builds 128 bytes from the 256-word sine LAB_0520 ($2127A2): every other word,
 * ASR #1, clamped to +127. */
static const uint8_t WAVE_SINE128[128] = {
    0x00,0x06,0x0c,0x13,0x19,0x1f,0x25,0x2b,0x31,0x36,0x3c,0x42,0x47,0x4c,0x51,0x56,0x5a,0x5f,0x63,0x67,0x6a,0x6e,0x71,0x73,0x76,0x78,0x7a,0x7c,0x7d,0x7e,0x7f,0x7f,
    0x7f,0x7f,0x7f,0x7e,0x7d,0x7c,0x7a,0x78,0x76,0x73,0x71,0x6e,0x6a,0x67,0x63,0x5f,0x5a,0x56,0x51,0x4c,0x47,0x42,0x3c,0x36,0x31,0x2b,0x25,0x1f,0x19,0x13,0x0c,0x06,
    0x00,0xf9,0xf3,0xed,0xe7,0xe1,0xdb,0xd5,0xcf,0xc9,0xc3,0xbe,0xb9,0xb4,0xaf,0xaa,0xa5,0xa1,0x9d,0x99,0x95,0x92,0x8f,0x8c,0x89,0x87,0x85,0x84,0x82,0x81,0x80,0x80,
    0x80,0x80,0x80,0x81,0x82,0x84,0x85,0x87,0x89,0x8c,0x8f,0x92,0x95,0x99,0x9d,0xa1,0xa5,0xaa,0xaf,0xb4,0xb9,0xbe,0xc3,0xc9,0xcf,0xd5,0xdb,0xe1,0xe7,0xed,0xf3,0xf9 };

/* ------------------------------------------------------------------ voice bodies
 * Each starts with wait() = the BSR LAB_03AF that opens every body (the coroutine is
 * entered on the VBL after the start call).  v->a0/a2 = (A5)/2(A5) = starter D1/D2. */
#define SCRATCH(v) ((v)->lc = (v)->scratch)

static void body_03C7(Voice *v) {                 /* BIGEXPL.SND once at period (A5), vol 64 */
    wait(v); v->lc = v->big; v->len = v->bign >> 1; v->vol = 0x40; v->per = v->a0; dma_on(v);
    wait2(v); v->lc = ZERO_WORD; v->len = 1;      /* LAB_03C8: park on a silent word, wait -1 */
    wait_sample_done(v); voice_end(v);
}
static void body_03CB(Voice *v) {                 /* SMART.SND once at period (A5), vol 64 */
    wait(v); v->lc = v->smart; v->len = v->smartn >> 1; v->vol = 0x40; v->per = v->a0; dma_on(v);
    wait2(v); v->lc = ZERO_WORD; v->len = 1;
    wait_sample_done(v); voice_end(v);
}
static void body_03CD(Voice *v) {                 /* HIT wave, 48 frames, vol 48..1, vibrato, pitch falls */
    wait(v); SCRATCH(v); v->len = 8; dma_on(v); set_scratch(v, WAVE_HIT, 16); v->a2 = 0x30;
    do { int d0 = v->a2; v->vol = d0; if (d0 & 4) d0 = ~d0; d0 = (d0 & 3) << 5;
         v->per = W(d0 + v->a0); wait(v); v->a0 = W(v->a0 + 3); v->a2--; } while (v->a2);
    voice_end(v);
}
static void body_03D1(Voice *v) {                 /* HIT wave vol 48, period 800 -> 500 by 20 */
    wait(v); SCRATCH(v); v->len = 8; v->vol = 0x30; dma_on(v); set_scratch(v, WAVE_HIT, 16);
    do { v->per = v->a0; wait(v); v->a0 = W(v->a0 - 20); } while (v->a0 >= 500);
    voice_end(v);
}
static void body_03D4(Voice *v) {                 /* HIT wave, vol 2(A5)--, period ^= 64, -= 2 */
    wait(v); SCRATCH(v); v->len = 8; v->vol = 0x30; dma_on(v); set_scratch(v, WAVE_HIT, 16);
    do { v->vol = v->a2; v->per = v->a0; wait(v); v->a0 ^= 0x40; v->a0 = W(v->a0 - 2); v->a2--; } while (v->a2);
    voice_end(v);
}
static void body_03DA(Voice *v) {                 /* 24-byte noise, vol (A5)--, period 2(A5) *= 33/32 */
    wait(v); SCRATCH(v); v->len = 12; dma_on(v); wr32(v->scratch, rng());
    do { noise(v->scratch, 6); v->vol = v->a0; int d0 = v->a2; v->per = (uint16_t)d0; v->a2 = W(v->a2 + (d0 >> 5));
         wait(v); v->a0--; } while (v->a0);
    voice_end(v);
}
static void body_03DC(Voice *v) {                 /* decaying pluck wave, period 248, LEN 32..63 words */
    wait(v); SCRATCH(v); dma_on(v); v->per = 0xf8; v->vol = 0x40;
    set_scratch(v, WAVE_PLUCK, 64); memset(v->scratch + 64, 0, 64); v->a0 = 0x20;
    do { v->len = v->a0; wait4(v); v->a0++; } while ((uint16_t)v->a0 < 0x40);
    voice_end(v);
}
static void body_sweep16(Voice *v, const uint8_t *wave, int shift, int dec) {   /* LAB_03E3/03E5/03EC */
    wait(v); SCRATCH(v); v->len = 8; dma_on(v); set_scratch(v, wave, 16);
    do { v->vol = v->a0; int d0 = v->a2; v->per = (uint16_t)d0; v->a2 = W(v->a2 + (d0 >> shift));
         wait(v); v->a0 = W(v->a0 - dec); } while (v->a0);
    voice_end(v);
}
static void body_03E3(Voice *v) { body_sweep16(v, WAVE_SHOT, 4, 1); }
static void body_03E5(Voice *v) { body_sweep16(v, WAVE_HIT, 4, 1); }
static void body_03EC(Voice *v) { body_sweep16(v, WAVE_HIT, 2, 4); }
static void body_03E8(Voice *v) {                 /* HIT wave at (A5) + vibrato*4, vol 48..1 */
    wait(v); SCRATCH(v); v->len = 8; dma_on(v); set_scratch(v, WAVE_HIT, 16); v->a2 = 0x30;
    do { int d0 = v->a2; v->vol = d0; if (d0 & 4) d0 = ~d0; d0 = (d0 & 3) * 4;
         v->per = W(d0 + v->a0); wait(v); v->a2--; } while (v->a2);
    voice_end(v);
}
static void body_03EF(Voice *v) {                 /* noise hiss: vol up 0..(A5) at per 500, then down at 1000 */
    wait(v); SCRATCH(v); v->per = 0x1f4; dma_on(v); v->len = 0x10; uint16_t cnt = 0;
    do { noise(v->scratch, 8); v->vol = cnt; wait(v); cnt += 2; } while (cnt < (uint16_t)v->a0);
    v->per = 0x3e8;
    do { noise(v->scratch, 8); v->vol = cnt; wait2(v); cnt--; } while (cnt);
    voice_end(v);
}
static void body_03F5(Voice *v) {                 /* PICK wave: glide 4*per -> per, hold (A5) frames, fade 48x4 */
    wait(v); SCRATCH(v); v->len = 8; v->vol = 0x30; dma_on(v); set_scratch(v, WAVE_PICK, 16);
    int16_t cnt = W(v->a2 * 4);
    do { int d0 = cnt; v->per = (uint16_t)d0; cnt = W(cnt - (d0 >> 5)); wait(v); } while (cnt > v->a2);
    waitn(v, (uint16_t)v->a0);
    cnt = 0x30;
    do { int d0 = v->a2; v->per = (uint16_t)d0; v->a2 = W(v->a2 + (d0 >> 4)); v->vol = cnt; wait4(v); cnt--; } while (cnt);
    voice_end(v);
}
static void body_03FA(Voice *v) {                 /* 16-byte noise at (A5), vol = n>>7 rising forever */
    wait(v); SCRATCH(v); v->len = 8; v->vol = 0; v->per = v->a0; dma_on(v); v->a2 = 0;
    for (;;) { noise(v->scratch, 4); v->vol = (uint16_t)v->a2 >> 7; v->a2++; wait(v); }
}
static void body_03FD(Voice *v) {                 /* $7F80 square at (A5), vol 64, forever */
    wait(v); SCRATCH(v); v->len = 1; v->vol = 0x40; v->per = v->a0; dma_on(v);
    v->scratch[0] = 0x7f; v->scratch[1] = (int8_t)0x80;
    for (;;) wait(v);
}
static void body_03FF(Voice *v) {                 /* 32-byte noise, period 2(A5)--, vol (A5)-- */
    wait(v); SCRATCH(v); v->len = 0x10; dma_on(v);
    do { noise(v->scratch, 8); v->per = (uint16_t)v->a2; v->a2--; v->vol = v->a0; wait(v); v->a0--; } while (v->a0);
    voice_end(v);
}
static void body_0402(Voice *v) {                 /* 64-byte noise swell: vol up over 64 frames, then down + pitch falls */
    wait(v); SCRATCH(v); v->len = 0x20; dma_on(v); v->a0 = 0;
    do { noise(v->scratch, 16); v->per = (uint16_t)v->a2; v->vol = v->a0 >> 3; wait2(v); v->a0 = W(v->a0 + 16); } while (v->a0 < 0x200);
    do { noise(v->scratch, 16); v->per = (uint16_t)v->a2; v->a2 = W(v->a2 + 2); v->vol = v->a0 >> 3; wait2(v); v->a0 = W(v->a0 - 3); } while (v->a0 >= 0);
    voice_end(v);
}
static void body_0408(Voice *v) {                 /* 128-byte sine, random period 127..254 every 4 frames, vol (A5)-- */
    wait(v); SCRATCH(v); v->len = 0x40; dma_on(v); set_scratch(v, WAVE_SINE128, 128);
    do { v->per = (rngw() & 127) + 127; v->vol = v->a0; wait4(v); v->a0--; } while (v->a0);
    voice_end(v);
}
static void body_040D(Voice *v) {                 /* 8-byte noise, period (A5) *= 9/8, vol 2(A5) -= 2, LEN 1..4 words */
    wait(v); SCRATCH(v); dma_on(v); memset(v->scratch, 0, 12);
    do { noise(v->scratch, 2); int d0 = v->a0; v->per = (uint16_t)d0; v->a0 = W(v->a0 + (d0 >> 3));
         v->vol = v->a2; v->len = (v->a2 & 3) + 1; wait(v); v->a2 = W(v->a2 - 2); } while (v->a2);
    voice_end(v);
}
static void body_0414(Voice *v) {                 /* 8-byte square, period 2(A5) +/- (A5) alternating, slow rise */
    wait(v); SCRATCH(v); v->len = 4; dma_on(v); set_scratch(v, WAVE_SQ8, 8);
    int16_t cnt = v->a0 == 0x32 ? 0x7e : 0x30;
    do { v->vol = cnt; int d0 = v->a2; v->per = W(d0 + v->a0); v->a2 = W(v->a2 + (d0 >> 5)); v->a0 = W(-v->a0);
         wait(v); cnt--; } while (cnt);
    voice_end(v);
}
static void body_0413(Voice *v) { wait4(v); wait4(v); body_0414(v); }   /* same, 8 frames later */
static void body_0418(Voice *v) {                 /* HIT wave: exponential glide (A5) -> 1500, then 1500<->1600 wobble fading */
    wait(v); SCRATCH(v); v->len = 8; dma_on(v); v->vol = 0x40; set_scratch(v, WAVE_HIT, 16);
    int16_t cnt = 0x60;
    for (;;) {
        do { v->per = (uint16_t)v->a0; v->a0 = W(v->a0 - (v->a0 >> 8)); wait(v); } while (v->a0 > 1500);
        do { v->per = (uint16_t)v->a0; v->a0 = W(v->a0 + (v->a0 >> 8)); wait(v); } while (v->a0 < 1600);
        v->vol = cnt; if ((uint16_t)cnt < 3) break; cnt -= 3;
    }
    voice_end(v);
}
static void body_041D(Voice *v) {                 /* 8-byte triangle, period 2(A5) +/- 4*(A5) every 3 frames, vol (A5)-- */
    wait(v); SCRATCH(v); v->len = 4; v->vol = 0x20; dma_on(v); set_scratch(v, WAVE_TRI8, 8);
    do { v->vol = v->a0; v->per = W(v->a0 * 4 + v->a2); wait3(v); v->a0 = W(-v->a0);
         v->per = W(v->a0 * 4 + v->a2); wait3(v); v->a0 = W(-v->a0); v->a0--; } while (v->a0);
    voice_end(v);
}
static void body_0420(Voice *v) {                 /* $7F81 square: 300, 500, 700 for 2 frames each */
    wait(v); SCRATCH(v); v->len = 1; v->vol = 0x40; dma_on(v); v->scratch[0] = 0x7f; v->scratch[1] = (int8_t)0x81;
    v->per = 0x12c; wait2(v); v->per = 0x1f4; wait2(v); v->per = 0x2bc; wait2(v);
    voice_end(v);
}
static void body_0429(Voice *v) {                 /* jingle note: 8-byte wave = tbl[i]-tbl[i+off], 64 x 2 frames */
    wait(v); SCRATCH(v); dma_on(v); v->per = (uint16_t)v->a2; v->vol = 0x40; v->len = 4;
    do { v->vol = v->a0; int off = (v->a0 & 0x1f) >> 2;
         for (int i = 0; i < 8; i++) v->scratch[i] = (int8_t)(TBL_042C[i] - TBL_042C[i + off]);
         wait2(v); v->a0--; } while (v->a0);
    voice_end(v);
}

/* ------------------------------------------------------------------ driver model */
enum { SIDE_LEFT = 1, SIDE_RIGHT = 2 };                     /* LAB_03BA / LAB_03B5 */
typedef struct { int frame, prio, side, d1, d2; void (*body)(Voice *); } Req;
typedef struct {
    Req req[32]; int nreq;
    const int8_t *big; int bign; const int8_t *smart; int smartn;
} Bank;
static void start(Bank *b, int frame, int prio, int side, void (*body)(Voice *), int d1, int d2) {
    if (b->nreq < 32) b->req[b->nreq++] = (Req){ frame, prio, side, d1, d2, body };
}

/* ------------------------------------------------------------------ entry points */
static void synth_LAB_03C1(Bank *b) {               /* 4 x BIGEXPL chord, prio 100 */
    static const int p[4] = { 0x400, 0x408, 0x480, 0x488 };
    for (int i = 0; i < 4; i++) start(b, 0, 100, SIDE_RIGHT, body_03C7, p[i], 0);
}
static void synth_LAB_03C3(Bank *b) {               /* BIGEXPL at $300 + BIGEXPL at $301+rnd&7, prio 60 */
    start(b, 0, 60, SIDE_RIGHT, body_03C7, 0x300, 0);
    start(b, 0, 60, SIDE_RIGHT, body_03C7, 0x301 + (rngw() & 7), 0);
}
static void synth_LAB_03C5(Bank *b) {               /* 2 x BIGEXPL at $250+rnd&31, prio 50, positional */
    start(b, 0, 50, SIDE_RIGHT, body_03C7, 0x250 + (rngw() & 31), 0);
    start(b, 0, 50, SIDE_RIGHT, body_03C7, 0x250 + (rngw() & 31), 0);
}
static void synth_LAB_03C9(Bank *b) {               /* 4 x SMART.SND chord, prio 127 */
    static const int p[4] = { 0x410, 0x401, 0x3f2, 0x3e4 };
    for (int i = 0; i < 4; i++) start(b, 0, 127, SIDE_RIGHT, body_03CB, p[i], 0);
}
static void synth_LAB_03CC(Bank *b) { start(b, 0, 30, SIDE_RIGHT, body_03CD, 0xfa, 0); }
static void synth_LAB_03D0(Bank *b) { start(b, 0, 40, SIDE_RIGHT, body_03D1, 0x320, 0); }
static void synth_LAB_03D3(Bank *b) { start(b, 0, 10, SIDE_RIGHT, body_03D4, 0x5dc, 0x40); }
static void synth_LAB_03D6(Bank *b) { start(b, 0, 40, SIDE_LEFT, body_03DA, 64, 0x190); start(b, 0, 40, SIDE_RIGHT, body_03DA, 64, 0x190); }
static void synth_LAB_03D8(Bank *b) { start(b, 0, 40, SIDE_LEFT, body_03DA, 64, 0x96);  start(b, 0, 40, SIDE_RIGHT, body_03DA, 64, 0x96); }
static void synth_210BC4(Bank *b)   { start(b, 0, 40, SIDE_RIGHT, body_03DC, 64, 0x15e); }
static void synth_LAB_03E2(Bank *b) { start(b, 0, 20, SIDE_RIGHT, body_03E3, 32, 0x3e8); }
static void synth_210CBE(Bank *b)   { start(b, 0, 20, SIDE_RIGHT, body_03E5, 64, 0x3e8); }
static void synth_LAB_03E7(Bank *b) { start(b, 0, 60, SIDE_RIGHT, body_03E8, 0x96, 0); }
static void synth_LAB_03EB(Bank *b) { start(b, 0, 20, SIDE_RIGHT, body_03EC, 64, 0xc8); }
static void synth_LAB_03EE(Bank *b) { start(b, 0, 40, SIDE_LEFT, body_03EF, 64, 0); }    /* called with D0=0 -> left */
static void synth_LAB_03F3(Bank *b) { start(b, 0, 80, SIDE_RIGHT, body_03F5, 50, 0x9c4); start(b, 0, 80, SIDE_RIGHT, body_03F5, 90, 0x8b3); }
static void synth_LAB_03F8(Bank *b) {
    static const int p[4] = { 0x190, 0x1e0, 0x19d, 0x1b9 };
    for (int i = 0; i < 4; i++) start(b, 0, 127, SIDE_LEFT, body_03FA, p[i], 0);
}
static void synth_210F5A(Bank *b) {
    static const int p[4] = { 0x3f0, 0x3b8, 0x380, 0x320 };
    for (int i = 0; i < 4; i++) start(b, 0, 127, SIDE_LEFT, body_03FD, p[i], 0);
}
static void synth_LAB_03FE(Bank *b) { start(b, 0, 80, SIDE_RIGHT, body_03FF, 64, 0x140); }
static void synth_LAB_0401(Bank *b) { start(b, 0, 70, SIDE_RIGHT, body_0402, 0, 0x12c); }
static void synth_LAB_0405(Bank *b) {
    start(b, 0, 200, SIDE_RIGHT, body_0408, 127, 0);
    start(b, 0, 60, SIDE_LEFT, body_0408, 127, 0);
    start(b, 0, 60, SIDE_RIGHT, body_0408, 127, 0);
}
static void synth_LAB_040C(Bank *b) { start(b, 0, 50, SIDE_RIGHT, body_040D, 0x100, 0x60); }
static void synth_LAB_040F(Bank *b) { start(b, 0, 100, SIDE_RIGHT, body_0414, 50, 0x1f4); start(b, 0, 100, SIDE_RIGHT, body_0413, 50, 0x1f4); }
static void synth_LAB_0411(Bank *b) { start(b, 0, 100, SIDE_RIGHT, body_0414, 20, 0xc8);  start(b, 0, 100, SIDE_RIGHT, body_0413, 20, 0xc8); }
static void synth_LAB_0416(Bank *b) { start(b, 0, 10000, SIDE_LEFT, body_0418, 0x2710, 0); start(b, 0, 10000, SIDE_RIGHT, body_0418, 0x2af8, 0); }
static void synth_LAB_041B(Bank *b) { start(b, 0, 100, SIDE_RIGHT, body_041D, 32, 0xc8); start(b, 0, 100, SIDE_LEFT, body_041D, 32, 0xca); }
static void synth_LAB_041F(Bank *b) { start(b, 0, 20, SIDE_RIGHT, body_0420, 0, 0); }
/* LAB_0424: a kernel task (LAB_0426) plays a 0-terminated period list, one note every 5
 * VBLs (= 20 driver ticks), each note = prio 120, 64 x 2 ticks, body LAB_0429, via the
 * positional starter. */
static void jingle(Bank *b, const int *notes) { for (int i = 0; notes[i]; i++) start(b, (int)(i * 5 * TICK_HZ / 50.0 + 0.5), 120, SIDE_RIGHT, body_0429, 64, notes[i]); }
static void synth_LAB_0421(Bank *b) { static const int n[] = { 0x1a8, 0x150, 0x10a, 0xd4, 0xa8, 0x85, 0 }; jingle(b, n); }
static void synth_LAB_0423_6(Bank *b) { static const int n[] = { 0x9f, 0xd4, 0x9f, 0x8d, 0 }; jingle(b, n); }

/* ------------------------------------------------------------------ table */
typedef struct { SfxDesc d; void (*synth)(Bank *); } Entry;
static const Entry entries[] = {
    { { "LAB_03C1", "large explosion: 4-voice BIGEXPL chord $400/$408/$480/$488 (LAB_0630 boss/ring death)", 2 }, synth_LAB_03C1 },
    { { "LAB_03C3", "explosion pair: BIGEXPL $300 + $301..$308 (burning wreck LAB_062D, ring LAB_062F)", 2 }, synth_LAB_03C3 },
    { { "LAB_03C5", "explosion: 2 x BIGEXPL at $250+rnd (default enemy death LAB_0634)", 0 }, synth_LAB_03C5 },
    { { "LAB_03C9", "smart bomb: 4-voice SMART.SND chord $410/$401/$3F2/$3E4", 2 }, synth_LAB_03C9 },
    { { "LAB_03CC", "fireball / Xevious bomb drop: vibrato tone falling (LAB_05E9)", 0 }, synth_LAB_03CC },
    { { "LAB_03D0", "cannon fire: tone 800->500 (LAB_073D)", 0 }, synth_LAB_03D0 },
    { { "LAB_03D3", "jeep jump: warbling tone 1500, 64 frames", 0 }, synth_LAB_03D3 },
    { { "LAB_03D6", "enemy hit: noise burst both sides, period 400 rising (LAB_07B3/07C8)", 3 }, synth_LAB_03D6 },
    { { "LAB_03D8", "enemy hit (high): noise burst both sides, period 150 rising (LAB_0627/0860)", 3 }, synth_LAB_03D8 },
    { { "$210BC4", "UNUSED: pluck wave, period 248, loop length 32..63 words", 2 }, synth_210BC4 },
    { { "LAB_03E2", "player shot: SHOT wave 1000, 32 frames, pitch falls", 0 }, synth_LAB_03E2 },
    { { "$210CBE", "UNUSED: HIT wave 1000, 64 frames, pitch falls", 0 }, synth_210CBE },
    { { "LAB_03E7", "pickup / collect: vibrato tone 150, 48 frames (LAB_06C2)", 0 }, synth_LAB_03E7 },
    { { "LAB_03EB", "hit / damage: HIT wave 200, 16 frames, pitch falls fast (LAB_0726)", 0 }, synth_LAB_03EB },
    { { "LAB_03EE", "flamethrower hiss: noise swell at 500 then fade at 1000 (FLAME LAB_0765)", 1 }, synth_LAB_03EE },
    { { "LAB_03F3", "bonus token pickup: 2 gliding tones 2500/2227 + long fade (LAB_072F/0745/0759/0761)", 0 }, synth_LAB_03F3 },
    { { "LAB_03F8", "game-start noise bed: 4 voices fading in very slowly, endless (LAB_00CE; capped)", 1 }, synth_LAB_03F8 },
    { { "$210F5A", "UNUSED: 4-voice square chord $3F0/$3B8/$380/$320, endless (capped)", 1 }, synth_210F5A },
    { { "LAB_03FE", "homing bullet launch: noise, period 320 falling, 64 frames (LAB_0616)", 0 }, synth_LAB_03FE },
    { { "LAB_0401", "jet fly-by whoosh: noise swell 300, ~8 s (DADA LAB_05C7)", 0 }, synth_LAB_0401 },
    { { "LAB_0405", "big enemy death warble: 3 sine voices random pitch, ~10 s (LAB_0785)", 3 }, synth_LAB_0405 },
    { { "LAB_040C", "missile launch: noise, period 256 rising, 48 frames (LAB_06A1)", 0 }, synth_LAB_040C },
    { { "LAB_040F", "turret / cannon shot: 2 square voices 500+-50, 126 frames (LAB_07C7/07D3)", 0 }, synth_LAB_040F },
    { { "LAB_0411", "bomb drop: 2 square voices 200+-20, 48 frames (LAB_0759/07DA)", 0 }, synth_LAB_0411 },
    { { "LAB_0416", "crop-plane take-off drone: 2 voices 10000/11000 -> 1500 wobble, ~30 s (LAB_05FF)", 3 }, synth_LAB_0416 },
    { { "LAB_041B", "enemy destroyed bleep: triangle 200+-128 both sides, 192 frames (LAB_0628/0862)", 3 }, synth_LAB_041B },
    { { "LAB_041F", "ricochet ping: square 300/500/700, 6 frames (LAB_05BD)", 0 }, synth_LAB_041F },
    { { "LAB_0421", "extra-life jingle: 6 notes 1A8/150/10A/D4/A8/85 (LAB_0566)", 0 }, synth_LAB_0421 },
    { { "LAB_0423+6", "weapon-token jingle: 4 notes 9F/D4/9F/8D (LAB_06B2)", 0 }, synth_LAB_0423_6 },
};
#define NENTRIES ((int)(sizeof entries / sizeof entries[0]))

int sfx_bank_count(void) { return NENTRIES * 2; }   /* [0,N) = 200 Hz driver tick, [N,2N) = 50 Hz variant */
static SfxDesc slow_desc[64]; static char slow_label[64][200]; static char slow_name[64][32];
const SfxDesc *sfx_bank_desc(int i) {
    if (i >= 0 && i < NENTRIES) return &entries[i].d;
    if (i >= NENTRIES && i < 2 * NENTRIES) {
        int k = i - NENTRIES;
        if (!slow_desc[k].name) { snprintf(slow_name[k], 32, "%s~50", entries[k].d.name); snprintf(slow_label[k], 200, "%s  [50 Hz timing variant]", entries[k].d.label); slow_desc[k] = (SfxDesc){ slow_name[k], slow_label[k], entries[k].d.channel_hint }; }
        return &slow_desc[k];
    }
    return NULL;
}

/* ------------------------------------------------------------------ renderer */
typedef struct { int voice, start, end; int16_t *pcm; } Clip;     /* end = exclusive frame */

static int run_body(const Bank *b, const Req *r, Voice *v) {    /* renders standalone, returns frames */
    memset(v, 0, sizeof *v);
    v->out = calloc((size_t)MAX_FRAMES * FRAME_SAMPLES, sizeof(int16_t)); if (!v->out) return 0;
    v->a0 = (int16_t)r->d1; v->a2 = (int16_t)r->d2; v->big = b->big; v->bign = b->bign; v->smart = b->smart; v->smartn = b->smartn;
    v->len = 1; v->per = 1000;
    if (setjmp(v->jb) == 0) r->body(v);
    return v->nframes;
}

/* priority of hardware voice k at frame f given the clip (if any) running on it */
static int voice_prio(const Clip *c, int prio4, int f) {
    if (!c || f >= c->end) return 0;
    int p = prio4 - (f - c->start); return p < 0 ? 0 : p;
}
/* LAB_03B5 / LAB_03BA: pick a voice for (prio4, side) at frame f; -1 = dropped */
static int alloc_voice(Clip *cur[4], const int prio4_of[4], int prio4, int side, int f) {
    static const int pairs[2][2][2] = { { { 1, 2 }, { 0, 3 } }, { { 0, 3 }, { 1, 2 } } };   /* right-first, left-first */
    const int (*pr)[2] = pairs[side == SIDE_LEFT ? 1 : 0];
    for (int p = 0; p < 2; p++) {
        int a = pr[p][0], bb = pr[p][1];
        int pa = voice_prio(cur[a], prio4_of[a], f), pb = voice_prio(cur[bb], prio4_of[bb], f);
        int pick = a, pmin = pa; if (pb < pa) { pick = bb; pmin = pb; }
        if ((unsigned)prio4 > (unsigned)pmin) return pick;
    }
    return -1;
}

int16_t *sfx_bank_render(int i, SwivDisk *d, int *frames_out) {
    if (frames_out) *frames_out = 0;
    if (i < 0 || i >= 2 * NENTRIES) return NULL;
    if (i >= NENTRIES) { i -= NENTRIES; TICK_HZ = 50.0; FRAME_SAMPLES = 441; } else { TICK_HZ = 200.0; FRAME_SAMPLES = 110; }
    Bank b; memset(&b, 0, sizeof b);
    if (d) {
        uint32_t n; int k; const uint8_t *p;
        k = swiv_find(d, "BIGEXPL.SND"); if (k >= 0 && (p = swiv_load(d, k, &n))) { b.big = (const int8_t *)p; b.bign = (int)n; }
        k = swiv_find(d, "SMART.SND");   if (k >= 0 && (p = swiv_load(d, k, &n))) { b.smart = (const int8_t *)p; b.smartn = (int)n; }
    }
    entries[i].synth(&b);

    Clip clips[32]; int nclips = 0; Clip *cur[4] = { 0 }; int prio4_of[4] = { 0 };
    Voice *v = malloc(sizeof *v); if (!v) return NULL;
    int total = 0;
    for (int r = 0; r < b.nreq; r++) {            /* requests are already in start-frame order */
        const Req *q = &b.req[r];
        int nf = run_body(&b, q, v);
        int prio4 = (uint16_t)(q->prio * 4);
        int k = alloc_voice(cur, prio4_of, prio4, q->side, q->frame);
        if (k < 0 || nf == 0) { free(v->out); continue; }
        if (cur[k] && cur[k]->end > q->frame) cur[k]->end = q->frame;   /* stolen: DMA off now */
        Clip *c = &clips[nclips++]; *c = (Clip){ k, q->frame, q->frame + nf, v->out };
        cur[k] = c; prio4_of[k] = prio4;
        if (c->end > total) total = c->end;
    }
    free(v);
    /* Mix as the listener's nearer ear hears it: Paula voices 0/3 (AUD3/AUD0) are the
     * left channel, 1/2 the right; the A500's output blended 0.75 own side + 0.25 other.
     * The louder side is taken as "near" (audio.c pans the result by x afterwards), so a
     * single voice comes out at 0.75 * 8192 = 6144 and a 4-voice chord (two per side) at
     * 16384 -- the 1 : 2.67 ratio of the real stereo mix, measured on the Musashi host
     * (a hardware mono sum would be 1 : 4; the bank's old mono sum was that). */
    int nsamp = total * FRAME_SAMPLES;
    int16_t *out = calloc((size_t)(nsamp ? nsamp : 1), sizeof(int16_t));
    int32_t *side = calloc((size_t)(nsamp ? nsamp : 1) * 2, sizeof(int32_t));
    if (out && side) {
        for (int c = 0; c < nclips; c++) {
            int n = (clips[c].end - clips[c].start) * FRAME_SAMPLES, o = clips[c].start * FRAME_SAMPLES;
            int32_t *dst = side + ((clips[c].voice == 0 || clips[c].voice == 3) ? 0 : nsamp);
            for (int s = 0; s < n; s++) dst[o + s] += clips[c].pcm[s];
        }
        int32_t pl = 0, pr = 0;
        for (int s = 0; s < nsamp; s++) { int32_t a = side[s] < 0 ? -side[s] : side[s], b = side[nsamp + s] < 0 ? -side[nsamp + s] : side[nsamp + s]; if (a > pl) pl = a; if (b > pr) pr = b; }
        const int32_t *near = pl >= pr ? side : side + nsamp, *far = pl >= pr ? side + nsamp : side;
        for (int s = 0; s < nsamp; s++) { int m = (near[s] * 3 + far[s]) / 4; out[s] = (int16_t)(m > 32767 ? 32767 : m < -32768 ? -32768 : m); }
    }
    free(side);
    for (int c = 0; c < nclips; c++) free(clips[c].pcm);
    if (!out) return NULL;
    if (frames_out) *frames_out = nsamp;
    return out;
}
