#pragma once

#include <exec/types.h>

void audio_init(const void *wave_a, const void *wave_b, UWORD period, UWORD samples);
void audio_start(void);
void audio_update(const void *wave_a, UWORD period_a, UWORD samples_a, UBYTE volume_a,
	const void *wave_b, UWORD period_b, UWORD samples_b, UBYTE volume_b);
void audio_stop(void);
