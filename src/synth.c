#include "synth.h"
#include "config.h"

#include <proto/exec.h>

extern struct ExecBase *SysBase;

/* Hz * 256, equal temperament, A4 = 440. One octave per row, C1 through C6. */
static const ULONG note_hz_q8[SYNTH_NOTE_COUNT] = {
	8372, 8870, 9397, 9956, 10548, 11175, 11840, 12544, 13290, 14080, 14917, 15804,
	16744, 17740, 18795, 19912, 21096, 22351, 23680, 25088, 26580, 28160, 29834, 31609,
	33488, 35479, 37589, 39824, 42192, 44701, 47359, 50175, 53159, 56320, 59669, 63217,
	66976, 70959, 75178, 79649, 84385, 89402, 94719, 100351, 106318, 112640, 119338, 126434,
	133952, 141918, 150356, 159297, 168769, 178805, 189437, 200702, 212636, 225280, 238676, 252868,
	267905
};

static const char *wave_names[SYNTH_WAVE_COUNT] = {
	"saw", "reverse saw", "square", "pulse", "triangle", "custom"
};

/* Drawing templates, one per fixed wave. */
static const char *preset_names[SYNTH_WAVE_CUSTOM] = { "SAW", "RSAW", "SQR", "PLS", "TRI" };

/* A fresh or reset custom wave starts as a sine. */
static const BYTE custom_start[SYNTH_WAVE_LEN] = {
	0, 21, 42, 61, 78, 91, 102, 108,
	110, 108, 102, 91, 78, 61, 42, 21,
	0, -21, -42, -61, -78, -91, -102, -108,
	-110, -108, -102, -91, -78, -61, -42, -21
};

/* The fixed waves, then one custom wave per oscillator. */
#define WAVE_TABLES (SYNTH_WAVE_CUSTOM + SYNTH_OSC_COUNT)

static BYTE *waves;
static BYTE *output;
static BYTE *output2;
static volatile UWORD custom_gen[SYNTH_OSC_COUNT];
static UBYTE preset_index[SYNTH_OSC_COUNT];
static UBYTE preset_loaded[SYNTH_OSC_COUNT];

static int fixed_sample(SynthWave wave, int i) {
	const int amp = SYNTH_AMPLITUDE;
	const int len = SYNTH_WAVE_LEN;
	const int half = len / 2;

	switch (wave) {
	case SYNTH_WAVE_REVERSE_SAW:
		return amp - (2 * amp * i) / (len - 1);
	case SYNTH_WAVE_SQUARE:
		return i < half ? amp : -amp;
	case SYNTH_WAVE_PULSE:
		return i < len / 4 ? amp : -amp;
	case SYNTH_WAVE_TRIANGLE:
		if (i < half)
			return -amp + (2 * amp * i) / half;
		return amp - (2 * amp * (i - half)) / half;
	case SYNTH_WAVE_SAW:
	default:
		return -amp + (2 * amp * i) / (len - 1);
	}
}

static BYTE *custom_table(UBYTE osc) {
	if (osc >= SYNTH_OSC_COUNT)
		osc = 0;
	return waves + (SYNTH_WAVE_CUSTOM + osc) * SYNTH_WAVE_LEN;
}

static void custom_fill(UBYTE osc, const BYTE *src) {
	BYTE *dst = custom_table(osc);

	for (int i = 0; i < SYNTH_WAVE_LEN; i++)
		dst[i] = src[i];
	custom_gen[osc]++;
}

short synth_init(void) {
	waves = AllocMem(WAVE_TABLES * SYNTH_WAVE_LEN, MEMF_CHIP | MEMF_CLEAR);
	output = AllocMem(SYNTH_WAVE_LEN, MEMF_CHIP | MEMF_CLEAR);
	output2 = AllocMem(SYNTH_WAVE_LEN, MEMF_CHIP | MEMF_CLEAR);
	if (!waves || !output || !output2) {
		synth_shutdown();
		return 0;
	}

	for (int wave = 0; wave < SYNTH_WAVE_CUSTOM; wave++) {
		for (int i = 0; i < SYNTH_WAVE_LEN; i++)
			waves[wave * SYNTH_WAVE_LEN + i] = (BYTE)fixed_sample((SynthWave)wave, i);
	}
	for (UBYTE osc = 0; osc < SYNTH_OSC_COUNT; osc++)
		synth_custom_reset(osc);
	return 1;
}

void synth_shutdown(void) {
	if (waves)
		FreeMem(waves, WAVE_TABLES * SYNTH_WAVE_LEN);
	if (output)
		FreeMem(output, SYNTH_WAVE_LEN);
	if (output2)
		FreeMem(output2, SYNTH_WAVE_LEN);
	waves = 0;
	output = 0;
	output2 = 0;
}

BYTE *synth_voice(void) { return output; }
BYTE *synth_voice2(void) { return output2; }

const BYTE *synth_wave(SynthWave wave, UBYTE osc) {
	if (!waves)
		return 0;
	if (wave == SYNTH_WAVE_CUSTOM)
		return custom_table(osc);
	if (wave < 0 || wave >= SYNTH_WAVE_CUSTOM)
		wave = SYNTH_WAVE_SAW;
	return waves + (wave * SYNTH_WAVE_LEN);
}

void synth_custom_set(UBYTE osc, UWORD index, int value) {
	BYTE *sample;

	if (!waves || osc >= SYNTH_OSC_COUNT || index >= SYNTH_WAVE_LEN)
		return;
	if (value > SYNTH_AMPLITUDE)
		value = SYNTH_AMPLITUDE;
	if (value < -SYNTH_AMPLITUDE)
		value = -SYNTH_AMPLITUDE;
	sample = custom_table(osc) + index;
	if (*sample == (BYTE)value)
		return;
	*sample = (BYTE)value;
	custom_gen[osc]++;
}

void synth_custom_reset(UBYTE osc) {
	if (!waves || osc >= SYNTH_OSC_COUNT)
		return;
	custom_fill(osc, custom_start);
	preset_index[osc] = 0;
	preset_loaded[osc] = 0;
}

/* The first press loads the current template, later presses move to the next. */
void synth_cycle_preset(UBYTE osc) {
	if (!waves || osc >= SYNTH_OSC_COUNT)
		return;
	if (preset_loaded[osc])
		preset_index[osc] = (UBYTE)((preset_index[osc] + 1) % SYNTH_WAVE_CUSTOM);
	custom_fill(osc, waves + preset_index[osc] * SYNTH_WAVE_LEN);
	preset_loaded[osc] = 1;
}

const char *synth_preset_name(UBYTE osc) {
	if (osc >= SYNTH_OSC_COUNT)
		osc = 0;
	return preset_names[preset_index[osc]];
}

UWORD synth_custom_generation(UBYTE osc) {
	if (osc >= SYNTH_OSC_COUNT)
		osc = 0;
	return custom_gen[osc];
}

const char *synth_wave_name(SynthWave wave) {
	if (wave < 0 || wave >= SYNTH_WAVE_COUNT)
		return wave_names[SYNTH_WAVE_SAW];
	return wave_names[wave];
}

const char *synth_note_name(UWORD index) {
	static char name[4];
	static const char natural[] = "CCDDEFFGGAAB";
	static const char accidental[] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
	int midi;
	int pitch;
	int octave;
	int at = 0;

	if (index >= SYNTH_NOTE_COUNT)
		index = 0;
	midi = SYNTH_NOTE_FIRST + index;
	pitch = midi % 12;
	octave = midi / 12 - 1;
	name[at++] = natural[pitch];
	if (accidental[pitch])
		name[at++] = '#';
	name[at++] = (char)('0' + octave);
	name[at] = 0;
	return name;
}

static ULONG note_freq(UWORD index) {
	if (index >= SYNTH_NOTE_COUNT)
		index = 0;
	return note_hz_q8[index];
}

UWORD synth_note_samples(UWORD index) {
	ULONG period = (SYNTH_PAULA_CLOCK * 256UL) / (note_freq(index) * (ULONG)SYNTH_WAVE_LEN);

	if (period < SYNTH_MIN_PERIOD)
		return SYNTH_WAVE_LEN / 2;
	return SYNTH_WAVE_LEN;
}

UWORD synth_note_period(UWORD index) {
	ULONG period = (SYNTH_PAULA_CLOCK * 256UL) / (note_freq(index) * synth_note_samples(index));

	if (period < SYNTH_MIN_PERIOD)
		period = SYNTH_MIN_PERIOD;
	if (period > 65535UL)
		period = 65535UL;
	return (UWORD)period;
}

/* Period scale for 0..12 semitones, Q15. 32768 is unison. */
static const UWORD period_ratio[13] = {
	32768, 30929, 29193, 27554, 26008, 24548,
	23170, 21870, 20643, 19484, 18390, 17358, 16384
};

void synth_tuned_pitch(UWORD index, int cents, UWORD *period_out, UWORD *samples_out) {
	int note;
	int octaves = 0;
	int up = 1;
	int steps;
	int frac;
	unsigned ratio;
	ULONG freq;
	ULONG period;
	UWORD samples;

	if (index >= SYNTH_NOTE_COUNT)
		index = 0;
	if (cents == 0) {
		*period_out = synth_note_period(index);
		*samples_out = synth_note_samples(index);
		return;
	}

	note = (int)index;
	while (cents >= 100 && note < SYNTH_NOTE_COUNT - 1) {
		note++;
		cents -= 100;
	}
	while (cents <= -100 && note > 0) {
		note--;
		cents += 100;
	}
	while (cents >= 1200) {
		octaves++;
		cents -= 1200;
	}
	while (cents <= -1200) {
		octaves--;
		cents += 1200;
	}

	freq = note_freq((UWORD)note);
	if (octaves > 8)
		octaves = 8;
	if (octaves < -15)
		octaves = -15;
	if (octaves > 0)
		freq <<= octaves;
	else if (octaves < 0)
		freq >>= -octaves;
	if (freq < 1)
		freq = 1;

	if (cents < 0) {
		up = 0;
		cents = -cents;
	}
	steps = cents / 100;
	frac = cents % 100;
	if (steps > 11) {
		steps = 11;
		frac = 0;
	}
	ratio = (unsigned)period_ratio[steps] +
		(unsigned)(((int)period_ratio[steps + 1] - (int)period_ratio[steps]) * frac / 100);

	period = (SYNTH_PAULA_CLOCK * 256UL) / (freq * (ULONG)SYNTH_WAVE_LEN);
	if (period > 65535UL)
		period = 65535UL;
	if (up)
		period = (period * (ULONG)ratio) >> 15;
	else
		period = (period << 15) / (ULONG)ratio;

	samples = SYNTH_WAVE_LEN;
	if (period < SYNTH_MIN_PERIOD) {
		samples = SYNTH_WAVE_LEN / 2;
		period *= 2;
	}
	if (period < SYNTH_MIN_PERIOD)
		period = SYNTH_MIN_PERIOD;
	if (period > 65535UL)
		period = 65535UL;
	*period_out = (UWORD)period;
	*samples_out = samples;
}

static int clamp_sample(int sample) {
	if (sample > SYNTH_AMPLITUDE)
		return SYNTH_AMPLITUDE;
	if (sample < -SYNTH_AMPLITUDE)
		return -SYNTH_AMPLITUDE;
	return sample;
}

static BYTE source_sample(const BYTE *src, int samples, int index) {
	return src[index * (SYNTH_WAVE_LEN / samples)];
}

static int clamp_width(int high) {
	if (high < 1)
		return 1;
	if (high >= SYNTH_WAVE_LEN)
		return SYNTH_WAVE_LEN - 1;
	return high;
}

static void pulse_shape(int high, BYTE *dest) {
	high = clamp_width(high);
	for (int i = 0; i < SYNTH_WAVE_LEN; i++)
		dest[i] = (BYTE)(i < high ? SYNTH_AMPLITUDE : -SYNTH_AMPLITUDE);
}

/* Pulse width on a drawn wave: the first half of the cycle is squeezed into
   `high` samples and the second half into the rest, so the outline stays and
   its midpoint moves. The centre width leaves the wave unchanged. */
static void width_shape(const BYTE *src, int high, BYTE *dest) {
	const int len = SYNTH_WAVE_LEN;
	const int half = len / 2;

	high = clamp_width(high);
	for (int i = 0; i < len; i++) {
		int pos = i < high ? (i * half * 256) / high : half * 256 + ((i - high) * half * 256) / (len - high);
		int at = pos >> 8;
		int frac = pos & 255;
		int a = src[at % len];
		int b = src[(at + 1) % len];

		dest[i] = (BYTE)(a + ((b - a) * frac) / 256);
	}
}

void synth_render(SynthWave wave, UBYTE osc, UWORD note, UBYTE cutoff, UBYTE resonance, UBYTE pulse_high, BYTE *dest) {
	BYTE pulse[SYNTH_WAVE_LEN];
	const BYTE *src = synth_wave(wave, osc);
	int samples = synth_note_samples(note);
	UWORD period = synth_note_period(note);
	ULONG sr = SYNTH_PAULA_CLOCK / period;
	ULONG max_fc = sr / 6;
	ULONG span;
	ULONG fc;
	LONG low = 0;
	LONG band = 0;
	LONG f;
	LONG damp;
	int cycle[SYNTH_WAVE_LEN];
	int peak = 1;

	if (!dest || !src)
		return;
	if (wave == SYNTH_WAVE_PULSE) {
		pulse_shape(pulse_high, pulse);
		src = pulse;
	} else if (wave == SYNTH_WAVE_CUSTOM) {
		width_shape(src, pulse_high, pulse);
		src = pulse;
	}
	if (cutoff >= 250 && resonance == 0) {
		for (int i = 0; i < samples; i++)
			dest[i] = source_sample(src, samples, i);
		return;
	}

	span = max_fc > 40 ? max_fc - 40 : 1;
	fc = 40 + span * cutoff * cutoff / (255UL * 255UL);
	if (fc >= max_fc)
		fc = max_fc - 1;

	{
		ULONG z = fc * 205887UL / sr;
		ULONG z2 = (z * z) >> 16;
		ULONG z3 = (z2 * z) >> 16;
		ULONG sine = z - z3 / 6;
		f = (LONG)(sine >> 1);
	}
	if (f < 8)
		f = 8;
	if (f > 15000)
		f = 15000;
	damp = 16384 - ((LONG)resonance * 13000) / 255;
	if (damp < 2800)
		damp = 2800;

	for (int pass = 0; pass < 8; pass++) {
		for (int i = 0; i < samples; i++) {
			LONG in = (LONG)source_sample(src, samples, i) << 4;
			LONG high = in - low - ((damp * band) >> 14);
			band += (f * high) >> 14;
			low += (f * band) >> 14;
			if (band > 40000)
				band = 40000;
			if (band < -40000)
				band = -40000;
			if (low > 40000)
				low = 40000;
			if (low < -40000)
				low = -40000;
			if (pass == 7)
				cycle[i] = (int)(low >> 4);
		}
	}

	for (int i = 0; i < samples; i++) {
		int mag = cycle[i] < 0 ? -cycle[i] : cycle[i];
		if (mag > peak)
			peak = mag;
	}
	for (int i = 0; i < samples; i++) {
		int sample = cycle[i];
		if (peak > SYNTH_AMPLITUDE)
			sample = sample * SYNTH_AMPLITUDE / peak;
		dest[i] = (BYTE)clamp_sample(sample);
	}
}

UWORD synth_note_from_x(WORD x) {
	if (x < 0)
		x = 0;
	if (x >= SCREEN_WIDTH)
		x = SCREEN_WIDTH - 1;
	return (UWORD)(((UWORD)x * SYNTH_NOTE_COUNT) / SCREEN_WIDTH);
}
