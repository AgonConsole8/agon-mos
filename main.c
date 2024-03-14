/*
 * Title:			AGON MOS
 * Author:			Dean Belfield
 * Created:			19/06/2022
 * Last Updated:	11/11/2023
 *
 * Modinfo:
 * 11/07/2022:		Version 0.01: Tweaks for Agon Light, Command Line code added
 * 13/07/2022:		Version 0.02
 * 15/07/2022:		Version 0.03: Warm boot support, VBLANK interrupt
 * 25/07/2022:		Version 0.04; Tweaks to initialisation and interrupts
 * 03/08/2022:		Version 0.05: Extended MOS for BBC Basic, added config file
 * 05/08/2022:		Version 0.06: Interim release with hardware flow control enabled
 * 10/08/2022:		Version 0.07: Bug fixes
 * 05/09/2022:		Version 0.08: Minor updates to MOS
 * 02/10/2022:		Version 1.00: Improved error handling for languages, changed bootup title to Quark
 * 03/10/2022:		Version 1.01: Added SET command, tweaked error handling
 * 20/10/2022:					+ Tweaked error handling
 * 13/11/2022:		Version 1.02
 * 14/03/2023		Version 1.03: SD now uses timer0, does not require interrupt
 *								+ Stubbed command history
 * 22/03/2023:					+ Moved command history to mos_editor.c
 * 23/03/2023:				RC2	+ Increased baud rate to 1152000
 * 								+ Improved ESP32->eZ80 boot sync
 * 29/03/2023:				RC3 + Added UART1 initialisation, tweaked startup sequence timings
 * 16/05/2023:		Version 1.04: Fixed MASTERCLOCK value in uart.h, added startup beep
 * 03/08/2023:				RC2	+ Enhanced low-level keyboard functionality
 * 27/09/2023:					+ Updated RTC
 * 11/11/2023:				RC3	+ See Github for full list of changes
 * 20/01/2024:	    CW Added support for bidirectional packet protocol
 */

#include <eZ80.h>
#include <defines.h>
#include <stdio.h>
#include <CTYPE.h>
#include <String.h>

#include "defines.h"
#include "version.h"
#include "config.h"
#include "uart.h"
#include "spi.h"
#include "timer.h"
#include "ff.h"
#include "clock.h"
#include "mos.h"
#include "i2c.h"
#include "bdp_protocol.h"

extern void *	set_vector(unsigned int vector, void(*handler)(void));

extern void 	vblank_handler(void);
extern void 	uart0_handler(void);
extern void 	i2c_handler(void);

extern char 			coldBoot;		// 1 = cold boot, 0 = warm boot
extern volatile	char 	keycode;		// Keycode 
extern volatile char	gp;				// General poll variable

extern volatile BYTE history_no;
extern volatile BYTE history_size;

// Wait for the ESP32 to respond with a GP packet to signify it is ready
// Parameters:
// - pUART: Pointer to a UART structure
// - baudRate: Baud rate to initialise UART with
// Returns:
// - 1 if the function succeeded, otherwise 0
//
int wait_ESP32(UART * pUART, UINT24 baudRate) {	
	int	i, t;

	pUART->baudRate = baudRate;			// Initialise the UART object
	pUART->dataBits = 8;
	pUART->stopBits = 1;
	pUART->parity = PAR_NOPARITY;
	pUART->flowControl = FCTL_HW;
	pUART->interrupts = UART_IER_RECEIVEINT;

	open_UART0(pUART);					// Open the UART 
	init_timer0(10, 16, 0x00);  		// 10ms timer for delay
	gp = 0;								// Reset the general poll byte
	for(t = 0; t < 200; t++) {			// A timeout loop (200 x 50ms = 10s)
		putch(23);						// Send a general poll packet
		putch(0);
		putch(VDP_gp);
		
		// There are now 4 possible cases, relative to the
		// bidirectional packet protocol (BDPP):
		//
		// (a) EZ80 does NOT support BDPP, and ESP32 does NOT support BDPP.
		//     - EZ80 sends 0x01; ESP32 returns 0x01.
		//
		// (b) EZ80 does NOT support BDPP, but ESP32 DOES support BDPP.
		//     - EZ80 sends 0x01, ESP32 returns 0x01.
		//
		// (c) EZ80 DOES support BDPP, but ESP32 does NOT support BDPP.
		//     - EZ80 sends 0x04..0x0F, ESP32 returns 0x04..0x0F.
		//
		// (d) EZ80 DOES support BDPP, and ESP32 DOES support BDPP.
		//     - EZ80 sends 0x04..0x0F, ESP32 returns 0x84..0x8F.
		//
		// The range of values allows us to enhance the BDPP over time,
		// if needed, and be able to identify which version of BDDP is
		// on each side of the communication. This is EZ80 code, so it
		// obviously knows its own protocol version.

		putch(EZ80_COMM_PROTOCOL_VERSION); // older EZ80 code will send a 0x01 here!

		for(i = 0; i < 5; i++) {		// Wait 50ms
			wait_timer0();
		}

		// If general poll returned, then exit for loop.
		// If gp is 0x84..0x8F, then both CPUs support bidirectional protocol;
		// If gp is 0x04..0x0F, then only the EZ80 supports it.

		if ((gp & 0x7F) >= 0x04 && (gp & 0x7F) <= 0x0F) {
			// Pack the 2 protocol versions into a single system variable, for app use.
			if (gp & 0x80) {
				// The ESP32 DOES support BDPP.
				gp = ((gp & 0x0F) << 4) | EZ80_COMM_PROTOCOL_VERSION;
			} else {
				// The ESP32 does NOT support BDPP.
				gp = (0x01 << 4) | EZ80_COMM_PROTOCOL_VERSION;
			}
			break;
		}
	}
	enable_timer0(0);					// Disable the timer
	return gp;
}

// Initialise the interrupts
//
void init_interrupts(void) {
	set_vector(PORTB1_IVECT, vblank_handler); 	// 0x32
	set_vector(UART0_IVECT, uart0_handler);		// 0x18
	set_vector(I2C_IVECT, i2c_handler);			// 0x1C
}

// The main loop
//
int main(void) {
	UART 	pUART0;
	int		err;

	DI();											// Ensure interrupts are disabled before we do anything
	init_interrupts();								// Initialise the interrupt vectors
	init_rtc();										// Initialise the real time clock
	init_spi();										// Initialise SPI comms for the SD card interface
	init_UART0();									// Initialise UART0 for the ESP32 interface
	init_UART1();									// Initialise UART1
	EI();											// Enable the interrupts now
	
	if(!wait_ESP32(&pUART0, 1152000)) {				// Try to lock onto the ESP32 at maximum rate
		if(!wait_ESP32(&pUART0, 384000))	{		// If that fails, then fallback to the lower baud rate
			gp = 2;									// Flag GP as 2, just in case we need to handle this error later
		}
	}
	
	if (((BYTE)gp & 0xF0) >= 0x40) {
		// The ESP32 code supports bidirectional packet protocol.

		// Setup Port D bit 2 (RTS) for alt fcn (output)
		SETREG(PD_DDR, PORTPIN_TWO);
		RESETREG(PD_ALT1, PORTPIN_TWO);
		SETREG(PD_ALT2, PORTPIN_TWO);
		SETREG(UART0_MCTL, PORTPIN_ONE); // Turn on RTS for the ESP32 to see CTS

		// Allow BDPP (but don't enable it yet)
		bdpp_fg_initialize_driver();
	}
	
	if(coldBoot == 0) {								// If a warm boot detected then
		putch(12);									// Clear the screen
	}
	printf("Agon %s MOS Version %d.%d.%d", VERSION_VARIANT, VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
	#if VERSION_CANDIDATE > 0
		printf(" %s%d", VERSION_TYPE, VERSION_CANDIDATE);
	#endif
	// Show build if defined (intended to be auto-generated string from build script from git commit hash)
	#ifdef VERSION_BUILD
		printf(" Build %s", VERSION_BUILD);
	#endif

	printf("\n\rProtocol versions: MOS(%hu), VDP(%hu)\n\r",
			(((BYTE)gp) & 0x0F), (((BYTE)gp) >> 4));

	#if	DEBUG > 0
	printf("@Baud Rate: %d\n\r\n\r", pUART0.baudRate);
	#endif
	
	mos_mount();									// Mount the SD card

	putch(7);										// Startup beep
	history_no = 0;
	history_size = 0;

	// Load the autoexec.bat config file
	//
	#if enable_config == 1	
	if(coldBoot > 0) {								// Check it's a cold boot (after reset, not RST 00h)
		err = mos_EXEC("autoexec.txt", cmd, sizeof cmd);	// Then load and run the config file
		if (err > 0 && err != FR_NO_FILE) {
			mos_error(err);
		}
	}	
	#endif

	// The main loop
	//
	while(1) {
		bdpp_fg_flush_drv_tx_packet();
		if(mos_input(&cmd, sizeof(cmd)) == 13) {
			bdpp_fg_flush_drv_tx_packet();
			err = mos_exec(&cmd);
			bdpp_fg_flush_drv_tx_packet();
			if(err > 0) {
				mos_error(err);
			}
		}
		else {
			printf("%cEscape\n\r", MOS_prompt);
		}
	}

	return 0;
}
