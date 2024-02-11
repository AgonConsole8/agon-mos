;
; Title:	AGON MOS - BDDP API code
; Author:	Curtis Whitley
; Created:	24/01/2024
; Last Updated:	24/01/2024
;
; Modinfo:
; 24/01/2024:	Defined jump table for initial BDPP functions


			INCLUDE "macros.inc"

			.ASSUME	ADL = 1
			
			DEFINE .STARTUP, SPACE = ROM
			SEGMENT .STARTUP
			
			XDEF	bdpp_api	

			XREF	_bdpp_is_allowed					; 0x00
			XREF	_bdpp_is_enabled					; 0x01
			XREF	_bdpp_enable						; 0x02
			XREF	_bdpp_disable						; 0x03
			XREF	_bdpp_queue_tx_app_packet			; 0x04
			XREF	_bdpp_prepare_rx_app_packet			; 0x05
			XREF	_bdpp_is_tx_app_packet_done			; 0x06
			XREF	_bdpp_is_rx_app_packet_done			; 0x07
			XREF	_bdpp_get_rx_app_packet_flags		; 0x08
			XREF	_bdpp_get_rx_app_packet_size		; 0x09
			XREF	_bdpp_stop_using_app_packet 		; 0x0A
			XREF	_bdpp_write_byte_to_drv_tx_packet	; 0x0B
			XREF	_bdpp_write_bytes_to_drv_tx_packet	; 0x0C
			XREF	_bdpp_write_drv_tx_byte_with_usage	; 0x0D
			XREF	_bdpp_write_drv_tx_bytes_with_usage	; 0x0E
			XREF	_bdpp_flush_drv_tx_packet			; 0x0F
			XREF	_bdpp_is_busy						; 0x10


; Call a BDPP API function
;
; - IX: Data address
; - IY: Size of buffer or Count of bytes
; -  D: Data byte
; -  C: Packet Flags
; -  B: Packet/Stream Index(es)
; -  A: BDPP function code
;
bdpp_api:	LD	HL, bdpp_table ; Get address of table below
			SLA	A			; Multiply A by two (equals A*2)
			SLA	A			; Multiply A by two again (equals A*4)
			ADD8U_HL 		; Add to HL (macro)
			LD	A, (HL)		; Get signature number
			INC	HL			; Increment table pointer by 2
			INC	HL			;   to reach function address
			LD.S HL, (HL)	; Get a function address from the table
			CP	1			; Signature #1?
			JR	Z, signature_1	; Go if yes
			CP	2			; Signature #2?
			JR	Z, signature_2	; Go if yes
			CP	3			; Signature #3?
			JR	Z, signature_3	; Go if yes
			CP	4			; Signature #4?
			JR	Z, signature_4	; Go if yes
			CP	5			; Signature #5?
			JR	Z, signature_5	; Go if yes
			CP	6			; Signature #6?
			JR	Z, signature_6	; Go if yes
			CP	7			; Signature #7?
			JR	Z, signature_7	; Go if yes
			CP	8			; Signature #8?
			JR	Z, signature_8	; Go if yes
			RET				; Quit

bdpp_table:	DW	1, _bdpp_is_allowed						; 0x00 signature 1
			DW	1, _bdpp_is_enabled						; 0x01 signature 1
			DW	2, _bdpp_enable							; 0x02 signature 2
			DW	1, _bdpp_disable						; 0x03 signature 1
			DW	7, _bdpp_queue_tx_app_packet			; 0x04 signature 7
			DW	6, _bdpp_prepare_rx_app_packet			; 0x05 signature 6
			DW	2, _bdpp_is_tx_app_packet_done			; 0x06 signature 2
			DW	2, _bdpp_is_rx_app_packet_done			; 0x07 signature 2
			DW	2, _bdpp_get_rx_app_packet_flags		; 0x08 signature 2
			DW	8, _bdpp_get_rx_app_packet_size			; 0x09 signature 8
			DW	2, _bdpp_stop_using_app_packet	 		; 0x0A signature 2
			DW	4, _bdpp_write_byte_to_drv_tx_packet	; 0x0B signature 4
			DW	5, _bdpp_write_bytes_to_drv_tx_packet	; 0x0C signature 5
			DW	4, _bdpp_write_drv_tx_byte_with_usage	; 0x0D signature 4
			DW	5, _bdpp_write_drv_tx_bytes_with_usage	; 0x0E signature 5
			DW	3, _bdpp_flush_drv_tx_packet			; 0x0F signature 3
			DW	1, _bdpp_is_busy						; 0x10 signature 1

; - IX: Data address
; - IY: Size of buffer or Count of bytes
; -  D: Data byte
; -  C: Packet Flags
; -  B: Packet/Stream Index
; -  A: BDPP function code

signature_1: ; BOOL fcn();
			CALL	jmp_fcn	; Call the intended function
			RET

signature_2: ; BOOL fcn(BYTE index);
			LD		C, B	; Move index to lower byte
			LD		B, 0	; Clear other parameter bits
			PUSH	BC		; Packet/Stream Index
			CALL	jmp_fcn	; Call the intended function
			POP		BC		; Packet/Stream Index
			RET

signature_3: ; void fcn();
			CALL	jmp_fcn	; Call the intended function
			RET

signature_4: ; void fcn(BYTE data);
			LD		E, D	; Move data to lower byte
			LD		D, 0	; Clear other parameter bits
			PUSH	DE		; Data byte
			CALL	jmp_fcn	; Call the intended function
			POP		DE		; Data byte
			RET

signature_5: ; void fcn(const BYTE* data, WORD size);
			PUSH	IY		; Buffer size
			PUSH	IX		; Data address
			CALL	jmp_fcn	; Call the intended function
			POP		IX		; Data address
			POP		IY		; Buffer size
			RET

signature_6: ; BOOL fcn(BYTE index, BYTE* data, WORD size);
			PUSH	IY		; Buffer size
			PUSH	IX		; Data address
			LD		C, B	; Move index to lower byte
			LD		B, 0	; Clear other parameter bits
			PUSH	BC		; Packet/Stream Indexes
			CALL	jmp_fcn	; Call the intended function
			POP		BC		; Packet/Stream Indexes
			POP		IX		; Data address
			POP		IY		; Buffer size
			RET

signature_7: ; BOOL fcn(BYTE indexes, BYTE flags, const BYTE* data, WORD size);
			PUSH	IY		; Buffer size
			PUSH	IX		; Data address
			LD		E, B	; Save Packet/Stream Indexes
			LD		B, 0	; Clear other parameter bits
			PUSH	BC		; Packet Flags
			LD		C, E	; Move indexes to lower byte
			PUSH	BC		; Packet/Stream Indexes
			CALL	jmp_fcn	; Call the intended function
			POP		BC		; Packet/Stream Indexes
			POP		BC		; Packet Flags
			POP		IX		; Data address
			POP		IY		; Buffer size
			RET

signature_8: ; WORD fcn(BYTE index);
			LD		C, B	; Move index to lower byte
			LD		B, 0	; Clear other parameter bits
			PUSH	BC		; Packet Index
			CALL	jmp_fcn	; Call the intended function
			POP		BC		; Packet Index
			RET

jmp_fcn:	JP	(HL)		; Jump to the function
