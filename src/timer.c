/*
 * Title:			AGON MOS - Timer
 * Author:			Dean Belfield
 * Created:			19/06/2022
 * Last Updated:	27/02/2024
 * 
 * Modinfo:
 * 11/07/2022:		Removed unused functions
 * 24/07/2022:		Moved interrupt handler to vectors16.asm and initialisation to main
 * 13/03/2023:		Refactored
 * 31/03/2023:		Added wait_VDP
 * 08/04/2023:		Fixed timing loop in wait_VDP
 * 03/08/2023:		Fixed timer0 setup overflow in init_timer0
 * 27/02/2024:		CW Added timer5 functions
 */

#include <eZ80.h>
#include <defines.h>

#include "timer.h"

// Configure Timer 0
// Parameters:
// - interval: Interval in ms
// - clkdiv: 4, 16, 64 or 256
// - clkflag: Other clock flags (interrupt, etc)
// Returns:
// - interval value (timer ticks)
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

	rr = (unsigned short)((SysClkFreq * interval) / (clkdiv * 1000));

	TMR0_CTL = 0x00;													// Disable the timer and clear all settings	
	TMR0_RR_L = (unsigned char)(rr);
	TMR0_RR_H = (unsigned char)(rr >> 8);
    TMR0_CTL = ctl;

	return rr;
}

// Enable Timer 0
// Parameters:
// - enable: 0 = disable, 1 = enable
//
void enable_timer0(unsigned char enable) {
	unsigned char b;

	if(enable <= 1) {
		b = TMR0_CTL;
		b &= 0xFC;
		b |= (enable | 2); 
		TMR0_CTL = b;	
	}
}

// Get data count of Timer 0
//
unsigned short get_timer0() {
	unsigned char l = TMR0_DR_L;
	unsigned char h = TMR0_DR_H;
	return (h << 8) | l;
}

// Wait for the VDP packet to come in, with a timeout
// Parameters:
// - mask: Mask for the packet(s) we're expecting
// Returns:
// - True if the packet is received, False if there is a timeout
//
BOOL wait_VDP(unsigned char mask) {
	int		i;
	BOOL	retVal = 0;

	for(i = 0; i < 250000; i++) {				// A small delay loop (~1s)
		if(vdp_protocol_flags & mask) {			// If we get a result then
			retVal = 1;							// Set the return value to true
			break;								// And exit the loop
		}
	}
	return retVal;
}

// Configure Timer 5
// Parameters:
// - interval: Interval in serial bit times
// - clkflag: Other clock flags (interrupt, etc)
//
/*void init_timer5(int interval, unsigned char ctrlbits) {
	unsigned char ctl = (ctrlbits | 0x04); // use main clock / 16

	TMR5_CTL = 0x00; // Disable the timer and clear all settings	
	TMR5_RR_L = (unsigned char)(interval);
	TMR5_RR_H = (unsigned char)(interval >> 8);
    TMR5_CTL = ctl;
}*/

// Enable Timer 5
// Parameters:
// - enable: 0 = disable, 1 = enable
//
/*void enable_timer5(unsigned char enable) {
	unsigned char b = TMR5_CTL;
	b &= 0xFC;
	if (enable) {
		b |= 0x03; // ENA and RST bits
	}
	TMR5_CTL = b;	
}*/
