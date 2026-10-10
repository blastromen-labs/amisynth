#pragma once

#include <exec/types.h>
#include "config.h"

/* Screen-wide rows of equal columns, shared by the synth and FX views. */
static inline int panel_column(int x, int columns) {
	if (x < 0)
		x = 0;
	if (x >= SCREEN_WIDTH)
		x = SCREEN_WIDTH - 1;
	return (int)(((long)x * columns) / SCREEN_WIDTH);
}

static inline int panel_column_left(int column, int columns) {
	return (int)(((long)column * SCREEN_WIDTH) / columns);
}

static inline int panel_column_right(int column, int columns) {
	return (int)(((long)(column + 1) * SCREEN_WIDTH) / columns) - 1;
}

/* Fader tracks read 255 at the top and 0 at the bottom. */
static inline UBYTE panel_value_from_y(int y, int top, int bottom) {
	int span = bottom - top;

	if (span < 1)
		span = 1;
	if (y <= top)
		return 255;
	if (y >= bottom)
		return 0;
	return (UBYTE)(((bottom - y) * 255) / span);
}
