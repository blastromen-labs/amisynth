#include "fx.h"
#include "sequencer.h"

#if FX_DELAY_MAX_STEPS < 2 || FX_DELAY_DEFAULT_STEPS < 1 || FX_DELAY_DEFAULT_STEPS > FX_DELAY_MAX_STEPS
#error "FX_DELAY_DEFAULT_STEPS must be within 1..FX_DELAY_MAX_STEPS, and the maximum at least 2"
#endif

#define STEPS_KNOB(steps) ((((steps) - 1) * 255 + (FX_DELAY_MAX_STEPS - 1) / 2) / (FX_DELAY_MAX_STEPS - 1))
/* ln(64) in Q16: a tail of n ticks falls by the full volume range. */
#define TAIL_FALL 272600UL
#define CLIP_KNEE 768

/* Paula can only loop a cycle, so the delay and reverb carry the note itself:
   its period, cycle length and volume, one entry per tick. Level is volume * 256. */
typedef struct {
	UWORD period;
	UWORD samples;
	UWORD level;
} Tap;

typedef struct {
	Tap line[FX_DELAY_RING];
	Tap tail;
} Lane;

static volatile UBYTE controls[FX_CONTROL_COUNT] = {
	0, STEPS_KNOB(FX_DELAY_DEFAULT_STEPS), 0, 112, 128, 0
};
static Lane lanes[SYNTH_OSC_COUNT];
static UWORD head;
static UWORD drift_phase;

static void clear_tap(Tap *tap) {
	tap->period = 0;
	tap->samples = 0;
	tap->level = 0;
}

void fx_reset(void) {
	for (int osc = 0; osc < SYNTH_OSC_COUNT; osc++) {
		for (UWORD i = 0; i < FX_DELAY_RING; i++)
			clear_tap(&lanes[osc].line[i]);
		clear_tap(&lanes[osc].tail);
	}
	head = 0;
}

void fx_set(UBYTE control, UBYTE value) {
	if (control < FX_CONTROL_COUNT)
		controls[control] = value;
}

UBYTE fx_get(UBYTE control) {
	return control < FX_CONTROL_COUNT ? controls[control] : 0;
}

UBYTE fx_delay_steps(void) {
	return (UBYTE)(1 + ((int)controls[FX_DELAY_STEPS] * (FX_DELAY_MAX_STEPS - 1) + 127) / 255);
}

/* tanh-like curve, Q8 in and out, flat beyond three times full scale. */
static int soft_clip(int t) {
	int t2;

	if (t >= CLIP_KNEE)
		return 256;
	if (t <= -CLIP_KNEE)
		return -256;
	t2 = (t * t) >> 8;
	return (t * (27 * 256 + t2)) / (27 * 256 + 9 * t2);
}

/* Q8 gain into the clipper, on a squared taper of the knob. */
static int drive_gain(int drive) {
	return 256 + (drive * drive * (FX_DRIVE_MAX_GAIN - 1)) / 255;
}

/* Drive into an offset clipper, then bring the peak back to full scale so the
   loudness stays put while the harmonics grow. A square is already fully
   clipped and keeps its shape; fx_drive_volume carries the drive for it. */
void fx_distort(BYTE *cycle, UWORD samples) {
	int drive = controls[FX_DRIVE];
	int gain = drive_gain(drive);
	int bias = (drive * FX_DRIVE_BIAS) / 255;
	int shaped[SYNTH_WAVE_LEN];
	int peak = 1;

	if (!cycle || drive == 0)
		return;
	if (samples > SYNTH_WAVE_LEN)
		samples = SYNTH_WAVE_LEN;
	for (UWORD i = 0; i < samples; i++) {
		int out = soft_clip((cycle[i] * gain) / SYNTH_AMPLITUDE + bias);
		int mag = out < 0 ? -out : out;

		shaped[i] = out;
		if (mag > peak)
			peak = mag;
	}
	for (UWORD i = 0; i < samples; i++)
		cycle[i] = (BYTE)((shaped[i] * SYNTH_AMPLITUDE) / peak);
}

/* Clipping a note scales its cycle by the clipped envelope, so a driven note
   holds up as it fades. Full volume stays full. */
UBYTE fx_drive_volume(UBYTE volume) {
	int drive = controls[FX_DRIVE];
	int gain = drive_gain(drive);

	if (drive == 0 || volume == 0)
		return volume;
	if (volume > SYNTH_MAX_VOLUME)
		volume = SYNTH_MAX_VOLUME;
	return (UBYTE)((SYNTH_MAX_VOLUME * soft_clip((volume * gain) / SYNTH_MAX_VOLUME)) / soft_clip(gain));
}

static UWORD scaled(UWORD level, UBYTE knob) {
	return (UWORD)(((ULONG)level * knob) / 255);
}

static const Tap *louder(const Tap *a, const Tap *b) {
	return b->level > a->level ? b : a;
}

static UWORD delay_ticks(void) {
	UWORD ticks = sequencer_step_ticks(fx_delay_steps());

	if (ticks < 1)
		ticks = 1;
	if (ticks >= FX_DELAY_RING)
		ticks = FX_DELAY_RING - 1;
	return ticks;
}

/* Per-tick tail gain in Q16 for the decay knob. */
static ULONG tail_decay(void) {
	ULONG ticks = FX_REVERB_MIN_TICKS +
		((ULONG)controls[FX_REVERB_DECAY] * (FX_REVERB_MAX_TICKS - FX_REVERB_MIN_TICKS)) / 255;

	return 65536UL - TAIL_FALL / ticks;
}

/* The tail fades on its own and swells toward any louder input, taking its pitch. */
static void feed_tail(Tap *tail, const Tap *send, ULONG decay) {
	UWORD faded = (UWORD)(((ULONG)tail->level * decay) >> 16);

	if (send->level > faded) {
		tail->period = send->period;
		tail->samples = send->samples;
		faded = (UWORD)(faded + ((send->level - faded) >> FX_REVERB_SWELL_SHIFT));
	}
	tail->level = faded;
}

static int drift(void) {
	int tri = drift_phase < 32768 ? ((int)drift_phase >> 7) - 128 : 383 - ((int)drift_phase >> 7);

	drift_phase = (UWORD)(drift_phase + FX_REVERB_DRIFT_RATE);
	return tri;
}

static UWORD clamp_period(ULONG period) {
	if (period < SYNTH_MIN_PERIOD)
		return SYNTH_MIN_PERIOD;
	if (period > 65535UL)
		return 65535;
	return (UWORD)period;
}

/* The wet voice loops the oscillator's current buffer, so a pitch stored
   with another cycle length is played at the matching period. */
static void play_tap(const Tap *tap, int bend, const AudioVoice *dry, AudioVoice *wet) {
	LONG period = tap->period;

	*wet = *dry;
	wet->volume = (UBYTE)(tap->level >> 8);
	if (tap->level == 0 || tap->samples == 0 || dry->samples == 0)
		return;
	if (tap->samples != dry->samples)
		period = (period * tap->samples) / dry->samples;
	period -= (period * bend) / (128L * 4096L);
	wet->period = clamp_period((ULONG)period);
}

void fx_apply(const AudioVoice dry[SYNTH_OSC_COUNT], AudioVoice wet[SYNTH_OSC_COUNT]) {
	UWORD delay = delay_ticks();
	UWORD from = head >= delay ? (UWORD)(head - delay) : (UWORD)(head + FX_DELAY_RING - delay);
	UBYTE feedback = (UBYTE)(((UWORD)controls[FX_DELAY_FEEDBACK] * FX_FEEDBACK_MAX) / 255);
	UBYTE echo_mix = controls[FX_DELAY_LEVEL];
	UBYTE tail_mix = controls[FX_REVERB_LEVEL];
	ULONG decay = tail_decay();
	int bend = drift() * FX_REVERB_DETUNE;

	for (int osc = 0; osc < SYNTH_OSC_COUNT; osc++) {
		Lane *lane = &lanes[osc];
		Tap in = { dry[osc].period, dry[osc].samples, (UWORD)(dry[osc].volume << 8) };
		Tap echo = lane->line[from];
		Tap fed = echo;
		Tap tail;
		const Tap *out;

		fed.level = (UWORD)(((ULONG)echo.level * feedback) >> 8);
		lane->line[head] = *louder(&in, &fed);
		echo.level = scaled(echo.level, echo_mix);
		feed_tail(&lane->tail, louder(&in, &echo), decay);
		tail = lane->tail;
		tail.level = scaled(tail.level, tail_mix);

		if (echo_mix == 0 && tail_mix == 0) {
			wet[osc] = dry[osc];
			continue;
		}
		out = louder(&echo, &tail);
		if (FX_WET_KEEPS_DRY)
			out = louder(&in, out);
		play_tap(out, out == &tail ? bend : 0, &dry[osc], &wet[osc]);
	}
	head = (UWORD)((head + 1) % FX_DELAY_RING);
}
