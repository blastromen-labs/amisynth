#pragma once

#include <exec/types.h>
#include "input.h"
#include "options.h"

/* Where the program runs, picked by options->host: a Workbench window, an
   Intuition screen of its own, or the whole machine. host_stop undoes as much
   of host_start as got done, so it is safe after a failure or a crash. */
short host_start(const Options *options);
void host_stop(void);
void host_wait_frame(void);
/* Shows a finished SCREEN_WIDTH wide, one-bitplane picture. */
void host_present(const UBYTE *plane);
void host_read_input(HostInput *input);
/* Logs whether the window really shows plane. Nothing to check in takeover. */
void host_check(const UBYTE *plane);
