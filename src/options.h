#pragma once

#include <exec/types.h>
#include <workbench/startup.h>

/* Shell switches, or the same words as tooltypes in the program's icon.
   Anything not given uses the defaults from config.h. */
typedef struct {
	const char *log_path;
	UBYTE log;
	UBYTE serial;
	UBYTE ticks;
	UBYTE audio;
	UBYTE catch_crashes;
	/* Self tests: crash or hang on purpose once the screen has been up a while. */
	UBYTE crash_test;
	UBYTE hang_test;
	/* HOST_WINDOW, HOST_SCREEN or HOST_TAKEOVER. */
	UBYTE host;
	UWORD watchdog_seconds;
	UWORD settle_ticks;
	/* Quits by itself after this many frames, for unattended tests. 0 runs until told to stop. */
	UWORD quit_frames;
} Options;

#define OPTIONS_TEMPLATE "LOG/K,NOLOG/S,SERIAL/S,NOTICKS/S,NOAUDIO/S,NOCATCH/S,WATCHDOG/K/N,SETTLE/K/N,CRASHTEST/S,HANGTEST/S,WINDOW/S,SCREEN/S,TAKEOVER/S,QUITAFTER/K/N"

/* Reads the Shell arguments, or the icon's tooltypes when workbench is set.
   Returns 0 when the Shell arguments do not match OPTIONS_TEMPLATE. */
short options_read(Options *options, const struct WBStartup *workbench);
void options_free(void);
const char *options_host_name(UBYTE host);
