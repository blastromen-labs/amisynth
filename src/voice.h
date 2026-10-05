#pragma once

#include "config.h"
#include "synth.h"

enum {
	VOICE_ATTACK = 0,
	VOICE_DECAY,
	VOICE_SUSTAIN,
	VOICE_RELEASE,
	VOICE_MIX,
	VOICE_OSC2_SEMI,
	VOICE_OSC2_FINE,
	VOICE_CUTOFF,
	VOICE_RESONANCE,
	VOICE_FILTER_ATTACK,
	VOICE_FILTER_DECAY,
	VOICE_FILTER_SUSTAIN,
	VOICE_FILTER_RELEASE,
	VOICE_FILTER_ENV,
	VOICE_PULSE_WIDTH,
	VOICE_PW_DEPTH,
	VOICE_PITCH_DEPTH,
	VOICE_FILTER_DEPTH,
	VOICE_LFO1_RATE,
	VOICE_LFO2_RATE,
	VOICE_CONTROL_COUNT
};

#define SYNTH_GATE_SLOT 252
#define SYNTH_OSC2_SLOT 253
#define SYNTH_WAVE_SLOT 255

void voice_init(void);
void voice_set(UBYTE control, UBYTE value);
UBYTE voice_get(UBYTE control);
void voice_trigger(SynthWave wave, UWORD note);
void voice_set_wave(SynthWave wave);
void voice_set_note(UWORD note);
void voice_audition_custom(short on);
void voice_cycle_osc2(void);
UBYTE voice_osc2(void);
int voice_osc2_semitone(void);
void voice_step_semitone(int direction);
int voice_osc2_fine(void);
void voice_step_fine(int direction);
short voice_cycle_route(UBYTE control);
const char *voice_route_label(UBYTE control);
int voice_live(UBYTE control);
void voice_release(void);
void voice_toggle_gate(void);
short voice_gate_mode(void);
void voice_tick(void);
UBYTE voice_volume(void);

static inline int synth_track_top(int y) {
	if (y >= SYNTH_ROW3_TOP)
		return SYNTH_ROW3_TRACK_TOP;
	if (y >= SYNTH_ROW2_TOP)
		return SYNTH_ROW2_TRACK_TOP;
	return SYNTH_ROW1_TRACK_TOP;
}

static inline int synth_panel_slot(int x, int y) {
	int slot;

	if (x < 0)
		x = 0;
	if (x >= SCREEN_WIDTH)
		x = SCREEN_WIDTH - 1;
	if (y >= SYNTH_ROW3_TOP) {
		slot = (int)(((long)x * SYNTH_ROW3_COLUMNS) / SCREEN_WIDTH);
		return VOICE_PULSE_WIDTH + slot;
	}
	if (y >= SYNTH_ROW2_TOP) {
		slot = (int)(((long)x * SYNTH_ROW2_COLUMNS) / SCREEN_WIDTH);
		return VOICE_CUTOFF + slot;
	}
	slot = (int)(((long)x * SYNTH_ROW1_COLUMNS) / SCREEN_WIDTH);
	if (slot <= VOICE_OSC2_FINE)
		return slot;
	if (y < SYNTH_OSC_BUTTON_BOTTOM)
		return slot == VOICE_OSC2_FINE + 1 ? SYNTH_WAVE_SLOT : SYNTH_OSC2_SLOT;
	return SYNTH_GATE_SLOT;
}

static inline UBYTE voice_value_from_y(int y, int control) {
	int top = SYNTH_ROW1_TRACK_TOP;
	int bottom = SYNTH_ROW1_TRACK_BOTTOM;

	if (control == VOICE_OSC2_SEMI || control == VOICE_OSC2_FINE)
		bottom = SYNTH_SEMI_TRACK_BOTTOM;
	else if (control >= VOICE_PULSE_WIDTH) {
		top = SYNTH_ROW3_TRACK_TOP;
		bottom = SYNTH_ROW3_TRACK_BOTTOM;
	} else if (control >= VOICE_CUTOFF) {
		top = SYNTH_ROW2_TRACK_TOP;
		bottom = SYNTH_ROW2_TRACK_BOTTOM;
	}
	int span = bottom - top;
	int pos;

	if (span < 1)
		span = 1;
	if (y <= top)
		return 255;
	if (y >= bottom)
		return 0;
	pos = bottom - y;
	return (UBYTE)((pos * 255) / span);
}

static inline int synth_column_step(int x, int y, int left, int right, int top, int bottom) {
	int mid = (left + right) / 2;

	if (y < top || y >= bottom || x < left || x > right)
		return 0;
	return x < mid ? -1 : 1;
}

static inline int synth_semi_step(int x, int y) {
	int left = (int)(((long)VOICE_OSC2_SEMI * SCREEN_WIDTH) / SYNTH_ROW1_COLUMNS);
	int right = (int)(((long)(VOICE_OSC2_SEMI + 1) * SCREEN_WIDTH) / SYNTH_ROW1_COLUMNS) - 1;

	return synth_column_step(x, y, left, right, SYNTH_SEMI_BUTTON_TOP, SYNTH_SEMI_BUTTON_BOTTOM);
}

static inline int synth_fine_step(int x, int y) {
	int left = (int)(((long)VOICE_OSC2_FINE * SCREEN_WIDTH) / SYNTH_ROW1_COLUMNS);
	int right = (int)(((long)(VOICE_OSC2_FINE + 1) * SCREEN_WIDTH) / SYNTH_ROW1_COLUMNS) - 1;

	return synth_column_step(x, y, left, right, SYNTH_SEMI_BUTTON_TOP, SYNTH_SEMI_BUTTON_BOTTOM);
}
