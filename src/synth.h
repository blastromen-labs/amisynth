#pragma once

#include <exec/types.h>

/* Every wave before SYNTH_WAVE_CUSTOM is a fixed shape that can also be loaded
   into a custom wave as a drawing template. */
typedef enum {
	SYNTH_WAVE_SAW = 0,
	SYNTH_WAVE_REVERSE_SAW,
	SYNTH_WAVE_SQUARE,
	SYNTH_WAVE_PULSE,
	SYNTH_WAVE_TRIANGLE,
	SYNTH_WAVE_CUSTOM,
	SYNTH_WAVE_COUNT
} SynthWave;

/* Each oscillator has its own custom wave. */
#define SYNTH_OSC_COUNT 2

/* Waves that follow the pulse width control and its modulation. */
static inline short synth_wave_has_width(SynthWave wave) {
	return wave == SYNTH_WAVE_PULSE || wave == SYNTH_WAVE_CUSTOM;
}

short synth_init(void);
void synth_shutdown(void);

const BYTE *synth_wave(SynthWave wave, UBYTE osc);
void synth_custom_set(UBYTE osc, UWORD index, int value);
void synth_custom_reset(UBYTE osc);
void synth_cycle_preset(UBYTE osc);
const char *synth_preset_name(UBYTE osc);
UWORD synth_custom_generation(UBYTE osc);
BYTE *synth_voice(void);
BYTE *synth_voice2(void);
void synth_render(SynthWave wave, UBYTE osc, UWORD note, UBYTE cutoff, UBYTE resonance, UBYTE pulse_high, BYTE *dest);
const char *synth_wave_name(SynthWave wave);
const char *synth_note_name(UWORD index);
UWORD synth_note_samples(UWORD index);
UWORD synth_note_period(UWORD index);
void synth_tuned_pitch(UWORD index, int cents, UWORD *period, UWORD *samples);
UWORD synth_note_from_x(WORD x);
