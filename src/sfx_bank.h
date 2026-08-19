/* sfx_bank.h -- SWIV sound-effect bank synthesised from the original Paula sound
 * driver in AMPROG.OBJ (LAB_03AD .. LAB_0430, see re/amprog.asm).
 *
 * Every entry is a data-faithful re-run of the 68000 per-frame voice coroutine
 * through an exact Paula model (8-bit signed data at 3546895/period Hz, volume
 * 0..64 linear, register changes once per 50 Hz VBL, 4 hardware voices with the
 * driver's priority/voice allocation), rendered to 16-bit mono PCM at 22050 Hz. */
#ifndef SWIV_SFX_BANK_H
#define SWIV_SFX_BANK_H
#include <stdint.h>
#include "swivdata.h"

typedef struct {
    const char *name;      /* AMPROG label of the entry point, e.g. "LAB_03E2" */
    const char *label;     /* human guess at its use, from the call sites */
    int channel_hint;      /* 0 = positional (x < 160 -> left pair 0/3 else right pair 1/2),
                              1 = left pair preferred, 2 = right pair preferred, 3 = both sides */
} SfxDesc;

int sfx_bank_count(void);
const SfxDesc *sfx_bank_desc(int i);
/* malloc'd 22050 Hz mono 16-bit PCM; *frames_out = sample count (0 / NULL on failure).
 * d may be NULL (sample-based entries then render silence). */
int16_t *sfx_bank_render(int i, SwivDisk *d, int *frames_out);

#endif
