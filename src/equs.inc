;
; Title:	AGON MOS - Equs
; Author:	Dean Belfield
; Created:	15/07/2022
; Last Updated:	08/06/2023
;
; Modinfo:
; 24/07/2022:	Added TMR2_CTL
; 03/08/2022:	Added UART0_BUFFERLEN
; 20/08/2022:	Added some VDP protocol flags
; 18/09/2022:	Added VDPP_FLAG_MODE
; 09/03/2023:	Renamed TMR2_CTL to TMR0_CTL
; 15/03/2023:	Added VDPP_FLAG_RTC
; 19/03/2023:	Fixed TMR0_RR_H to point to correct register
; 08/06/2023:	Add MASTERCLOCK to permit clock delay calculations
; 20/01/2024:	CW Added support for bidirectional packet protocol

; System clock speed in Hz
MASTERCLOCK:		EQU		18432000

; MOS specific
;
VDPP_BUFFERLEN:		EQU		16	; VDP Protocol Buffer Length
	
VDPP_FLAG_CURSOR:	EQU		00000001b
VDPP_FLAG_SCRCHAR:	EQU		00000010b
VDPP_FLAG_POINT:	EQU		00000100b
VDPP_FLAG_AUDIO:	EQU		00001000b	
VDPP_FLAG_MODE:		EQU		00010000b
VDPP_FLAG_RTC:		EQU		00100000b
VDPP_FLAG_MOUSE:	EQU		01000000b
; VDPP_FLAG_BUFFERED:	EQU		10000000b

; Bidirectional packet protocol
;
BDPP_FLAG_ENABLED				EQU	01h ; Whether bidirectional protocol is enabled

BDPP_MAX_DATA_SIZE				EQU	128	; Maximum payload data length
BDPP_MAX_PACKET_SIZE			EQU	(BDPP_DATALEN+4) ; Maximum total packet size
BDPP_PACKET_START_MARKER		EQU	8Ch
BDPP_PACKET_ESCAPE				EQU	9Dh
BDPP_PACKET_END_MARKER			EQU	AEh

BDPP_RX0_STATE_AWAIT_START		EQU	01h	; Waiting for the packet start marker
BDPP_RX0_STATE_AWAIT_FLAGS		EQU	02h	; Waiting for the packet flags
BDPP_RX0_STATE_AWAIT_SIZE		EQU	03h	; Waiting for the packet size
BDPP_RX0_STATE_AWAIT_DATA_ESC	EQU	04h	; Waiting for a packet data byte or escape
BDPP_RX0_STATE_AWAIT_DATA		EQU	05h	; Waiting for a packet data byte only
BDPP_RX0_STATE_AWAIT_END		EQU	06h	; Waiting for the packet end marker

BDPP_RX1_STATE_AWAIT_START		EQU	11h	; Waiting for the packet start marker
BDPP_RX1_STATE_AWAIT_FLAGS		EQU	12h	; Waiting for the packet flags
BDPP_RX1_STATE_AWAIT_SIZE		EQU	13h	; Waiting for the packet size
BDPP_RX1_STATE_AWAIT_DATA_ESC	EQU	14h	; Waiting for a packet data byte or escape
BDPP_RX1_STATE_AWAIT_DATA		EQU	15h	; Waiting for a packet data byte only
BDPP_RX1_STATE_AWAIT_END		EQU	16h	; Waiting for the packet end marker

BDPP_TX_STATE_IDLE				EQU	20h	; Doing nothing (not transmitting)
BDPP_TX_STATE_SENT_START		EQU	21h	; Recently sent the packet start marker
BDPP_TX_STATE_SENT_TYPE			EQU	22h	; Recently sent the packet type
BDPP_TX_STATE_SENT_SIZE			EQU	23h	; Recently sent the packet size
BDPP_TX_STATE_SENT_ESC			EQU	24h	; Recently sent an escape character
BDPP_TX_STATE_SENT_DATA			EQU	25h	; Recently sent a packet data byte

BDPP_PKT_FLAG_PRINT				EQU	00h	; Indicates packet contains printable data
BDPP_PKT_FLAG_COMMAND			EQU 01h	; Indicates packet contains a command or request
BDPP_PKT_FLAG_RESPONSE			EQU	02h ; Indicates packet contains a response
BDPP_PKT_FLAG_FIRST				EQU	04h	; Indicates packet is first part of a message
BDPP_PKT_FLAG_MIDDLE			EQU	00h	; Indicates packet is middle part of a message
BDPP_PKT_FLAG_LAST				EQU	08h	; Indicates packet is last part of a message

; For GPIO
; PA not available on eZ80F92
;
PA_DR:			EQU		96h
PA_DDR:			EQU		97h
PA_ALT1:		EQU		98h
PA_ALT2:		EQU		99h
PB_DR:          	EQU		9Ah
PB_DDR:        	 	EQU		9Bh
PB_ALT1:        	EQU		9Ch
PB_ALT2:        	EQU		9Dh
PC_DR:          	EQU		9Eh
PC_DDR:         	EQU		9Fh
PC_ALT1:        	EQU		A0h
PC_ALT2:        	EQU		A1h
PD_DR:          	EQU		A2h
PD_DDR:			EQU		A3h
PD_ALT1:		EQU		A4h
PD_ALT2:		EQU		A5h
	
GPIOMODE_OUT:		EQU		0	; Output
GPIOMODE_IN:		EQU		1	; Input
GPIOMODE_DIO:		EQU		2	; Open Drain IO
GPIOMODE_SIO:		EQU		3	; Open Source IO
GPIOMODE_INTD:		EQU		4	; Interrupt, Dual Edge
GPIOMODE_ALTF:		EQU		5;	; Alt Function
GPIOMODE_INTAL:		EQU		6	; Interrupt, Active Low
GPIOMODE_INTAH:		EQU		7	; Interrupt, Active High
GPIOMODE_INTFE:		EQU		8	; Interrupt, Falling Edge
GPIOMODE_INTRE:		EQU		9	; Interrupt, Rising Edge
	
; For interrupts.asm
;

;UARTs
;
UART0_IVECT		EQU	18h
UART1_IVECT		EQU	1Ah

;Ports
;
PB0_IVECT   		EQU   	30h	; AGON ITRP Interrupt   (Pin 28/IO17 of the ESP32)
PB1_IVECT  	  	EQU  	32h	; AGON VBLANK Interrupt (Pin 23/IO15 of the ESP32)
PB2_IVECT  	  	EQU   	34h
PB3_IVECT  	  	EQU   	36h
PB4_IVECT    		EQU   	38h
PB5_IVECT    		EQU   	3Ah
PB6_IVECT    		EQU   	3Ch
PB7_IVECT    		EQU   	3Eh
                       
PC0_IVECT    		EQU   	40h
PC1_IVECT    		EQU   	42h
PC2_IVECT    		EQU   	44h
PC3_IVECT    		EQU   	46h
PC4_IVECT    		EQU   	48h
PC5_IVECT    		EQU   	4Ah
PC6_IVECT    		EQU   	4Ch
PC7_IVECT    		EQU   	4Eh
                       
PD0_IVECT    		EQU   	50h
PD1_IVECT    		EQU   	52h
PD2_IVECT    		EQU   	54h
PD3_IVECT    		EQU   	56h
PD4_IVECT    		EQU   	58h
PD5_IVECT    		EQU   	5Ah
PD6_IVECT    		EQU   	5Ch
PD7_IVECT    		EQU   	5Eh

; For vectors16.asm
;
TMR0_CTL		EQU	80h
TMR0_DR_L               EQU     81h
TMR0_RR_L               EQU     81h
TMR0_DR_H               EQU     82h
TMR0_RR_H               EQU     82h
