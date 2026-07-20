/*
 * Title:			AGON MOS - UART code
 * Author:			Dean Belfield
 * Created:			06/07/2022
 * Last Updated:	08/04/2023
 * 
 * Modinfo:
 * 03/08/2022:		Enabled UART0 receive interrupt
 * 08/08/2022:		Enabled UART0 CTS port
 * 22/03/2023:		Moved putch and getch to serial.asm
 * 23/03/2023:		Fixed maths overflow in init_UART0 to work with bigger baud rates
 * 28/03/2023:		Added support for UART1
 * 08/04/2023:		Interrupts now disabled in close_UART1
 *
 * NB:
 * The UART is on Port D
 *
 * - 0: RX
 * - 1: TX
 * - 2: RTS (the CTS input of the ESP32
 * - 3: CTS (the RTS output of the ESP32)
 */
 
#include <stddef.h>
#include <stdio.h>
#include "defines.h"
#include "z80_io.h"
#include "ez80f92.h"
#include "uart.h"
 
// Set the Line Control Register for data, stop and parity bits
//
#define SETREG_LCR0(data, stop, parity) (io_out(UART0_LCTL, ((BYTE)(((data)-(BYTE)5)&(BYTE)0x3)|(BYTE)((((stop)-(BYTE)0x1)&(BYTE)0x1)<<(BYTE)0x2)|(BYTE)((parity)<<(BYTE)0x3))))
#define SETREG_LCR1(data, stop, parity) (io_out(UART1_LCTL, ((BYTE)(((data)-(BYTE)5)&(BYTE)0x3)|(BYTE)((((stop)-(BYTE)0x1)&(BYTE)0x1)<<(BYTE)0x2)|(BYTE)((parity)<<(BYTE)0x3))))

void init_UART0() {
	io_out(PD_DR, PORTD_DRVAL_DEF);
	io_out(PD_DDR, PORTD_DDRVAL_DEF);
	io_out(PD_ALT1, PORTD_ALT1VAL_DEF);
	io_out(PD_ALT2, PORTD_ALT2VAL_DEF);
	return ;
}

void init_UART1() {
	io_out(PC_DR, PORTC_DRVAL_DEF);
	io_out(PC_DDR, PORTC_DDRVAL_DEF);
	io_out(PC_ALT1, PORTC_ALT1VAL_DEF);
	io_out(PC_ALT2, PORTC_ALT2VAL_DEF);
	return ;
}

// Open UART0
// Parameters:
// - pUART: Structure containing the initialisation data
//
BYTE open_UART0(UART * pUART) {
	UINT32	mc = MASTERCLOCK;										// UART baud rate calculation
	UINT32	cb = CLOCK_DIVISOR_16 * (uint32_t)pUART->baudRate;				// split to avoid eZ80 maths overflow error
	UINT32	br = mc / cb;											// with larger baud rate values

	UCHAR	pins = PORTPIN_ZERO | PORTPIN_ONE;						// The transmit and receive pins											

	serialFlags &= 0xF0;
	
	io_setreg(PD_DDR, pins);											// Set Port D bits 0, 1 (TX. RX) for alternate function.
	io_resetreg(PD_ALT1, pins);
	io_setreg(PD_ALT2, pins);

	if(pUART->flowControl == FCTL_HW) {
		io_setreg(PD_DDR, PORTPIN_THREE);								// Set Port D bit 3 (CTS) for input
		io_resetreg(PD_ALT1, PORTPIN_THREE);
		io_resetreg(PD_ALT2, PORTPIN_THREE);
		serialFlags |= 0x02;
	}

	io_setreg(UART0_LCTL, UART_LCTL_DLAB);									// Select DLAB to access baud rate generator
	io_out(UART0_BRG_L, br & 0xFF);										// Load divisor low
	io_out(UART0_BRG_H, (BYTE)(( br & 0xFF00 ) >> 8));						// Load divisor high
	io_out(UART0_LCTL, io_in(UART0_LCTL) & (~UART_LCTL_DLAB)); 								// Reset DLAB; dont disturb other bits
	io_out(UART0_MCTL, 0x00);												// Bring modem control register to reset value
	io_out(UART0_FCTL, 0x07);												// Enable and clear hardware FIFOs
	io_out(UART0_IER, pUART->interrupts);									// Set interrupts
	
	SETREG_LCR0(pUART->dataBits, pUART->stopBits, pUART->parity);	// Set the line status register

	serialFlags |= 0x01;
	
	return UART_ERR_NONE;
}

// Open UART1
// Parameters:
// - pUART: Structure containing the initialisation data
//
BYTE open_UART1(UART * pUART) {
	UINT32	mc = MASTERCLOCK;										// UART baud rate calculation
	UINT32	cb = CLOCK_DIVISOR_16 * (uint32_t)pUART->baudRate;				// split to avoid eZ80 maths overflow error
	UINT32	br = mc / cb;											// with larger baud rate values

	UCHAR	pins = PORTPIN_ZERO | PORTPIN_ONE;						// The transmit and receive pins											

	serialFlags &= 0x0F;

	io_setreg(PC_DDR, pins);											// Set Port C bits 0, 1 (TX. RX) for alternate function.
	io_resetreg(PC_ALT1, pins);
	io_setreg(PC_ALT2, pins);

	if(pUART->flowControl == FCTL_HW) {
		io_setreg(PC_DDR, PORTPIN_THREE);								// Set Port C bit 3 (CTS) for input
		io_resetreg(PC_ALT1, PORTPIN_THREE);
		io_resetreg(PC_ALT2, PORTPIN_THREE);
		serialFlags |= 0x20;
	}
	
	io_setreg(UART1_LCTL, UART_LCTL_DLAB);									// Select DLAB to access baud rate generator
	io_out(UART1_BRG_L, br & 0xFF);										// Load divisor low
	io_out(UART1_BRG_H, (BYTE)(( br & 0xFF00 ) >> 8));						// Load divisor high
	io_out(UART1_LCTL, io_in(UART1_LCTL) & (~UART_LCTL_DLAB)); 								// Reset DLAB; dont disturb other bits
	io_out(UART1_MCTL, 0x00);												// Bring modem control register to reset value
	io_out(UART1_FCTL, 0x07);												// Enable and clear hardware FIFOs
	io_out(UART1_IER, pUART->interrupts);									// Set interrupts

	serialFlags |= 0x10;
	
	SETREG_LCR1(pUART->dataBits, pUART->stopBits, pUART->parity);	// Set the line status register
	
	return UART_ERR_NONE;
}

// Close UART1
//
void close_UART1() {
	io_out(UART1_IER, 0x00);												// Disable UART1 interrupts
	io_out(UART1_LCTL, 0x00); 												// Bring line control register to reset value.
	io_out(UART1_MCTL, 0x00);												// Bring modem control register to reset value.
	io_out(UART1_FCTL, 0x00);												// Bring FIFO control register to reset value.	
	serialFlags &= 0x0F;
}
