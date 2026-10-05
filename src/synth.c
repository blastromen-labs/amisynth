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
	"sine", "triangle", "saw", "square",
	"exact saw", "exact square", "exact triangle", "exact pulse",
	"custom"
};

static const BYTE sine[SYNTH_WAVE_LEN] = {
	0, 21, 42, 61, 78, 91, 102, 108,
	110, 108, 102, 91, 78, 61, 42, 21,
	0, -21, -42, -61, -78, -91, -102, -108,
	-110, -108, -102, -91, -78, -61, -42, -21
};

static BYTE *waves;
static BYTE *output;
static BYTE *output2;
static volatile UWORD custom_gen;

enum {
	PRESET_SAW = 0,
	PRESET_SQUARE,
	PRESET_TRIANGLE,
	PRESET_PULSE,
	PRESET_COUNT
};

static const char *preset_names[PRESET_COUNT] = { "SAW", "SQR", "TRI", "PLS" };
static UBYTE preset_index;
static UBYTE preset_loaded;

static int preset_sample(int preset, int i) {
	const int amp = SYNTH_AMPLITUDE;
	const int len = SYNTH_WAVE_LEN;
	const int half = len / 2;

	if (preset == PRESET_SAW)
		return -amp + (2 * amp * i) / (len - 1);
	if (preset == PRESET_SQUARE)
		return i < half ? amp : -amp;
	if (preset == PRESET_TRIANGLE) {
		if (i < half)
			return -amp + (2 * amp * i) / half;
		return amp - (2 * amp * (i - half)) / half;
	}
	return i < len / 4 ? amp : -amp;
}

static void write_exact(SynthWave wave, int preset) {
	BYTE *dst = waves + (int)wave * SYNTH_WAVE_LEN;

	for (int i = 0; i < SYNTH_WAVE_LEN; i++)
		dst[i] = (BYTE)preset_sample(preset, i);
}

short synth_init(void) {
	const int half = SYNTH_WAVE_LEN / 2;

	waves = AllocMem(SYNTH_WAVE_COUNT * SYNTH_WAVE_LEN, MEMF_CHIP | MEMF_CLEAR);
	output = AllocMem(SYNTH_WAVE_LEN, MEMF_CHIP | MEMF_CLEAR);
	output2 = AllocMem(SYNTH_WAVE_LEN, MEMF_CHIP | MEMF_CLEAR);
	if (!waves || !output || !output2) {
		synth_shutdown();
		return 0;
	}

	for (int i = 0; i < SYNTH_WAVE_LEN; i++) {
		int tri_phase = i < half ? i : SYNTH_WAVE_LEN - i;
		waves[SYNTH_WAVE_SINE * SYNTH_WAVE_LEN + i] = sine[i];
		waves[SYNTH_WAVE_TRIANGLE * SYNTH_WAVE_LEN + i] =
			(BYTE)(-SYNTH_AMPLITUDE + (2 * SYNTH_AMPLITUDE * tri_phase) / half);
		waves[SYNTH_WAVE_SAW * SYNTH_WAVE_LEN + i] =
			(BYTE)(-SYNTH_AMPLITUDE + (2 * SYNTH_AMPLITUDE * i) / SYNTH_WAVE_LEN);
		waves[SYNTH_WAVE_SQUARE * SYNTH_WAVE_LEN + i] =
			(BYTE)(i < half ? SYNTH_AMPLITUDE : -SYNTH_AMPLITUDE);
	}

	for (int wave = 0; wave <= SYNTH_WAVE_SQUARE; wave++) {
		BYTE *cycle = waves + wave * SYNTH_WAVE_LEN;
		BYTE start = cycle[0];
		for (int i = 0; i < 4; i++) {
			int index = SYNTH_WAVE_LEN - 4 + i;
			int toward = i + 1;
			cycle[index] = (BYTE)((cycle[index] * (4 - toward) + start * toward) / 4);
		}
	}
	write_exact(SYNTH_WAVE_EXACT_SAW, PRESET_SAW);
	write_exact(SYNTH_WAVE_EXACT_SQUARE, PRESET_SQUARE);
	write_exact(SYNTH_WAVE_EXACT_TRIANGLE, PRESET_TRIANGLE);
	write_exact(SYNTH_WAVE_EXACT_PULSE, PRESET_PULSE);
	for (int i = 0; i < SYNTH_WAVE_LEN; i++)
		waves[SYNTH_WAVE_CUSTOM * SYNTH_WAVE_LEN + i] = waves[SYNTH_WAVE_SINE * SYNTH_WAVE_LEN + i];
	custom_gen = 1;
	return 1;
}

void synth_shutdown(void) {
	if (waves)
		FreeMem(waves, SYNTH_WAVE_COUNT * SYNTH_WAVE_LEN);
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

const BYTE *synth_wave(SynthWave wave) {
	if (wave < 0 || wave >= SYNTH_WAVE_COUNT)
		wave = SYNTH_WAVE_SINE;
	return waves + (wave * SYNTH_WAVE_LEN);
}

void synth_custom_set(UWORD index, int value) {
	BYTE *sample;

	if (!waves || index >= SYNTH_WAVE_LEN)
		return;
	if (value > SYNTH_AMPLITUDE)
		value = SYNTH_AMPLITUDE;
	if (value < -SYNTH_AMPLITUDE)
		value = -SYNTH_AMPLITUDE;
	sample = waves + SYNTH_WAVE_CUSTOM * SYNTH_WAVE_LEN + index;
	if (*sample == (BYTE)value)
		return;
	*sample = (BYTE)value;
	custom_gen++;
}

void synth_custom_reset(void) {
	if (!waves)
		return;
	for (int i = 0; i < SYNTH_WAVE_LEN; i++)
		waves[SYNTH_WAVE_CUSTOM * SYNTH_WAVE_LEN + i] = waves[SYNTH_WAVE_SINE * SYNTH_WAVE_LEN + i];
	preset_index = 0;
	preset_loaded = 0;
	custom_gen++;
}

void synth_cycle_preset(void) {
	BYTE *dst;

	if (!waves)
		return;
	if (preset_loaded)
		preset_index = (UBYTE)((preset_index + 1) % PRESET_COUNT);
	dst = waves + SYNTH_WAVE_CUSTOM * SYNTH_WAVE_LEN;
	for (int i = 0; i < SYNTH_WAVE_LEN; i++)
		dst[i] = (BYTE)preset_sample(preset_index, i);
	preset_loaded = 1;
	custom_gen++;
}

const char *synth_preset_name(void) {
	return preset_names[preset_index];
}

UWORD synth_custom_generation(void) {
	return custom_gen;
}

const char *synth_wave_name(SynthWave wave) {
	if (wave < 0 || wave >= SYNTH_WAVE_COUNT)
		return wave_names[0];
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

void synth_render(SynthWave wave, UWORD note, UBYTE cutoff, UBYTE resonance, UBYTE pulse_high, BYTE *dest) {
	BYTE pulse[SYNTH_WAVE_LEN];
	const BYTE *src = synth_wave(wave);
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
	if (wave == SYNTH_WAVE_SQUARE) {
		int high = pulse_high;

		if (high < 1)
			high = 1;
		if (high >= SYNTH_WAVE_LEN)
			high = SYNTH_WAVE_LEN - 1;
		for (int i = 0; i < SYNTH_WAVE_LEN; i++)
			pulse[i] = (BYTE)(i < high ? SYNTH_AMPLITUDE : -SYNTH_AMPLITUDE);
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
