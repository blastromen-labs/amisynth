#pragma once

/* A 32-byte cycle stays in tune up to A5. A#5–C6 use a 16-byte cycle. */
#define SYNTH_WAVE_LEN 32
#define SYNTH_PAULA_CLOCK 3546895UL
#define SYNTH_MIN_PERIOD 124
#define SYNTH_MAX_VOLUME 64
#define SYNTH_AMPLITUDE 110

/* MIDI note 24 is C1. 61 notes reach C6. */
#define SYNTH_NOTE_FIRST 24
#define SYNTH_NOTE_COUNT 61

#define SEQUENCER_STEPS 16
#define SEQUENCER_BPM_MIN 40
#define SEQUENCER_BPM_MAX 240
#define SEQUENCER_BPM_DEFAULT 120

#define SEQ_STEP_PITCH (SCREEN_WIDTH / SEQUENCER_STEPS)
#define SEQ_GRID_TOP 4
#define SEQ_GRID_BOTTOM 176
#define SEQ_REST_TOP (SEQ_GRID_BOTTOM - 16)
#define SEQ_ICON_Y 182
#define SEQ_TAB_TOP 188
#define SEQ_TAB_BOTTOM 205
#define SEQ_TAB_SEQ_LEFT 2
#define SEQ_TAB_SEQ_RIGHT 37
#define SEQ_TAB_DRAW_LEFT 41
#define SEQ_TAB_DRAW_RIGHT 76
#define SEQ_TAB_SYNTH_LEFT 80
#define SEQ_TAB_SYNTH_RIGHT 115
#define SEQ_TAB_FX_LEFT 119
#define SEQ_TAB_FX_RIGHT 154
#define SEQ_TAB_PRESET_LEFT 158
#define SEQ_TAB_PRESET_RIGHT 210
#define SEQ_WAVE_MODE_LEFT 214
#define SEQ_WAVE_MODE_RIGHT 268
#define SEQ_DUR_LEFT 272
#define SEQ_DUR_RIGHT 318
/* Draw view tab that picks which oscillator's custom wave is edited. */
#define DRAW_OSC_LEFT SEQ_WAVE_MODE_LEFT
#define DRAW_OSC_RIGHT SEQ_WAVE_MODE_RIGHT
#define SYNTH_ROW1_COLUMNS 9
#define SYNTH_ROW2_COLUMNS 7
#define SYNTH_ROW3_COLUMNS 6
#define SYNTH_ROW1_TOP 2
#define SYNTH_ROW1_BOTTOM 62
#define SYNTH_ROW1_TRACK_TOP 22
#define SYNTH_ROW1_TRACK_BOTTOM 58
#define SYNTH_SEMI_TRACK_BOTTOM 44
#define SYNTH_SEMI_BUTTON_TOP 46
#define SYNTH_SEMI_BUTTON_BOTTOM 60
#define SYNTH_OSC_BUTTON_BOTTOM 44
#define SYNTH_GATE_TOP 46
#define SYNTH_ROW2_TOP 64
#define SYNTH_ROW2_BOTTOM 124
#define SYNTH_ROW2_TRACK_TOP 76
#define SYNTH_ROW2_TRACK_BOTTOM 120
#define SYNTH_ROW3_TOP 126
#define SYNTH_ROW3_BOTTOM (SEQ_TAB_TOP - 2)
#define SYNTH_ROW3_TRACK_TOP 138
#define SYNTH_ROW3_TRACK_BOTTOM (SYNTH_ROW3_BOTTOM - 4)
#define SYNTH_PANE_TOP SYNTH_ROW1_TOP
#define SYNTH_PANE_BOTTOM SYNTH_ROW3_BOTTOM
/* FX view: group headers over one row of faders, one column per effect control. */
#define FX_GROUP_TOP 2
#define FX_PANEL_TOP 18
#define FX_PANEL_BOTTOM (SEQ_TAB_TOP - 2)
#define FX_TRACK_TOP (FX_PANEL_TOP + 22)
#define FX_TRACK_BOTTOM (FX_PANEL_BOTTOM - 6)

/* Effects. Delay and reverb echo the notes on the right-hand voices; the dry
   oscillators stay on the left. Set FX_WET_KEEPS_DRY to let the dry note win
   on the right whenever it is louder than the effect. */
#define FX_WET_KEEPS_DRY 0
#define FX_DRIVE_MAX_GAIN 16
/* Clipper offset at full drive, Q8 of full scale: uneven halves add even harmonics. */
#define FX_DRIVE_BIAS 64
#define FX_DELAY_MAX_STEPS 8
#define FX_DELAY_DEFAULT_STEPS 3
/* Feedback per repeat at full knob, Q8. */
#define FX_FEEDBACK_MAX 230
/* Reverb tail length in sequencer ticks, from the shortest to the longest decay. */
#define FX_REVERB_MIN_TICKS 40
#define FX_REVERB_MAX_TICKS 800
/* The tail moves 1/2^n of the way to a louder input each tick. */
#define FX_REVERB_SWELL_SHIFT 3
/* Slow pitch drift on the tail, in 1/4096 of the period, and its phase step per tick. */
#define FX_REVERB_DETUNE 12
#define FX_REVERB_DRIFT_RATE 164
/* Ring length for the longest delay at the slowest tempo on the faster NTSC tick. */
#define FX_DELAY_RING (FX_DELAY_MAX_STEPS * \
	(VIDEO_NTSC_FRAME_MHZ * CLOCK_TICKS_PER_FRAME * 15UL / (SEQUENCER_BPM_MIN * 1000UL) + 1) + 1)

#define DRAW_LEFT 2
#define DRAW_RIGHT (SCREEN_WIDTH - 2)
#define DRAW_PLOT_LEFT 14
#define DRAW_PLOT_RIGHT (SCREEN_WIDTH - 8)
#define DRAW_TOP SEQ_GRID_TOP
#define DRAW_BOTTOM (SEQ_TAB_TOP - 2)
#define SEQ_PLAY_RIGHT 40
#define SEQ_TEMPO_TOP 206
#define SEQ_TEMPO_BOTTOM 242
#define SEQ_TEMPO_MINUS_LEFT (SEQ_PLAY_RIGHT + 6)
#define SEQ_TEMPO_MINUS_RIGHT 74
#define SEQ_TEMPO_BOX_LEFT 78
#define SEQ_TEMPO_BOX_RIGHT 118
#define SEQ_TEMPO_PLUS_LEFT 122
#define SEQ_TEMPO_PLUS_RIGHT 146
#define SEQ_OCTAVE_LEFT 150
#define SEQ_OCTAVE_RIGHT 180
#define SEQ_RANDOM_LEFT 184
#define SEQ_RANDOM_RIGHT 214
#define SEQ_LENGTH_LEFT 218
#define SEQ_LENGTH_RIGHT 284
#define SEQ_EXIT_LEFT 288
#define SEQ_EXIT_RIGHT 318

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 256
#define SCREEN_BYTES ((SCREEN_WIDTH / 8) * SCREEN_HEIGHT)

/* Top-left corner of the picture on the beam, in lowres pixels and lines. */
#define DISPLAY_LEFT 129
#define DISPLAY_TOP 44
/* Background and drawing colour, $RGB. */
#define DISPLAY_COLOR_BACK 0x112
#define DISPLAY_COLOR_FRONT 0x6cf

/* How the program runs: in a window on Workbench, on its own Intuition
   screen, or owning the whole machine. SCREEN and TAKEOVER pick the others. */
#define HOST_WINDOW 0
#define HOST_SCREEN 1
#define HOST_TAKEOVER 2
#define HOST_DEFAULT HOST_WINDOW
#define WINDOW_TITLE "amisynth"
/* Rows shown in a window or screen. Everything below the tempo row is empty. */
#define WINDOW_HEIGHT SEQ_TEMPO_BOTTOM
/* A lowres pixel is 44 resolution ticks wide and high. The window widens the
   picture 1, 2 or 4 times to keep that shape on the Workbench screen. */
#define WINDOW_PIXEL_TICKS 44
/* Widened columns that may be left out, spread evenly, when the window is a
   little too wide for Workbench. Any more and the picture gets its own screen. */
#define WINDOW_MAX_DROP 16
/* Only rows that changed since the last frame are redrawn. Changed rows this
   close together are drawn as one band, which costs fewer blits. */
#define WINDOW_BAND_GAP 8
/* QUITAFTER compares every this many window rows with the picture before quitting. */
#define WINDOW_CHECK_STEP 3
/* audio.device priority for claiming all four channels. */
#define AUDIO_CLAIM_PRIORITY 0

/* Non-interlaced beam: lines per frame and frames per 1000 seconds.
   PAL is 313 lines of 227 colour clocks, NTSC 263 lines of 227.5. */
#define VIDEO_PAL_LINES 313
#define VIDEO_PAL_FRAME_MHZ 49920UL
#define VIDEO_NTSC_LINES 263
#define VIDEO_NTSC_FRAME_MHZ 59826UL

/* The copper raises this many evenly spaced sequencer ticks per frame. */
#define CLOCK_TICKS_PER_FRAME 4
/* Setup moves, a wait and an interrupt per tick, the line-255 wait and the end. */
#define COPPER_WORDS (32 + CLOCK_TICKS_PER_FRAME * 4)
/* Beam polls before a frame wait gives up. */
#define BEAM_WAIT_POLLS 400000UL

/* Diagnostics. Every default here can be changed from the Shell, see README. */
/* 0: no log unless LOG=<file> is given. 1: always log to DIAG_LOG_PATH. */
#define DIAG_LOG_DEFAULT 0
#define DIAG_LOG_PATH "PROGDIR:amisynth.log"
/* Used when the program's own drawer is write-protected. */
#define DIAG_LOG_FALLBACK "RAM:amisynth.log"
#define DIAG_LINE_MAX 128
/* Log lines kept in memory while the program owns the machine. */
#define DIAG_RING_LINES 48
/* Seconds without a drawn frame before the watchdog rescues the program. 0 turns it off. */
#define DIAG_WATCHDOG_SECONDS 4
/* DOS ticks (1/50 s) for disks and the log file to settle before the takeover. */
#define DIAG_SETTLE_TICKS 50
/* Frames drawn before CRASHTEST or HANGTEST fires. */
#define DIAG_SELFTEST_FRAMES 100
/* Long words of the crashed stack written to the log. */
#define DIAG_STACK_LONGS 16
/* Background colours while the screen is still blank, so a hang shows how far startup got. */
#define DIAG_COLOR_TAKEN 0x606
#define DIAG_COLOR_HOOKED 0x066
#define DIAG_COLOR_CRASH 0xf00
#define DIAG_COLOR_WATCHDOG 0xf80
