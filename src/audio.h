#pragma once

#include <exec/types.h>
#include "synth.h"

/* What one Paula voice plays: a looped cycle at a period and volume. */
typedef struct {
	const void *wave;
	UWORD period;
	UWORD samples;
	UBYTE volume;
} AudioVoice;

/* Off leaves Paula untouched, apart from silencing it on stop. */
void audio_enable(short on);
/* Takes all four channels from audio.device while AmigaOS keeps running. */
short audio_claim(void);
void audio_release(void);
void audio_init(const void *wave_a, const void *wave_b, UWORD period, UWORD samples);
void audio_start(void);
/* One voice per oscillator on each side of the stereo output. */
void audio_update(const AudioVoice left[SYNTH_OSC_COUNT], const AudioVoice right[SYNTH_OSC_COUNT]);
void audio_stop(void);
