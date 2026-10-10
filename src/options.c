#include "options.h"
#include "config.h"

#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/icon.h>
#include <workbench/workbench.h>

enum {
	ARG_LOG,
	ARG_NOLOG,
	ARG_SERIAL,
	ARG_NOTICKS,
	ARG_NOAUDIO,
	ARG_NOCATCH,
	ARG_WATCHDOG,
	ARG_SETTLE,
	ARG_CRASHTEST,
	ARG_HANGTEST,
	ARG_WINDOW,
	ARG_SCREEN,
	ARG_TAKEOVER,
	ARG_QUITAFTER,
	ARG_COUNT
};

enum { KIND_SWITCH, KIND_TEXT, KIND_NUMBER };

/* Same order and words as OPTIONS_TEMPLATE. */
static const struct {
	const char *name;
	UBYTE kind;
} arg_info[ARG_COUNT] = {
	[ARG_LOG] = { "LOG", KIND_TEXT },
	[ARG_NOLOG] = { "NOLOG", KIND_SWITCH },
	[ARG_SERIAL] = { "SERIAL", KIND_SWITCH },
	[ARG_NOTICKS] = { "NOTICKS", KIND_SWITCH },
	[ARG_NOAUDIO] = { "NOAUDIO", KIND_SWITCH },
	[ARG_NOCATCH] = { "NOCATCH", KIND_SWITCH },
	[ARG_WATCHDOG] = { "WATCHDOG", KIND_NUMBER },
	[ARG_SETTLE] = { "SETTLE", KIND_NUMBER },
	[ARG_CRASHTEST] = { "CRASHTEST", KIND_SWITCH },
	[ARG_HANGTEST] = { "HANGTEST", KIND_SWITCH },
	[ARG_WINDOW] = { "WINDOW", KIND_SWITCH },
	[ARG_SCREEN] = { "SCREEN", KIND_SWITCH },
	[ARG_TAKEOVER] = { "TAKEOVER", KIND_SWITCH },
	[ARG_QUITAFTER] = { "QUITAFTER", KIND_NUMBER }
};

struct Library *IconBase;

static struct RDArgs *args;
static struct DiskObject *icon;
static LONG numbers[ARG_COUNT];

static void options_default(Options *options) {
	options->log_path = DIAG_LOG_PATH;
	options->log = DIAG_LOG_DEFAULT;
	options->serial = 0;
	options->ticks = 1;
	options->audio = 1;
	options->catch_crashes = 1;
	options->crash_test = 0;
	options->hang_test = 0;
	options->host = HOST_DEFAULT;
	options->watchdog_seconds = DIAG_WATCHDOG_SECONDS;
	options->settle_ticks = DIAG_SETTLE_TICKS;
	options->quit_frames = 0;
}

static UWORD number_arg(LONG value, UWORD fallback) {
	LONG *number = (LONG *)value;

	if (!number)
		return fallback;
	if (*number < 0)
		return 0;
	return *number > 65535 ? 65535 : (UWORD)*number;
}

/* Fills values the way ReadArgs would, from the icon the program started from. */
static void read_tooltypes(LONG values[ARG_COUNT], const struct WBStartup *workbench) {
	const struct WBArg *self = &workbench->sm_ArgList[0];
	BPTR previous;

	IconBase = OpenLibrary((CONST_STRPTR)"icon.library", 36);
	if (!IconBase)
		return;
	previous = CurrentDir(self->wa_Lock);
	icon = GetDiskObject((STRPTR)self->wa_Name);
	CurrentDir(previous);
	if (!icon)
		return;
	for (int i = 0; i < ARG_COUNT; i++) {
		STRPTR value = FindToolType((STRPTR *)icon->do_ToolTypes, (STRPTR)arg_info[i].name);

		if (!value)
			continue;
		if (arg_info[i].kind == KIND_SWITCH)
			values[i] = 1;
		else if (arg_info[i].kind == KIND_TEXT)
			values[i] = *value ? (LONG)value : 0;
		else if (StrToLong(value, &numbers[i]) > 0)
			values[i] = (LONG)&numbers[i];
	}
}

static UBYTE host_arg(const LONG values[ARG_COUNT]) {
	if (values[ARG_TAKEOVER])
		return HOST_TAKEOVER;
	if (values[ARG_SCREEN])
		return HOST_SCREEN;
	if (values[ARG_WINDOW])
		return HOST_WINDOW;
	return HOST_DEFAULT;
}

short options_read(Options *options, const struct WBStartup *workbench) {
	LONG values[ARG_COUNT] = { 0 };

	options_default(options);
	if (workbench) {
		read_tooltypes(values, workbench);
	} else {
		args = ReadArgs((CONST_STRPTR)OPTIONS_TEMPLATE, values, 0);
		if (!args)
			return 0;
	}
	if (values[ARG_LOG]) {
		options->log_path = (const char *)values[ARG_LOG];
		options->log = 1;
	}
	if (values[ARG_NOLOG])
		options->log = 0;
	options->serial = values[ARG_SERIAL] != 0;
	options->ticks = !values[ARG_NOTICKS];
	options->audio = !values[ARG_NOAUDIO];
	options->catch_crashes = !values[ARG_NOCATCH];
	options->crash_test = values[ARG_CRASHTEST] != 0;
	options->hang_test = values[ARG_HANGTEST] != 0;
	options->host = host_arg(values);
	options->watchdog_seconds = number_arg(values[ARG_WATCHDOG], options->watchdog_seconds);
	options->settle_ticks = number_arg(values[ARG_SETTLE], options->settle_ticks);
	options->quit_frames = number_arg(values[ARG_QUITAFTER], options->quit_frames);
	return 1;
}

void options_free(void) {
	if (args)
		FreeArgs(args);
	args = 0;
	if (icon)
		FreeDiskObject(icon);
	icon = 0;
	if (IconBase)
		CloseLibrary(IconBase);
	IconBase = 0;
}

const char *options_host_name(UBYTE host) {
	switch (host) {
	case HOST_SCREEN: return "screen";
	case HOST_TAKEOVER: return "takeover";
	default: return "window";
	}
}
