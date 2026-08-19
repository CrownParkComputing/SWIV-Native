/* audio.c -- raylib audio for the native engine.  The game has two PCM samples
 * (BIGEXPL.SND, SMART.SND); every other effect is synthesised by the original's
 * Paula driver (LAB_03AD + LAB_03xx), not yet ported -- stand-ins below. */
#include "engine/engine.h"
#include "raylib.h"
#include <xmp.h>
#include "sfx_bank.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

static Sound snd[SFX_COUNT]; static int ready;
/* optional synthesised bank (src/sfx_bank.c) */
int sfx_bank_count(void) __attribute__((weak)); const SfxDesc *sfx_bank_desc(int i) __attribute__((weak)); int16_t *sfx_bank_render(int i, SwivDisk *d, int *frames_out) __attribute__((weak));
const char *audio_bank_name(int i);
static Sound bank_snd[64]; static int bank_n; static int16_t *bank_pcm[64]; static int bank_frames[64];
typedef struct { float pitch, vol, maxsec; } Tune; static Tune tune[64];
static void bank_rebuild(int i) {
    if (!bank_pcm[i]) return;
    if (bank_snd[i].frameCount) UnloadSound(bank_snd[i]);
    int fr = bank_frames[i]; int cap = (int)(tune[i].maxsec * 22050); if (cap > 0 && fr > cap) fr = cap;
    Wave w = { .frameCount = fr, .sampleRate = 22050, .sampleSize = 16, .channels = 1, .data = bank_pcm[i] };
    bank_snd[i] = LoadSoundFromWave(w); SetSoundPitch(bank_snd[i], tune[i].pitch); SetSoundVolume(bank_snd[i], tune[i].vol);
}
float audio_tune_get(int i, int what) { return what == 0 ? tune[i].pitch : what == 1 ? tune[i].vol : tune[i].maxsec; }
void audio_tune_set(int i, int what, float v) { if (what == 0) tune[i].pitch = v; else if (what == 1) tune[i].vol = v; else tune[i].maxsec = v; bank_rebuild(i); }
void audio_tune_save(void) { FILE *f = fopen("sfxtune.txt", "w"); if (!f) return; for (int i = 0; i < bank_n; i++) fprintf(f, "%s %.3f %.3f %.2f\n", audio_bank_name(i), tune[i].pitch, tune[i].vol, tune[i].maxsec); fclose(f); }
static volatile int bank_done[64]; static int bank_loaded[64];
static void bank_postprocess(int i) {
    int16_t *pcm = bank_pcm[i]; int fr = bank_frames[i]; if (!pcm || fr <= 0) return;
    double a = 1.0 - exp(-2.0 * 3.14159265 * 4000.0 / 22050.0), y1 = 0, y2 = 0;   /* Amiga output filter ~4 kHz */
    for (int k = 0; k < fr; k++) { y1 += a * (pcm[k] - y1); y2 += a * (y1 - y2); pcm[k] = (int16_t)y2; }
    /* A500 stereo blend gain (the bank renders the nearer ear at 0.75 own + 0.25 other; x1.8 restores full-scale chords) */
    for (int k = 0; k < fr; k++) { int v = (int)(pcm[k] * 1.8f); pcm[k] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
}
static void *bank_worker(void *arg) {
    SwivDisk *d = arg;
    for (int i = 0; i < bank_n; i++) { int fr = 0; int16_t *pcm = sfx_bank_render(i, d, &fr); bank_pcm[i] = pcm; bank_frames[i] = fr; bank_postprocess(i); bank_done[i] = 1; }
    return NULL;
}
static void bank_pickup(void) { for (int i = 0; i < bank_n; i++) if (bank_done[i] && !bank_loaded[i]) { bank_loaded[i] = 1; bank_rebuild(i); } }
static void audio_tune_load(void) { FILE *f = fopen("sfxtune.txt", "r"); char nm[64]; float p, v, m; if (!f) return; while (fscanf(f, "%63s %f %f %f", nm, &p, &v, &m) == 4) for (int i = 0; i < bank_n; i++) if (!strcmp(nm, audio_bank_name(i))) { tune[i] = (Tune){ p, v, m }; bank_rebuild(i); } fclose(f); }
int audio_bank_frames(int i) { return bank_frames[i]; } static int event_bank[SFX_COUNT]; static SwivDisk *adisk;
int audio_bank_count(void) { return bank_n; }
const char *audio_bank_name(int i) { return sfx_bank_desc ? sfx_bank_desc(i)->name : ""; }
const char *audio_bank_label(int i) { return sfx_bank_desc ? sfx_bank_desc(i)->label : ""; }
void audio_bank_play(int i) { if (i >= 0 && i < bank_n && bank_snd[i].frameCount) PlaySound(bank_snd[i]); }
int  audio_event_bank(int ev) { return event_bank[ev]; }
void audio_event_set(int ev, int bank) { if (ev >= 0 && ev < SFX_COUNT) event_bank[ev] = bank; }
void audio_map_save(void) { FILE *f = fopen("sfxmap.txt", "w"); if (!f) return; for (int e = 0; e < SFX_COUNT; e++) fprintf(f, "%s %s\n", sfx_event_names[e], event_bank[e] >= 0 ? audio_bank_name(event_bank[e]) : "-"); fclose(f); }
static void audio_map_load(void) { FILE *f = fopen("sfxmap.txt", "r"); char ev[64], nm[64]; if (!f) return; while (fscanf(f, "%63s %63s", ev, nm) == 2) for (int e = 0; e < SFX_COUNT; e++) if (!strcmp(ev, sfx_event_names[e])) { event_bank[e] = -1; for (int i = 0; i < bank_n; i++) if (!strcmp(nm, audio_bank_name(i))) event_bank[e] = i; } fclose(f); }
static float sfx_gain = 1.0f, music_gain = 1.0f;
static xmp_context xc; static AudioStream mstream; static int music_on; static int16_t mbuf[2048];   /* 1024 stereo frames = the stream buffer size */

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
static const int8_t WAVE_SHOT[16] = { 0x00,0x10,0x20,0x40,0x7f,0x40,0x20,0x10, 0x00,(int8_t)0xc0,(int8_t)0xa0,(int8_t)0x90,(int8_t)0x81,(int8_t)0x90,(int8_t)0xa0,(int8_t)0xc0 };   /* LAB_03E3 */
static const int8_t WAVE_HIT[16]  = { 0x00,0x20,0x40,0x60,0x7f,0x60,0x40,0x20, 0x00,(int8_t)0xe0,(int8_t)0xc0,(int8_t)0xa0,(int8_t)0x80,(int8_t)0xa0,(int8_t)0xc0,(int8_t)0xe0 };   /* LAB_03EC */
/* LAB_03E4/03E6 style voice: each VBL volume = frames remaining, period += period>>4 (pitch falls) */
static void voice_sweep(const int8_t *wave, int len, int period, int frames) {
    int total = frames * OUT / 50; if (total > mixn) { mixbuf = realloc(mixbuf, total * 2); memset(mixbuf + mixn, 0, (total - mixn) * 2); mixn = total; }
    double pos = 0; int per = period;
    for (int fr = 0; fr < frames; fr++) {
        int vol = frames - fr; double step = (PAULA / per) / OUT;
        for (int i = fr * OUT / 50; i < (fr + 1) * OUT / 50; i++) { mixbuf[i] += (int16_t)(wave[(int)pos % len] * vol); pos += step; if (pos >= len) pos -= len; }
        per += per >> 4;
    }
}
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
    voice_sweep(WAVE_SHOT, 16, 0x3e8, 32); snd[SFX_SHOT] = finish();                          /* LAB_03E2: 32 frames, period 1000 rising */
    voice_sweep(WAVE_HIT, 16, 0xc8, 64); snd[SFX_HIT] = finish();                             /* LAB_03EB: 64 frames, period 200 rising */
    noise8 = malloc(256); uint32_t r = 1; for (int k = 0; k < 256; k++) { r = r * 1103515245u + 12345u; noise8[k] = (int8_t)(r >> 24); }
    voice(noise8, 256, 1, 0x60, 64, 50, 0); snd[SFX_MISSILE] = finish();                       /* LAB_040C approx (period $60, 50 frames) */
    voice(WAVE_SHOT, 16, 1, 0x200, 40, 10, 0); voice(WAVE_SHOT, 16, 1, 0x100, 40, 10, 10); snd[SFX_PICKUP] = finish();   /* LAB_03F3 two-tone approx */
    ready = 1; adisk = d;
    for (int e = 0; e < SFX_COUNT; e++) event_bank[e] = -1;
    if (sfx_bank_count) {
        bank_n = sfx_bank_count(); if (bank_n > 64) bank_n = 64;
        for (int i = 0; i < bank_n; i++) tune[i] = (Tune){ 1.0f, 1.0f, 4.0f };
        audio_map_load(); audio_tune_load();
        pthread_t th; pthread_create(&th, NULL, bank_worker, d); pthread_detach(th);   /* renders in the background; audio_update() picks up finished entries */
    }
    xc = xmp_create_context();
    SetAudioStreamBufferSizeDefault(1024); mstream = LoadAudioStream(44100, 16, 2);
}
/* MOD music via libxmp: name = "AMTITUNE.MOD" (title) / "AMHITUNE.MOD" (hi-score) / NULL = stop */
static SwivDisk *mdisk; static char mcur[32];
void audio_music_play(SwivDisk *d, const char *name) {
    if (!ready) return;
    if (!name) { if (music_on) { StopAudioStream(mstream); xmp_end_player(xc); xmp_release_module(xc); music_on = 0; mcur[0] = 0; } return; }
    if (music_on && !strcmp(mcur, name)) return;
    audio_music_play(d, NULL);
    uint32_t n; int i = swiv_find(d, name); const uint8_t *p;
    if (i < 0 || !(p = swiv_load(d, i, &n))) return;
    int rc = xmp_load_module_from_memory(xc, (void *)p, n); fprintf(stderr, "music: %s load=%d\n", name, rc); if (rc != 0) return;
    xmp_start_player(xc, 44100, 0); xmp_set_player(xc, XMP_PLAYER_AMP, 1); xmp_set_player(xc, XMP_PLAYER_MIX, 70);
    SetAudioStreamVolume(mstream, music_gain); PlayAudioStream(mstream); music_on = 1; snprintf(mcur, sizeof mcur, "%s", name); mdisk = d;
}
void audio_set_volumes(float sfxv, float musicv) { sfx_gain = sfxv; music_gain = musicv; if (mstream.buffer) SetAudioStreamVolume(mstream, music_gain); }
void audio_update(void) {
    bank_pickup();
    if (!music_on) return;
    while (IsAudioStreamProcessed(mstream)) { xmp_play_buffer(xc, mbuf, sizeof mbuf, 0); UpdateAudioStream(mstream, mbuf, 1024); }
}
void sfx(int id, int x) {
    if (!ready || id < 0 || id >= SFX_COUNT) return;
    Sound s = (event_bank[id] >= 0 && bank_snd[event_bank[id]].frameCount) ? bank_snd[event_bank[id]] : snd[id];
    if (!s.frameCount) return;
    SetSoundPan(s, 1.0f - (float)x / 320.0f * 0.6f - 0.2f);
    { int b = event_bank[id]; SetSoundVolume(s, sfx_gain * (b >= 0 ? tune[b].vol : 1.0f)); }
    PlaySound(s);
}
