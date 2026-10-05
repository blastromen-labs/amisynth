#pragma once

#include <exec/types.h>

void system_init(void);
void system_shutdown(void);
void system_take(void);
void system_free(void);
APTR system_swap_exter(APTR handler);

void system_wait_vbl(void);
void system_mouse_update(void);

WORD system_mouse_x(void);
WORD system_mouse_y(void);
short system_mouse_left(void);
short system_mouse_right(void);
