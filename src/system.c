#include "system.h"
#include "config.h"
#include "diag.h"

#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <dos/dosextens.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <graphics/gfxbase.h>
#include <graphics/modeid.h>
#include <graphics/monitor.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <hardware/intbits.h>
#include <workbench/startup.h>

struct ExecBase *SysBase;
volatile struct Custom *custom;
struct GfxBase *GfxBase;
struct DosLibrary *DOSBase;

static const VideoTiming pal_timing = { VIDEO_PAL_LINES, VIDEO_PAL_FRAME_MHZ };
static const VideoTiming ntsc_timing = { VIDEO_NTSC_LINES, VIDEO_NTSC_FRAME_MHZ };

static const VideoTiming *video = &pal_timing;
static UWORD sync_line;
static UWORD system_ints;
static UWORD system_dma;
static UWORD system_adkcon;
static APTR system_irq;
static struct View *saved_view;
static volatile APTR vbr;

static WORD mouse_x = SCREEN_WIDTH / 2;
static WORD mouse_y = SCREEN_HEIGHT / 3;
static UBYTE mouse_raw_x;
static UBYTE mouse_raw_y;
static struct WBStartup *workbench_message;

static void workbench_accept(void) {
	struct Process *self = (struct Process *)FindTask(0);

	if (self->pr_CLI)
		return;
	WaitPort(&self->pr_MsgPort);
	workbench_message = (struct WBStartup *)GetMsg(&self->pr_MsgPort);
}

/* Workbench unloads the program as soon as it sees the reply. Forbid keeps it
   waiting until this process has ended. */
static void workbench_reply(void) {
	if (!workbench_message)
		return;
	Forbid();
	ReplyMsg(&workbench_message->sm_Message);
	workbench_message = 0;
}

static __attribute__((interrupt)) void supervisor_get_vbr(void) {
	__asm__ volatile(".short 0x4e7a, 0x0801"); /* movec.l vbr, d0 */
}

static APTR get_vbr(void) {
	if (SysBase->AttnFlags & AFF_68010)
		return (APTR)Supervisor((ULONG (*)())supervisor_get_vbr);
	return 0;
}

static void set_interrupt_handler(APTR handler) {
	*(volatile APTR *)(((UBYTE *)vbr) + 0x6c) = handler;
}

static APTR get_interrupt_handler(void) {
	return *(volatile APTR *)(((UBYTE *)vbr) + 0x6c);
}

short system_init(void) {
	UWORD raw;

	SysBase = *((struct ExecBase **)4UL);
	custom = (struct Custom *)0xdff000;
	workbench_accept();

	DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 36);
	GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 36);
	if (!DOSBase || !GfxBase)
		return 0;

	video = (GfxBase->DisplayFlags & PAL) ? &pal_timing : &ntsc_timing;
	sync_line = DISPLAY_TOP + SCREEN_HEIGHT;
	if (sync_line > video->lines - 2)
		sync_line = video->lines - 2;

	raw = custom->joy0dat;
	mouse_raw_x = (UBYTE)raw;
	mouse_raw_y = (UBYTE)(raw >> 8);
	return 1;
}

void system_shutdown(void) {
	if (GfxBase)
		CloseLibrary((struct Library *)GfxBase);
	if (DOSBase)
		CloseLibrary((struct Library *)DOSBase);
	GfxBase = 0;
	DOSBase = 0;
	workbench_reply();
}

short system_from_cli(void) {
	return !workbench_message;
}

const struct WBStartup *system_workbench(void) {
	return workbench_message;
}

const VideoTiming *system_video(void) {
	return video;
}

APTR system_vbr(void) {
	return vbr;
}

/* Only V0-V8 count: the VPOSR bits above V8 are not stable on an A1200. */
static UWORD read_beam_line(void) {
	return (UWORD)((*(volatile ULONG *)0xdff004 >> 8) & 0x1ff);
}

/* The chip bus reads VPOSR and VHPOSR one after the other, so a line change
   in between, such as 255 to 256, gives a line 256 off. Two equal reads in a
   row cannot have straddled it. */
UWORD system_beam_line(void) {
	UWORD line;

	do
		line = read_beam_line();
	while (line != read_beam_line());
	return line;
}

/* Returns once the beam is below the picture, so the next frame shows whatever
   the copper list points at now. */
void system_wait_vbl(void) {
	ULONG polls = BEAM_WAIT_POLLS;

	while (system_beam_line() >= sync_line && --polls)
		;
	polls = BEAM_WAIT_POLLS;
	while (system_beam_line() < sync_line && --polls)
		;
}

static const char *cpu_name(UWORD attn) {
	if (attn & AFF_68060)
		return "68060";
	if (attn & AFF_68040)
		return "68040";
	if (attn & AFF_68030)
		return "68030";
	if (attn & AFF_68020)
		return "68020";
	return (attn & AFF_68010) ? "68010" : "68000";
}

static const char *fpu_name(UWORD attn) {
	if (attn & AFF_FPU40)
		return "internal";
	if (attn & AFF_68882)
		return "68882";
	return (attn & AFF_68881) ? "68881" : "none";
}

/* Lisa answers $F8 and the ECS Denise $FC. An OCS Denise has no id register. */
static const char *chipset_hardware(UWORD denise_id) {
	if ((denise_id & 0xff) == 0xf8)
		return "AGA";
	return (denise_id & 0xff) == 0xfc ? "ECS" : "OCS";
}

/* Kickstart 3.x stays in ECS mode until SetPatch switches graphics to AGA. */
static const char *chipset_mode(UBYTE bits) {
	if ((bits & (GFXF_AA_ALICE | GFXF_AA_LISA)) == (GFXF_AA_ALICE | GFXF_AA_LISA))
		return "AGA";
	return (bits & GFXF_HR_AGNUS) ? "ECS" : "OCS";
}

/* The picture is a plain 15 kHz PAL or NTSC display. Any other Workbench
   monitor is usually a 31 kHz mode for a VGA-only display, which goes black
   when the program switches to 15 kHz. */
static short standard_monitor(ULONG mode) {
	ULONG monitor = mode & MONITOR_ID_MASK;

	return mode == (ULONG)INVALID_ID || monitor == DEFAULT_MONITOR_ID ||
		monitor == PAL_MONITOR_ID || monitor == NTSC_MONITOR_ID;
}

void system_describe(void) {
	UWORD attn = SysBase->AttnFlags;
	struct View *view = GfxBase->ActiView;
	struct MonitorSpec *monitor = GfxBase->current_monitor;
	ULONG mode = view && view->ViewPort ? (ULONG)GetVPModeID(view->ViewPort) : INVALID_ID;
	struct Task *self = FindTask(0);

	diag_log("exec %ld.%ld, graphics %ld.%ld, dos %ld.%ld",
		(ULONG)SysBase->LibNode.lib_Version, (ULONG)SysBase->LibNode.lib_Revision,
		(ULONG)GfxBase->LibNode.lib_Version, (ULONG)GfxBase->LibNode.lib_Revision,
		(ULONG)DOSBase->dl_lib.lib_Version, (ULONG)DOSBase->dl_lib.lib_Revision);
	diag_log("cpu %s, fpu %s, attnflags $%04lx, vbr $%08lx",
		(ULONG)cpu_name(attn), (ULONG)fpu_name(attn), (ULONG)attn, (ULONG)get_vbr());
	diag_log("chipset %s, graphics in %s mode, chiprevbits $%02lx, agnus id $%02lx, denise id $%04lx",
		(ULONG)chipset_hardware(custom->deniseid), (ULONG)chipset_mode(GfxBase->ChipRevBits0),
		(ULONG)GfxBase->ChipRevBits0, (ULONG)((custom->vposr >> 8) & 0x7f), (ULONG)custom->deniseid);
	diag_log("video %s, %ld lines, vblank %ld Hz, displayflags $%04lx",
		(ULONG)(video == &pal_timing ? "PAL" : "NTSC"), (ULONG)video->lines,
		(ULONG)SysBase->VBlankFrequency, (ULONG)GfxBase->DisplayFlags);
	diag_log("workbench mode id $%08lx, monitor %s, beamcon0 $%04lx", mode,
		(ULONG)(monitor && monitor->ms_Node.xln_Name ? monitor->ms_Node.xln_Name : "?"),
		(ULONG)(monitor ? monitor->BeamCon0 : 0));
	if (!standard_monitor(mode))
		diag_log("WARNING: Workbench is not on a 15 kHz PAL/NTSC monitor. This program shows 15 kHz PAL/NTSC.");
	diag_log("chip free %lu largest %lu, fast free %lu largest %lu",
		AvailMem(MEMF_CHIP), AvailMem(MEMF_CHIP | MEMF_LARGEST),
		AvailMem(MEMF_FAST), AvailMem(MEMF_FAST | MEMF_LARGEST));
	diag_log("started from %s, task stack %lu bytes, supervisor stack $%08lx-$%08lx",
		(ULONG)(system_from_cli() ? "Shell" : "Workbench"),
		(ULONG)self->tc_SPUpper - (ULONG)self->tc_SPLower,
		(ULONG)SysBase->SysStkLower, (ULONG)SysBase->SysStkUpper);
}

void system_take(UWORD settle_ticks) {
	if (settle_ticks)
		Delay(settle_ticks);
	Forbid();
	system_adkcon = custom->adkconr;
	system_ints = custom->intenar;
	system_dma = custom->dmaconr;
	saved_view = GfxBase->ActiView;

	LoadView(0);
	WaitTOF();
	WaitTOF();
	system_wait_vbl();
	system_wait_vbl();

	OwnBlitter();
	WaitBlit();
	Disable();

	custom->intena = 0x7fff;
	custom->intreq = 0x7fff;
	custom->dmacon = 0x7fff;

	for (int i = 0; i < 32; i++)
		custom->color[i] = 0;

	system_wait_vbl();
	system_wait_vbl();

	vbr = get_vbr();
	system_irq = get_interrupt_handler();
}

void system_free(void) {
	system_wait_vbl();
	WaitBlit();
	custom->intena = 0x7fff;
	custom->intreq = 0x7fff;
	custom->dmacon = 0x7fff;

	set_interrupt_handler(system_irq);

	custom->cop1lc = (ULONG)GfxBase->copinit;
	custom->cop2lc = (ULONG)GfxBase->LOFlist;
	custom->copjmp1 = 0x7fff;

	custom->intena = system_ints | 0x8000;
	custom->dmacon = system_dma | 0x8000;
	custom->adkcon = system_adkcon | 0x8000;

	WaitBlit();
	DisownBlitter();
	Enable();

	LoadView(saved_view);
	WaitTOF();
	WaitTOF();
	Permit();
}

/* Level 3 only carries Alice's own requests, so nothing on the expansion or
   PCMCIA side can raise it. The CIAs stay untouched for AmigaOS. */
void system_ticks_on(void (*handler)(void)) {
	custom->intena = INTF_COPER;
	custom->intreq = INTF_COPER;
	custom->intreq = INTF_COPER;
	set_interrupt_handler((APTR)handler);
	custom->intena = INTF_SETCLR | INTF_INTEN | INTF_COPER;
}

void system_ticks_off(void) {
	custom->intena = INTF_COPER;
	custom->intreq = INTF_COPER;
	custom->intreq = INTF_COPER;
	set_interrupt_handler(system_irq);
}

void system_mouse_update(void) {
	UWORD raw = custom->joy0dat;
	UBYTE x = (UBYTE)raw;
	UBYTE y = (UBYTE)(raw >> 8);

	mouse_x += (WORD)((BYTE)(x - mouse_raw_x) * 2);
	mouse_y += (WORD)((BYTE)(y - mouse_raw_y) * 2);
	mouse_raw_x = x;
	mouse_raw_y = y;

	if (mouse_x < 0)
		mouse_x = 0;
	if (mouse_x >= SCREEN_WIDTH)
		mouse_x = SCREEN_WIDTH - 1;
	if (mouse_y < 0)
		mouse_y = 0;
	if (mouse_y >= SCREEN_HEIGHT)
		mouse_y = SCREEN_HEIGHT - 1;
}

WORD system_mouse_x(void) { return mouse_x; }
WORD system_mouse_y(void) { return mouse_y; }

short system_mouse_left(void) {
	return !((*(volatile UBYTE *)0xbfe001) & 64);
}

short system_mouse_right(void) {
	return !((*(volatile UWORD *)0xdff016) & (1 << 10));
}
