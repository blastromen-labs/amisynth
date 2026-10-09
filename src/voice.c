#include "voice.h"
#include "audio.h"

#define ENV_PEAK (SYNTH_MAX_VOLUME * 256)
#define ENV_IDLE 0
#define ENV_ATTACK 1
#define ENV_DECAY 2
#define ENV_SUSTAIN 3
#define ENV_RELEASE 4

static volatile UBYTE controls[VOICE_CONTROL_COUNT] = {
	0, 48, 0, 0, 0, 128, 128,
	255, 0, 4, 48, 255, 72, 0,
	128, 114, 0, 0, 23, 96
};
static SynthWave wave = SYNTH_WAVE_PULSE;
static SynthWave wave2 = SYNTH_WAVE_SAW;
static UWORD note = 24;
static UBYTE route_pw = 1;
static UBYTE route_pitch;
static UBYTE route_filter;
static UBYTE gate_mode;
static UBYTE note_held;

typedef struct {
	WORD level;
	WORD stage_from;
	WORD stage_to;
	UWORD stage_pos;
	UWORD stage_len;
	UBYTE stage;
} Env;

static Env amp;
static Env filt;
typedef struct {
	UBYTE wave;
	UBYTE note;
	UBYTE cutoff;
	UBYTE resonance;
	UBYTE pulse;
	UBYTE audible;
	UWORD gen;
} Rendered;

static Rendered rendered[2];
static UBYTE render_hold;
static volatile UWORD lfo_phase[2];

static SynthWave valid_wave(SynthWave next) {
	if (next < 0 || next >= SYNTH_WAVE_COUNT)
		return SYNTH_WAVE_SAW;
	return next;
}

static UWORD time_ticks(UBYTE knob) {
	return (UWORD)(1 + ((unsigned)knob * knob) / 163);
}

static WORD sustain_level(UBYTE knob) {
	return (WORD)(((knob * SYNTH_MAX_VOLUME) / 255) * 256);
}

static int modulate(int base, UBYTE depth, UBYTE route);

static UBYTE shaped_cutoff(void) {
	int cutoff = controls[VOICE_CUTOFF];
	int amount = controls[VOICE_FILTER_ENV];
	int env = filt.level;

	if (amount != 0) {
		if (env < 0)
			env = 0;
		if (env > ENV_PEAK)
			env = ENV_PEAK;
		cutoff += (int)(((long)env * amount) / ENV_PEAK);
	}
	cutoff = modulate(cutoff, controls[VOICE_FILTER_DEPTH], route_filter);
	if (cutoff > 255)
		cutoff = 255;
	if (cutoff < 0)
		cutoff = 0;
	return (UBYTE)cutoff;
}

/* The filter is heavy. Rebuild only when the cutoff moves by a noticeable step. */
static UBYTE cutoff_step(int cutoff) {
	int stepped;

	if (cutoff >= 248)
		return 255;
	if (cutoff < 0)
		cutoff = 0;
	stepped = (cutoff + 4) & ~7;
	if (stepped > 247)
		stepped = 240;
	return (UBYTE)stepped;
}

static int lfo_triangle(int which) {
	UWORD phase = lfo_phase[which];

	if (phase < 32768)
		return ((int)phase * 255) / 32768 - 128;
	return 127 - (((int)phase - 32768) * 255) / 32768;
}

static void advance_lfos(void) {
	static const UBYTE rate_control[2] = { VOICE_LFO1_RATE, VOICE_LFO2_RATE };

	for (int which = 0; which < 2; which++) {
		int rate = controls[rate_control[which]];
		int inc = 33 + (rate * 3244) / 255;

		lfo_phase[which] = (UWORD)(lfo_phase[which] + inc);
	}
}

static int lfo_for(UBYTE route) {
	if (route == 1 || route == 2)
		return lfo_triangle((int)route - 1);
	return 0;
}

static int modulate(int base, UBYTE depth, UBYTE route) {
	int shifted;

	if (depth == 0 || route == 0)
		return base;
	shifted = base + ((int)depth * lfo_for(route)) / 128;
	if (shifted < 0)
		shifted = 0;
	if (shifted > 255)
		shifted = 255;
	return shifted;
}

static int pulse_effective(void) {
	return modulate(controls[VOICE_PULSE_WIDTH], controls[VOICE_PW_DEPTH], route_pw);
}

static UBYTE pulse_high_count(void) {
	return (UBYTE)(1 + (pulse_effective() * (SYNTH_WAVE_LEN - 2)) / 255);
}

static void start_stage(Env *env, UBYTE next, WORD target, UWORD ticks) {
	env->stage = next;
	env->stage_from = env->level;
	env->stage_to = target;
	env->stage_pos = 0;
	env->stage_len = ticks < 1 ? 1 : ticks;
}

static void advance_env(Env *env, UBYTE sustain_knob, UBYTE decay_knob) {
	if (env->stage == ENV_ATTACK || env->stage == ENV_DECAY || env->stage == ENV_RELEASE) {
		env->stage_pos++;
		if (env->stage_pos >= env->stage_len) {
			env->level = env->stage_to;
			if (env->stage == ENV_ATTACK)
				start_stage(env, ENV_DECAY, sustain_level(sustain_knob), time_ticks(decay_knob));
			else if (env->stage == ENV_DECAY)
				env->stage = ENV_SUSTAIN;
			else
				env->stage = ENV_IDLE;
		} else {
			env->level = (WORD)(env->stage_from +
				((LONG)(env->stage_to - env->stage_from) * env->stage_pos) / env->stage_len);
		}
		return;
	}
	if (env->stage == ENV_SUSTAIN)
		env->level = sustain_level(sustain_knob);
}

static UWORD bent_period(UWORD period) {
	int bend;
	int delta;
	int next;

	if (controls[VOICE_PITCH_DEPTH] == 0 || route_pitch == 0)
		return period;
	bend = ((int)controls[VOICE_PITCH_DEPTH] * lfo_for(route_pitch)) / 128;
	delta = (int)(((long)period * bend) / 4096);
	next = (int)period - delta;
	if (next < SYNTH_MIN_PERIOD)
		next = SYNTH_MIN_PERIOD;
	if (next > 65535)
		next = 65535;
	return (UWORD)next;
}

static void osc_mix(int *level1, int *level2) {
	int bal = controls[VOICE_MIX];

	if (bal <= 128) {
		*level1 = 255;
		*level2 = bal == 0 ? 0 : (bal * 255) / 128;
	} else {
		*level1 = ((255 - bal) * 255) / 127;
		*level2 = 255;
	}
}

void voice_init(void) {
	UBYTE cutoff = cutoff_step(shaped_cutoff());
	UBYTE pulse = pulse_high_count();

	amp.level = 0;
	amp.stage = ENV_IDLE;
	filt.level = 0;
	filt.stage = ENV_IDLE;
	note = 24;
	wave = SYNTH_WAVE_PULSE;
	wave2 = SYNTH_WAVE_SAW;
	synth_render(wave, 0, note, cutoff, controls[VOICE_RESONANCE], pulse, synth_voice());
	synth_render(wave2, 1, note, cutoff, controls[VOICE_RESONANCE], pulse, synth_voice2());
	for (int i = 0; i < 2; i++) {
		rendered[i].wave = 0xff;
		rendered[i].note = (UBYTE)note;
		rendered[i].cutoff = cutoff;
		rendered[i].resonance = controls[VOICE_RESONANCE];
		rendered[i].pulse = pulse;
		rendered[i].audible = 0;
	}
	rendered[0].wave = (UBYTE)wave;
	rendered[1].wave = (UBYTE)wave2;
}

void voice_set(UBYTE control, UBYTE value) {
	if (control >= VOICE_CONTROL_COUNT)
		return;
	controls[control] = value;
}

UBYTE voice_get(UBYTE control) {
	if (control >= VOICE_CONTROL_COUNT)
		return 0;
	return controls[control];
}

void voice_trigger(SynthWave next, UWORD next_note) {
	if (next_note >= SYNTH_NOTE_COUNT)
		next_note = SYNTH_NOTE_COUNT - 1;
	wave = valid_wave(next);
	note = next_note;
	note_held = 1;
	start_stage(&amp, ENV_ATTACK, ENV_PEAK, time_ticks(controls[VOICE_ATTACK]));
	start_stage(&filt, ENV_ATTACK, ENV_PEAK, time_ticks(controls[VOICE_FILTER_ATTACK]));
}

void voice_set_wave(SynthWave next) {
	wave = valid_wave(next);
}

void voice_set_note(UWORD next_note) {
	if (next_note >= SYNTH_NOTE_COUNT)
		next_note = SYNTH_NOTE_COUNT - 1;
	note = next_note;
}

void voice_set_osc2(SynthWave next) {
	wave2 = valid_wave(next);
}

void voice_cycle_osc2(void) {
	voice_set_osc2((SynthWave)((wave2 + 1) % SYNTH_WAVE_COUNT));
}

UBYTE voice_osc2(void) {
	return (UBYTE)wave2;
}

int voice_osc2_semitone(void) {
	return ((int)controls[VOICE_OSC2_SEMI] * 48 + 127) / 255 - 24;
}

void voice_step_semitone(int direction) {
	int semi = voice_osc2_semitone() + direction;
	int target;
	int knob;

	if (semi < -24)
		semi = -24;
	if (semi > 24)
		semi = 24;
	target = semi + 24;
	knob = (target * 255 + 24) / 48;
	if (knob < 0)
		knob = 0;
	if (knob > 255)
		knob = 255;
	controls[VOICE_OSC2_SEMI] = (UBYTE)knob;
}

int voice_osc2_fine(void) {
	return ((int)controls[VOICE_OSC2_FINE] * 200 + 127) / 255 - 100;
}

void voice_step_fine(int direction) {
	int cents = voice_osc2_fine() + direction;
	int target;
	int knob;

	if (cents < -100)
		cents = -100;
	if (cents > 100)
		cents = 100;
	target = cents + 100;
	knob = (target * 255 + 100) / 200;
	if (knob < 0)
		knob = 0;
	if (knob > 255)
		knob = 255;
	controls[VOICE_OSC2_FINE] = (UBYTE)knob;
}

short voice_cycle_route(UBYTE control) {
	UBYTE *route = 0;

	if (control == VOICE_PW_DEPTH)
		route = &route_pw;
	else if (control == VOICE_PITCH_DEPTH)
		route = &route_pitch;
	else if (control == VOICE_FILTER_DEPTH)
		route = &route_filter;
	if (!route)
		return 0;
	*route = (UBYTE)((*route + 1) % 3);
	return 1;
}

const char *voice_route_label(UBYTE control) {
	static char text[8];
	const char *name = "PWM";
	UBYTE route = route_pw;
	int at = 0;

	if (control == VOICE_PITCH_DEPTH) {
		name = "PMOD";
		route = route_pitch;
	} else if (control == VOICE_FILTER_DEPTH) {
		name = "FMOD";
		route = route_filter;
	}
	while (name[at]) {
		text[at] = name[at];
		at++;
	}
	text[at++] = route == 0 ? '-' : (char)('0' + route);
	text[at] = 0;
	return text;
}

int voice_live(UBYTE control) {
	if (control == VOICE_PULSE_WIDTH && controls[VOICE_PW_DEPTH] > 0 && route_pw != 0)
		return pulse_effective();
	if (control == VOICE_CUTOFF && controls[VOICE_FILTER_DEPTH] > 0 && route_filter != 0)
		return shaped_cutoff();
	return -1;
}

void voice_release(void) {
	note_held = 0;
	if (amp.stage != ENV_IDLE)
		start_stage(&amp, ENV_RELEASE, 0, time_ticks(controls[VOICE_RELEASE]));
	if (filt.stage != ENV_IDLE)
		start_stage(&filt, ENV_RELEASE, 0, time_ticks(controls[VOICE_FILTER_RELEASE]));
}

void voice_toggle_gate(void) {
	gate_mode = gate_mode ? 0 : 1;
}

short voice_gate_mode(void) {
	return gate_mode ? 1 : 0;
}

/* 0 idle, 1 rebuilt a modulated parameter, 2 modulation is waiting for the hold. */
static UWORD note_with_samples(UWORD preferred, UWORD samples) {
	if (preferred >= SYNTH_NOTE_COUNT)
		preferred = SYNTH_NOTE_COUNT - 1;
	if (synth_note_samples(preferred) == samples)
		return preferred;
	if (samples < SYNTH_WAVE_LEN)
		return SYNTH_NOTE_COUNT - 1;
	for (int i = SYNTH_NOTE_COUNT - 1; i >= 0; i--) {
		if (synth_note_samples((UWORD)i) == SYNTH_WAVE_LEN)
			return (UWORD)i;
	}
	return preferred;
}

static int render_osc(int index, SynthWave sounding, UWORD render_note, UBYTE cutoff, UBYTE pulse, BYTE *dest, short audible) {
	UBYTE resonance = controls[VOICE_RESONANCE];
	UWORD gen = synth_custom_generation((UBYTE)index);
	Rendered *state = &rendered[index];
	short open = cutoff >= 250 && resonance == 0;
	short note_changed = state->note != (UBYTE)render_note;
	short same_length = synth_note_samples(state->note) == synth_note_samples(render_note);
	short cutoff_moved = state->cutoff != cutoff;
	short pulse_moved = synth_wave_has_width(sounding) && state->pulse != pulse;
	short moved = cutoff_moved || pulse_moved;
	/* An open filter copies the source. The same wave and length is the same buffer. */
	short force = !state->audible || state->wave != (UBYTE)sounding ||
		state->resonance != resonance ||
		(note_changed && !(open && same_length)) ||
		(sounding == SYNTH_WAVE_CUSTOM && state->gen != gen);

	if (!audible) {
		state->audible = 0;
		return 0;
	}
	state->audible = 1;
	if (!force && note_changed)
		state->note = (UBYTE)render_note;
	if (!(force || (moved && render_hold == 0)))
		return moved ? 2 : 0;
	synth_render(sounding, (UBYTE)index, render_note, cutoff, resonance, pulse, dest);
	state->wave = (UBYTE)sounding;
	state->note = (UBYTE)render_note;
	state->cutoff = cutoff;
	state->resonance = resonance;
	state->pulse = pulse;
	state->gen = gen;
	return moved ? 1 : 0;
}

void voice_tick(void) {
	SynthWave sounding = wave;
	UBYTE cutoff = cutoff_step(shaped_cutoff());
	UBYTE pulse = pulse_high_count();
	int tune = voice_osc2_semitone() * 100 + voice_osc2_fine();
	int shifted = (int)note + voice_osc2_semitone();
	UWORD period = bent_period(synth_note_period(note));
	UWORD samples = synth_note_samples(note);
	UWORD period2;
	UWORD samples2;
	UWORD note2;
	int mix1;
	int mix2;
	int volume;
	BYTE *buf1 = synth_voice();
	BYTE *buf2 = synth_voice2();
	int rebuilt = 0;
	int waiting = 0;

	advance_env(&amp, controls[VOICE_SUSTAIN], controls[VOICE_DECAY]);
	advance_env(&filt, controls[VOICE_FILTER_SUSTAIN], controls[VOICE_FILTER_DECAY]);
	advance_lfos();
	osc_mix(&mix1, &mix2);
	if (gate_mode)
		volume = note_held ? SYNTH_MAX_VOLUME : 0;
	else
		volume = amp.level / 256;
	if (volume < 0)
		volume = 0;
	if (volume > SYNTH_MAX_VOLUME)
		volume = SYNTH_MAX_VOLUME;

	synth_tuned_pitch(note, tune, &period2, &samples2);
	period2 = bent_period(period2);
	if (shifted < 0)
		shifted = 0;
	if (shifted >= SYNTH_NOTE_COUNT)
		shifted = SYNTH_NOTE_COUNT - 1;
	note2 = note_with_samples((UWORD)shifted, samples2);

	/* The custom waves differ per oscillator, so they never share a buffer. */
	if (mix2 > 0 && sounding == wave2 && sounding != SYNTH_WAVE_CUSTOM && note2 == note && samples2 == samples) {
		int status = render_osc(0, sounding, note, cutoff, pulse, buf1, 1);

		rendered[1] = rendered[0];
		rendered[1].audible = 1;
		buf2 = buf1;
		rebuilt = status == 1;
		waiting = status == 2;
	} else {
		int first = render_osc(0, sounding, note, cutoff, pulse, buf1, mix1 > 0);
		int second = render_osc(1, wave2, note2, cutoff, pulse, buf2, mix2 > 0);

		rebuilt = first == 1 || second == 1;
		waiting = first == 2 || second == 2;
	}
	if (rebuilt)
		render_hold = 7;
	else if (waiting) {
		if (render_hold > 0)
			render_hold--;
	} else
		render_hold = 0;

	audio_update(buf1, period, samples, (UBYTE)(volume * mix1 / 255),
		buf2, period2, samples2, (UBYTE)(volume * mix2 / 255));
}

UBYTE voice_volume(void) {
	int volume = amp.level / 256;
	if (volume < 0)
		return 0;
	if (volume > SYNTH_MAX_VOLUME)
		return SYNTH_MAX_VOLUME;
	return (UBYTE)volume;
}
