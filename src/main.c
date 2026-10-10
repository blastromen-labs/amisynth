#include "config.h"
#include "crash.h"
#include "diag.h"
#include "display.h"
#include "fx.h"
#include "host.h"
#include "options.h"
#include "sequencer.h"
#include "synth.h"
#include "system.h"
#include "voice.h"

#include <proto/dos.h>

static short leave;
static Options options;

static short in_plot(int x, int y) {
	return x >= DRAW_PLOT_LEFT && x < DRAW_PLOT_RIGHT && y >= DRAW_TOP && y < DRAW_BOTTOM;
}

/* An edited custom wave becomes that oscillator's wave, so it is heard at
   once and stays after leaving the draw view. */
static void use_custom_wave(UBYTE osc) {
	if (osc == 0)
		sequencer_set_osc1_wave(SYNTH_WAVE_CUSTOM);
	else
		voice_set_osc2(SYNTH_WAVE_CUSTOM);
}

static void paint_custom(UBYTE osc, int x, int y, short fresh) {
	static int last = -1;
	static int last_value;
	int sample = draw_sample_from_x(x);
	int value = draw_value_from_y(y);

	if (fresh)
		last = -1;
	if (last < 0 || last == sample) {
		synth_custom_set(osc, (UWORD)sample, value);
	} else {
		int from = last;
		int count = sample - from;
		int dir = 1;

		if (count < 0) {
			count = -count;
			dir = -1;
		}
		for (int i = 0; i <= count; i++) {
			int blended = last_value + ((value - last_value) * i) / count;
			synth_custom_set(osc, (UWORD)(from + dir * i), blended);
		}
	}
	last = sample;
	last_value = value;
}

static void edit_pattern(const HostInput *input) {
	WORD x = input->x;
	WORD y = input->y;
	UWORD step = sequencer_step_from_x(x);
	short left = input->left;
	short right = input->right;
	static short previous_left;
	static short previous_right;
	static int control_grab = -1;
	static int fx_grab = -1;
	static short painting;
	short clicked = left && !previous_left;
	short began = 0;
	UBYTE draw_osc = display_draw_osc();

	if (left && right) {
		previous_left = left;
		previous_right = right;
		return;
	}

	int slot = synth_panel_slot(x, y);

	if (clicked && y >= SEQ_TAB_TOP && y < SEQ_TAB_BOTTOM) {
		if (x >= SEQ_TAB_SEQ_LEFT && x < SEQ_TAB_SEQ_RIGHT) {
			display_set_view(DISPLAY_SEQUENCE);
			clicked = 0;
		} else if (x >= SEQ_TAB_DRAW_LEFT && x < SEQ_TAB_DRAW_RIGHT) {
			display_set_view(DISPLAY_DRAW);
			clicked = 0;
		} else if (x >= SEQ_TAB_SYNTH_LEFT && x < SEQ_TAB_SYNTH_RIGHT) {
			display_set_view(DISPLAY_SYNTH);
			clicked = 0;
		} else if (x >= SEQ_TAB_FX_LEFT && x < SEQ_TAB_FX_RIGHT) {
			display_set_view(DISPLAY_FX);
			clicked = 0;
		} else if (display_view() == DISPLAY_DRAW && x >= SEQ_TAB_PRESET_LEFT && x < SEQ_TAB_PRESET_RIGHT) {
			synth_cycle_preset(draw_osc);
			use_custom_wave(draw_osc);
			clicked = 0;
		} else if (display_view() == DISPLAY_DRAW && x >= DRAW_OSC_LEFT && x < DRAW_OSC_RIGHT) {
			display_cycle_draw_osc();
			clicked = 0;
		} else if (display_view() == DISPLAY_SEQUENCE && x >= SEQ_TAB_PRESET_LEFT && x < SEQ_TAB_PRESET_RIGHT) {
			sequencer_toggle_edit();
			clicked = 0;
		} else if (display_view() == DISPLAY_SEQUENCE && x >= SEQ_DUR_LEFT && x < SEQ_DUR_RIGHT) {
			sequencer_cycle_dur();
			clicked = 0;
		} else if (display_view() == DISPLAY_SEQUENCE && x >= SEQ_WAVE_MODE_LEFT && x < SEQ_WAVE_MODE_RIGHT) {
			sequencer_toggle_wave_mode();
			clicked = 0;
		}
	}

	if (!left)
		painting = 0;
	else if (clicked && display_view() == DISPLAY_DRAW && in_plot(x, y)) {
		painting = 1;
		began = 1;
	}

	if (!left)
		control_grab = -1;
	else if (!painting && clicked && display_view() == DISPLAY_SYNTH &&
		y >= SYNTH_PANE_TOP && y < SYNTH_PANE_BOTTOM && y < synth_track_top(y) &&
		voice_cycle_route((UBYTE)slot))
		clicked = 0;
	else if (!painting && clicked && display_view() == DISPLAY_SYNTH && synth_semi_step(x, y)) {
		voice_step_semitone(synth_semi_step(x, y));
		clicked = 0;
	} else if (!painting && clicked && display_view() == DISPLAY_SYNTH && synth_fine_step(x, y)) {
		voice_step_fine(synth_fine_step(x, y));
		clicked = 0;
	} else if (!painting && display_view() == DISPLAY_SYNTH && control_grab < 0 &&
		y >= SYNTH_PANE_TOP && y < SYNTH_PANE_BOTTOM && slot < VOICE_CONTROL_COUNT)
		control_grab = slot;

	if (!left)
		fx_grab = -1;
	else if (display_view() == DISPLAY_FX && fx_grab < 0)
		fx_grab = fx_panel_slot(x, y);

	if (painting) {
		paint_custom(draw_osc, x, y, began);
		use_custom_wave(draw_osc);
	} else if (display_view() == DISPLAY_DRAW && right && !previous_right && in_plot(x, y)) {
		synth_custom_reset(draw_osc);
		use_custom_wave(draw_osc);
	} else if (left && control_grab >= 0 &&
		!(control_grab == VOICE_OSC2_SEMI && synth_semi_step(x, y)) &&
		!(control_grab == VOICE_OSC2_FINE && synth_fine_step(x, y))) {
		voice_set((UBYTE)control_grab, voice_value_from_y(y, control_grab));
	} else if (left && fx_grab >= 0) {
		fx_set((UBYTE)fx_grab, fx_value_from_y(y));
	} else if (clicked && display_view() == DISPLAY_SYNTH && slot == SYNTH_GATE_SLOT) {
		voice_toggle_gate();
	} else if (clicked && display_view() == DISPLAY_SYNTH && y >= SYNTH_PANE_TOP &&
		y < SYNTH_PANE_BOTTOM && slot == SYNTH_OSC2_SLOT) {
		voice_cycle_osc2();
	} else if (clicked && display_view() == DISPLAY_SYNTH && y >= SYNTH_PANE_TOP &&
		y < SYNTH_PANE_BOTTOM && slot == SYNTH_WAVE_SLOT) {
		sequencer_cycle_all_waves();
	} else if (clicked && y >= SEQ_TEMPO_TOP && x < SEQ_PLAY_RIGHT) {
		sequencer_toggle_play();
	} else if (clicked && y >= SEQ_TEMPO_TOP && x >= SEQ_OCTAVE_LEFT && x < SEQ_OCTAVE_RIGHT) {
		sequencer_cycle_octave();
	} else if (clicked && y >= SEQ_TEMPO_TOP && x >= SEQ_RANDOM_LEFT && x < SEQ_RANDOM_RIGHT) {
		sequencer_randomize((UWORD)(x ^ (y << 8)));
	} else if (clicked && y >= SEQ_TEMPO_TOP && x >= SEQ_LENGTH_LEFT && x < SEQ_LENGTH_RIGHT) {
		sequencer_cycle_length();
	} else if (clicked && y >= SEQ_TEMPO_TOP && x >= SEQ_EXIT_LEFT && x < SEQ_EXIT_RIGHT) {
		leave = 1;
	} else if (clicked && y >= SEQ_TEMPO_TOP && x >= SEQ_TEMPO_MINUS_LEFT && x < SEQ_TEMPO_MINUS_RIGHT) {
		sequencer_set_bpm((UWORD)(sequencer_bpm() - 1));
	} else if (clicked && y >= SEQ_TEMPO_TOP && x >= SEQ_TEMPO_PLUS_LEFT && x < SEQ_TEMPO_PLUS_RIGHT) {
		sequencer_set_bpm((UWORD)(sequencer_bpm() + 1));
	} else if (display_view() == DISPLAY_SEQUENCE && left && y >= SEQ_GRID_TOP && y <= SEQ_GRID_BOTTOM) {
		if (sequencer_edit_dur())
			sequencer_set_dur(step, sequencer_dur_from_y(y));
		else
			sequencer_set_note(step, sequencer_note_from_y(y));
	} else if (display_view() == DISPLAY_SEQUENCE && clicked && y >= SEQ_GRID_TOP && y < SEQ_TAB_TOP) {
		sequencer_cycle_wave(step);
	}

	if (!painting && display_view() == DISPLAY_SEQUENCE && right && !left && !previous_right &&
		y >= SEQ_GRID_TOP && y < SEQ_TAB_TOP)
		sequencer_cycle_wave(step);
	previous_left = left;
	previous_right = right;
}

static const char *on_off(short on) {
	return on ? "on" : "off";
}

static void self_test(void) {
	static UWORD frames;

	if ((!options.crash_test && !options.hang_test) || ++frames != DIAG_SELFTEST_FRAMES)
		return;
	if (options.crash_test) {
		diag_log("self test: illegal instruction");
		__asm volatile("illegal");
	}
	diag_log("self test: endless loop");
	for (;;)
		__asm volatile("");
}

/* Brings everything down from wherever startup or a crash left it. */
static void finish(void) {
	/* With the log going straight to disk, the crash record is written before
	   cleanup, which may wait on a lock the crashed code still held. */
	short reported = !diag_held();

	diag_stage(DIAG_STAGE_SHUTDOWN);
	if (reported)
		crash_report();
	host_stop();
	crash_uninstall();
	display_shutdown();
	synth_shutdown();
	if (!reported)
		crash_report();
	diag_log("done");
	diag_close();
	options_free();
	system_shutdown();
}

static short prepare(void) {
	diag_stage(DIAG_STAGE_SEQUENCER);
	sequencer_init();
	diag_stage(DIAG_STAGE_SYNTH);
	if (!synth_init()) {
		diag_say("amisynth: not enough chip memory for the waves");
		return 0;
	}
	diag_stage(DIAG_STAGE_DISPLAY);
	if (!display_init()) {
		diag_say("amisynth: not enough chip memory for the screen");
		return 0;
	}
	diag_stage(DIAG_STAGE_VOICE);
	voice_init();
	return 1;
}

int main(void) {
	static short running;
	static UWORD frames;
	HostInput input = { SCREEN_WIDTH / 2, SCREEN_HEIGHT / 3, 0, 0, 0 };

	if (!system_init()) {
		system_shutdown();
		return RETURN_FAIL;
	}
	if (!options_read(&options, system_workbench())) {
		PrintFault(IoErr(), (CONST_STRPTR)"amisynth");
		PutStr((CONST_STRPTR)"Template: " OPTIONS_TEMPLATE "\n");
		system_shutdown();
		return RETURN_FAIL;
	}
	diag_open(&options);
	diag_stage(DIAG_STAGE_START);
	system_describe();
	diag_log("options: host %s, ticks %s, audio %s, catch %s, watchdog %ld s, settle %ld, serial %s, crashtest %s, hangtest %s",
		(ULONG)options_host_name(options.host),
		(ULONG)on_off(options.ticks), (ULONG)on_off(options.audio), (ULONG)on_off(options.catch_crashes),
		(ULONG)options.watchdog_seconds, (ULONG)options.settle_ticks, (ULONG)on_off(options.serial),
		(ULONG)on_off(options.crash_test), (ULONG)on_off(options.hang_test));

	if (__builtin_setjmp(crash_jump)) {
		finish();
		return RETURN_FAIL;
	}
	if (options.catch_crashes)
		crash_install_task();
	if (!prepare() || !host_start(&options)) {
		finish();
		return RETURN_FAIL;
	}

	while (1) {
		const UBYTE *shown;

		host_wait_frame();
		shown = display_flip();
		host_present(shown);
		host_read_input(&input);
		if ((input.left && input.right) || input.quit || leave)
			break;
		if (++frames == options.quit_frames) {
			host_check(shown);
			break;
		}

		edit_pattern(&input);
		display_frame(sequencer_steps(), sequencer_index(), sequencer_bpm(),
			input.x, input.y, voice_volume());
		crash_heartbeat();
		if (!running) {
			running = 1;
			diag_stage(DIAG_STAGE_RUNNING);
		}
		self_test();
	}

	diag_log("ran %ld frames", (ULONG)frames);
	finish();
	return RETURN_OK;
}
