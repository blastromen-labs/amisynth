#pragma once

#include "audio.h"
#include "config.h"
#include "panel.h"

/* One FX view column per control, in this order. */
enum {
	FX_DRIVE = 0,
	FX_DELAY_STEPS,
	FX_DELAY_LEVEL,
	FX_DELAY_FEEDBACK,
	FX_REVERB_DECAY,
	FX_REVERB_LEVEL,
	FX_CONTROL_COUNT
};

void fx_reset(void);
void fx_set(UBYTE control, UBYTE value);
UBYTE fx_get(UBYTE control);
UBYTE fx_delay_steps(void);
/* Waveshapes a rendered cycle in place. */
void fx_distort(BYTE *cycle, UWORD samples);
/* Envelope volume after the drive. */
UBYTE fx_drive_volume(UBYTE volume);
/* Runs once per tick: echoes the dry voices into the wet ones. */
void fx_apply(const AudioVoice dry[SYNTH_OSC_COUNT], AudioVoice wet[SYNTH_OSC_COUNT]);

static inline int fx_panel_slot(int x, int y) {
	if (y < FX_PANEL_TOP || y >= FX_PANEL_BOTTOM)
		return -1;
	return panel_column(x, FX_CONTROL_COUNT);
}

static inline UBYTE fx_value_from_y(int y) {
	return panel_value_from_y(y, FX_TRACK_TOP, FX_TRACK_BOTTOM);
}
