	.text
	.globl sequencer_step_isr
	.type sequencer_step_isr, function
sequencer_step_isr:
	movem.l d0-d1/a0-a1,sp@-
	jsr sequencer_step_service
	movem.l sp@+,d0-d1/a0-a1
	rte
	.size sequencer_step_isr, .-sequencer_step_isr
