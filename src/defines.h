/*
 * Title:			AGON MOS - MOS defines
 * Author:			Dean Belfield
 * Created:			21/03/2023
 * Last Updated:	10/11/2023
 * 
 * Modinfo:
 * 22/03/2023:		The VDP commands are now indexed from 0x80
 * 24/03/2023:		Added DEBUG
 * 10/11/2023:		Added VDP_consolemode
 */

#ifndef MOS_DEFINES_H
#define MOS_DEFINES_H

// Zilog's funny types
#include <defines.h>

/* Just for sanity */
typedef UINT32 uint32_t;
typedef INT32 int32_t;
typedef UINT24 uint24_t;
typedef INT24 int24_t;
typedef UINT16 uint16_t;
typedef INT16 int16_t;
typedef UINT8 uint8_t;
typedef INT8 int8_t;
typedef BOOL bool;
#define false FALSE
#define true TRUE

#define	DEBUG					0			// Set to 0 for production, 1 for extra debug information

// ZDS segment stuff
extern void _heapbot[];
extern void _heaptop[];
extern void _stack[];
extern void _low_data[];
extern void _low_bss[];
extern void _low_romdata[];
extern int _len_data;

/**
 * MOS-specific return codes
 * These extend the FatFS return codes FRESULT
 */
typedef enum {
	MOS_INVALID_COMMAND = 20,	/* (20) Command could not be understood */
	MOS_INVALID_EXECUTABLE, 	/* (21) Executable file format not recognised */
	MOS_OUT_OF_MEMORY,			/* (22) Generic out of memory error */
	MOS_NOT_IMPLEMENTED,		/* (23) API call not implemented */
	MOS_OVERLAPPING_SYSTEM,		/* (24) File load prevented to stop overlapping system memory */
	MOS_BAD_STRING,				/* (25) Bad or incomplete string */
	MOS_TOO_DEEP,				/* (26) Too many nested commands */
} MOSRESULT;

#define SPL_STACK_SIZE				2048
#define HEAP_LEN ((int)_stack - (int)_heapbot - SPL_STACK_SIZE)

// VDP specific (for VDU 23,0,n commands)
//
#define VDP_gp			 		0x80
#define VDP_keycode				0x81
#define VDP_cursor				0x82
#define VDP_scrchar				0x83
#define VDP_scrpixel			0x84
#define VDP_audio				0x85
#define VDP_mode				0x86
#define VDP_rtc					0x87
#define VDP_keystate			0x88
#define VDP_checkkey			0x99
#define VDP_palette				0x94
#define VDP_logicalcoords		0xC0
#define VDP_feature				0xF8
#define VDP_consolemode			0xFE
#define VDP_terminalmode		0xFF

#endif MOS_DEFINES_H
