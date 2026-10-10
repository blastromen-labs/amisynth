#pragma once

#include <exec/types.h>
#include "options.h"

/* Startup and shutdown steps, in the order main() runs them. */
enum {
	DIAG_STAGE_START = 0,
	DIAG_STAGE_SEQUENCER,
	DIAG_STAGE_SYNTH,
	DIAG_STAGE_DISPLAY,
	DIAG_STAGE_VOICE,
	DIAG_STAGE_TAKEOVER,
	DIAG_STAGE_HOOKS,
	DIAG_STAGE_WINDOW,
	DIAG_STAGE_SCREEN,
	DIAG_STAGE_AUDIO,
	DIAG_STAGE_CLOCK,
	DIAG_STAGE_RUNNING,
	DIAG_STAGE_SHUTDOWN,
	DIAG_STAGE_COUNT
};

/* Format strings follow exec's RawDoFmt: numbers need %ld, %lu or %lx. */
void diag_open(const Options *options);
void diag_close(void);
void diag_log(const char *format, ...);
/* Logs the line and also prints it in the Shell window. */
void diag_say(const char *format, ...);
void diag_stage(UBYTE stage);
UBYTE diag_current_stage(void);
const char *diag_stage_name(UBYTE stage);
/* While held, lines stay in memory and the screen colour shows the stage. */
void diag_hold(void);
void diag_release(void);
short diag_held(void);
ULONG diag_code_base(void);
