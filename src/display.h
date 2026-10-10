#pragma once

#include <exec/types.h>
#include "config.h"
#include "sequencer.h"

enum {
	DISPLAY_SEQUENCE = 0,
	DISPLAY_DRAW,
	DISPLAY_SYNTH,
	DISPLAY_FX
};

short display_init(void);
void display_shutdown(void);
/* Hardware mode only: points the copper at the picture. */
void display_start(void);
/* Shows the frame just drawn and starts the next one. Returns the finished plane. */
const UBYTE *display_flip(void);
/* The drawn crosshair stands in for the pointer while the program owns the machine. */
void display_set_cursor(short on);
void display_set_view(UBYTE view);
UBYTE display_view(void);
UBYTE display_draw_osc(void);
void display_cycle_draw_osc(void);
void display_frame(const SeqStep *steps, UWORD current, UWORD bpm, WORD mouse_x, WORD mouse_y, UBYTE volume);

static inline int draw_plot_half(void) {
	int height = DRAW_BOTTOM - DRAW_TOP;

	if (height < 2)
		height = 2;
	return height / 2;
}

static inline int draw_value_from_y(int y) {
	int half = draw_plot_half();
	int mid = DRAW_TOP + half;
	int value;

	if (y < DRAW_TOP)
		y = DRAW_TOP;
	if (y >= DRAW_BOTTOM)
		y = DRAW_BOTTOM - 1;
	value = ((mid - y) * SYNTH_AMPLITUDE) / half;
	if (value > SYNTH_AMPLITUDE)
		value = SYNTH_AMPLITUDE;
	if (value < -SYNTH_AMPLITUDE)
		value = -SYNTH_AMPLITUDE;
	return value;
}

static inline int draw_y_from_value(int value) {
	int half = draw_plot_half();
	int mid = DRAW_TOP + half;
	int y;

	if (value > SYNTH_AMPLITUDE)
		value = SYNTH_AMPLITUDE;
	if (value < -SYNTH_AMPLITUDE)
		value = -SYNTH_AMPLITUDE;
	y = mid - (value * half) / SYNTH_AMPLITUDE;
	if (y < DRAW_TOP)
		y = DRAW_TOP;
	if (y >= DRAW_BOTTOM)
		y = DRAW_BOTTOM - 1;
	return y;
}

static inline int draw_sample_from_x(int x) {
	int span = DRAW_PLOT_RIGHT - DRAW_PLOT_LEFT;
	int rel;

	if (span < 1)
		span = 1;
	if (x < DRAW_PLOT_LEFT)
		x = DRAW_PLOT_LEFT;
	if (x >= DRAW_PLOT_RIGHT)
		x = DRAW_PLOT_RIGHT - 1;
	rel = x - DRAW_PLOT_LEFT;
	return (rel * SYNTH_WAVE_LEN) / span;
}

static inline int draw_x_from_sample(int index) {
	int span = DRAW_PLOT_RIGHT - DRAW_PLOT_LEFT;
	int x0;
	int x1;

	if (span < 1)
		span = 1;
	if (index < 0)
		index = 0;
	if (index >= SYNTH_WAVE_LEN)
		index = SYNTH_WAVE_LEN - 1;
	x0 = DRAW_PLOT_LEFT + (index * span) / SYNTH_WAVE_LEN;
	x1 = DRAW_PLOT_LEFT + ((index + 1) * span) / SYNTH_WAVE_LEN;
	return (x0 + x1) / 2;
}
