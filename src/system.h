#pragma once

#include <exec/types.h>

typedef struct {
	UWORD lines;
	ULONG frame_mhz;
} VideoTiming;

short system_init(void);
void system_shutdown(void);
short system_from_cli(void);
/* The Workbench start message, or 0 when started from the Shell. */
const struct WBStartup *system_workbench(void);
/* Logs the machine: Kickstart, CPU, chipset, Workbench monitor, memory. */
void system_describe(void);
/* Waits settle_ticks (1/50 s) for disk activity to finish, then owns the machine. */
void system_take(UWORD settle_ticks);
void system_free(void);
const VideoTiming *system_video(void);
APTR system_vbr(void);
UWORD system_beam_line(void);
void system_ticks_on(void (*handler)(void));
void system_ticks_off(void);

void system_wait_vbl(void);
void system_mouse_update(void);

WORD system_mouse_x(void);
WORD system_mouse_y(void);
short system_mouse_left(void);
short system_mouse_right(void);
