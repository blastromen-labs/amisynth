#pragma once

#include <exec/types.h>

/* Mouse in picture pixels, buttons, and a close request from the window. */
typedef struct {
	WORD x;
	WORD y;
	UBYTE left;
	UBYTE right;
	UBYTE quit;
} HostInput;
