#include "sequencer.h"
#include "audio.h"
#include "config.h"
#include "fx.h"
#include "synth.h"
#include "system.h"
#include "voice.h"

#include <hardware/custom.h>
#include <hardware/intbits.h>

extern volatile struct Custom *custom;

static SeqStep steps[SEQUENCER_STEPS];
static volatile UWORD bpm = SEQUENCER_BPM_DEFAULT;
static volatile UWORD index;
static UBYTE length_index = 5;
static UBYTE octave_index = 1;
static UBYTE global_wave = SYNTH_WAVE_PULSE;
static UBYTE per_step;
static UBYTE edit_dur;
static UBYTE dur_preset = 6;
/* The main loop only asks; the tick interrupt owns the voice and Paula. */
static volatile UBYTE want_play;
static UBYTE playing;
static UWORD step_countdown;
static UWORD gate_left;
static ULONG step_numer;
static ULONG step_error;
static UBYTE clock_running;

static const UBYTE pattern_lengths[] = { 3, 4, 6, 8, 12, 16 };
static const UBYTE dur_presets[] = { 5, 10, 25, 50, 75, 100, 127 };

void sequencer_init(void) {
	static const UBYTE notes[SEQUENCER_STEPS] = {
		21, 21, 33, 33, 43, 40, 38, 41,
		45, 48, 43, 40, 36, 40, 45, 48
	};
	static const UBYTE waves[SEQUENCER_STEPS] = {
		SYNTH_WAVE_PULSE, SYNTH_WAVE_PULSE, SYNTH_WAVE_PULSE, SYNTH_WAVE_PULSE,
		SYNTH_WAVE_PULSE, SYNTH_WAVE_TRIANGLE, SYNTH_WAVE_SAW, SYNTH_WAVE_REVERSE_SAW,
		SYNTH_WAVE_SQUARE, SYNTH_WAVE_SAW, SYNTH_WAVE_TRIANGLE, SYNTH_WAVE_REVERSE_SAW,
		SYNTH_WAVE_PULSE, SYNTH_WAVE_TRIANGLE, SYNTH_WAVE_SAW, SYNTH_WAVE_REVERSE_SAW
	};

	for (UWORD i = 0; i < SEQUENCER_STEPS; i++) {
		steps[i].on = 1;
		steps[i].note = notes[i];
		steps[i].wave = waves[i];
		steps[i].dur = 127;
	}
	bpm = SEQUENCER_BPM_DEFAULT;
	length_index = 1;
	octave_index = 1;
	global_wave = SYNTH_WAVE_PULSE;
	per_step = 0;
	edit_dur = 0;
	dur_preset = 6;
	index = 0;
	step_countdown = 0;
	step_error = 0;
	want_play = 0;
	playing = 0;
	clock_running = 0;
}

static ULONG step_rate(void) {
	return (ULONG)bpm * 1000UL;
}

/* Ticks per sixteenth note, spreading the remainder so the tempo is exact on
   average. step_numer is the tick rate in millihertz times 15. */
static UWORD step_interval(void) {
	ULONG rate = step_rate();
	ULONG base = step_numer / rate;

	step_error += step_numer % rate;
	if (step_error >= rate) {
		step_error -= rate;
		base++;
	}
	if (base < 1)
		base = 1;
	return (UWORD)base;
}

UWORD sequencer_step_ticks(UWORD count) {
	ULONG ticks = (step_numer * count) / step_rate();

	return ticks > 65535UL ? 65535 : (UWORD)ticks;
}

static SynthWave step_wave(const SeqStep *step) {
	if (!per_step)
		return (SynthWave)global_wave;
	return (SynthWave)step->wave;
}

static UWORD gate_ticks(UBYTE dur, UWORD ticks) {
	UWORD hold;

	if (dur == 0 || ticks == 0)
		return 0;
	if (dur >= 127)
		return ticks;
	hold = (UWORD)(((ULONG)ticks * dur + 126) / 127);
	if (hold < 1)
		hold = 1;
	if (hold > ticks)
		hold = ticks;
	return hold;
}

static void trigger_step(UWORD ticks) {
	const SeqStep *step = &steps[index];

	gate_left = 0;
	if (!step->on || step->dur == 0) {
		voice_release();
		return;
	}
	gate_left = gate_ticks(step->dur, ticks);
	if (gate_left == 0) {
		voice_release();
		return;
	}
	voice_trigger(step_wave(step), sequencer_sounding_note(step->note));
}

static void begin_playback(void) {
	UWORD ticks;

	step_error = 0;
	index = 0;
	fx_reset();
	ticks = step_interval();
	trigger_step(ticks);
	step_countdown = ticks;
}

static void end_playback(void) {
	index = 0;
	gate_left = 0;
	voice_release();
	audio_stop();
}

static void advance_step(void) {
	step_countdown--;
	if (step_countdown == 0) {
		UWORD ticks = step_interval();

		index = (UWORD)((index + 1) % sequencer_length());
		trigger_step(ticks);
		step_countdown = ticks;
	} else if (gate_left > 0) {
		gate_left--;
		if (gate_left == 0)
			voice_release();
	}
	if (gate_left > 0) {
		voice_set_wave(step_wave(&steps[index]));
		voice_set_note(sequencer_sounding_note(steps[index].note));
	}
}

extern void sequencer_tick_isr(void);

/* Called from sequencer_isr.s. A C interrupt attribute emits an FPU
   save, and a plain A1200 traps that as a software failure. */
__attribute__((used, externally_visible, noinline))
void sequencer_tick_service(void) {
	short started = 0;

	if (!(custom->intreqr & INTF_COPER))
		return;
	custom->intreq = INTF_COPER;
	custom->intreq = INTF_COPER;
	if (want_play != playing) {
		playing = want_play;
		if (playing) {
			begin_playback();
			started = 1;
		} else {
			end_playback();
		}
	} else if (playing) {
		advance_step();
	}
	voice_tick();
	if (started)
		audio_start();
}

short sequencer_playing(void) { return want_play; }

void sequencer_toggle_play(void) {
	if (clock_running)
		want_play = want_play ? 0 : 1;
}

void sequencer_clock_start(void) {
	step_numer = system_video()->frame_mhz * CLOCK_TICKS_PER_FRAME * 15UL;
	playing = 0;
	clock_running = 1;
	system_ticks_on(sequencer_tick_isr);
}

void sequencer_clock_stop(void) {
	if (!clock_running)
		return;
	system_ticks_off();
	want_play = 0;
	playing = 0;
	clock_running = 0;
}

UWORD sequencer_index(void) { return index; }
UWORD sequencer_length(void) { return pattern_lengths[length_index]; }

UWORD sequencer_step_from_x(WORD x) {
	if (x < 0)
		x = 0;
	if (x >= SCREEN_WIDTH)
		x = SCREEN_WIDTH - 1;
	return (UWORD)(((ULONG)x * sequencer_length()) / SCREEN_WIDTH);
}

static const BYTE octave_steps[] = { -2, -1, 0, 1, 2 };

void sequencer_cycle_octave(void) {
	octave_index = (UBYTE)((octave_index + 1) % (sizeof(octave_steps) / sizeof(octave_steps[0])));
}

short sequencer_octave(void) { return octave_steps[octave_index]; }

UWORD sequencer_sounding_note(UBYTE note) {
	int shifted = (int)note + (int)sequencer_octave() * 12;

	if (shifted < 0)
		shifted = 0;
	if (shifted >= SYNTH_NOTE_COUNT)
		shifted = SYNTH_NOTE_COUNT - 1;
	return (UWORD)shifted;
}

void sequencer_cycle_length(void) {
	length_index = (UBYTE)((length_index + 1) % (sizeof(pattern_lengths) / sizeof(pattern_lengths[0])));
	if (index >= sequencer_length())
		index = 0;
}

UWORD sequencer_bpm(void) { return bpm; }
const SeqStep *sequencer_steps(void) { return steps; }

void sequencer_set_bpm(UWORD value) {
	if (value < SEQUENCER_BPM_MIN)
		value = SEQUENCER_BPM_MIN;
	if (value > SEQUENCER_BPM_MAX)
		value = SEQUENCER_BPM_MAX;
	bpm = value;
}

void sequencer_set_dur(UWORD step, UBYTE dur) {
	if (step >= sequencer_length())
		return;
	if (dur > 127)
		dur = 127;
	steps[step].dur = dur;
}

void sequencer_toggle_edit(void) {
	edit_dur = edit_dur ? 0 : 1;
}

short sequencer_edit_dur(void) {
	return edit_dur ? 1 : 0;
}

UBYTE sequencer_dur_from_y(WORD y) {
	int span = SEQ_REST_TOP - SEQ_GRID_TOP;

	if (span < 1)
		span = 1;
	if (y >= SEQ_REST_TOP)
		return 0;
	if (y <= SEQ_GRID_TOP)
		return 127;
	return (UBYTE)(((SEQ_REST_TOP - y) * 127) / span);
}

void sequencer_cycle_dur(void) {
	dur_preset = (UBYTE)((dur_preset + 1) % (sizeof(dur_presets) / sizeof(dur_presets[0])));
	for (UWORD i = 0; i < SEQUENCER_STEPS; i++)
		steps[i].dur = dur_presets[dur_preset];
}

UBYTE sequencer_dur_preset(void) {
	return dur_presets[dur_preset];
}

void sequencer_set_note(UWORD step, short note) {
	if (step >= sequencer_length())
		return;
	if (note < 0) {
		steps[step].on = 0;
		return;
	}
	if (note >= SYNTH_NOTE_COUNT)
		note = SYNTH_NOTE_COUNT - 1;
	steps[step].on = 1;
	steps[step].note = (UBYTE)note;
}

void sequencer_cycle_wave(UWORD step) {
	if (!per_step) {
		global_wave = (UBYTE)((global_wave + 1) % SYNTH_WAVE_COUNT);
		return;
	}
	if (step >= sequencer_length())
		return;
	steps[step].wave = (UBYTE)((steps[step].wave + 1) % SYNTH_WAVE_COUNT);
	if (!steps[step].on) {
		steps[step].on = 1;
		steps[step].note = 36;
	}
}

void sequencer_randomize(UWORD salt) {
	static const UBYTE scale[] = {
		0, 2, 4, 5, 7, 9, 11,
		12, 14, 16, 17, 19, 21, 23,
		24, 26, 28, 29, 31, 33, 35,
		36, 38, 40, 41, 43, 45, 47,
		48, 50, 52, 53, 55, 57, 59,
		60
	};
	static UWORD rng = 1;
	UWORD length = sequencer_length();

	rng = (UWORD)(rng + salt + 1);
	for (UWORD i = 0; i < length; i++) {
		rng = (UWORD)(rng * 25173 + 13849);
		if ((rng % 5) == 0) {
			steps[i].on = 0;
		} else {
			rng = (UWORD)(rng * 25173 + 13849);
			steps[i].on = 1;
			steps[i].note = scale[rng % (sizeof(scale) / sizeof(scale[0]))];
		}
		if (!per_step)
			continue;
		rng = (UWORD)(rng * 25173 + 13849);
		steps[i].wave = (UBYTE)(rng % SYNTH_WAVE_COUNT);
	}
}

/* OSC1 plays the global wave, or each step's own wave in per-step mode. */
UBYTE sequencer_osc1_wave(void) {
	return per_step ? steps[0].wave : global_wave;
}

void sequencer_set_osc1_wave(UBYTE wave) {
	if (wave >= SYNTH_WAVE_COUNT)
		return;
	if (!per_step) {
		global_wave = wave;
		return;
	}
	for (UWORD i = 0; i < sequencer_length(); i++)
		steps[i].wave = wave;
}

void sequencer_cycle_all_waves(void) {
	sequencer_set_osc1_wave((UBYTE)((sequencer_osc1_wave() + 1) % SYNTH_WAVE_COUNT));
}

void sequencer_toggle_wave_mode(void) {
	per_step = per_step ? 0 : 1;
}

short sequencer_per_step(void) {
	return per_step ? 1 : 0;
}

UBYTE sequencer_global_wave(void) {
	return global_wave;
}

short sequencer_note_from_y(WORD y) {
	int span;
	int note;

	if (y < SEQ_GRID_TOP || y > SEQ_GRID_BOTTOM)
		return -2;
	if (y >= SEQ_REST_TOP)
		return -1;

	span = SEQ_REST_TOP - SEQ_GRID_TOP;
	if (span < 1)
		span = 1;
	note = (SYNTH_NOTE_COUNT - 1) - ((y - SEQ_GRID_TOP) * SYNTH_NOTE_COUNT / span);
	if (note < 0)
		note = 0;
	if (note >= SYNTH_NOTE_COUNT)
		note = SYNTH_NOTE_COUNT - 1;
	return (short)note;
}
