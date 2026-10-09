#include "audio.h"
#include "config.h"

#include <hardware/custom.h>
#include <hardware/dmabits.h>

#define AUDIO_DMA (DMAF_AUD0 | DMAF_AUD1 | DMAF_AUD2 | DMAF_AUD3)

extern volatile struct Custom *custom;

static const void *current_wave[2];
static UWORD current_samples[2];

/* Voices 0 and 3 are the left output. Voices 1 and 2 are the right output. */
static void write_voice(int channel, const void *wave, UWORD period, UWORD samples, UBYTE volume, short reload) {
	if (reload) {
		custom->aud[channel].ac_ptr = (UWORD *)wave;
		custom->aud[channel].ac_len = (UWORD)(samples / 2);
	}
	custom->aud[channel].ac_per = period;
	custom->aud[channel].ac_vol = volume;
}

static void write_pair(int index, int left, int right, const void *wave, UWORD period, UWORD samples, UBYTE volume) {
	short reload = 0;

	if (volume > SYNTH_MAX_VOLUME)
		volume = SYNTH_MAX_VOLUME;
	if (period < SYNTH_MIN_PERIOD)
		period = SYNTH_MIN_PERIOD;
	if (samples < 2)
		samples = 2;
	if (!wave)
		volume = 0;
	if (wave != current_wave[index] || samples != current_samples[index]) {
		current_wave[index] = wave;
		current_samples[index] = samples;
		reload = wave != 0;
	}
	write_voice(left, wave, period, samples, volume, reload);
	write_voice(right, wave, period, samples, volume, reload);
}

void audio_init(const void *wave_a, const void *wave_b, UWORD period, UWORD samples) {
	current_wave[0] = 0;
	current_wave[1] = 0;
	write_pair(0, 0, 1, wave_a, period, samples, 0);
	write_pair(1, 3, 2, wave_b, period, samples, 0);
}

/* Paula's audio registers are write-only. Reading one latches whatever is on
   the chip bus into it, so every value comes from write_pair. */
void audio_start(void) {
	custom->dmacon = AUDIO_DMA;
	custom->dmacon = DMAF_SETCLR | DMAF_MASTER | AUDIO_DMA;
}

void audio_update(const void *wave_a, UWORD period_a, UWORD samples_a, UBYTE volume_a,
	const void *wave_b, UWORD period_b, UWORD samples_b, UBYTE volume_b) {
	write_pair(0, 0, 1, wave_a, period_a, samples_a, volume_a);
	write_pair(1, 3, 2, wave_b, period_b, samples_b, volume_b);
}

void audio_stop(void) {
	custom->aud[0].ac_vol = 0;
	custom->aud[1].ac_vol = 0;
	custom->aud[2].ac_vol = 0;
	custom->aud[3].ac_vol = 0;
	custom->dmacon = AUDIO_DMA;
	current_wave[0] = 0;
	current_wave[1] = 0;
}
