/*
 * Title:			AGON MOS - Timer
 * Author:			Dean Belfield
 * Created:			19/06/2022
 * Last Updated:	03/08/2023
 * 
 * Modinfo:
 * 11/07/2022:		Removed unused functions
 * 24/07/2022:		Moved interrupt handler to vectors16.asm and initialisation to main
 * 13/03/2023:		Refactored
 * 31/03/2023:		Added wait_VDP
 * 08/04/2023:		Fixed timing loop in wait_VDP
 * 03/08/2023:		Fixed timer0 setup overflow in init_timer0
 */

#include "ez80f92.h"
#include "defines.h"
#include "z80_io.h"
#include "timer.h"

// Configure Timer 0
// Parameters:
// - interval: Interval in ms
// - clkdiv: 4, 16, 64 or 256
// - clkflag: Other clock flags (interrupt, etc)
// Returns:
// - interval value
//
unsigned short init_timer0(int interval, int clkdiv, unsigned char ctrlbits) {
	unsigned short	rr;
	unsigned char	clkbits = 0;
	unsigned char	ctl;

	switch(clkdiv) {
		case  16: clkbits = 0x04; break;
		case  64: clkbits = 0x08; break;	
		case 256: clkbits = 0x0C; break;
	}
	ctl = (ctrlbits | clkbits);

	rr = (unsigned short)((SysClkFreq / 1000) / clkdiv) * interval;

	io_out(TMR0_CTL, 0x00);													// Disable the timer and clear all settings	
	io_out(TMR0_RR_L, (unsigned char)(rr));
	io_out(TMR0_RR_H, (unsigned char)(rr >> 8));
	io_out(TMR0_CTL, ctl);

	return rr;
}

// Enable Timer 0
// Parameters:
// - enable: 0 = disable, 1 = enable
//
void enable_timer0(unsigned char enable) {
	unsigned char b;

	if(enable <= 1) {
		b = io_in(TMR0_CTL);
		b &= 0xFC;
		b |= (enable | 2); 
		io_out(TMR0_CTL, b);	
	}
}

// Get data count of Timer 0
//
unsigned short get_timer0() {
	unsigned char l = io_in(TMR0_DR_L);
	unsigned char h = io_in(TMR0_DR_H);
	return (h << 8) | l;
}
