/* sfx_bank.h -- all original SWIV sound routines synthesised from their Paula parameters. */
#ifndef SWIV_SFX_BANK_H
#define SWIV_SFX_BANK_H
#include <stdint.h>
#include "swivdata.h"
typedef struct { const char *name; const char *label; int channel_hint; } SfxDesc;
int sfx_bank_count(void);
const SfxDesc *sfx_bank_desc(int i);
int16_t *sfx_bank_render(int i, SwivDisk *d, int *frames_out);   /* 22050 Hz mono, malloc'd */
#endif
