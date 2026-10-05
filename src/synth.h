#pragma once

#include <exec/types.h>

typedef enum {
	SYNTH_WAVE_SINE = 0,
	SYNTH_WAVE_TRIANGLE,
	SYNTH_WAVE_SAW,
	SYNTH_WAVE_SQUARE,
	SYNTH_WAVE_EXACT_SAW,
	SYNTH_WAVE_EXACT_SQUARE,
	SYNTH_WAVE_EXACT_TRIANGLE,
	SYNTH_WAVE_EXACT_PULSE,
	SYNTH_WAVE_CUSTOM,
	SYNTH_WAVE_COUNT
} SynthWave;

short synth_init(void);
void synth_shutdown(void);

const BYTE *synth_wave(SynthWave wave);
void synth_custom_set(UWORD index, int value);
void synth_custom_reset(void);
void synth_cycle_preset(void);
const char *synth_preset_name(void);
UWORD synth_custom_generation(void);
BYTE *synth_voice(void);
BYTE *synth_voice2(void);
void synth_render(SynthWave wave, UWORD note, UBYTE cutoff, UBYTE resonance, UBYTE pulse_high, BYTE *dest);
const char *synth_wave_name(SynthWave wave);
const char *synth_note_name(UWORD index);
UWORD synth_note_samples(UWORD index);
UWORD synth_note_period(UWORD index);
void synth_tuned_pitch(UWORD index, int cents, UWORD *period, UWORD *samples);
UWORD synth_note_from_x(WORD x);
