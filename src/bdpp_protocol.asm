;
; Title:	AGON MOS - Bidirectional packet protocol (BDPP)
; Author:	Curtis Whitley
; Created:	20/01/2024
; Last Updated:	20/01/2024
;
; Modinfo:
; 20/01/2024:	Created initial version of protocol.

			INCLUDE	"macros.inc"
			INCLUDE	"equs.inc"

			.ASSUME	ADL = 1

			DEFINE .STARTUP, SPACE = ROM
			SEGMENT .STARTUP
			
			XDEF	bdpp_protocol

			XREF	vdp_protocol

;							
; The BDPP interrupt handler state machine
;
bdpp_protocol:
			LD		A, (_bdpp_rx_state)				; Get the current receive state
			CP		BDPP_RX1_STATE_AWAIT_START		; Are we processing RX buffer #1?
			JR		NC,_rx1_check_state				; Go if yes
			CP		BDPP_RX0_STATE_AWAIT_START		; Check for start state
			JR		Z,_rx0_state_await_start
			DEC		A								; Check for flags state
			JR		Z,_rx0_state_await_flags
			DEC		A								; Check for size state
			JR		Z,_rx0_state_await_size
			DEC		A								; Check for data/esc state
			JR		Z,_rx0_state_await_data_esc
			DEC		A								; Check for data only state
			JR		Z,_rx0_state_await_data
			DEC		A								; Check for end state
			JR		Z,_rx0_state_await_end
			J		_tx_check_state

_rx1_check_state:
			JR		Z,_rx1_state_await_start		; Check for start state
			DEC		A								; Check for flags state
			JR		Z,_rx1_state_await_flags
			DEC		A								; Check for size state
			JR		Z,_rx1_state_await_size
			DEC		A								; Check for data/esc state
			JR		Z,_rx1_state_await_data_esc
			DEC		A								; Check for data only state
			JR		Z,_rx1_state_await_data
			DEC		A								; Check for end state
			JR		Z,_rx1_state_await_end

_tx_check_state:
			LD		A, (_bdpp_tx_state)				; Get the current receive state
			CP		BDPP_TX_STATE_IDLE				; Check for idle state
			JR		Z,_rx_tx_ret
			DEC		A								; Check for start state
			JR		Z,_tx_state_sent_start
			DEC		A								; Check for flags state
			JR		Z,_tx_state_sent_flags
			DEC		A								; Check for size state
			JR		Z,_tx_state_sent_size
			DEC		A								; Check for escape state
			JR		Z,_tx_state_sent_esc
			DEC		A								; Check for data state
			JR		Z,_tx_state_sent_data
			JR		_rx_tx_ret						; Bad state; quit!

_rx0_state_await_start:
			LD		A,C								; Move incoming byte to accumulator
			CP		A,BDPP_PACKET_START_MARKER		; Start of packet?
			JR		NZ,_tx_check_state				; Quit if not
			LD		(_bdpp_rx_state),BDPP_RX0_STATE_AWAIT_FLAGS	; Set next state
			J		_tx_check_state					; Go check transmit state

_rx0_state_await_flags:
			LD		(_bdpp_rx0_flags),C				; Save the incoming packet flags
			LD		(_bdpp_rx_state),BDPP_RX0_STATE_AWAIT_SIZE	; Set next state
			J		_tx_check_state					; Go check transmit state

_rx0_state_await_size:
			LD		(_bdpp_rx0_size),C				; Save the incoming packet size
			LD		A,C								; Move incoming byte to accumulator
			OR		A								; Check for size equal to zero
			JR		Z,_rx0_no_data					; Go if no data to receive
			LD		(_bdpp_rx_data_count),C			; Set down-counter for bytes received
			LD		(_bdpp_rx_state),BDPP_RX0_STATE_AWAIT_DATA_ESC ; Set next state
			J		_tx_check_state					; Go check transmit state

_rx0_state_await_data_esc:
			LD		A,C								; Move incoming byte to accumulator
			CP		A,BDPP_PACKET_ESCAPE			; Is it the escape character?
			JR		NZ,$F							; If not, go save incoming data byte
			LD		(HL),BDPP_RX0_STATE_AWAIT_DATA	; Set next state
			J		_tx_check_state					; Go check transmit state
$$:			LD		HL,(_bdpp_rx_data_ptr)			; Get address in buffer
			LD		(HL),A							; Save incoming data byte
			LD		DE,_bdpp_rx_data_count			; Get address of data count
			DEC		(DE)							; Decrement data count
			JZ		_rx0_no_data					; Go if all data bytes received
			INC		HL								; Increment buffer pointer
			LD		(_bdpp_rx_data_ptr),HL			; Update buffer pointer
			LD		(HL),BDPP_RX0_STATE_AWAIT_DATA_ESC ; Set next state
			J		_tx_check_state					; Go check transmit state
_rx0_no_data: LD	(HL),BDPP_RX0_STATE_AWAIT_END	; Set next state
			J		_tx_check_state					; Go check transmit state

_rx0_state_await_data:

_rx0_state_await_end:
			LD		A,C								; Move incoming byte to accumulator
			CP		A,BDPP_PACKET_END_MARKER		; End of packet?
			JR		NZ,$F							; Error if not
			--ready ??
			LD		(_bdpp_rx_state),BDPP_RX1_STATE_AWAIT_START	; Set next state
			J		_tx_check_state					; Go check transmit state
$$:			LD		(_bdpp_rx_state),BDPP_RX0_STATE_AWAIT_START	; Set next state
			J		_tx_check_state					; Go check transmit state

_rx_tx_ret:	RET

_tx_state_sent_start:
_tx_state_sent_flags:
_tx_state_sent_size:
_tx_state_sent_esc:
_tx_state_sent_data:
