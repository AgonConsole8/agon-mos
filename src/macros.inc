; vim:ft=ez80
; Title:	AGON MOS - Useful Macros
; Author:	Dean Belfield
; Created:	15/07/2022
; Last Updated:	08/06/2023
;
; Modinfo:
; 08/08/2022:	Added GET_GPIO
; 09/09/2022:	Commented
; 08/06/2023:	Add timer-related macros


; Add A to HL (unsigned)
;
	.MACRO ADD8U_HL
		ADD	A, L 
		LD	L, A 
		ADC	A, H
		SUB	L
		LD	H, A 
	.ENDM

; Set a GPIO register
; Parameters:
; - REG: Register to set
; - VAL: Bit(s) to set (1: set, 0: ignore)
;
	.MACRO	SET_GPIO REG, VAL
		IN0	A,(\REG)
		OR	\VAL
		OUT0	(\REG),A
	.ENDM

; Reset a GPIO register
; Parameters:
; - REG: Register to set
; - VAL: Bit(s) to reset (1: reset, 0: ignore)
;	
	.MACRO	RES_GPIO		REG, VAL
		PUSH	BC
		LD	A, \VAL
		CPL
		LD	C, A
		IN0	A,(\REG)
		AND	C
		OUT0	(\REG),A
		POP	BC
	.ENDM

; Get a GPIO register
; Parameters:
; - REG: Register to test
; - VAL: Bit(s) to test
;	
	.MACRO	GET_GPIO REG, VAL
		IN0	A,(\REG)
		TST	A, \VAL
	.ENDM

; Set a TIMER to the value in register BC
; Parameters:
; - TIMER: Timer to set
;
	.MACRO TIMER_SET_BC TIMER
		.IF	\TIMER >= 0 && \TIMER <= 5
			LD	A,0xC
			OUT0	(TMR\TIMER\()_CTL),A
			OUT0	(TMR\TIMER\()_RR_L),C
			OUT0	(TMR\TIMER\()_RR_H),B
		.ELSE
			.ERROR
		.ENDIF
	.ENDM

; Set a timer to the given *constant* value in milliseconds
; Parameters:
; - TIMER:    Timer set set
; - DELAY_MS: Constant delay in milliseconds
;
	.MACRO	TIMER_SET		TIMER,DELAY_MS
			.IF	\DELAY_MS < 1 || \DELAY_MS > 910
				ERROR
			.ENDIF
			LD		BC,MASTERCLOCK / 1000 / 256 * \DELAY_MS
			TIMER_SET_BC	0
	.ENDM

;Start a timer
; Parameters:
; - TIMER: Timer to start
;
	.MACRO	TIMER_START		TIMER
			.IF	\TIMER >= 0 && \TIMER <= 5
				IN0	A,(TMR\TIMER\()_CTL)
				OR	A,0x03
				OUT0	(TMR\TIMER\()_CTL),A
			.ENDIF
	.ENDM

; Reset a timer to stop it running
; Leaves the rest of the timer configuration unchanged so it can be restarted
; Parameters:
; - TIMER: Timer to reset
;
MACRO	TIMER_RESET		TIMER
		IF	\TIMER >= 0 && \TIMER <= 5
			IN0	A,(TMR\TIMER\()_CTL)
			AND	A,0b11111100
			OUT0	(TMR\TIMER\()_CTL),A
		ELSE
			ERROR
		ENDIF
ENDM

; Test if the given timer has expired
; Parameters:
; - TIMER: Timer to test
; Output:
; - AF: Carry Flag set or clear depending on whether timer has expired
;
	.MACRO	TIMER_EXP		TIMER
		.IF	\TIMER >= 0 && \TIMER <= 5
			IN0	A,(TMR\TIMER\()_CTL)
		.ELSE
			ERROR
		.ENDIF
		RLA
	.ENDM

; Wait for a timer to count down to zero
; Parameters:
; - TIMER: Timer to wait on
;
MACRO	TIMER_WAIT		TIMER
1:			TIMER_EXP	\TIMER
			JR		NC,1b
ENDM

; Delay for the given (constant) number of milliseconds using *Timer 0*
; Parameters:
; - DELAY_MS: Number of milliseconds to delay
;
; NB: Can wait between 1 and 910 milliseconds based on 18.432MHz clock
;
	.MACRO	DELAY_MS DELAY_MS
			IF	\DELAY_MS < 1 || \DELAY_MS > 910
				ERROR
			ENDIF
			LD	BC,MASTERCLOCK / 1000 / 256 * \DELAY_MS
			PUSH	BC
			CALL	_timer0_delay
			POP	BC
	.ENDM
