	.text
	.globl crash_trap
	.type crash_trap, function
	.globl crash_task_trap
	.type crash_task_trap, function
| exec's tc_TrapCode entry: the trap number is on top of the exception frame.
| Only faults in this task's own user-mode code are caught. Supervisor-mode
| faults belong to AmigaOS and go on to the trap code that was there before.
crash_task_trap:
	btst	#5,sp@(4)
	beq.s	1f
	move.l	crash_os_trap,sp@-
	rts
1:	addq.l	#4,sp
| Shared entry for every hooked CPU exception. The stacked format word holds
| the vector offset, so one entry serves them all. The failed code is never
| resumed: the supervisor stack is dropped and crash_resume runs in user mode.
crash_trap:
	ori.w	#0x0700,sr
	movem.l	d0-d7/a0-a6,sp@-
	move.l	usp,a0
	move.l	a0,sp@-
	pea	sp@(64)
	pea	sp@(8)
	jsr	crash_capture
	tst.b	crash_keep_os
	bne.s	2f
	move.w	#0x7fff,0xdff09a
	move.w	#0x7fff,0xdff09c
2:	movea.l	crash_ssp,sp
	clr.w	sp@-
	move.l	#crash_resume,sp@-
	clr.w	sp@-
	rte
	.size crash_trap, .-crash_trap
	.size crash_task_trap, crash_trap-crash_task_trap
