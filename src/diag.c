#include "diag.h"
#include "config.h"

#include <proto/dos.h>
#include <proto/exec.h>
#include <hardware/custom.h>

#ifndef BUILD_ID
#define BUILD_ID "unknown"
#endif

#define DIAG_PATH_MAX 108

extern volatile struct Custom *custom;
extern void _start(void);
/* RawDoFmt callback in diag_putc.s: stores d0 through the DiagSink at a3. */
extern void diag_put_char(void);

typedef struct {
	char *at;
	char *end;
} DiagSink;

static const char *const stage_names[DIAG_STAGE_COUNT] = {
	[DIAG_STAGE_START] = "start",
	[DIAG_STAGE_SEQUENCER] = "sequencer init",
	[DIAG_STAGE_SYNTH] = "synth init",
	[DIAG_STAGE_DISPLAY] = "display init",
	[DIAG_STAGE_VOICE] = "voice init",
	[DIAG_STAGE_TAKEOVER] = "takeover",
	[DIAG_STAGE_HOOKS] = "exception hooks",
	[DIAG_STAGE_WINDOW] = "window open",
	[DIAG_STAGE_SCREEN] = "screen start",
	[DIAG_STAGE_AUDIO] = "audio init",
	[DIAG_STAGE_CLOCK] = "clock start",
	[DIAG_STAGE_RUNNING] = "running",
	[DIAG_STAGE_SHUTDOWN] = "shutdown"
};

static const UWORD stage_colors[DIAG_STAGE_COUNT] = {
	[DIAG_STAGE_HOOKS] = DIAG_COLOR_HOOKED
};

static char path[DIAG_PATH_MAX];
static UBYTE serial;
static UBYTE held;
static volatile UBYTE stage;
static char ring[DIAG_RING_LINES][DIAG_LINE_MAX];
static UWORD ring_first;
static UWORD ring_count;
static ULONG ring_dropped;

static void format_into(char *dest, UWORD size, const char *format, APTR args) {
	DiagSink sink = { dest, dest + size - 1 };

	RawDoFmt((CONST_STRPTR)format, args, (VOID (*)())diag_put_char, &sink);
	*sink.at = 0;
}

static ULONG text_length(const char *text) {
	ULONG length = 0;

	while (text[length])
		length++;
	return length;
}

static void copy_text(char *dest, const char *src, UWORD size) {
	UWORD i = 0;

	while (src[i] && i < size - 1) {
		dest[i] = src[i];
		i++;
	}
	dest[i] = 0;
}

/* exec's RawPutChar needs neither interrupts nor DMA, so it also works while
   the program owns the machine. */
static void serial_char(char c) {
	register char ch __asm("d0") = c;

	__asm volatile(
		"move.l %%a6,-(%%sp)\n"
		"movea.l 4.w,%%a6\n"
		"jsr -0x204(%%a6)\n"
		"movea.l (%%sp)+,%%a6"
		: "+d"(ch) : : "d1", "a0", "a1", "cc", "memory");
}

static void serial_line(const char *line) {
	while (*line)
		serial_char(*line++);
	serial_char('\r');
	serial_char('\n');
}

static void write_line(BPTR file, const char *line) {
	Write(file, (APTR)line, (LONG)text_length(line));
	Write(file, (APTR)"\n", 1);
}

/* The file is closed after every write so it is complete on disk if the
   machine hangs later. */
static BPTR open_append(void) {
	BPTR file;

	if (!path[0] || !DOSBase)
		return 0;
	file = Open((CONST_STRPTR)path, MODE_READWRITE);
	if (file)
		Seek(file, 0, OFFSET_END);
	return file;
}

static void ring_push(const char *line) {
	UWORD slot;

	if (ring_count == DIAG_RING_LINES) {
		ring_first = (UWORD)((ring_first + 1) % DIAG_RING_LINES);
		ring_count--;
		ring_dropped++;
	}
	slot = (UWORD)((ring_first + ring_count) % DIAG_RING_LINES);
	copy_text(ring[slot], line, DIAG_LINE_MAX);
	ring_count++;
}

static void emit(const char *line) {
	BPTR file;

	if (serial)
		serial_line(line);
	if (held) {
		ring_push(line);
		return;
	}
	file = open_append();
	if (!file)
		return;
	write_line(file, line);
	Close(file);
}

static short create_log(const char *name) {
	BPTR file;

	copy_text(path, name, sizeof(path));
	file = Open((CONST_STRPTR)path, MODE_NEWFILE);
	if (!file) {
		path[0] = 0;
		return 0;
	}
	Close(file);
	return 1;
}

void diag_open(const Options *options) {
	serial = options->serial;
	path[0] = 0;
	if (options->log && !create_log(options->log_path))
		create_log(DIAG_LOG_FALLBACK);
	diag_log("amisynth build %s, code at $%08lx", (ULONG)BUILD_ID, diag_code_base());
	if (options->log && path[0])
		diag_say("amisynth: logging to %s", (ULONG)path);
	else if (options->log)
		diag_say("amisynth: cannot create %s or %s, not logging",
			(ULONG)options->log_path, (ULONG)DIAG_LOG_FALLBACK);
}

void diag_close(void) {
	diag_release();
	path[0] = 0;
}

void diag_log(const char *format, ...) {
	char line[DIAG_LINE_MAX];
	__builtin_va_list args;

	__builtin_va_start(args, format);
	format_into(line, sizeof(line), format, (APTR)args);
	__builtin_va_end(args);
	emit(line);
}

void diag_say(const char *format, ...) {
	char line[DIAG_LINE_MAX];
	__builtin_va_list args;

	__builtin_va_start(args, format);
	format_into(line, sizeof(line), format, (APTR)args);
	__builtin_va_end(args);
	emit(line);
	if (!held && DOSBase && Output()) {
		PutStr((CONST_STRPTR)line);
		PutStr((CONST_STRPTR)"\n");
	}
}

void diag_stage(UBYTE next) {
	if (next >= DIAG_STAGE_COUNT)
		return;
	stage = next;
	if (held && stage_colors[next])
		custom->color[0] = stage_colors[next];
	diag_log("stage: %s", (ULONG)stage_names[next]);
}

UBYTE diag_current_stage(void) {
	return stage;
}

const char *diag_stage_name(UBYTE which) {
	return which < DIAG_STAGE_COUNT ? stage_names[which] : "?";
}

short diag_held(void) {
	return held;
}

void diag_hold(void) {
	held = 1;
	custom->color[0] = DIAG_COLOR_TAKEN;
}

void diag_release(void) {
	BPTR file;

	if (!held)
		return;
	held = 0;
	file = open_append();
	if (file && ring_dropped) {
		char line[DIAG_LINE_MAX];
		ULONG dropped = ring_dropped;

		format_into(line, sizeof(line), "(%lu earlier lines dropped)", (APTR)&dropped);
		write_line(file, line);
	}
	for (UWORD i = 0; i < ring_count; i++) {
		if (file)
			write_line(file, ring[(ring_first + i) % DIAG_RING_LINES]);
	}
	if (file)
		Close(file);
	ring_first = 0;
	ring_count = 0;
	ring_dropped = 0;
}

ULONG diag_code_base(void) {
	return (ULONG)_start;
}
