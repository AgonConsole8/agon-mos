;
; Title:	AGON MOS - BDDP API code
; Author:	Curtis Whitley
; Created:	24/01/2024
; Last Updated:	24/01/2024
;
; Modinfo:
; 24/01/2024:	Defined jump table for initial BDPP functions


			.ASSUME	ADL = 1
			
			DEFINE .STARTUP, SPACE = ROM
			SEGMENT .STARTUP
			
			XDEF	bddp_api	

			XREF	SWITCH_A		; In misc.asm
			XREF	SET_AHL24
			XREF	GET_AHL24
			XREF	SET_ADE24

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
			XREF	_bdpp_write_drv_tx_data_with_usage	; 0x0D
			XREF	_bdpp_flush_drv_tx_packet			; 0x0E

; Call a BDPP API function
;  A: function to call
;
bddp_api:	CALL	SWITCH_A		; Switch on this table
			DW		_bdpp_is_allowed
			DW		_bdpp_is_enabled
			DW		_bdpp_enable
			DW		_bdpp_disable
			DW		_bdpp_queue_tx_app_packet
			DW		_bdpp_prepare_rx_app_packet
			DW		_bdpp_is_tx_app_packet_done
			DW		_bdpp_is_rx_app_packet_done
			DW		_bdpp_get_rx_app_packet_flags
			DW		_bdpp_get_rx_app_packet_size
			DW		_bdpp_stop_using_app_packet 
			DW		_bdpp_write_byte_to_drv_tx_packet
			DW		_bdpp_write_bytes_to_drv_tx_packet
			DW		_bdpp_write_drv_tx_data_with_usage
			DW		_bdpp_flush_drv_tx_packet
