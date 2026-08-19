/* audio.c -- raylib audio for the native engine.  The game has two PCM samples
 * (BIGEXPL.SND, SMART.SND); every other effect is synthesised by the original's
 * Paula driver (LAB_03AD + LAB_03xx), not yet ported -- stand-ins below. */
#include "engine/engine.h"
#include "raylib.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static Sound snd[8]; static int ready;
static Music music; static int have_music;

/* Paula emulation for the original's sound routines (LAB_03C1.. in AMPROG): a voice plays
 * 8-bit signed data at rate 3546895/period with a volume envelope over n VBLs. */
#define PAULA 3546895.0
static int16_t *mixbuf; static int mixn; static const int OUT = 22050;
static void voice(const int8_t *data, int len, int loop, int period, int vol, int frames, int delay_frames) {
    double rate = PAULA / period, step = rate / OUT; int total = (delay_frames + frames) * OUT / 50;
    if (total > mixn) { mixbuf = realloc(mixbuf, total * 2); memset(mixbuf + mixn, 0, (total - mixn) * 2); mixn = total; }
    double pos = 0; int d0 = delay_frames * OUT / 50;
    for (int i = d0; i < total; i++) {
        int fr = (i - d0) * 50 / OUT; double env = vol * (1.0 - (double)fr / frames);
        int idx = (int)pos; if (idx >= len) { if (!loop) break; idx %= len; pos -= len * (idx >= len ? 0 : 0); }
        mixbuf[i] += (int16_t)(data[(int)pos % len] * env);     /* vol 64 * 127 ~= 8128 */
        pos += step; if (loop && pos >= len) pos -= len;
    }
}
static Sound finish(void) {
    Wave w = { .frameCount = mixn, .sampleRate = OUT, .sampleSize = 16, .channels = 1, .data = mixbuf };
    Sound s = LoadSoundFromWave(w); free(mixbuf); mixbuf = NULL; mixn = 0; return s;
}
static const int8_t WAVE_SHOT[8] = { 0x00, 0x10, 0x20, 0x40, 0x7f, 0x40, 0x20, 0x10 };   /* LAB_03E3 */
static const int8_t WAVE_HIT[8]  = { 0x00, 0x20, 0x40, 0x60, 0x7f, 0x60, 0x40, 0x20 };   /* LAB_03EC */
static int8_t *noise8; 
void audio_init(SwivDisk *d) {
    InitAudioDevice(); if (!IsAudioDeviceReady()) return;
    uint32_t n; const uint8_t *p; const int8_t *big = NULL, *smart = NULL; uint32_t bign = 0, smartn = 0;
    int i = swiv_find(d, "BIGEXPL.SND"); if (i >= 0 && (p = swiv_load(d, i, &n))) { big = (const int8_t *)p; bign = n; }
    i = swiv_find(d, "SMART.SND"); if (i >= 0 && (p = swiv_load(d, i, &n))) { smart = (const int8_t *)p; smartn = n; }
    if (big) {
        voice(big, bign, 0, 0x250 + 16, 50, 60, 0); snd[SFX_EXPL2] = finish();                 /* LAB_03C5/03C6: period $250+rnd&31, vol 50 */
        voice(big, bign, 0, 0x300, 60, 60, 0); voice(big, bign, 0, 0x304, 60, 60, 0); snd[SFX_EXPL1] = finish();   /* LAB_03C3 */
        voice(big, bign, 0, 0x400, 64, 80, 0); voice(big, bign, 0, 0x408, 64, 80, 0); voice(big, bign, 0, 0x480, 64, 80, 0); voice(big, bign, 0, 0x488, 64, 80, 0);
        snd[SFX_BIGEXPL] = finish();                                                             /* LAB_03C1 four-voice chord */
    }
    if (smart) { voice(smart, smartn, 0, 0x410, 64, 80, 0); voice(smart, smartn, 0, 0x401, 64, 80, 0); voice(smart, smartn, 0, 0x3f2, 64, 80, 0); voice(smart, smartn, 0, 0x3e4, 64, 80, 0); snd[SFX_BOMB] = finish(); }
    voice(WAVE_SHOT, 8, 1, 0x3e8, 32, 20, 0); snd[SFX_SHOT] = finish();                        /* LAB_03E2: vol 32, 20 frames, period 1000 */
    voice(WAVE_HIT, 8, 1, 0xc8, 64, 20, 0); snd[SFX_HIT] = finish();                           /* LAB_03EB: vol 64, 20 frames, period 200 */
    noise8 = malloc(256); uint32_t r = 1; for (int k = 0; k < 256; k++) { r = r * 1103515245u + 12345u; noise8[k] = (int8_t)(r >> 24); }
    voice(noise8, 256, 1, 0x60, 64, 50, 0); snd[SFX_MISSILE] = finish();                       /* LAB_040C approx (period $60, 50 frames) */
    voice(WAVE_SHOT, 8, 1, 0x200, 40, 10, 0); voice(WAVE_SHOT, 8, 1, 0x100, 40, 10, 10); snd[SFX_PICKUP] = finish();   /* LAB_03F3 two-tone approx */
    ready = 1;
    /* MOD playback opt-in: raylib's jar_mod hung on AMTITUNE.MOD (set SWIV_MUSIC=1 to try) */
    if (getenv("SWIV_MUSIC")) { i = swiv_find(d, "AMTITUNE.MOD");
        if (i >= 0 && (p = swiv_load(d, i, &n))) { music = LoadMusicStreamFromMemory(".mod", p, n); have_music = music.frameCount > 0; } }
}
void audio_update(void) { if (have_music) UpdateMusicStream(music); }
void audio_music(int on) { if (!have_music) return; if (on) { if (!IsMusicStreamPlaying(music)) PlayMusicStream(music); } else StopMusicStream(music); }
void sfx(int id, int x) {
    if (!ready || id < 0 || id >= 8 || snd[id].frameCount == 0) return;
    SetSoundPan(snd[id], 1.0f - (float)x / 320.0f * 0.6f - 0.2f);
    PlaySound(snd[id]);
}
