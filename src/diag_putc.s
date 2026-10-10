	.text
	.globl diag_put_char
	.type diag_put_char, function
| RawDoFmt hands each character over in d0 and the DiagSink in a3.
| Characters past the end of the buffer are dropped.
diag_put_char:
	move.l	a0,sp@-
	movea.l	a3@,a0
	cmpa.l	a3@(4),a0
	bcc.s	1f
	move.b	d0,a0@+
	move.l	a0,a3@
1:	movea.l	sp@+,a0
	rts
	.size diag_put_char, .-diag_put_char
