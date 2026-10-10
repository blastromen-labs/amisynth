#include "audio.h"
#include "config.h"

#include <proto/exec.h>
#include <devices/audio.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>

#define AUDIO_DMA (DMAF_AUD0 | DMAF_AUD1 | DMAF_AUD2 | DMAF_AUD3)
#define AUDIO_CHANNELS 4

extern struct ExecBase *SysBase;
extern volatile struct Custom *custom;

static struct MsgPort *claim_port;
static struct IOAudio *claim_request;
static UBYTE claimed;

/* Voices 0 and 3 are the left output. Voices 1 and 2 are the right output. */
static const UBYTE left_channel[SYNTH_OSC_COUNT] = { 0, 3 };
static const UBYTE right_channel[SYNTH_OSC_COUNT] = { 1, 2 };

static const void *current_wave[AUDIO_CHANNELS];
static UWORD current_samples[AUDIO_CHANNELS];
static UBYTE enabled = 1;

void audio_enable(short on) {
	enabled = on ? 1 : 0;
}

/* Opening audio.device with a channel map allocates the channels. Once they
   are ours, Paula's registers for them are written directly as before. */
short audio_claim(void) {
	static UBYTE all_channels[] = { (1 << AUDIO_CHANNELS) - 1 };

	claim_port = CreateMsgPort();
	if (claim_port)
		claim_request = (struct IOAudio *)CreateIORequest(claim_port, sizeof(struct IOAudio));
	if (!claim_request) {
		audio_release();
		return 0;
	}
	claim_request->ioa_Request.io_Message.mn_Node.ln_Pri = AUDIO_CLAIM_PRIORITY;
	claim_request->ioa_Request.io_Flags = ADIOF_NOWAIT;
	claim_request->ioa_Data = all_channels;
	claim_request->ioa_Length = sizeof(all_channels);
	if (OpenDevice((CONST_STRPTR)AUDIONAME, 0, (struct IORequest *)claim_request, 0)) {
		audio_release();
		return 0;
	}
	claimed = 1;
	return 1;
}

void audio_release(void) {
	if (claimed)
		CloseDevice((struct IORequest *)claim_request);
	if (claim_request)
		DeleteIORequest((struct IORequest *)claim_request);
	if (claim_port)
		DeleteMsgPort(claim_port);
	claimed = 0;
	claim_request = 0;
	claim_port = 0;
}

static void write_voice(int channel, const AudioVoice *voice) {
	const void *wave = voice->wave;
	UWORD period = voice->period;
	UWORD samples = voice->samples;
	UBYTE volume = voice->volume;

	if (volume > SYNTH_MAX_VOLUME)
		volume = SYNTH_MAX_VOLUME;
	if (period < SYNTH_MIN_PERIOD)
		period = SYNTH_MIN_PERIOD;
	if (samples < 2)
		samples = 2;
	if (!wave)
		volume = 0;
	if (wave != current_wave[channel] || samples != current_samples[channel]) {
		current_wave[channel] = wave;
		current_samples[channel] = samples;
		if (wave) {
			custom->aud[channel].ac_ptr = (UWORD *)wave;
			custom->aud[channel].ac_len = (UWORD)(samples / 2);
		}
	}
	custom->aud[channel].ac_per = period;
	custom->aud[channel].ac_vol = volume;
}

static void forget_waves(void) {
	for (int channel = 0; channel < AUDIO_CHANNELS; channel++)
		current_wave[channel] = 0;
}

void audio_init(const void *wave_a, const void *wave_b, UWORD period, UWORD samples) {
	const AudioVoice silent[SYNTH_OSC_COUNT] = {
		{ wave_a, period, samples, 0 },
		{ wave_b, period, samples, 0 }
	};

	forget_waves();
	audio_update(silent, silent);
}

/* Paula's audio registers are write-only. Reading one latches whatever is on
   the chip bus into it, so every value comes from write_voice. */
void audio_start(void) {
	if (!enabled)
		return;
	custom->dmacon = AUDIO_DMA;
	custom->dmacon = DMAF_SETCLR | DMAF_MASTER | AUDIO_DMA;
}

void audio_update(const AudioVoice left[SYNTH_OSC_COUNT], const AudioVoice right[SYNTH_OSC_COUNT]) {
	if (!enabled)
		return;
	for (int osc = 0; osc < SYNTH_OSC_COUNT; osc++) {
		write_voice(left_channel[osc], &left[osc]);
		write_voice(right_channel[osc], &right[osc]);
	}
}

void audio_stop(void) {
	for (int channel = 0; channel < AUDIO_CHANNELS; channel++)
		custom->aud[channel].ac_vol = 0;
	custom->dmacon = AUDIO_DMA;
	forget_waves();
}
