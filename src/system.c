#include "system.h"
#include "config.h"

#include <proto/exec.h>
#include <proto/graphics.h>
#include <dos/dosextens.h>
#include <exec/execbase.h>
#include <graphics/gfxbase.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <hardware/intbits.h>
#include <workbench/startup.h>

struct ExecBase *SysBase;
volatile struct Custom *custom;
struct GfxBase *GfxBase;

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

	GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 0);
	if (!GfxBase)
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
	GfxBase = 0;
	workbench_reply();
}

const VideoTiming *system_video(void) {
	return video;
}

static UWORD beam_line(void) {
	return (UWORD)((*(volatile ULONG *)0xdff004 >> 8) & 0x7ff);
}

/* Returns once the beam is below the picture, so the next frame shows whatever
   the copper list points at now. */
void system_wait_vbl(void) {
	ULONG polls = BEAM_WAIT_POLLS;

	while (beam_line() >= sync_line && --polls)
		;
	polls = BEAM_WAIT_POLLS;
	while (beam_line() < sync_line && --polls)
		;
}

void system_take(void) {
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
