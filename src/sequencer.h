#pragma once

#include <exec/types.h>

typedef struct {
	UBYTE on;
	UBYTE note;
	UBYTE wave;
	UBYTE dur;
} SeqStep;

void sequencer_init(void);
void sequencer_clock_start(void);
void sequencer_clock_stop(void);
short sequencer_playing(void);
void sequencer_toggle_play(void);
UWORD sequencer_index(void);
UWORD sequencer_length(void);
UWORD sequencer_step_from_x(WORD x);
void sequencer_cycle_length(void);
void sequencer_cycle_octave(void);
short sequencer_octave(void);
UWORD sequencer_sounding_note(UBYTE note);
UWORD sequencer_bpm(void);
/* Clock ticks in `count` sixteenth notes at the current tempo. */
UWORD sequencer_step_ticks(UWORD count);
void sequencer_set_bpm(UWORD bpm);
void sequencer_set_note(UWORD step, short note);
void sequencer_set_dur(UWORD step, UBYTE dur);
void sequencer_toggle_edit(void);
short sequencer_edit_dur(void);
UBYTE sequencer_dur_from_y(WORD y);
void sequencer_cycle_dur(void);
UBYTE sequencer_dur_preset(void);
void sequencer_cycle_wave(UWORD step);
void sequencer_cycle_all_waves(void);
UBYTE sequencer_osc1_wave(void);
void sequencer_set_osc1_wave(UBYTE wave);
void sequencer_toggle_wave_mode(void);
short sequencer_per_step(void);
UBYTE sequencer_global_wave(void);
void sequencer_randomize(UWORD salt);
const SeqStep *sequencer_steps(void);
short sequencer_note_from_y(WORD y);
