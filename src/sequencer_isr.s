	.text
	.globl sequencer_tick_isr
	.type sequencer_tick_isr, function
sequencer_tick_isr:
	movem.l d0-d1/a0-a1,sp@-
	jsr sequencer_tick_service
	movem.l sp@+,d0-d1/a0-a1
	rte
	.size sequencer_tick_isr, .-sequencer_tick_isr
