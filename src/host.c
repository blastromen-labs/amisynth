#include "host.h"
#include "audio.h"
#include "config.h"
#include "crash.h"
#include "diag.h"
#include "display.h"
#include "sequencer.h"
#include "synth.h"
#include "system.h"
#include "ticker.h"
#include "window.h"

#include <proto/graphics.h>

extern void sequencer_tick_isr(void);

static UBYTE host = HOST_DEFAULT;
static UBYTE taken;
static UBYTE opened;
static UBYTE claimed;
static UBYTE ticking;

static void os_tick(void) {
	crash_isr_enter();
	sequencer_tick();
	crash_isr_leave(0);
}

static ULONG nominal_tick_mhz(void) {
	return system_video()->frame_mhz * CLOCK_TICKS_PER_FRAME;
}

static void start_audio(short on) {
	const SeqStep *steps = sequencer_steps();

	diag_stage(DIAG_STAGE_AUDIO);
	audio_enable(on);
	audio_init(synth_voice(), synth_voice2(), synth_note_period(steps[0].note), synth_note_samples(steps[0].note));
}

static void take_machine(const Options *options) {
	diag_stage(DIAG_STAGE_TAKEOVER);
	system_take(options->settle_ticks);
	taken = 1;
	diag_hold();
	if (options->catch_crashes) {
		crash_install(system_vbr());
		diag_stage(DIAG_STAGE_HOOKS);
	}
	diag_stage(DIAG_STAGE_SCREEN);
	display_start();
	start_audio(options->audio);
	diag_stage(DIAG_STAGE_CLOCK);
	crash_watchdog(options->watchdog_seconds);
	if (!options->ticks)
		return;
	sequencer_clock_start(nominal_tick_mhz());
	system_ticks_on(sequencer_tick_isr);
	ticking = 1;
}

/* The CPU vector hooks and the watchdog stay off here: under multitasking they
   would also catch other programs. main's task trap catches this program's own faults. */
static short open_window(const Options *options) {
	ULONG rate;

	diag_stage(DIAG_STAGE_WINDOW);
	if (!window_open(options->host == HOST_SCREEN))
		return 0;
	opened = 1;
	display_set_cursor(0);
	claimed = options->audio && audio_claim();
	if (options->audio && !claimed)
		diag_say("amisynth: another program has the audio channels, running silent");
	start_audio(claimed);
	diag_stage(DIAG_STAGE_CLOCK);
	if (!options->ticks)
		return 1;
	rate = ticker_claim(nominal_tick_mhz(), os_tick);
	diag_log("clock: %s at %ld mHz", (ULONG)ticker_source(), rate);
	sequencer_clock_start(rate);
	ticker_run();
	ticking = 1;
	return 1;
}

short host_start(const Options *options) {
	host = options->host;
	if (host != HOST_TAKEOVER)
		return open_window(options);
	take_machine(options);
	return 1;
}

void host_stop(void) {
	if (ticking) {
		if (taken)
			system_ticks_off();
		else
			ticker_stop();
	}
	ticking = 0;
	sequencer_clock_stop();
	if (taken || claimed)
		audio_stop();
	if (claimed)
		audio_release();
	claimed = 0;
	if (taken) {
		crash_uninstall();
		system_free();
		diag_release();
	}
	taken = 0;
	if (opened)
		window_close();
	opened = 0;
}

void host_wait_frame(void) {
	if (host == HOST_TAKEOVER)
		system_wait_vbl();
	else
		WaitTOF();
}

void host_present(const UBYTE *plane) {
	if (host != HOST_TAKEOVER)
		window_present(plane);
}

void host_check(const UBYTE *plane) {
	ULONG checked;
	ULONG wrong;

	if (host == HOST_TAKEOVER)
		return;
	wrong = window_check(plane, &checked);
	diag_log("window check: %ld of %ld pixels differ from the picture", wrong, checked);
}

void host_read_input(HostInput *input) {
	if (host != HOST_TAKEOVER) {
		window_input(input);
		return;
	}
	system_mouse_update();
	input->x = system_mouse_x();
	input->y = system_mouse_y();
	input->left = (UBYTE)system_mouse_left();
	input->right = (UBYTE)system_mouse_right();
	input->quit = 0;
}
