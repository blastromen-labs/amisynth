#include "audio.h"
#include "config.h"
#include "display.h"
#include "sequencer.h"
#include "synth.h"
#include "system.h"
#include "voice.h"

static short leave;

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

static void edit_pattern(void) {
	WORD x = system_mouse_x();
	WORD y = system_mouse_y();
	UWORD step = sequencer_step_from_x(x);
	short left = system_mouse_left();
	short right = system_mouse_right();
	static short previous_left;
	static short previous_right;
	static int control_grab = -1;
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

int main(void) {
	const SeqStep *steps;

	if (!system_init()) {
		system_shutdown();
		return 0;
	}
	sequencer_init();
	if (!synth_init() || !display_init()) {
		synth_shutdown();
		display_shutdown();
		system_shutdown();
		return 0;
	}
	voice_init();

	steps = sequencer_steps();
	system_take();
	display_start();
	audio_init(synth_voice(), synth_voice2(), synth_note_period(steps[0].note), synth_note_samples(steps[0].note));
	sequencer_clock_start();

	while (1) {
		system_wait_vbl();
		display_flip();
		system_mouse_update();
		if ((system_mouse_left() && system_mouse_right()) || leave)
			break;

		edit_pattern();
		display_frame(sequencer_steps(), sequencer_index(), sequencer_bpm(),
			system_mouse_x(), system_mouse_y(), voice_volume());
	}

	sequencer_clock_stop();
	audio_stop();
	system_free();
	display_shutdown();
	synth_shutdown();
	system_shutdown();
	return 0;
}
