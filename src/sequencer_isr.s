	.text
	.globl sequencer_tick_isr
	.type sequencer_tick_isr, function
| crash_isr_leave gets the interrupt frame, which sits above the four saved registers.
sequencer_tick_isr:
	movem.l d0-d1/a0-a1,sp@-
	jsr crash_isr_enter
	jsr sequencer_tick_service
	pea sp@(16)
	jsr crash_isr_leave
	addq.l #4,sp
	movem.l sp@+,d0-d1/a0-a1
	rte
	.size sequencer_tick_isr, .-sequencer_tick_isr
