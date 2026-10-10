#include "ticker.h"
#include "config.h"
#include "system.h"

#include <proto/cia.h>
#include <proto/exec.h>
#include <exec/execbase.h>
#include <exec/interrupts.h>
#include <hardware/cia.h>
#include <resources/cia.h>
#include <hardware/intbits.h>

extern struct ExecBase *SysBase;

enum { SOURCE_NONE, SOURCE_TIMER_A, SOURCE_TIMER_B, SOURCE_VBLANK };

static volatile struct CIA *const ciab_chip = (volatile struct CIA *)0xbfd000;

static void (*tick_handler)(void);
static struct Library *ciab_resource;
static UWORD latch;
static UBYTE source;
static UBYTE running;

/* Interrupt code: d2-d7/a2-a4 survive a C function, and 0 sets the Z flag
   that tells the vertical blank chain to go on. */
static int cia_entry(void) {
	tick_handler();
	return 0;
}

static int vblank_entry(void) {
	for (int i = 0; i < CLOCK_TICKS_PER_FRAME; i++)
		tick_handler();
	return 0;
}

static struct Interrupt tick_interrupt = {
	.is_Node = { .ln_Type = NT_INTERRUPT, .ln_Pri = 0, .ln_Name = (char *)WINDOW_TITLE }
};

static UWORD timer_latch(ULONG rate_mhz) {
	ULONG count = (SysBase->ex_EClockFrequency * 1000UL + rate_mhz / 2) / rate_mhz;

	return count > 65535UL ? 65535 : (UWORD)count;
}

static void start_timer(UBYTE timer, UWORD count) {
	if (timer == CIAICRB_TA) {
		ciab_chip->ciacra &= (UBYTE)(CIACRAF_TODIN | CIACRAF_SPMODE);
		ciab_chip->ciatalo = (UBYTE)count;
		ciab_chip->ciatahi = (UBYTE)(count >> 8);
		ciab_chip->ciacra |= (UBYTE)(CIACRAF_LOAD | CIACRAF_START);
	} else {
		ciab_chip->ciacrb &= (UBYTE)CIACRBF_ALARM;
		ciab_chip->ciatblo = (UBYTE)count;
		ciab_chip->ciatbhi = (UBYTE)(count >> 8);
		ciab_chip->ciacrb |= (UBYTE)(CIACRBF_LOAD | CIACRBF_START);
	}
}

static void stop_timer(UBYTE timer) {
	if (timer == CIAICRB_TA)
		ciab_chip->ciacra &= (UBYTE)~CIACRAF_START;
	else
		ciab_chip->ciacrb &= (UBYTE)~CIACRBF_START;
}

static short claim_cia(UBYTE timer) {
	if (!ciab_resource)
		return 0;
	tick_interrupt.is_Code = (void (*)())cia_entry;
	if (AddICRVector(ciab_resource, timer, &tick_interrupt))
		return 0;
	return 1;
}

static UBYTE source_timer(void) {
	return source == SOURCE_TIMER_A ? CIAICRB_TA : CIAICRB_TB;
}

ULONG ticker_claim(ULONG rate_mhz, void (*tick)(void)) {
	static const UBYTE timers[] = { CIAICRB_TA, CIAICRB_TB };

	tick_handler = tick;
	latch = timer_latch(rate_mhz);
	ciab_resource = (struct Library *)OpenResource((CONST_STRPTR)CIABNAME);
	for (UWORD i = 0; i < sizeof(timers); i++) {
		if (!claim_cia(timers[i]))
			continue;
		source = timers[i] == CIAICRB_TA ? SOURCE_TIMER_A : SOURCE_TIMER_B;
		return (SysBase->ex_EClockFrequency * 1000UL) / latch;
	}
	tick_interrupt.is_Code = (void (*)())vblank_entry;
	source = SOURCE_VBLANK;
	return system_video()->frame_mhz * CLOCK_TICKS_PER_FRAME;
}

void ticker_run(void) {
	if (source == SOURCE_VBLANK) {
		AddIntServer(INTB_VERTB, &tick_interrupt);
	} else if (source != SOURCE_NONE) {
		start_timer(source_timer(), latch);
		AbleICR(ciab_resource, CIAICRF_SETCLR | (1 << source_timer()));
	}
	running = source != SOURCE_NONE;
}

void ticker_stop(void) {
	if (source == SOURCE_VBLANK) {
		if (running)
			RemIntServer(INTB_VERTB, &tick_interrupt);
	} else if (source != SOURCE_NONE) {
		stop_timer(source_timer());
		AbleICR(ciab_resource, 1 << source_timer());
		RemICRVector(ciab_resource, source_timer(), &tick_interrupt);
	}
	source = SOURCE_NONE;
	running = 0;
}

const char *ticker_source(void) {
	switch (source) {
	case SOURCE_TIMER_A: return "CIA-B timer A";
	case SOURCE_TIMER_B: return "CIA-B timer B";
	case SOURCE_VBLANK: return "vertical blank";
	default: return "none";
	}
}
