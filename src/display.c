#include "display.h"
#include "config.h"
#include "synth.h"
#include "system.h"
#include "voice.h"

#include "support/gcc8_c_support.h"

#include <proto/exec.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <hardware/intbits.h>

extern struct ExecBase *SysBase;
extern volatile struct Custom *custom;

static UBYTE *planes[2];
static UBYTE *bitplane;
static UWORD *copper;
static UWORD *cop_plane_hi;
static UWORD *cop_plane_lo;
static UBYTE front;
static UBYTE view;
static UBYTE draw_osc;

static void plot(int x, int y) {
	if ((unsigned)x >= SCREEN_WIDTH || (unsigned)y >= SCREEN_HEIGHT)
		return;
	bitplane[y * (SCREEN_WIDTH / 8) + (x >> 3)] |= (UBYTE)(0x80 >> (x & 7));
}

static void line(int x0, int y0, int x1, int y1) {
	int dx = x1 - x0;
	int dy = y1 - y0;
	int sx = x0 < x1 ? 1 : -1;
	int sy = y0 < y1 ? 1 : -1;
	int err;

	if (dx < 0)
		dx = -dx;
	if (dy < 0)
		dy = -dy;
	err = (dx > dy ? dx : -dy) / 2;

	for (;;) {
		int e2;
		plot(x0, y0);
		if (x0 == x1 && y0 == y1)
			break;
		e2 = err;
		if (e2 > -dx) {
			err -= dy;
			x0 += sx;
		}
		if (e2 < dy) {
			err += dx;
			y0 += sy;
		}
	}
}

static void draw_box(int left, int top, int right, int bottom) {
	line(left, top, right, top);
	line(left, bottom, right, bottom);
	line(left, top, left, bottom);
	line(right, top, right, bottom);
}

static int note_y(UBYTE note) {
	int first = -1;
	int last = -1;

	if (note >= SYNTH_NOTE_COUNT)
		note = SYNTH_NOTE_COUNT - 1;
	for (int y = SEQ_GRID_TOP; y < SEQ_REST_TOP; y++) {
		if (sequencer_note_from_y((WORD)y) == (short)note) {
			if (first < 0)
				first = y;
			last = y;
		}
	}
	if (first < 0)
		return SEQ_GRID_TOP;
	return (first + last) / 2;
}

static void draw_wave_icon(int x, int y, UBYTE wave, int size) {
	switch (wave) {
	case SYNTH_WAVE_REVERSE_SAW:
		line(x - size, y + size, x - size, y);
		line(x - size, y, x + size - 1, y + size);
		break;
	case SYNTH_WAVE_SQUARE:
	case SYNTH_WAVE_PULSE: {
		int fall = wave == SYNTH_WAVE_PULSE ? x - size / 2 : x;

		line(x - size, y + size, x - size, y);
		line(x - size, y, fall, y);
		line(fall, y, fall, y + size);
		line(fall, y + size, x + size, y + size);
		line(x + size, y + size, x + size, y);
		break;
	}
	case SYNTH_WAVE_TRIANGLE:
		line(x - size, y + size, x, y);
		line(x, y, x + size, y + size);
		break;
	case SYNTH_WAVE_CUSTOM:
		line(x - size, y + size / 2, x - size / 2, y - size / 2);
		line(x - size / 2, y - size / 2, x - size / 4, y + size / 2);
		line(x - size / 4, y + size / 2, x + size / 4, y - size / 4);
		line(x + size / 4, y - size / 4, x + size, y + size / 2);
		break;
	case SYNTH_WAVE_SAW:
	default:
		line(x - size, y + size, x + size - 1, y);
		line(x + size - 1, y, x + size - 1, y + size);
		break;
	}
}

static void draw_text(int x, int y, const char *text);
static int text_width(const char *text);
static void format_number(char *text, UWORD value);

static void draw_centered(int mid, int y, const char *text) {
	draw_text(mid - text_width(text) / 2, y, text);
}

static int column_left(UWORD step, UWORD length) {
	return (int)(((ULONG)step * SCREEN_WIDTH) / length);
}

static int column_right(UWORD step, UWORD length) {
	return (int)((((ULONG)step + 1) * SCREEN_WIDTH) / length) - 2;
}

static void draw_column(int left, int right) {
	line(left, SEQ_GRID_TOP, right, SEQ_GRID_TOP);
	line(left, SEQ_GRID_BOTTOM, right, SEQ_GRID_BOTTOM);
	line(left, SEQ_GRID_TOP, left, SEQ_GRID_BOTTOM);
	line(right, SEQ_GRID_TOP, right, SEQ_GRID_BOTTOM);
}

static void draw_steps(const SeqStep *steps, UWORD current, WORD mouse_x, WORD mouse_y) {
	UWORD length = sequencer_length();
	UWORD hover = sequencer_step_from_x(mouse_x);

	line(0, SEQ_REST_TOP, SCREEN_WIDTH - 1, SEQ_REST_TOP);

	for (UWORD i = 0; i < length; i++) {
		int left = column_left(i, length);
		int right = column_right(i, length);
		int mid = (left + right) / 2;

		if (i == current || (mouse_y < SEQ_TEMPO_TOP && i == hover))
			draw_column(left, right);

		if (sequencer_edit_dur()) {
			int span = SEQ_REST_TOP - SEQ_GRID_TOP;
			int y = SEQ_REST_TOP - (int)steps[i].dur * span / 127;
			int cap = (right - left) / 4;
			char text[4];

			if (cap < 3)
				cap = 3;
			if (steps[i].dur > 0 && y < SEQ_REST_TOP) {
				line(mid - 1, y, mid - 1, SEQ_REST_TOP);
				line(mid, y, mid, SEQ_REST_TOP);
				line(mid + 1, y, mid + 1, SEQ_REST_TOP);
				line(mid - cap, y, mid + cap, y);
			}
			format_number(text, steps[i].dur);
			draw_centered(mid, SEQ_REST_TOP + 2, text);
		} else {
			if (steps[i].on) {
				int y = note_y(steps[i].note);
				int cap = (right - left) / 4;
				if (cap < 3)
					cap = 3;
				if (y < SEQ_REST_TOP) {
					line(mid - 1, y, mid - 1, SEQ_REST_TOP);
					line(mid, y, mid, SEQ_REST_TOP);
					line(mid + 1, y, mid + 1, SEQ_REST_TOP);
					line(mid - cap, y, mid + cap, y);
				}
			}
			draw_centered(mid, SEQ_REST_TOP + 2, steps[i].on ? synth_note_name(steps[i].note) : "---");
		}
		if (sequencer_per_step())
			draw_wave_icon(mid, SEQ_ICON_Y, steps[i].wave, 4);
	}
	if (!sequencer_per_step())
		draw_wave_icon(SCREEN_WIDTH / 2, SEQ_ICON_Y, sequencer_global_wave(), 4);

	if (mouse_y >= SEQ_GRID_TOP && mouse_y <= SEQ_GRID_BOTTOM) {
		int left = column_left(hover, length);
		int right = column_right(hover, length);
		line(left + 2, mouse_y, right - 2, mouse_y);
	}
}

static void cursor_pixel(int x, int y) {
	if ((unsigned)x >= SCREEN_WIDTH || (unsigned)y >= SCREEN_HEIGHT)
		return;
	bitplane[y * (SCREEN_WIDTH / 8) + (x >> 3)] ^= (UBYTE)(0x80 >> (x & 7));
}

static void draw_cursor(WORD x, WORD y) {
	for (int d = -5; d <= 5; d++) {
		if (d == 0)
			continue;
		cursor_pixel(x + d, y);
		cursor_pixel(x, y + d);
	}
	cursor_pixel(x, y);
}

static void draw_play_button(short playing) {
	int left = 2;
	int right = SEQ_PLAY_RIGHT - 2;
	int top = SEQ_TEMPO_TOP;
	int bottom = SEQ_TEMPO_BOTTOM - 1;
	int mid = (top + bottom) / 2;

	line(left, top, right, top);
	line(left, bottom, right, bottom);
	line(left, top, left, bottom);
	line(right, top, right, bottom);

	if (playing) {
		line(14, top + 8, 14, bottom - 8);
		line(16, top + 8, 16, bottom - 8);
		line(24, top + 8, 24, bottom - 8);
		line(26, top + 8, 26, bottom - 8);
		return;
	}

	line(14, top + 8, 14, bottom - 8);
	line(14, top + 8, right - 8, mid);
	line(14, bottom - 8, right - 8, mid);
}

static void format_number(char *text, UWORD value) {
	char reversed[3];
	int count = 0;

	if (value > 999)
		value = 999;
	do {
		reversed[count++] = (char)('0' + (value % 10));
		value = (UWORD)(value / 10);
	} while (value && count < 3);
	for (int i = 0; i < count; i++)
		text[i] = reversed[count - 1 - i];
	text[count] = 0;
}

static void draw_tempo(UWORD bpm) {
	int top = SEQ_TEMPO_TOP;
	int bottom = SEQ_TEMPO_BOTTOM - 1;
	int mid = (SEQ_TEMPO_BOX_LEFT + SEQ_TEMPO_BOX_RIGHT) / 2;
	char text[4];

	draw_box(SEQ_TEMPO_MINUS_LEFT, top, SEQ_TEMPO_MINUS_RIGHT - 2, bottom);
	draw_box(SEQ_TEMPO_BOX_LEFT, top, SEQ_TEMPO_BOX_RIGHT - 2, bottom);
	draw_box(SEQ_TEMPO_PLUS_LEFT, top, SEQ_TEMPO_PLUS_RIGHT - 2, bottom);
	draw_centered((SEQ_TEMPO_MINUS_LEFT + SEQ_TEMPO_MINUS_RIGHT) / 2, (top + bottom) / 2 - 3, "-");
	draw_centered((SEQ_TEMPO_PLUS_LEFT + SEQ_TEMPO_PLUS_RIGHT) / 2, (top + bottom) / 2 - 3, "+");
	draw_centered(mid, top + 4, "BPM");
	format_number(text, bpm);
	draw_centered(mid, bottom - 12, text);
}

static void draw_octave_button(short octave) {
	int left = SEQ_OCTAVE_LEFT;
	int right = SEQ_OCTAVE_RIGHT - 2;
	int top = SEQ_TEMPO_TOP;
	int bottom = SEQ_TEMPO_BOTTOM - 1;
	int mid = (left + right) / 2;
	char value[4];

	line(left, top, right, top);
	line(left, bottom, right, bottom);
	line(left, top, left, bottom);
	line(right, top, right, bottom);
	draw_centered(mid, top + 4, "OCT");
	if (octave > 0) {
		value[0] = '+';
		value[1] = (char)('0' + octave);
		value[2] = 0;
	} else if (octave < 0) {
		value[0] = '-';
		value[1] = (char)('0' - octave);
		value[2] = 0;
	} else {
		value[0] = '0';
		value[1] = 0;
	}
	draw_centered(mid, bottom - 12, value);
}

static void draw_random_button(void) {
	int left = SEQ_RANDOM_LEFT;
	int right = SEQ_RANDOM_RIGHT - 2;
	int top = SEQ_TEMPO_TOP;
	int bottom = SEQ_TEMPO_BOTTOM - 1;
	int mid = (left + right) / 2;

	draw_box(left, top, right, bottom);
	draw_centered(mid, top + 4, "RAN");
	draw_centered(mid, bottom - 12, "DOM");
}

static void draw_length_button(UWORD length) {
	int left = SEQ_LENGTH_LEFT;
	int right = SEQ_LENGTH_RIGHT - 2;
	int top = SEQ_TEMPO_TOP;
	int bottom = SEQ_TEMPO_BOTTOM - 1;
	int mid = (left + right) / 2;
	char text[4];

	draw_box(left, top, right, bottom);
	draw_centered(mid, top + 4, "SEQ LENGTH");
	format_number(text, length);
	draw_centered(mid, bottom - 12, text);
}

static void draw_exit_button(void) {
	int left = SEQ_EXIT_LEFT;
	int right = SEQ_EXIT_RIGHT - 2;
	int top = SEQ_TEMPO_TOP;
	int bottom = SEQ_TEMPO_BOTTOM - 1;

	draw_box(left, top, right, bottom);
	draw_centered((left + right) / 2, (top + bottom) / 2 - 3, "EXIT");
}

static UWORD *cop_move(UWORD *list, ULONG reg, UWORD value) {
	*list++ = (UWORD)reg;
	*list++ = value;
	return list;
}

static UWORD *cop_wait(UWORD *list, UWORD line, UWORD hpos) {
	*list++ = (UWORD)(((line & 0xff) << 8) | (hpos & 0xfe) | 1);
	*list++ = 0xfffe;
	return list;
}

/* The sequencer clock: evenly spaced copper interrupts down the frame. The
   copper only compares 8 line bits, so lines past 255 need the wrap wait. */
static UWORD *cop_ticks(UWORD *list) {
	UWORD lines = system_video()->lines;
	short wrapped = 0;

	for (UWORD i = 0; i < CLOCK_TICKS_PER_FRAME; i++) {
		UWORD line = (UWORD)(((ULONG)i * lines) / CLOCK_TICKS_PER_FRAME);

		if (line > 255 && !wrapped) {
			list = cop_wait(list, 255, 0xdf);
			wrapped = 1;
		}
		if (line > 0)
			list = cop_wait(list, line, 0x07);
		list = cop_move(list, offsetof(struct Custom, intreq), INTF_SETCLR | INTF_COPER);
	}
	return list;
}

short display_init(void) {
	planes[0] = AllocMem(SCREEN_BYTES, MEMF_CHIP | MEMF_CLEAR);
	planes[1] = AllocMem(SCREEN_BYTES, MEMF_CHIP | MEMF_CLEAR);
	copper = AllocMem(COPPER_WORDS * sizeof(UWORD), MEMF_CHIP | MEMF_CLEAR);
	bitplane = planes[0];
	front = 0;
	if (!planes[0] || !planes[1] || !copper) {
		display_shutdown();
		return 0;
	}
	return 1;
}

void display_shutdown(void) {
	if (planes[0])
		FreeMem(planes[0], SCREEN_BYTES);
	if (planes[1])
		FreeMem(planes[1], SCREEN_BYTES);
	if (copper)
		FreeMem(copper, COPPER_WORDS * sizeof(UWORD));
	planes[0] = 0;
	planes[1] = 0;
	bitplane = 0;
	copper = 0;
	cop_plane_hi = 0;
	cop_plane_lo = 0;
}

void display_start(void) {
	const UWORD x = DISPLAY_LEFT;
	const UWORD y = DISPLAY_TOP;
	const UWORD res = 8;
	UWORD xstop = x + SCREEN_WIDTH;
	UWORD ystop = y + SCREEN_HEIGHT;
	UWORD fetch = (x >> 1) - res;
	ULONG plane = (ULONG)planes[0];
	UWORD *list = copper;

	custom->fmode = 0;
	custom->bplcon3 = 0;
	custom->bplcon4 = 0;

	list = cop_move(list, offsetof(struct Custom, ddfstrt), fetch);
	list = cop_move(list, offsetof(struct Custom, ddfstop), (UWORD)(fetch + (((SCREEN_WIDTH >> 4) - 1) << 3)));
	list = cop_move(list, offsetof(struct Custom, diwstrt), (UWORD)(x + (y << 8)));
	list = cop_move(list, offsetof(struct Custom, diwstop), (UWORD)((xstop - 256) + ((ystop - 256) << 8)));
	list = cop_move(list, offsetof(struct Custom, bplcon0), 0x1200);
	list = cop_move(list, offsetof(struct Custom, bplcon1), 0);
	list = cop_move(list, offsetof(struct Custom, bplcon2), 0);
	list = cop_move(list, offsetof(struct Custom, bpl1mod), 0);
	list = cop_move(list, offsetof(struct Custom, bpl2mod), 0);
	list = cop_move(list, offsetof(struct Custom, bplpt[0]), (UWORD)(plane >> 16));
	cop_plane_hi = list - 1;
	list = cop_move(list, offsetof(struct Custom, bplpt[0]) + 2, (UWORD)plane);
	cop_plane_lo = list - 1;
	list = cop_move(list, offsetof(struct Custom, color[0]), 0x112);
	list = cop_move(list, offsetof(struct Custom, color[1]), 0x6cf);
	list = cop_ticks(list);
	*list++ = 0xffff;
	*list++ = 0xfffe;

	custom->cop1lc = (ULONG)copper;
	custom->dmacon = DMAF_SETCLR | DMAF_MASTER | DMAF_RASTER | DMAF_COPPER;
	custom->copjmp1 = 0x7fff;
}

void display_set_view(UBYTE next) {
	if (next == DISPLAY_DRAW || next == DISPLAY_SYNTH)
		view = next;
	else
		view = DISPLAY_SEQUENCE;
}

UBYTE display_view(void) {
	return view;
}

UBYTE display_draw_osc(void) {
	return draw_osc;
}

void display_cycle_draw_osc(void) {
	draw_osc = (UBYTE)((draw_osc + 1) % SYNTH_OSC_COUNT);
}

static UBYTE osc_wave(UBYTE osc) {
	return osc == 0 ? sequencer_osc1_wave() : voice_osc2();
}

void display_flip(void) {
	ULONG addr;

	if (!cop_plane_hi || !bitplane)
		return;
	addr = (ULONG)bitplane;
	*cop_plane_hi = (UWORD)(addr >> 16);
	*cop_plane_lo = (UWORD)addr;
	front ^= 1;
	bitplane = planes[front];
}

static const char glyph_keys[] = "ADSRCUTEBFG#0123456789-+QWIPLONYMHKXV";
static const UBYTE glyph_rows[][7] = {
	{ 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x00 },
	{ 0x1e, 0x11, 0x11, 0x11, 0x11, 0x1e, 0x00 },
	{ 0x0e, 0x10, 0x0e, 0x01, 0x01, 0x0e, 0x00 },
	{ 0x1e, 0x11, 0x1e, 0x14, 0x12, 0x11, 0x00 },
	{ 0x0e, 0x11, 0x10, 0x10, 0x11, 0x0e, 0x00 },
	{ 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e, 0x00 },
	{ 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00 },
	{ 0x1f, 0x10, 0x1e, 0x10, 0x10, 0x1f, 0x00 },
	{ 0x1e, 0x11, 0x1e, 0x11, 0x11, 0x1e, 0x00 },
	{ 0x1f, 0x10, 0x1e, 0x10, 0x10, 0x10, 0x00 },
	{ 0x0e, 0x11, 0x10, 0x17, 0x11, 0x0e, 0x00 },
	{ 0x0a, 0x1f, 0x0a, 0x1f, 0x0a, 0x0a, 0x00 },
	{ 0x0e, 0x11, 0x13, 0x15, 0x19, 0x0e, 0x00 },
	{ 0x04, 0x0c, 0x04, 0x04, 0x04, 0x0e, 0x00 },
	{ 0x0e, 0x11, 0x02, 0x04, 0x08, 0x1f, 0x00 },
	{ 0x1e, 0x01, 0x0e, 0x01, 0x01, 0x1e, 0x00 },
	{ 0x12, 0x12, 0x1f, 0x02, 0x02, 0x02, 0x00 },
	{ 0x1f, 0x10, 0x1e, 0x01, 0x01, 0x1e, 0x00 },
	{ 0x0e, 0x10, 0x1e, 0x11, 0x11, 0x0e, 0x00 },
	{ 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x00 },
	{ 0x0e, 0x11, 0x0e, 0x11, 0x11, 0x0e, 0x00 },
	{ 0x0e, 0x11, 0x0f, 0x01, 0x01, 0x0e, 0x00 },
	{ 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00 },
	{ 0x0e, 0x11, 0x11, 0x15, 0x0e, 0x02, 0x00 },
	{ 0x11, 0x11, 0x11, 0x15, 0x15, 0x0a, 0x00 },
	{ 0x0e, 0x04, 0x04, 0x04, 0x04, 0x0e, 0x00 },
	{ 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x00 },
	{ 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f, 0x00 },
	{ 0x0e, 0x11, 0x11, 0x11, 0x11, 0x0e, 0x00 },
	{ 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x00 },
	{ 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x00 },
	{ 0x11, 0x1b, 0x15, 0x11, 0x11, 0x11, 0x00 },
	{ 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11, 0x00 },
	{ 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x00 },
	{ 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x00 },
	{ 0x11, 0x11, 0x11, 0x0a, 0x0a, 0x04, 0x00 }
};

static void draw_char(int x, int y, char letter) {
	const UBYTE *rows = 0;

	for (int i = 0; glyph_keys[i]; i++) {
		if (glyph_keys[i] == letter) {
			rows = glyph_rows[i];
			break;
		}
	}
	if (!rows)
		return;
	for (int row = 0; row < 7; row++) {
		UBYTE bits = rows[row];
		for (int col = 0; col < 5; col++) {
			if (bits & (0x10 >> col))
				plot(x + col, y + row);
		}
	}
}

static int text_width(const char *text) {
	int count = 0;
	while (text[count])
		count++;
	if (count < 1)
		return 0;
	return count * 6 - 1;
}

static void draw_text(int x, int y, const char *text) {
	while (*text) {
		draw_char(x, y, *text);
		x += 6;
		text++;
	}
}

static void draw_panel_box(int left, int right, int top, int bottom) {
	line(left + 2, top, right - 2, top);
	line(left + 2, bottom - 1, right - 2, bottom - 1);
	line(left + 2, top, left + 2, bottom - 1);
	line(right - 2, top, right - 2, bottom - 1);
}

static void hspan(int x0, int x1, int y) {
	UBYTE *row;

	if ((unsigned)y >= SCREEN_HEIGHT)
		return;
	if (x0 > x1) {
		int swap = x0;
		x0 = x1;
		x1 = swap;
	}
	if (x0 < 0)
		x0 = 0;
	if (x1 >= SCREEN_WIDTH)
		x1 = SCREEN_WIDTH - 1;
	if (x0 > x1)
		return;
	row = bitplane + y * (SCREEN_WIDTH / 8);
	for (int x = x0; x <= x1; x++)
		row[x >> 3] |= (UBYTE)(0x80 >> (x & 7));
}

static void format_signed(char *text, int value) {
	int at = 0;
	int count = 0;
	char digits[4];

	if (value < 0) {
		text[at++] = '-';
		value = -value;
	} else if (value > 0)
		text[at++] = '+';
	if (value == 0) {
		text[at++] = '0';
		text[at] = 0;
		return;
	}
	while (value > 0 && count < 4) {
		digits[count++] = (char)('0' + value % 10);
		value /= 10;
	}
	while (count > 0)
		text[at++] = digits[--count];
	text[at] = 0;
}

static void draw_step_buttons(int left, int right, int top, int bottom) {
	int mid = (left + right) / 2;

	draw_box(left + 2, top, mid - 1, bottom);
	draw_box(mid + 1, top, right - 2, bottom);
	draw_centered((left + mid) / 2, top + 3, "-");
	draw_centered((mid + right) / 2, top + 3, "+");
}

static void draw_fader_box(int left, int right, int control, const char *label, const char *readout, short bipolar,
	int panel_top, int panel_bottom, int track_top, int track_bottom) {
	int mid = (left + right) / 2;
	UBYTE value = voice_get((UBYTE)control);
	int knob = track_bottom - (int)(((long)(track_bottom - track_top) * value) / 255);
	int zero = track_bottom - (int)(((long)(track_bottom - track_top) * 128) / 255);

	draw_panel_box(left, right, panel_top, panel_bottom);
	draw_centered(mid, panel_top + 2, label);
	if (readout)
		draw_centered(mid, panel_top + 11, readout);
	line(mid, track_top, mid, track_bottom);
	if (bipolar) {
		int y0 = knob < zero ? knob : zero;
		int y1 = knob < zero ? zero : knob;

		for (int y = y0; y <= y1; y += 2)
			hspan(mid - 5, mid + 5, y);
		hspan(mid - 8, mid + 8, zero);
	} else {
		for (int y = knob; y <= track_bottom; y += 2)
			hspan(mid - 5, mid + 5, y);
	}
	hspan(mid - 6, mid + 6, knob);
	{
		int live = voice_live((UBYTE)control);

		if (live >= 0) {
			int mark = track_bottom - (int)(((long)(track_bottom - track_top) * live) / 255);

			hspan(mid - 8, mid - 6, mark);
			hspan(mid + 6, mid + 8, mark);
		}
	}
}

static void draw_fader(int column, int columns, int control, const char *label, const char *readout, short bipolar,
	int panel_top, int panel_bottom, int track_top, int track_bottom) {
	int left = (column * SCREEN_WIDTH) / columns;
	int right = ((column + 1) * SCREEN_WIDTH) / columns - 1;

	draw_fader_box(left, right, control, label, readout, bipolar, panel_top, panel_bottom, track_top, track_bottom);
}

static void draw_wave_button(int column, int columns, const char *label, UBYTE wave, int panel_top, int panel_bottom,
	int icon_y, int icon_size) {
	int left = (column * SCREEN_WIDTH) / columns;
	int right = ((column + 1) * SCREEN_WIDTH) / columns - 1;
	int mid = (left + right) / 2;

	draw_panel_box(left, right, panel_top, panel_bottom);
	draw_centered(mid, panel_top + 2, label);
	draw_wave_icon(mid, icon_y, wave, icon_size);
}

static void draw_controls(void) {
	static const char *amp_labels[] = { "ATK", "DEC", "SUS", "REL", "MIX" };
	static const char *filter_labels[] = { "CUTOFF", "RESON", "F-ATK", "F-DEC", "F-SUS", "F-REL", "FENV" };
	static const char *rate_labels[] = { "LFO1", "LFO2" };
	static const UBYTE depth_controls[] = { VOICE_PW_DEPTH, VOICE_PITCH_DEPTH, VOICE_FILTER_DEPTH };
	char semitone[5];
	char fine[5];

	format_signed(semitone, voice_osc2_semitone());
	format_signed(fine, voice_osc2_fine());
	for (int i = 0; i < 5; i++)
		draw_fader(i, SYNTH_ROW1_COLUMNS, i, amp_labels[i], 0, 0, SYNTH_ROW1_TOP, SYNTH_ROW1_BOTTOM,
			SYNTH_ROW1_TRACK_TOP, SYNTH_ROW1_TRACK_BOTTOM);
	draw_fader(VOICE_OSC2_SEMI, SYNTH_ROW1_COLUMNS, VOICE_OSC2_SEMI, "SEMI", semitone, 1,
		SYNTH_ROW1_TOP, SYNTH_ROW1_BOTTOM, SYNTH_ROW1_TRACK_TOP, SYNTH_SEMI_TRACK_BOTTOM);
	{
		int left = (VOICE_OSC2_SEMI * SCREEN_WIDTH) / SYNTH_ROW1_COLUMNS;
		int right = ((VOICE_OSC2_SEMI + 1) * SCREEN_WIDTH) / SYNTH_ROW1_COLUMNS - 1;

		draw_step_buttons(left, right, SYNTH_SEMI_BUTTON_TOP, SYNTH_SEMI_BUTTON_BOTTOM - 1);
	}
	draw_fader(VOICE_OSC2_FINE, SYNTH_ROW1_COLUMNS, VOICE_OSC2_FINE, "FINE", fine, 1,
		SYNTH_ROW1_TOP, SYNTH_ROW1_BOTTOM, SYNTH_ROW1_TRACK_TOP, SYNTH_SEMI_TRACK_BOTTOM);
	{
		int left = (VOICE_OSC2_FINE * SCREEN_WIDTH) / SYNTH_ROW1_COLUMNS;
		int right = ((VOICE_OSC2_FINE + 1) * SCREEN_WIDTH) / SYNTH_ROW1_COLUMNS - 1;

		draw_step_buttons(left, right, SYNTH_SEMI_BUTTON_TOP, SYNTH_SEMI_BUTTON_BOTTOM - 1);
	}
	draw_wave_button(VOICE_OSC2_FINE + 1, SYNTH_ROW1_COLUMNS, "OSC1", osc_wave(0), SYNTH_ROW1_TOP,
		SYNTH_OSC_BUTTON_BOTTOM, SYNTH_ROW1_TOP + 22, 8);
	draw_wave_button(VOICE_OSC2_FINE + 2, SYNTH_ROW1_COLUMNS, "OSC2", osc_wave(1), SYNTH_ROW1_TOP,
		SYNTH_OSC_BUTTON_BOTTOM, SYNTH_ROW1_TOP + 22, 8);
	{
		int left = ((VOICE_OSC2_FINE + 1) * SCREEN_WIDTH) / SYNTH_ROW1_COLUMNS;
		int right = SCREEN_WIDTH - 1;
		int top = SYNTH_GATE_TOP;
		int bottom = SYNTH_SEMI_BUTTON_BOTTOM - 1;
		short gate = voice_gate_mode();

		draw_box(left + 2, top, right - 2, bottom);
		if (gate)
			draw_box(left + 4, top + 2, right - 4, bottom - 2);
		draw_centered((left + right) / 2, top + (bottom - top - 7) / 2, gate ? "GATE" : "ENV");
	}
	for (int i = 0; i < SYNTH_ROW2_COLUMNS; i++)
		draw_fader(i, SYNTH_ROW2_COLUMNS, VOICE_CUTOFF + i, filter_labels[i], 0, 0, SYNTH_ROW2_TOP, SYNTH_ROW2_BOTTOM,
			SYNTH_ROW2_TRACK_TOP, SYNTH_ROW2_TRACK_BOTTOM);
	draw_fader(0, SYNTH_ROW3_COLUMNS, VOICE_PULSE_WIDTH, "PWIDTH", 0, 0, SYNTH_ROW3_TOP, SYNTH_ROW3_BOTTOM,
		SYNTH_ROW3_TRACK_TOP, SYNTH_ROW3_TRACK_BOTTOM);
	for (int i = 0; i < 3; i++)
		draw_fader(1 + i, SYNTH_ROW3_COLUMNS, depth_controls[i], voice_route_label(depth_controls[i]), 0, 0,
			SYNTH_ROW3_TOP, SYNTH_ROW3_BOTTOM, SYNTH_ROW3_TRACK_TOP, SYNTH_ROW3_TRACK_BOTTOM);
	for (int i = 0; i < 2; i++)
		draw_fader(4 + i, SYNTH_ROW3_COLUMNS, VOICE_LFO1_RATE + i, rate_labels[i], 0, 0, SYNTH_ROW3_TOP, SYNTH_ROW3_BOTTOM,
			SYNTH_ROW3_TRACK_TOP, SYNTH_ROW3_TRACK_BOTTOM);
}

static void draw_tab(int left, int right, const char *label, short active) {
	int top = SEQ_TAB_TOP;
	int bottom = SEQ_TAB_BOTTOM - 1;

	draw_box(left, top, right, bottom);
	if (active)
		draw_box(left + 2, top + 2, right - 2, bottom - 2);
	draw_centered((left + right) / 2, top + 5, label);
}

static void draw_tabs(void) {
	draw_tab(SEQ_TAB_SEQ_LEFT, SEQ_TAB_SEQ_RIGHT, "SEQ", view == DISPLAY_SEQUENCE);
	draw_tab(SEQ_TAB_DRAW_LEFT, SEQ_TAB_DRAW_RIGHT, "DRAW", view == DISPLAY_DRAW);
	draw_tab(SEQ_TAB_SYNTH_LEFT, SEQ_TAB_SYNTH_RIGHT, "SYN", view == DISPLAY_SYNTH);
	if (view == DISPLAY_DRAW) {
		/* The oscillator tab is highlighted while that oscillator plays its custom wave. */
		draw_tab(SEQ_TAB_PRESET_LEFT, SEQ_TAB_PRESET_RIGHT, synth_preset_name(draw_osc), 0);
		draw_tab(DRAW_OSC_LEFT, DRAW_OSC_RIGHT, draw_osc == 0 ? "OSC1" : "OSC2",
			osc_wave(draw_osc) == SYNTH_WAVE_CUSTOM);
	}
	if (view == DISPLAY_SEQUENCE) {
		char text[4];

		draw_tab(SEQ_TAB_PRESET_LEFT, SEQ_TAB_PRESET_RIGHT, sequencer_edit_dur() ? "LEN" : "PITCH",
			sequencer_edit_dur());
		format_number(text, sequencer_dur_preset());
		draw_tab(SEQ_DUR_LEFT, SEQ_DUR_RIGHT, text, 0);
		draw_tab(SEQ_WAVE_MODE_LEFT, SEQ_WAVE_MODE_RIGHT, sequencer_per_step() ? "STEP" : "ALL",
			sequencer_per_step());
	}
}

static void draw_oscillator(void) {
	const BYTE *wave = synth_wave(SYNTH_WAVE_CUSTOM, draw_osc);
	int top = DRAW_TOP;
	int bottom = DRAW_BOTTOM - 1;
	int mid = draw_y_from_value(0);
	int prev_x = 0;
	int prev_y = 0;

	draw_box(DRAW_LEFT, top, DRAW_RIGHT - 1, bottom);
	line(DRAW_PLOT_LEFT, mid, DRAW_PLOT_RIGHT - 1, mid);
	draw_text(4, top + 3, "+");
	draw_text(4, mid - 3, "0");
	draw_text(4, bottom - 9, "-");
	if (!wave)
		return;

	for (int i = 0; i < SYNTH_WAVE_LEN; i++) {
		int x = draw_x_from_sample(i);
		int y = draw_y_from_value(wave[i]);

		line(x, mid, x, y);
		if (i)
			line(prev_x, prev_y, x, y);
		prev_x = x;
		prev_y = y;
	}
}

void display_frame(const SeqStep *steps, UWORD current, UWORD bpm, WORD mouse_x, WORD mouse_y, UBYTE volume) {
	(void)volume;
	memset(bitplane, 0, SCREEN_BYTES);
	if (view == DISPLAY_DRAW)
		draw_oscillator();
	else if (view == DISPLAY_SYNTH)
		draw_controls();
	else
		draw_steps(steps, current, mouse_x, mouse_y);
	draw_tabs();
	draw_play_button(sequencer_playing());
	draw_tempo(bpm);
	draw_octave_button(sequencer_octave());
	draw_random_button();
	draw_length_button(sequencer_length());
	draw_exit_button();
	draw_cursor(mouse_x, mouse_y);
}
