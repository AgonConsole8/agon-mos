	.assume	adl = 1	

	include "equs.inc"
	include "macros.inc"

	.global __init
	.global _set_vector

section .text
__init:
		ld a, 0xFF
		out0 (PB_DDR), a         ; GPIO
		out0 (PC_DDR), a         ;
		out0 (PD_DDR), a         ;
		ld a, 0
		out0 (PB_ALT1), a        ;
		out0 (PC_ALT1), a        ;
		out0 (PD_ALT1), a        ;
		out0 (PB_ALT2), a        ;
		out0 (PC_ALT2), a        ;
		out0 (PD_ALT2), a        ;
		out0 (TMR0_CTL), a       ; timers
		out0 (TMR1_CTL), a       ;
		out0 (TMR2_CTL), a       ;
		out0 (TMR3_CTL), a       ;
		out0 (TMR4_CTL), a       ;
		out0 (TMR5_CTL), a       ;
		out0 (UART0_IER), a      ; UARTs
		out0 (UART1_IER), a      ;
		out0 (I2C_CTL), a        ; I2C
		out0 (FLASH_IRQ), a      ; Flash
		ld a, 4
		out0 (SPI_CTL), a        ; SPI
		in0 a, (RTC_CTRL)        ; RTC, Writing to the RTC_CTRL register also
		and a, 0xBE               ;      resets the RTC count prescaler allowing
		out0 (RTC_CTRL), a       ;      the RTC to be synchronized to another time source

		; Configure external memory/io
		ld a, __CS0_LBR_INIT_PARAM
		out0 (CS0_LBR), a
		ld a, __CS0_UBR_INIT_PARAM
		out0 (CS0_UBR), a
		ld a, __CS0_BMC_INIT_PARAM
		out0 (CS0_BMC), a
		ld a, __CS0_CTL_INIT_PARAM
		out0 (CS0_CTL), a

		ld a, __CS1_LBR_INIT_PARAM
		out0 (CS1_LBR), a
		ld a, __CS1_UBR_INIT_PARAM
		out0 (CS1_UBR), a
		ld a, __CS1_BMC_INIT_PARAM
		out0 (CS1_BMC), a
		ld a, __CS1_CTL_INIT_PARAM
		out0 (CS1_CTL), a

		ld a, __CS2_LBR_INIT_PARAM
		out0 (CS2_LBR), a
		ld a, __CS2_UBR_INIT_PARAM
		out0 (CS2_UBR), a
		ld a, __CS2_BMC_INIT_PARAM
		out0 (CS2_BMC), a
		ld a, __CS2_CTL_INIT_PARAM
		out0 (CS2_CTL), a

		ld a, __CS3_LBR_INIT_PARAM
		out0 (CS3_LBR), a
		ld a, __CS3_UBR_INIT_PARAM
		out0 (CS3_UBR), a
		ld a, __CS3_BMC_INIT_PARAM
		out0 (CS3_BMC), a
		ld a, __CS3_CTL_INIT_PARAM
		out0 (CS3_CTL), a

		; Enable internal memory
		ld a, __FLASH_ADDR_U_INIT_PARAM
		out0 (FLASH_ADDR_U), a
		ld a, __FLASH_CTL_INIT_PARAM
		out0 (FLASH_CTRL), a

		ld a, __RAM_ADDR_U_INIT_PARAM
		out0 (RAM_ADDR_U), a
		ld a, __RAM_CTL_INIT_PARAM
		out0 (RAM_CTL), a

		; Protect flash pages by default
		ld        A, 0xb6    ; unlock
		out0    (0xF5), A
		ld        A, 0x49
		out0    (0xF5), A
		ld        A, 0xff    ; protect all pages
		out0    (0xFA), A			

		; Setup Stack Pointer
		; (Temporary stack for init only - in on-chip sram)
		ld sp, 0xb80000

		; Detect preserved ram
		ld hl, (_warmboot_magic)
		ld de, 0xabcdef
		or a
		sbc hl, de
		jr z, .skip_memclr
		; Cold boot: wipe external 512k RAM (initialize to $ff (rst $38))
		ld hl, 0x40000
		ld de, 0x40001
		ld bc, 0x80000 - 1
		ld (hl), 0xff
		ldir
		; Cold boot: wipe internal SRAM
		ld hl, 0x0b7e000
		ld de, 0x0b7e001
		ld bc, 0x2000 - 1
		ld (hl), 0xff
		ldir
	.skip_memclr:
		; Detect hardware reset
		ld a, i			; Register I should be 0 on hard CPU reset
		or a, a		
		ld a, 1			; Set to 1 if CPU reset
		jr z, 1f
		dec a			; Otherwise set to 0
	1:	push af 		; Stack AF, will store later as __c_startup clears globals area

		; Initialize the interrupt vector table
		call __init_default_vectors

		; Set rising edge triggered interrupt for vblank GPIO
		ld	a, GPIOMODE_INTRE
		ld	b, 2
		CALL	GPIOB_SETMODE

		; copy .data section to RAM
		ld bc, ___data_len
		ld de, ___data_start
		ld hl, ___rodata_end
		call ldir_handle_zerolen

		; clear .bss
		ld hl, ___bss_start
		ld de, ___bss_start + 1
		ld bc, ___bss_len - 1
		ld (hl), 0
		call ldir_handle_zerolen

		pop af			; Pop the hardReset value
		ld (_hardReset), a	; And store

		ld hl, _warmboot_magic  ; Set warmboot magic value
		ld de, 0xabcdef
		ld (hl), de

		; Normal stack
		ld sp, 0xc0000

		call	_main 				; int main(void)
	.loopforever:
		jr .loopforever

ldir_handle_zerolen:	; bc=count, hl=src, de=dest
		; Do nothing if count==0
		push hl
		ld hl,0
		or a
		adc hl,bc
		pop hl
		ret z
		ldir
		ret


; Save Interrupt State
;
	.MACRO SAVEIMASK		
			LD	A, I		; Sets parity bit to value of IEF2
			PUSH	AF
			DI			; Disable interrupts while loading table 
	.endm

; Restore Interrupt State
;
	.MACRO RESTOREIMASK
			POP	AF
			JP	PO, 1f		; Parity bit is IEF2
			EI
		1:
	.endm

					
; Initialize all potential interrupt vector locations with a known
; default handler.
;
; void _init_default_vectors(void);
;
__init_default_vectors:
_init_default_vectors:	PUSH	AF
			SAVEIMASK
			LD	HL, __default_mi_handler
			LD	A, 0xC3
			LD 	(__2nd_jump_table), A		; Place jp opcode
			LD 	(__2nd_jump_table + 1), HL	; __default_hndlr
			LD 	HL, __2nd_jump_table
			LD	DE, __2nd_jump_table + 4
			LD	BC, NVECTORS * 4 - 4
			LDIR
			IM	2
			LD 	A, __vector_table >> 8
			LD 	I, A				; Load interrupt vector base
			RESTOREIMASK
			POP	AF
			RET

; Installs a user interrupt handler in the 2nd interrupt vector jump table
;
; void * _set_vector(unsigned int vector, void(*handler)(void));
;
__set_vector:
_set_vector:		PUSH	IY
			LD	IY, 0
			ADD	IY, SP				; Standard prologue
			PUSH	AF
			SAVEIMASK
			LD	BC, 0				; Clear BC
			LD	B, 2				; Calculate 2nd jump table offset
			LD	C, (IY + 6)			; Vector offset
			MLT	BC				; BC is 2nd jp table offset
			LD	HL, __2nd_jump_table
			ADD	HL, BC				; HL is location of jp in 2nd jp table
			LD 	(HL), 0xC3			; Place jp opcode just in case
			INC	HL				; HL is jp destination address
			LD	BC, (IY + 9)			; BC is isr address
			LD 	DE, (HL)			; Save previous handler
			LD 	(HL), BC			; Store new isr address
			PUSH	DE
			POP	HL				; Return previous handler
			RESTOREIMASK
			POP	AF
			LD 	SP, IY				; Standard epilogue
			POP	IY
			RET

section .bss
		.equ NVECTORS, 48			; Number of interrupt vectors

		.global __2nd_jump_table
__2nd_jump_table:
		.ds	NVECTORS * 4
_warmboot_magic:
		.ds	3
