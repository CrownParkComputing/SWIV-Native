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

static Sound make_pcm(const uint8_t *d, uint32_t n, int rate, int unsigned8) {
    Wave w = { .frameCount = n, .sampleRate = rate, .sampleSize = 16, .channels = 1 };
    int16_t *buf = malloc(n * 2);
    for (uint32_t i = 0; i < n; i++) buf[i] = (int16_t)((unsigned8 ? (int)d[i] - 128 : (int8_t)d[i]) << 8);
    w.data = buf; Sound s = LoadSoundFromWave(w); free(buf); return s;
}
static Sound synth(int ms, double f0, double f1, int noise, double decay) {
    int rate = 22050, n = rate * ms / 1000; int16_t *buf = malloc(n * 2); double ph = 0; uint32_t r = 12345;
    for (int i = 0; i < n; i++) {
        double t = (double)i / n, f = f0 + (f1 - f0) * t, env = exp(-decay * t);
        double v;
        if (noise) { r = r * 1103515245u + 12345u; v = ((int)(r >> 16) & 0xffff) / 32768.0 - 1.0; }
        else { ph += f / rate; v = (fmod(ph, 1.0) < 0.5) ? 1.0 : -1.0; }
        buf[i] = (int16_t)(v * env * 12000);
    }
    Wave w = { .frameCount = n, .sampleRate = rate, .sampleSize = 16, .channels = 1, .data = buf };
    Sound s = LoadSoundFromWave(w); free(buf); return s;
}
void audio_init(SwivDisk *d) {
    InitAudioDevice(); if (!IsAudioDeviceReady()) return;
    uint32_t n; const uint8_t *p;
    int i = swiv_find(d, "BIGEXPL.SND"); if (i >= 0 && (p = swiv_load(d, i, &n))) { snd[SFX_BIGEXPL] = make_pcm(p, n, 8000, 0); snd[SFX_EXPL1] = snd[SFX_BIGEXPL]; snd[SFX_EXPL2] = snd[SFX_BIGEXPL]; }
    i = swiv_find(d, "SMART.SND"); if (i >= 0 && (p = swiv_load(d, i, &n))) snd[SFX_BOMB] = make_pcm(p, n, 8000, 0);
    snd[SFX_SHOT] = synth(60, 1800, 600, 0, 6);
    snd[SFX_HIT] = synth(40, 900, 300, 1, 8);
    snd[SFX_MISSILE] = synth(250, 300, 1200, 1, 3);
    snd[SFX_PICKUP] = synth(160, 700, 1400, 0, 2);
    ready = 1;
    /* title music: AMTITUNE.MOD via raylib's module player, if the build supports .mod */
    /* MOD playback disabled by default: raylib's jar_mod hung on AMTITUNE.MOD (set SWIV_MUSIC=1 to try) */
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
