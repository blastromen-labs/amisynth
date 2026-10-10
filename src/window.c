#include "window.h"
#include "config.h"
#include "diag.h"

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <exec/memory.h>
#include <graphics/displayinfo.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>

#define KEY_ESCAPE 27
#define PICTURE_ROW_BYTES (SCREEN_WIDTH / 8)

struct IntuitionBase *IntuitionBase;

typedef struct {
	UBYTE down;
	UBYTE pressed_now;
	UBYTE release_later;
} Button;

static struct Screen *workbench;
static struct Screen *screen;
static struct Window *window;
static struct BitMap *scaled;
static UBYTE *shown;
static UBYTE redraw_all;
static ULONG widen_table[256];
static LONG pens[2];
static LONG obtained[2] = { -1, -1 };
static UWORD widen = 1;
static UWORD wide_width = SCREEN_WIDTH;
static UWORD slices = 1;
static UWORD view_width;
static UWORD view_height;
static Button left_button;
static Button right_button;
static UBYTE quit;

static const UWORD colors[2] = { DISPLAY_COLOR_BACK, DISPLAY_COLOR_FRONT };

static ULONG gun(UWORD rgb, short shift) {
	return ((ULONG)(rgb >> shift) & 15UL) * 0x11111111UL;
}

static WORD clamp(LONG value, WORD limit) {
	if (value < 0)
		return 0;
	return value >= limit ? (WORD)(limit - 1) : (WORD)value;
}

/* A one-frame click must still be seen as down for one frame, so a release
   that arrives with its press is held over to the next poll. */
static void button_begin(Button *button) {
	if (button->release_later)
		button->down = 0;
	button->release_later = 0;
	button->pressed_now = 0;
}

static void button_set(Button *button, short down) {
	if (down) {
		button->down = 1;
		button->pressed_now = 1;
	} else if (button->pressed_now) {
		button->release_later = 1;
	} else {
		button->down = 0;
	}
}

static UWORD whole_factor(LONG ticks) {
	LONG factor = ticks > 0 ? (WINDOW_PIXEL_TICKS + ticks / 2) / ticks : 1;

	return factor >= 4 ? 4 : factor >= 2 ? 2 : 1;
}

/* Widens every row by spreading each bit over `factor` bits. */
static void build_widen_table(void) {
	for (UWORD byte = 0; byte < 256; byte++) {
		ULONG wide = 0;

		for (short bit = 7; bit >= 0; bit--) {
			ULONG ones = (1UL << widen) - 1;

			wide = (wide << widen) | ((byte >> bit) & 1 ? ones : 0);
		}
		widen_table[byte] = wide;
	}
}

/* The picture widened to keep lowres pixels square on Workbench, with up to
   WINDOW_MAX_DROP columns left out and rows repeated to fit inside the borders. */
static short fit_workbench(void) {
	struct DisplayInfo info;
	ULONG mode = GetVPModeID(&workbench->ViewPort);
	LONG res_x = WINDOW_PIXEL_TICKS;
	LONG res_y = WINDOW_PIXEL_TICKS;
	LONG room_w = workbench->Width - workbench->WBorLeft - workbench->WBorRight;
	LONG room_h = workbench->Height - workbench->WBorTop - workbench->Font->ta_YSize - 1 - workbench->WBorBottom;
	LONG ideal_h;
	UWORD drop;

	if (mode != (ULONG)INVALID_ID && GetDisplayInfoData(0, (UBYTE *)&info, sizeof(info), DTAG_DISP, mode) &&
		info.Resolution.x > 0 && info.Resolution.y > 0) {
		res_x = info.Resolution.x;
		res_y = info.Resolution.y;
	}
	widen = whole_factor(res_x);
	wide_width = (UWORD)(SCREEN_WIDTH * widen);
	ideal_h = WINDOW_HEIGHT * whole_factor(res_y);
	view_width = (UWORD)(wide_width < room_w ? wide_width : room_w);
	view_height = (UWORD)(ideal_h < room_h ? ideal_h : room_h);
	drop = (UWORD)(wide_width - view_width);
	slices = (UWORD)(drop + 1);
	diag_log("window: Workbench mode $%lx %ldx%ld, resolution %ld:%ld, picture %ldx%ld of %ldx%ld",
		mode, (ULONG)workbench->Width, (ULONG)workbench->Height, (ULONG)res_x, (ULONG)res_y,
		(ULONG)view_width, (ULONG)view_height, (ULONG)wide_width, (ULONG)ideal_h);
	return drop <= WINDOW_MAX_DROP && drop <= wide_width - SCREEN_WIDTH && view_height >= WINDOW_HEIGHT;
}

static void obtain_pens(void) {
	struct ColorMap *map = workbench->ViewPort.ColorMap;

	for (int i = 0; i < 2; i++) {
		obtained[i] = ObtainBestPen(map, gun(colors[i], 8), gun(colors[i], 4), gun(colors[i], 0),
			OBP_Precision, PRECISION_GUI, TAG_END);
		pens[i] = obtained[i];
	}
	if (pens[0] < 0)
		pens[0] = 0;
	if (pens[1] < 0 || pens[1] == pens[0])
		pens[1] = pens[0] == 1 ? 2 : 1;
}

static void release_pens(void) {
	for (int i = 0; i < 2; i++) {
		if (obtained[i] >= 0)
			ReleasePen(workbench->ViewPort.ColorMap, (ULONG)obtained[i]);
		obtained[i] = -1;
	}
}

static short open_on_workbench(void) {
	WORD outer_w;
	WORD outer_h;

	workbench = LockPubScreen(0);
	if (!workbench || !fit_workbench())
		return 0;
	if (widen != 1 || view_height != WINDOW_HEIGHT) {
		scaled = AllocBitMap(wide_width, view_height, 1, 0, 0);
		if (!scaled)
			return 0;
		build_widen_table();
	}
	outer_w = (WORD)(view_width + workbench->WBorLeft + workbench->WBorRight);
	outer_h = (WORD)(view_height + workbench->WBorTop + workbench->Font->ta_YSize + 1 + workbench->WBorBottom);
	obtain_pens();
	window = OpenWindowTags(0,
		WA_PubScreen, (ULONG)workbench,
		WA_Title, (ULONG)WINDOW_TITLE,
		WA_Left, (ULONG)((workbench->Width - outer_w) / 2),
		WA_Top, (ULONG)((workbench->Height - outer_h) / 2),
		WA_InnerWidth, (ULONG)view_width,
		WA_InnerHeight, (ULONG)view_height,
		WA_DragBar, TRUE,
		WA_DepthGadget, TRUE,
		WA_CloseGadget, TRUE,
		WA_Activate, TRUE,
		WA_RMBTrap, TRUE,
		WA_SmartRefresh, TRUE,
		WA_NoCareRefresh, TRUE,
		WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_MOUSEBUTTONS | IDCMP_INACTIVEWINDOW | IDCMP_VANILLAKEY,
		TAG_END);
	return window != 0;
}

static short open_own_screen(void) {
	screen = OpenScreenTags(0,
		SA_Width, (ULONG)SCREEN_WIDTH,
		SA_Height, (ULONG)WINDOW_HEIGHT,
		SA_Depth, 1,
		SA_DisplayID, LORES_KEY,
		SA_Overscan, OSCAN_TEXT,
		SA_AutoScroll, TRUE,
		SA_Quiet, TRUE,
		SA_ShowTitle, FALSE,
		SA_Type, CUSTOMSCREEN,
		SA_Title, (ULONG)WINDOW_TITLE,
		TAG_END);
	if (!screen)
		return 0;
	for (int i = 0; i < 2; i++) {
		SetRGB4(&screen->ViewPort, i, (colors[i] >> 8) & 15, (colors[i] >> 4) & 15, colors[i] & 15);
		pens[i] = i;
	}
	widen = 1;
	wide_width = SCREEN_WIDTH;
	slices = 1;
	view_width = SCREEN_WIDTH;
	view_height = WINDOW_HEIGHT;
	window = OpenWindowTags(0,
		WA_CustomScreen, (ULONG)screen,
		WA_Left, 0,
		WA_Top, 0,
		WA_Width, (ULONG)SCREEN_WIDTH,
		WA_Height, (ULONG)WINDOW_HEIGHT,
		WA_Backdrop, TRUE,
		WA_Borderless, TRUE,
		WA_Activate, TRUE,
		WA_RMBTrap, TRUE,
		WA_SmartRefresh, TRUE,
		WA_NoCareRefresh, TRUE,
		WA_IDCMP, IDCMP_MOUSEBUTTONS | IDCMP_INACTIVEWINDOW | IDCMP_VANILLAKEY,
		TAG_END);
	diag_log("window: own screen %ldx%ld", (ULONG)SCREEN_WIDTH, (ULONG)WINDOW_HEIGHT);
	return window != 0;
}

/* A window must close before the screen it is on. */
static void close_display(void) {
	if (window)
		CloseWindow(window);
	window = 0;
	if (workbench) {
		release_pens();
		UnlockPubScreen(0, workbench);
	}
	workbench = 0;
	if (screen)
		CloseScreen(screen);
	screen = 0;
	if (scaled) {
		WaitBlit();
		FreeBitMap(scaled);
	}
	scaled = 0;
}

short window_open(short own_screen) {
	IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 39);
	if (!IntuitionBase) {
		diag_say("amisynth: window mode needs Kickstart 3.0 or newer, try TAKEOVER");
		return 0;
	}
	quit = 0;
	redraw_all = 1;
	shown = AllocVec(PICTURE_ROW_BYTES * WINDOW_HEIGHT, MEMF_ANY);
	if (!shown) {
		diag_say("amisynth: not enough memory for the window");
		window_close();
		return 0;
	}
	if (!own_screen) {
		if (open_on_workbench())
			return 1;
		diag_log("window: no room on Workbench, opening a screen instead");
		close_display();
	}
	if (open_own_screen())
		return 1;
	diag_say("amisynth: could not open a window or a screen");
	window_close();
	return 0;
}

void window_close(void) {
	close_display();
	if (shown)
		FreeVec(shown);
	shown = 0;
	if (IntuitionBase)
		CloseLibrary((struct Library *)IntuitionBase);
	IntuitionBase = 0;
}

static void widen_row(const UBYTE *in, UBYTE *out) {
	const UBYTE *end = in + PICTURE_ROW_BYTES;

	if (widen == 4) {
		for (ULONG *wide = (ULONG *)out; in < end;)
			*wide++ = widen_table[*in++];
	} else if (widen == 2) {
		for (UWORD *wide = (UWORD *)out; in < end;)
			*wide++ = (UWORD)widen_table[*in++];
	} else {
		CopyMem((APTR)in, out, PICTURE_ROW_BYTES);
	}
}

/* The first window row that shows picture row `row` or a later one. */
static UWORD window_row(UWORD row) {
	return (UWORD)(((ULONG)row * view_height + WINDOW_HEIGHT - 1) / WINDOW_HEIGHT);
}

/* Each picture row shows on one or more window rows. A repeat is a copy of
   the window row above it. */
static void widen_band(const UBYTE *plane, UWORD top, UWORD bottom) {
	UWORD stride = scaled->BytesPerRow;
	UBYTE *out = scaled->Planes[0] + (ULONG)top * stride;
	const UBYTE *previous = 0;

	for (UWORD row = top; row < bottom; row++, out += stride) {
		const UBYTE *in = plane + (ULONG)row * WINDOW_HEIGHT / view_height * PICTURE_ROW_BYTES;

		if (in == previous)
			CopyMem(out - stride, out, wide_width / 8);
		else
			widen_row(in, out);
		previous = in;
	}
}

/* Leaves out one widened column between slices, so the dropped columns are
   spread evenly instead of cutting off an edge. */
static void draw_slices(const UBYTE *picture, UWORD modulo, UWORD top, UWORD bottom) {
	struct RastPort *rp = window->RPort;
	const UBYTE *band = picture + (ULONG)top * modulo;

	for (UWORD i = 0; i < slices; i++) {
		UWORD left = (UWORD)((ULONG)i * view_width / slices);
		UWORD right = (UWORD)((ULONG)(i + 1) * view_width / slices);
		UWORD from = (UWORD)(left + i);

		BltTemplate((PLANEPTR)(band + (from >> 4) * 2), from & 15, modulo, rp,
			(WORD)(window->BorderLeft + left), (WORD)(window->BorderTop + top),
			(WORD)(right - left), (WORD)(bottom - top));
	}
}

static void draw_band(const UBYTE *plane, UWORD first, UWORD last) {
	UWORD top = window_row(first);
	UWORD bottom = window_row(last);

	if (!scaled) {
		draw_slices(plane, PICTURE_ROW_BYTES, top, bottom);
		return;
	}
	WaitBlit();
	widen_band(plane, top, bottom);
	draw_slices(scaled->Planes[0], scaled->BytesPerRow, top, bottom);
}

/* Compares with what the window shows and remembers the new row. */
static short row_changed(const UBYTE *plane, UWORD row) {
	const ULONG *now = (const ULONG *)(plane + (ULONG)row * PICTURE_ROW_BYTES);
	ULONG *was = (ULONG *)(shown + (ULONG)row * PICTURE_ROW_BYTES);
	short changed = redraw_all;

	for (UWORD i = 0; i < PICTURE_ROW_BYTES / 4; i++) {
		if (was[i] != now[i]) {
			was[i] = now[i];
			changed = 1;
		}
	}
	return changed;
}

void window_present(const UBYTE *plane) {
	struct RastPort *rp;
	UWORD row = 0;

	if (!window || !plane)
		return;
	rp = window->RPort;
	SetAPen(rp, (ULONG)pens[1]);
	SetBPen(rp, (ULONG)pens[0]);
	SetDrMd(rp, JAM2);
	while (row < WINDOW_HEIGHT) {
		UWORD first;
		UWORD last;

		if (!row_changed(plane, row++))
			continue;
		first = row - 1;
		last = row;
		while (row < WINDOW_HEIGHT && row - last < WINDOW_BAND_GAP)
			if (row_changed(plane, row++))
				last = row;
		draw_band(plane, first, last);
	}
	redraw_all = 0;
}

ULONG window_check(const UBYTE *plane, ULONG *checked) {
	struct RastPort *rp;
	ULONG wrong = 0;

	*checked = 0;
	if (!window || !plane)
		return 0;
	WaitBlit();
	rp = window->RPort;
	for (UWORD y = 0; y < view_height; y += WINDOW_CHECK_STEP) {
		const UBYTE *row = plane + (ULONG)y * WINDOW_HEIGHT / view_height * PICTURE_ROW_BYTES;
		UWORD slice = 0;

		for (UWORD x = 0; x < view_width; x++) {
			UWORD picture_x;
			ULONG want;

			while (slice + 1 < slices && x >= (ULONG)(slice + 1) * view_width / slices)
				slice++;
			picture_x = (UWORD)((x + slice) / widen);
			want = (ULONG)((row[picture_x >> 3] >> (7 - (picture_x & 7))) & 1 ? pens[1] : pens[0]);
			if (ReadPixel(rp, (LONG)(window->BorderLeft + x), (LONG)(window->BorderTop + y)) != want)
				wrong++;
			(*checked)++;
		}
	}
	return wrong;
}

void window_input(HostInput *input) {
	struct IntuiMessage *message;

	button_begin(&left_button);
	button_begin(&right_button);
	while (window && (message = (struct IntuiMessage *)GetMsg(window->UserPort))) {
		ULONG kind = message->Class;
		UWORD code = message->Code;

		ReplyMsg((struct Message *)message);
		if (kind == IDCMP_CLOSEWINDOW || (kind == IDCMP_VANILLAKEY && code == KEY_ESCAPE)) {
			quit = 1;
		} else if (kind == IDCMP_INACTIVEWINDOW) {
			left_button.down = right_button.down = 0;
		} else if (kind == IDCMP_MOUSEBUTTONS) {
			if (code == SELECTDOWN || code == SELECTUP)
				button_set(&left_button, code == SELECTDOWN);
			else if (code == MENUDOWN || code == MENUUP)
				button_set(&right_button, code == MENUDOWN);
		}
	}
	if (window) {
		input->x = clamp((LONG)(window->MouseX - window->BorderLeft) * SCREEN_WIDTH / view_width, SCREEN_WIDTH);
		input->y = clamp((LONG)(window->MouseY - window->BorderTop) * WINDOW_HEIGHT / view_height, WINDOW_HEIGHT);
	}
	input->left = left_button.down;
	input->right = right_button.down;
	input->quit = quit;
}
