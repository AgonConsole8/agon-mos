;
; Title:	AGON MOS - SPI low level assembly language
; Author:	Leigh Brown
; Created:	26/05/2023
; Last Updated:	26/05/2023

; Modinfo
;

; The approach taken to maximise performance is:
; 1) Minimise the time between receiving the response from the current request
;    and sending the next request.
; 2) Getting as much done whilst the byte is being shifted out/in

		INCLUDE "equs.inc"
		INCLUDE "macros.inc"

SPI_ENA_DELAY	.equ	50

SD_CS		.equ	4	; Bit 4

SPI_MOSI	.equ	7	; PB7
SPI_MISO	.equ	6	; PB6
SPI_CLK		.equ	3	; PB3

		XDEF	_init_spi
		XDEF	_spi_transfer
		XDEF	_spi_read_one
		XDEF	_spi_read
		XDEF	_spi_write

		.ASSUME ADL = 1


; void spi_init(void);

_init_spi:	;
		; SS must remain high for SPI to work properly
		;

		; PB_DR   |=  BIT(2);
		IN0		A,(PB_DR)
		SET		2,A
		OUT0		(PB_DR),A

		; PB_ALT1 &= ~BIT(2);
		IN0		A,(PB_ALT1)
		RES		2,A
		OUT0		(PB_ALT1),A

		; PB_ALT2 &= ~BIT(2);
		IN0		A,(PB_ALT2)
		RES		2,A
		OUT0		(PB_ALT2),A

		; PB_DDR  &= ~BIT(2);
		IN0		A,(PB_DDR)
		RES		2,A
		OUT0		(PB_DDR),A

		;
		; Enable the chip select outputs and deselect
		;

		; PB_DR   |=  BIT(SD_CS);
		IN0		A,(PB_DR)
		SET		4,A
		OUT0		(PB_DR),A

		; PB_ALT1 &= ~BIT(SD_CS);
		IN0		A,(PB_ALT1)
		RES		4,A
		OUT0		(PB_ALT1),A

		; PB_ALT2 &= ~BIT(SD_CS);
		IN0		A,(PB_ALT2)
		RES		0x4,A
		OUT0		(PB_ALT2),A

		; PB_DDR  &= ~BIT(SD_CS);
		IN0		A,(PB_DDR)
		RES		0x4,A
		OUT0		(PB_DDR),A

		;
		; Set port B pins 7 (MOSI), 6 (MISO), 3 (SCK), 2 (/SS) to SPI
		;

		; PB_ALT1 &= ~(BIT(SPI_MOSI) | BIT(SPI_MISO) | BIT(SPI_CLK))
		IN0		A,(PB_ALT1)
		AND		A,~((1<<SPI_MOSI)|(1<<SPI_MISO)|(1<<SPI_CLK))
		OUT0		(PB_ALT1),A

		; PB_ALT2 |=  (BIT(SPI_MOSI) | BIT(SP_MISO) | BIT(SPI_CLK))
		IN0		A,(PB_ALT2)
		OR		A,~~((1<<SPI_MOSI)|(1<<SPI_MISO)|(1<<SPI_CLK))
		OUT0		(PB_ALT2),A

		;
		; Disable SPI
		;

		XOR		A,A
		OUT0		(SPI_CTL),A
	
		;
		; Set SPI baud rate generator divisor registers
		;

		LD		BC,3		; I changed from 4 to 3
		OUT0		(SPI_BRG_H),B
		OUT0		(SPI_BRG_L),C

		;
		; Enable SPI as master
		;

		LD		A,0x30
		OUT0		(SPI_CTL),A
	
		; Delay for `SPI_ENA_DELAY' milliseconds
		;

		DELAY_MS	SPI_ENA_DELAY

		; All done
		RET


; unsigned char spi_read_one(void);
;
_spi_read_one:
		LD		C,0xFF		; Kick SPI into action before
		OUT0		(SPI_TSR),C	; anything else...
		POP		HL		; Pop return address for later
		JR		spi_wait

; unsigned char spi_transfer(unsigned char d);
;
_spi_transfer:
		POP		HL		; Pop return address for later
		POP		DE		; Transmit the byte ASAP...
		OUT0		(SPI_TSR),E
		PUSH		DE
		; fall through (no need for JR spi_wait)

; Both _spi_read_one and _spi_transfer use spi_wait to complete the operation

spi_wait:	LD		B,0
1:		IN0		A,(SPI_SR)
		RLA
		JR		C,2f
		DJNZ		1b
2:		IN0		A,(SPI_RBR)
		JP		(HL)		; Faster than RET if we have HL


; void spi_read(char *buf, unsigned int len);
;

_spi_read:
		; Request the first byte - do first to minimise delay
		LD		C,0xFF
		OUT0		(SPI_TSR),C

		; Function prologue
		PUSH		IX
		LD		IX,0
		ADD		IX,SP

		; DE := destination address
		LD		DE,(IX+6)

		; HL := number of bytes to read
		LD		HL,(IX+9)

		; Decrement the count of bytes
L_mainloop1:	LD		BC,1
		OR		A,A
		SBC		HL,BC

		; If this is the last, break out of loop
		JR		Z,L_waitlast

		; Wait for byte to arrive then store it
L_waitnext:	LD		B,0		; (256 iterations)
		LD		C,0xFF		; pre-load C with dummy byte
L_loopnext:	IN0		A,(SPI_SR)
		RLA
		JR		C,L_gotnext
		DJNZ		L_loopnext
		; TODO: detect errors

		; Minimise delay from receiving to requesting the next byte
L_gotnext:	IN0		A,(SPI_RBR)
		OUT0		(SPI_TSR),C
		LD		(DE),A
		INC		DE
		JR		L_mainloop1

L_waitlast:	; Do function epilogue now, we have time
		LD		SP,IX
		POP		IX

		; Now wait for last byte
		LD		B,0		; (256 iterations)
L_looplast:	IN0		A,(SPI_SR)
		RLA
		JR		C,L_gotlast
		DJNZ		L_looplast
		; TODO: detect errors

L_gotlast:	IN0		A,(SPI_RBR)
		LD		(DE),A
		RET


; void spi_write(char *buf, unsigned int len);

_spi_write:
		PUSH		IX
		LD		IX,0
		ADD		IX,SP

		; DE := source address
		LD		DE,(IX+6)

		; Write the first byte as soon as we can
		LD		A,(DE)
		OUT0		(SPI_TSR),A
		INC		DE

		; HL := number of bytes to write
		LD		HL,(IX+9)

		; Decrement the count of bytes
L_mainloop2:	LD		BC,1
		OR		A,A
		SBC		HL,BC

		; If this is the last, break out of loop
		JR		Z,L_waitlast2

		; Wait for the write to complete
L_waitnext2:	LD		B,0		; (256 iterations)
		LD		A,(DE)		; pre-load C with byte to write
		LD		C,A
		INC		DE

L_loopnext2:	IN0		A,(SPI_SR)
		RLA
		JR		C,L_sentnext
		DJNZ		L_loopnext2
		; TODO: detect errors

		; Minimise delay from send complete to sending the next octet
L_sentnext:	; Don't bother reading the dummy byte (IN0 A,(SPI_RBR))
		OUT0		(SPI_TSR),C
		JR		L_mainloop2

L_waitlast2:	; Do function epilogue now, whilst the last byte is transferring
		LD		SP,IX
		POP		IX

		LD		B,0		; (256 iterations)
L_looplast2:	IN0		A,(SPI_SR)
		RLA
		JR		C,L_sentlast
		DJNZ		L_looplast2
		; TODO: detect errors

L_sentlast:	; Don't bother reading the dummy byte (IN0 A,(SPI_RBR))
		RET

