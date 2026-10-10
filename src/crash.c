#include "crash.h"
#include "config.h"
#include "diag.h"
#include "system.h"

#include <proto/exec.h>
#include <exec/execbase.h>
#include <hardware/custom.h>

#define SR_SUPERVISOR 0x2000

enum { CRASH_NONE, CRASH_EXCEPTION, CRASH_WATCHDOG };

typedef struct {
	UWORD kind;
	UWORD vector;
	UWORD sr;
	UWORD format;
	UWORD nested;
	UBYTE stage;
	ULONG pc;
	ULONG fault_address;
	ULONG usp;
	ULONG ticks;
	ULONG regs[15];
	UWORD frame[16];
	ULONG stack_at;
	ULONG stack[DIAG_STACK_LONGS];
} CrashRecord;

extern struct ExecBase *SysBase;
extern volatile struct Custom *custom;
extern void crash_trap(void);
extern void crash_task_trap(void);

/* CPU exceptions that mean a bug: bus and address errors, illegal and
   unimplemented instructions, divide by zero, CHK, TRAPV, privilege violation,
   coprocessor protocol, format error, uninitialised and spurious interrupts. */
static const UBYTE hooked_vectors[] = { 2, 3, 4, 5, 6, 7, 8, 10, 11, 13, 14, 15, 24 };

/* Exception frame sizes by format nibble, 68010 to 68060. */
static const UBYTE frame_sizes[16] = { 8, 8, 12, 12, 16, 8, 8, 60, 58, 20, 32, 92, 8, 8, 8, 8 };

void *crash_jump[5];
__attribute__((used, externally_visible)) ULONG crash_ssp;
/* Set while AmigaOS runs: the trap leaves the interrupts on, and faults in
   supervisor mode go on to the task's previous trap code. */
__attribute__((used, externally_visible)) UBYTE crash_keep_os;
__attribute__((used, externally_visible)) APTR crash_os_trap;
static struct Task *trapped_task;

static APTR saved_vectors[sizeof(hooked_vectors)];
static volatile APTR *vector_table;
static UBYTE installed;
static CrashRecord record;

static volatile ULONG heartbeat;
static ULONG heartbeat_seen;
static UWORD stalled_ticks;
static UWORD watchdog_ticks;
static UWORD watchdog_seconds;
static volatile ULONG ticks;
static UWORD tick_began;
static UWORD longest_tick;
static ULONG overran_ticks;

static const char *vector_name(UWORD vector) {
	switch (vector) {
	case 2: return "bus error";
	case 3: return "address error";
	case 4: return "illegal instruction";
	case 5: return "divide by zero";
	case 6: return "CHK";
	case 7: return "TRAPV";
	case 8: return "privilege violation";
	case 10: return "line A instruction";
	case 11: return "line F instruction";
	case 13: return "coprocessor protocol violation";
	case 14: return "format error";
	case 15: return "uninitialised interrupt";
	case 24: return "spurious interrupt";
	default: return "exception";
	}
}

/* Where each frame format keeps the address that failed, or -1. */
static int fault_offset(UWORD format) {
	switch (format) {
	case 2: case 3: case 4: return 8;
	case 7: return 20;
	case 10: case 11: return 16;
	default: return -1;
	}
}

static ULONG read_usp(void) {
	ULONG usp;

	__asm volatile("move.l %%usp,%0" : "=a"(usp));
	return usp;
}

static void copy_stack(ULONG at) {
	record.stack_at = at;
	if (!at || (at & 1))
		return;
	for (UWORD i = 0; i < DIAG_STACK_LONGS; i++)
		record.stack[i] = ((const ULONG *)at)[i];
}

static void begin_record(UWORD kind, const UWORD *frame) {
	record.kind = kind;
	record.sr = frame[0];
	record.pc = *(const ULONG *)(frame + 1);
	record.format = (UWORD)(frame[3] >> 12);
	record.vector = (UWORD)((frame[3] & 0xfff) >> 2);
	record.stage = diag_current_stage();
	record.ticks = ticks;
}

static void release_task_trap(void) {
	if (trapped_task)
		trapped_task->tc_TrapCode = crash_os_trap;
	trapped_task = 0;
}

/* A second fault while cleaning up goes to AmigaOS instead of coming back here. */
__attribute__((used, externally_visible, noreturn, noinline))
void crash_resume(void) {
	release_task_trap();
	__builtin_longjmp(crash_jump, 1);
}

/* Called from crash_trap.s in supervisor mode. The trap then drops the
   supervisor stack to crash_ssp and enters crash_resume. */
__attribute__((used, externally_visible, noinline))
void crash_capture(const ULONG *regs, UWORD *frame, ULONG usp) {
	UWORD format = (UWORD)(frame[3] >> 12);
	ULONG frame_end = (ULONG)frame + frame_sizes[format];
	short from_user = !(frame[0] & SR_SUPERVISOR);
	int offset = fault_offset(format);

	if (!crash_keep_os)
		custom->color[0] = DIAG_COLOR_CRASH;
	crash_ssp = from_user ? frame_end : (ULONG)SysBase->SysStkUpper;
	if (record.kind != CRASH_NONE) {
		record.nested++;
		return;
	}
	begin_record(CRASH_EXCEPTION, frame);
	record.usp = usp;
	record.fault_address = offset >= 0 ? *(const ULONG *)((const UBYTE *)frame + offset) : 0;
	for (UWORD i = 0; i < 15; i++)
		record.regs[i] = regs[i];
	for (UWORD i = 0; i < 16 && i < frame_sizes[format] / 2; i++)
		record.frame[i] = frame[i];
	copy_stack(from_user ? usp : frame_end);
}

/* Both run around every clock tick. frame is the interrupt frame from
   sequencer_isr.s, or 0 under an OS clock, which needs no watchdog. */
__attribute__((used, externally_visible, noinline))
void crash_isr_enter(void) {
	tick_began = system_beam_line();
}

__attribute__((used, externally_visible, noinline))
void crash_isr_leave(UWORD *frame) {
	UWORD lines = system_video()->lines;
	UWORD spent = (UWORD)((system_beam_line() + lines - tick_began) % lines);

	ticks++;
	if (spent > longest_tick)
		longest_tick = spent;
	if (spent >= lines / CLOCK_TICKS_PER_FRAME)
		overran_ticks++;

	if (!frame || !watchdog_ticks || record.kind != CRASH_NONE)
		return;
	if (heartbeat != heartbeat_seen) {
		heartbeat_seen = heartbeat;
		stalled_ticks = 0;
		return;
	}
	/* Main can only be redirected while it runs its own user-mode code. */
	if (++stalled_ticks < watchdog_ticks || (frame[0] & SR_SUPERVISOR))
		return;
	begin_record(CRASH_WATCHDOG, frame);
	record.usp = read_usp();
	copy_stack(record.usp);
	custom->color[0] = DIAG_COLOR_WATCHDOG;
	custom->intena = 0x7fff;
	custom->intreq = 0x7fff;
	*(ULONG *)(frame + 1) = (ULONG)crash_resume;
}

void crash_install(APTR vbr) {
	vector_table = (volatile APTR *)(ULONG)vbr;
	crash_ssp = (ULONG)SysBase->SysStkUpper;
	crash_keep_os = 0;
	for (UWORD i = 0; i < sizeof(hooked_vectors); i++) {
		saved_vectors[i] = vector_table[hooked_vectors[i]];
		vector_table[hooked_vectors[i]] = (APTR)crash_trap;
	}
	installed = 1;
}

void crash_install_task(void) {
	trapped_task = FindTask(0);
	crash_os_trap = trapped_task->tc_TrapCode;
	crash_keep_os = 1;
	trapped_task->tc_TrapCode = (APTR)crash_task_trap;
}

void crash_uninstall(void) {
	release_task_trap();
	if (!installed)
		return;
	for (UWORD i = 0; i < sizeof(hooked_vectors); i++)
		vector_table[hooked_vectors[i]] = saved_vectors[i];
	installed = 0;
}

void crash_watchdog(UWORD seconds) {
	ULONG limit = ((ULONG)seconds * system_video()->frame_mhz * CLOCK_TICKS_PER_FRAME) / 1000UL;

	watchdog_ticks = limit > 65535UL ? 65535 : (UWORD)limit;
	watchdog_seconds = seconds;
	stalled_ticks = 0;
}

void crash_heartbeat(void) {
	heartbeat++;
}

short crash_happened(void) {
	return record.kind != CRASH_NONE;
}

static void log_code_address(const char *label, ULONG address) {
	ULONG base = diag_code_base();

	diag_log("%s $%08lx = code+$%lx", (ULONG)label, address, address - base);
}

static void log_longs(const char *label, const ULONG *values, UWORD count) {
	for (UWORD i = 0; i < count; i += 4)
		diag_log("%s%02ld: %08lx %08lx %08lx %08lx", (ULONG)label, (ULONG)i,
			values[i], values[i + 1], values[i + 2], values[i + 3]);
}

void crash_report(void) {
	UWORD lines = system_video()->lines;

	diag_log("ticks %lu, longest tick handler %lu of %lu lines, %lu overran",
		ticks, (ULONG)longest_tick, (ULONG)(lines / CLOCK_TICKS_PER_FRAME), overran_ticks);
	if (record.kind == CRASH_NONE)
		return;

	if (record.kind == CRASH_EXCEPTION)
		diag_say("amisynth: CRASH %s (vector %ld) in stage '%s', see the log",
			(ULONG)vector_name(record.vector), (ULONG)record.vector, (ULONG)diag_stage_name(record.stage));
	else
		diag_say("amisynth: WATCHDOG no frame drawn for %ld s in stage '%s', see the log",
			(ULONG)watchdog_seconds, (ULONG)diag_stage_name(record.stage));
	log_code_address("crash.pc", record.pc);
	diag_log("crash.sr $%04lx format $%lx tick %lu nested %lu",
		(ULONG)record.sr, (ULONG)record.format, record.ticks, (ULONG)record.nested);
	if (record.kind == CRASH_EXCEPTION) {
		diag_log("crash.fault $%08lx", record.fault_address);
		log_longs("crash.d", record.regs, 8);
		diag_log("crash.a00: %08lx %08lx %08lx %08lx", record.regs[8], record.regs[9], record.regs[10], record.regs[11]);
		diag_log("crash.a04: %08lx %08lx %08lx usp %08lx", record.regs[12], record.regs[13], record.regs[14], record.usp);
		diag_log("crash.frame: %04lx %04lx %04lx %04lx %04lx %04lx %04lx %04lx",
			(ULONG)record.frame[0], (ULONG)record.frame[1], (ULONG)record.frame[2], (ULONG)record.frame[3],
			(ULONG)record.frame[4], (ULONG)record.frame[5], (ULONG)record.frame[6], (ULONG)record.frame[7]);
	}
	diag_log("crash.stack at $%08lx", record.stack_at);
	log_longs("crash.stack", record.stack, DIAG_STACK_LONGS);
}
