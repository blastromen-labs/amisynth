#pragma once

#include <exec/types.h>

typedef struct {
	UWORD lines;
	ULONG frame_mhz;
} VideoTiming;

short system_init(void);
void system_shutdown(void);
void system_take(void);
void system_free(void);
const VideoTiming *system_video(void);
void system_ticks_on(void (*handler)(void));
void system_ticks_off(void);

void system_wait_vbl(void);
void system_mouse_update(void);

WORD system_mouse_x(void);
WORD system_mouse_y(void);
short system_mouse_left(void);
short system_mouse_right(void);
