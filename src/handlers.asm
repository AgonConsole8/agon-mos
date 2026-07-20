	.assume adl = 1

	.text
	.global rst_08_handler
	.global rst_10_handler
	.global rst_18_handler
	.global rst_38_handler
	.global __default_mi_handler
	.global __default_nmi_handler

; Execute an API command
; Parameters
; - A: The API command to run
;
rst_08_handler:	CALL	mos_api
			RET.L

; Output a single character to the ESP32
; Parameters:
; - A: The character
;
rst_10_handler:	CALL	UART0_serial_PUTCH
			RET.L

; Write a block of bytes out to the ESP32
; Parameters:
; - HLU: Buffer address
; -  BC: Size of buffer
; -   A: Delimiter (only if BCU = 0)
;
rst_18_handler:	LD	E, A 			; Preserve the delimiter
			LD	A, MB			; Check if MBASE is 0
			OR	A, A 
			CALL	NZ, SET_AHL24		; No, so create a 24-bit pointer
			LD	A, B			; Check for BC = 0
			OR	C 			; Yes, so run in delimited mode?
			JR	Z, rst_18_handler_1
;
; Standard loop mode
;
rst_18_handler_0:	LD 	A, (HL)			; Fetch the character
			CALL	UART0_serial_PUTCH	; Output
			INC 	HL 			; Increment the buffer pointer
			DEC	BC 			; Decrement the loop counter
			LD	A, B 			; Is it 0?
			OR 	C 
			JR	NZ, rst_18_handler_0	; No, so loop
			RET.L
;
; Delimited mode
;
rst_18_handler_1:	LD 	A, (HL)			; Fetch the character
			CP 	E 			; Is it the delimiter?
			RET.L	Z 			; Yes, so return
			CALL	UART0_serial_PUTCH	; Output
			INC 	HL 			; Increment the buffer pointer
			JR 	rst_18_handler_1	; Loop

; Crash handler
rst_38_handler:	JP	_on_crash

; Default Non-Maskable Interrupt handler
;
__default_nmi_handler:	RETN.LIL

; Default Maskable Interrupt handler
;
__default_mi_handler:	EI
			RETI.L
