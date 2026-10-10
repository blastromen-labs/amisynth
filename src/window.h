#pragma once

#include <exec/types.h>
#include "input.h"

/* Opens a window on Workbench, or a screen of its own when own_screen is set
   or the picture does not fit on Workbench without losing pixels. */
short window_open(short own_screen);
void window_close(void);
/* Draws a SCREEN_WIDTH wide, one-bitplane picture in chip memory. */
void window_present(const UBYTE *plane);
void window_input(HostInput *input);
/* Reads the window back and counts pixels that differ from plane. */
ULONG window_check(const UBYTE *plane, ULONG *checked);
