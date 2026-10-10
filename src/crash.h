#pragma once

#include <exec/types.h>

/* main() arms this with __builtin_setjmp. A caught CPU exception or a stalled
   main loop jumps back there in user mode with every Paula interrupt off. */
extern void *crash_jump[5];

/* Points the CPU exception vectors in the table at vbr to crash_trap.s. */
void crash_install(APTR vbr);
/* Catches exceptions of this task only, through exec's tc_TrapCode, so AmigaOS
   can keep running. crash_uninstall undoes either install. */
void crash_install_task(void);
void crash_uninstall(void);
/* Seconds without a heartbeat before the tick interrupt rescues main. 0 turns it off. */
void crash_watchdog(UWORD seconds);
void crash_heartbeat(void);
/* Time every clock tick. frame is the interrupt frame, or 0 when the OS runs the clock. */
void crash_isr_enter(void);
void crash_isr_leave(UWORD *frame);
short crash_happened(void);
/* Writes the tick statistics and any crash record to the log. */
void crash_report(void);
