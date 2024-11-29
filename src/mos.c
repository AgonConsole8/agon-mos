/*
 * Title:			AGON MOS - MOS code
 * Author:			Dean Belfield
 * Created:			10/07/2022
 * Last Updated:	11/11/2023
 * 
 * Modinfo:
 * 11/07/2022:		Added mos_cmdDIR, mos_cmdLOAD, removed mos_cmdBYE
 * 12/07/2022:		Added mos_cmdJMP
 * 13/07/2022:		Added mos_cmdSAVE, mos_cmdDEL, improved command parsing and file error reporting
 * 14/07/2022:		Added mos_cmdRUN
 * 25/07/2022:		Added mos_getkey; variable keycode is now declared as a volatile
 * 03/08/2022:		Added a handful of MOS API calls
 * 05/08/2022:		Added mos_FEOF
 * 05/09/2022:		Added mos_cmdREN, mos_cmdBOOT; moved mos_EDITLINE into mos_editline.c, default args for LOAD and RUN commands
 * 25/09/2022:		Added mos_GETERROR, mos_MKDIR; mos_input now sets first byte of buffer to 0
 * 03/10/2022:		Added mos_cmdSET
 * 13/10/2022:		Added mos_OSCLI and supporting code
 * 20/10/2022:		Tweaked error handling
 * 08/11/2022:		Fixed return value bug in mos_cmdRUN
 * 13/11/2022:		Case insensitive command processing with abbreviations; mos_exec now runs commands off SD card
 * 19/11/2022:		Added support for passing params to executables & ADL mode
 * 14/02/2023:		Added mos_cmdVDU, support for more keyboard layouts in mos_cmdSET
 * 20/02/2023:		Function mos_getkey now returns a BYTE
 * 12/03/2023:		Renamed keycode to keyascii, keyascii now a BYTE, added mos_cmdTIME, mos_cmdCREDITS, mos_DIR now accepts a path
 * 15/03/2023:		Added mos_cmdCOPY, mos_COPY, mos_GETRTC, aliase for mos_REN, made error messages a bit more user friendly
 * 19/03/2023:		Fixed compilation warnings in mos_cmdTIME
 * 21/03/2023:		Added mos_SETINTVECTOR, uses VDP values from defines.h
 * 26/03/2023:		Fixed SET KEYBOARD command
 * 14/04/2023:		Added fat_EOF
 * 15/04/2023:		Added mos_GETFIL, mos_FREAD, mos_FWRITE, mos_FLSEEK, refactored MOS file commands
 * 30/05/2023:		Fixed bug in mos_parseNumber to detect invalid numeric characters, mos_FGETC now returns EOF flag
 * 08/07/2023:		Added mos_trim function; mos_exec now trims whitespace from input string, various bug fixes
 * 15/09/2023:		Function mos_trim now includes the asterisk character as whitespace
 * 26/09/2023:		Refactored mos_GETRTC and mos_SETRTC
 * 10/11/2023:		Added CONSOLE to mos_cmdSET
 * 11/11/2023:		Added mos_cmdHELP, mos_cmdTYPE, mos_cmdCLS, mos_cmdMOUNT, mos_mount
 */

#include <eZ80.h>
#include <defines.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "defines.h"
#include "mos.h"
#include "config.h"
#include "mos_editor.h"
#include "uart.h"
#include "clock.h"
#include "ff.h"
#include "strings.h"
#include "umm_malloc.h"
#if DEBUG > 0
# include "tests.h"
#endif /* DEBUG */

char  	cmd[256];				// Array for the command line handler

extern void *	set_vector(unsigned int vector, void(*handler)(void));	// In vectors16.asm

extern int 		exec16(UINT24 addr, char * params);	// In misc.asm
extern int 		exec24(UINT24 addr, char * params);	// In misc.asm

extern BYTE scrcols, scrcolours, scrpixelIndex; // In globals.asm
extern volatile	BYTE keyascii;					// In globals.asm
extern volatile	BYTE vpd_protocol_flags;		// In globals.asm
extern BYTE 	rtc;							// In globals.asm

static FATFS	fs;					// Handle for the file system
static char * 	mos_strtok_ptr;		// Pointer for current position in string tokeniser

TCHAR cwd[256];						// Hold current working directory.
BOOL sdcardDelay = FALSE;

extern volatile BYTE history_no;

t_mosFileObject	mosFileObjects[MOS_maxOpenFiles];

BOOL	vdpSupportsTextPalette = FALSE;


// Array of MOS commands and pointer to the C function to run
// NB this list is iterated over, so the order is important
// both for abbreviations and for the help command
//
static t_mosCommand mosCommands[] = {
	{ ".", 			&mos_cmdDIR,		HELP_CAT_ARGS,		HELP_CAT },
	{ "CAT",		&mos_cmdDIR,		HELP_CAT_ARGS,		HELP_CAT },
	{ "CD", 		&mos_cmdCD,			HELP_CD_ARGS,		HELP_CD },
	{ "CDIR", 		&mos_cmdCD,			HELP_CD_ARGS,		HELP_CD },
	{ "CLS",		&mos_cmdCLS,		NULL,			HELP_CLS },
	{ "COPY", 		&mos_cmdCOPY,		HELP_COPY_ARGS,		HELP_COPY },
	{ "CP", 		&mos_cmdCOPY,		HELP_COPY_ARGS,		HELP_COPY },
	{ "CREDITS",	&mos_cmdCREDITS,	NULL,			HELP_CREDITS },
	{ "DELETE",		&mos_cmdDEL,		HELP_DELETE_ARGS,	HELP_DELETE },
	{ "DIR",		&mos_cmdDIR,		HELP_CAT_ARGS,		HELP_CAT },
	{ "DISC",		&mos_cmdDISC,		NULL,		NULL },
	{ "ECHO",		&mos_cmdECHO,		HELP_ECHO_ARGS,		HELP_ECHO },
	{ "ERASE",		&mos_cmdDEL,		HELP_DELETE_ARGS,	HELP_DELETE },
	{ "EXEC",		&mos_cmdEXEC,		HELP_EXEC_ARGS,		HELP_EXEC },
	{ "HELP",		&mos_cmdHELP,		HELP_HELP_ARGS,		HELP_HELP },
	{ "JMP",		&mos_cmdJMP,		HELP_JMP_ARGS,		HELP_JMP },
	{ "LOAD",		&mos_cmdLOAD,		HELP_LOAD_ARGS,		HELP_LOAD },
	{ "LS",			&mos_cmdDIR,		HELP_CAT_ARGS,		HELP_CAT },
	{ "HOTKEY",		&mos_cmdHOTKEY,		HELP_HOTKEY_ARGS,	HELP_HOTKEY },
	{ "MEM",		&mos_cmdMEM,		NULL,		HELP_MEM },
	{ "MKDIR", 		&mos_cmdMKDIR,		HELP_MKDIR_ARGS,	HELP_MKDIR },
	{ "MOUNT",		&mos_cmdMOUNT,		NULL,			HELP_MOUNT },
	{ "MOVE",		&mos_cmdREN,		HELP_RENAME_ARGS,	HELP_RENAME },
	{ "MV",			&mos_cmdREN,		HELP_RENAME_ARGS,	HELP_RENAME },
	{ "PRINTF",		&mos_cmdPRINTF,		HELP_PRINTF_ARGS,	HELP_PRINTF },
	{ "RENAME",		&mos_cmdREN,		HELP_RENAME_ARGS,	HELP_RENAME },
	{ "RM",			&mos_cmdDEL,		HELP_DELETE_ARGS,	HELP_DELETE },
	{ "RUN", 		&mos_cmdRUN,		HELP_RUN_ARGS,		HELP_RUN },
	{ "SAVE", 		&mos_cmdSAVE,		HELP_SAVE_ARGS,		HELP_SAVE },
	{ "SET",		&mos_cmdSET,		HELP_SET_ARGS,		HELP_SET },
	{ "TIME", 		&mos_cmdTIME,		HELP_TIME_ARGS,		HELP_TIME },
	{ "TYPE",		&mos_cmdTYPE,		HELP_TYPE_ARGS,		HELP_TYPE },
	{ "VDU",		&mos_cmdVDU,		HELP_VDU_ARGS,		HELP_VDU },
#if DEBUG > 0
	{ "RUN_MOS_TESTS",		&mos_cmdTEST,		NULL,		"Run the MOS OS test suite" },
#endif /* DEBUG */
};

#define mosCommands_count (sizeof(mosCommands)/sizeof(t_mosCommand))

// Array of file errors; mapped by index to the error numbers returned by FatFS
//
static char * mos_errors[] = {
	"OK",
	"Error accessing SD card",
	"Assertion failed",
	"SD card failure",
	"Could not find file",
	"Could not find path",
	"Invalid path name",
	"Access denied or directory full",
	"Access denied",
	"Invalid file/directory object",
	"SD card is write protected",
	"Logical drive number is invalid",
	"Volume has no work area",
	"No valid FAT volume",
	"Error occurred during mkfs",
	"Volume timeout",
	"Volume locked",
	"LFN working buffer could not be allocated",
	"Too many open files",
	"Invalid parameter",
	// MOS-specific errors beyond this point (index 20+)
	"Invalid command",
	"Invalid executable",
	"Out of memory",
	"Not implemented",
	"Load overlaps system area",
	"Bad string",
};

#define mos_errors_count (sizeof(mos_errors)/sizeof(char *))

// Output a file error
// Parameters:
// - error: The FatFS error number
//
void mos_error(int error) {
	if(error >= 0 && error < mos_errors_count) {
		printf("\n\r%s\n\r", mos_errors[error]);
	}
}

// Wait for a keycode character from the VPD
// Returns:
// - ASCII keycode
//
BYTE mos_getkey() {
	BYTE ch = 0;
	while(ch == 0) {		// Loop whilst no key pressed
		ch = keyascii;		// Variable keyascii is updated by interrupt
	}
	keyascii = 0;			// Reset keycode to debounce the key
	return ch;
}

// Call the line editor from MOS
// Used by main loop to get input from the user
// Parameters:
// - buffer: Pointer to the line edit buffer
// - bufferLength: Size of the line edit buffer in bytes
// Returns:
// - The keycode (ESC or CR)
//
UINT24 mos_input(char * buffer, int bufferLength) {
	INT24 retval;
	printf("%s %c", cwd, MOS_prompt);
	retval = mos_EDITLINE(buffer, bufferLength, 3);
	printf("\n\r");
	return retval;
}

// Parse a MOS command from the line edit buffer
// Parameters:
// - ptr: Pointer to the MOS command in the line edit buffer
// Returns:
// - Function pointer, or 0 if command not found
//
t_mosCommand *mos_getCommand(char * ptr) {
	int	   i;
	t_mosCommand * cmd;	
	for(i = 0; i < mosCommands_count; i++) {
		cmd = &mosCommands[i];
		if(mos_cmp(cmd->name, ptr) == 0) {
			//return cmd->func;
			return cmd;
		}
	}
	return NULL;
}

// Case insensitive commpare with abbreviations
// Parameters:
// - p1: The command to be compared against
// - p2: The inputted command
//
BOOL mos_cmp(const char *p1, const char *p2) {
	char c1;
	char c2;	
	do {		
		c1 = toupper(*p1++);
		c2 = toupper(*p2++);
		if(c2 == '.') {
			c1 = 0;
			c2 = 0;
		}
		if(c1 < 0x20) c1 = 0;
		if(c2 < 0x20) c2 = 0;
	} while(c1 && c2 && c1 == c2);
	return (const unsigned char*)c1 - (const unsigned char*)c2;
}

// String trim function
// NB: This also includes the asterisk character as whitespace
// Parameters:
// - s: Pointer to the string to trim
// Returns:
// - s: Pointer to the start of the new string
//
char * mos_trim(char * s) {
    char * ptr;

    if(!s) {								// Return NULL if a null string is passed
        return NULL;
	}
    if(!*s) {
        return s;      						// Handle empty string
	}
	while(isspace(*s) || *s == '*') {		// Advance the pointer to the first non-whitespace or asterisk character in the string
		s++;
	}
	ptr = s + strlen(s) - 1;
	while(ptr > s && isspace(*ptr)) {
		ptr--;
	}
	ptr[1] = '\0';
    return s;
}

// String tokeniser
// Parameters:
// - s1: String to tokenise
// - s2: Delimiter
// - ptr: Pointer to store the current position in (mos_strtok_r)
// Returns:
// - Pointer to tokenised string
//
char * mos_strtok(char *s1, char * s2) {
	return mos_strtok_r(s1, s2, &mos_strtok_ptr);
}

char * mos_strtok_r(char *s1, const char *s2, char **ptr) {
	char *end;

	if (s1 == NULL) {
		s1 = *ptr;
	}
	
	if (*s1 == '\0') {
		*ptr = s1;
		return NULL;
    }
	// Scan leading delimiters
	//
	s1 += strspn(s1, s2);
	if (*s1 == '\0') {
		*ptr = s1;
		return NULL;
    }
	// Find the end of the token
	//
	end = s1 + strcspn(s1, s2);
	if (*end == '\0') {
      *ptr = end;
      return s1;
    }
	// Terminate the token and make *SAVE_PTR point past it
	//
	*end = '\0';
	*ptr = end + 1;
	
	return s1;
}

// Parse a number from the line edit buffer
// Parameters:
// - ptr: Pointer to the number in the line edit buffer
// - p_Value: Pointer to the return value
// Returns:
// - true if the function succeeded, otherwise false
//
BOOL mos_parseNumber(char * ptr, UINT24 * p_Value) {
	char * 	p = ptr;
	char * 	e;
	int 	base = 10;
	long 	value;

	p = mos_strtok(p, " ");
	if(p == NULL) {
		return 0;
	}
	if(*p == '&') {
		base = 16;
		p++;
	}	
	value = strtol(p, &e, base);
	if(*e != 0) {
		return 0;
	}
	*p_Value = value;
	return 1;
}

// Parse a string from the line edit buffer
// Parameters:
// - ptr: Pointer to the string in the line edit buffer
// - p_Value: Pointer to the return value
// Returns:
// - true if the function succeeded, otherwise false
//
BOOL mos_parseString(char * ptr, char ** p_Value) {
	char *	p = ptr;

	p = mos_strtok(p, " ");
	if(p == NULL) {
		return 0;
	}
	*p_Value = p;
	return 1;
}

int mos_runBin(UINT24 addr) {
	UINT8 mode = mos_execMode((UINT8 *)addr);
	switch(mode) {
		case 0:		// Z80 mode
			return exec16(addr, mos_strtok_ptr);
			break;
		case 1: 	// ADL mode
			return exec24(addr, mos_strtok_ptr);
			break;	
		default:	// Unrecognised header
			return MOS_INVALID_EXECUTABLE;
			break;
	}
}

// Execute a MOS command
// Parameters:
// - buffer: Pointer to a zero terminated string that contains the MOS command with arguments
// Returns:
// - MOS error code
//
int mos_exec(char * buffer, BOOL in_mos) {
	char * 	ptr;
	int 	fr = 0;
	int 	(*func)(char * ptr);
	char	path[256];
	UINT8	mode;
	t_mosCommand *cmd;

	ptr = mos_trim(buffer);
	if (ptr != NULL && *ptr == '#') {
	    return FR_OK;
	}

	ptr = mos_strtok(ptr, " ");
	if (ptr != NULL) {
		cmd = mos_getCommand(ptr);
		func = cmd->func;
		if (cmd != NULL && func != 0) {
			return func(ptr);
		}
		else {		
			if (strlen(ptr) > 246) {	// Maximum command length (to prevent buffer overrun)
				return MOS_INVALID_COMMAND;
			}
			else {
				sprintf(path, "/mos/%s.bin", ptr);
				fr = mos_LOAD(path, MOS_starLoadAddress, 0);
				if (fr == FR_OK) {
					return mos_runBin(MOS_starLoadAddress);
				}
				if (fr == MOS_OVERLAPPING_SYSTEM) {
					return fr;
				}
				
				if (in_mos) {
					sprintf(path, "%s.bin", ptr);
					fr = mos_LOAD(path, MOS_defaultLoadAddress, 0);
					if (fr == FR_OK) {
						return mos_runBin(MOS_defaultLoadAddress);
					}
					if (fr == MOS_OVERLAPPING_SYSTEM) {
						return fr;
					}
					sprintf(path, "/bin/%s.bin", ptr);
					fr = mos_LOAD(path, MOS_defaultLoadAddress, 0);
					if (fr == FR_OK) {
						return mos_runBin(MOS_defaultLoadAddress);
					}
					if (fr == MOS_OVERLAPPING_SYSTEM) {
						return fr;
					}
				}				
				if (fr == FR_NO_FILE || fr == FR_NO_PATH) {
					return MOS_INVALID_COMMAND;
				}
			}
		}
	}
	return fr;
}

// Get the MOS Z80 execution mode
// Parameters:
// - ptr: Pointer to the code block
// Returns:
// - 0: Z80 mode
// - 1: ADL mode
//
UINT8 mos_execMode(UINT8 * ptr) {
	if(
		*(ptr+0x40) == 'M' &&
		*(ptr+0x41) == 'O' &&
		*(ptr+0x42) == 'S'
	) {
		return *(ptr+0x44);
	}
	return 0xFF;
}

int mos_cmdDISC(char *ptr) {
	sdcardDelay = TRUE;
	return 0;
}

// DIR command
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdDIR(char * ptr) {
	BOOL longListing = FALSE;
	char	*path;

	for (;;) {
		if (!mos_parseString(NULL, &path)) {
			return mos_DIR(".", longListing);
		}
		if (strcasecmp(path, "-l") == 0) {
			longListing = TRUE;
		} else {
			break;
		}
	}
	return mos_DIR(path, longListing);
}

// ECHO command
//
int mos_cmdECHO(char *ptr) {
	int c;
	const char *p = mos_strtok_ptr;

	while (*p) {
		switch (*p) {
			case '|': {
				// interpret pipe-escaped characters
				p++;

				if (*p == 0) {
					// no more characters, so this is an error
					return MOS_BAD_STRING;
				} else if (*p == '?') {
					// prints character 127
					putch(0x7F);
					p++;
				} else if (*p == '!') {
					// prints next character with top bit set
					p++;
					if (*p == 0) {
						// no more characters, so this is an error
						return MOS_BAD_STRING;
					}
					putch(*p++ | 0x80);
				} else if (*p >= 0x40 && *p < 0x7F) {
					// characters from &40-7F (letters and some punctuation)
					// are printed as just their bottom 5 bits
					// (which Acorn documents as CTRL( ASCII(uppercase(char) – 64))
					putch(*p & 0x1F);
					p++;
				} else {
					// all other characters are passed thru
					putch(*p);
					p++;
				}

				break;
			}

			case '<': {
				// possibly a number or variable
				// so search for an end tag
				char *end = p + 1;
				while (*end && *end != '>') {
					end++;
				}
				if (*end == '>' && end > p + 1) {
					// we have one - so is this a number?
					int number = 0;
					int base = 10;
					char *endptr = NULL;
					char *start = p + 1;
					p++;
					*end = '\0';
					// number can be decimal, &hex, or base_number
					if (*p == '&') {
						base = 16;
						p++;
					} else {
						char *underscore = strchr(p, '_');
						if (underscore != NULL && underscore > p) {
							char *baseEnd;
							*underscore = '\0';
							base = strtol(p, &baseEnd, 10);
							if (baseEnd != underscore) {
								// we didn't use all chars before underscore, so invalid base
								base = -1;
							}
							// Move p pointer to the number part
							p = underscore + 1;
							*underscore = '_';
						}
					}

					if (base > 1 && base <= 36) {
						number = strtol(p, &endptr, base);
					}

					if (endptr != end) {
						// we didn't consume whole string, so it was not a valid number
						// we therefore should interpret it as a variable
						// TODO
						#if DEBUG > 0
						printf("variable: '%.*s'\n\r", end - p, p);
						#endif
					} else {
						putch(number & 0xFF);
					}
					*end = '>';
					p = end + 1;
				} else {
					// no end tag, so just print the character
					putch(*p);
					p++;
				}
				break;
			}
			default:
				putch(*p);
				p++;
				break;
		}
	}

	printf("\r\n");
	return FR_OK;
}

// Assumes isxdigit(digit)
static int xdigit_to_int(char digit) {
	digit = toupper(digit);
	if (digit < 'A') {
		return digit - '0';
	} else {
		return digit - 55;
	}
}

// PRINTF command
//
int mos_cmdPRINTF(char *ptr) {
	int c;
	const char *p = mos_strtok_ptr;

	while (*p) {
		switch (*p) {
			case '\\': {
				// interpret escaped characters
				p++;
				if (*p == '\\') {
					putch('\\');
					p++;
				} else if (*p == 'r') {
					putch('\r');
					p++;
				} else if (*p == 'n') {
					putch('\n');
					p++;
				} else if (*p == 'f') {
					putch(12);
					p++;
				} else if (*p == 't') {
					putch('\t');
					p++;
				} else if (*p == 'x') {
					p++;
					c = 0;
					if (isxdigit(*p)) {
						c = xdigit_to_int(*p);
						p++;
						if (isxdigit(*p)) {
							c = c * 16 + xdigit_to_int(*p);
							p++;
						}
					}
					putch(c);
				} else {
					// invalid. skip it entirely
					if (*p) p++;
				}
				break;
			}
			default:
				putch(*p);
				p++;
				break;
		}
	}

	return 0;
}

// HOTKEY command
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdHOTKEY(char *ptr) {
	UINT24 fn_number = 0;
	char *hotkey_string;

	if (!mos_parseNumber(NULL, &fn_number)) {
		UINT8 key;
		printf("Hotkey assignments:\r\n\r\n");

		for (key = 0; key < 12; key++) {
				printf("F%d: %s\r\n", key+1, hotkey_strings[key] == NULL ? "N/A" : hotkey_strings[key]);
		}

		printf("\r\n");
		return 0;
	}

	if (fn_number < 1 || fn_number > 12) {
		printf("Invalid FN-key number.\r\n");
		return 0;
	}

	if (strlen(mos_strtok_ptr) < 1) {		
		if (hotkey_strings[fn_number - 1] != NULL) {
			umm_free(hotkey_strings[fn_number - 1]);
			hotkey_strings[fn_number - 1] = NULL;
			printf("F%u cleared.\r\n", fn_number);
		} else printf("F%u already clear, no hotkey command provided.\r\n", fn_number);
		
		return 0;
	}

	if (mos_strtok_ptr[0] == '\"' && mos_strtok_ptr[strlen(mos_strtok_ptr) - 1] == '\"') {
		mos_strtok_ptr[strlen(mos_strtok_ptr) - 1] = '\0';
		mos_strtok_ptr++;		
	}

	if (hotkey_strings[fn_number - 1] != NULL) umm_free(hotkey_strings[fn_number - 1]);

	hotkey_strings[fn_number - 1] = umm_malloc((strlen(mos_strtok_ptr) + 1) * sizeof(char));
	if (!hotkey_strings[fn_number - 1]) return FR_INT_ERR;
	strncpy(hotkey_strings[fn_number - 1], mos_strtok_ptr, strlen(mos_strtok_ptr));
	hotkey_strings[fn_number - 1][strlen(mos_strtok_ptr)] = '\0';

	return 0;
}

// LOAD <filename> <addr> command
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdLOAD(char * ptr) {
	FRESULT	fr;
	char *  filename;
	UINT24 	addr;
	
	if(
		!mos_parseString(NULL, &filename)
	) {
		return FR_INVALID_PARAMETER;
	}
	if(!mos_parseNumber(NULL, &addr)) addr = MOS_defaultLoadAddress;
	fr = mos_LOAD(filename, addr, 0);
	return fr;	
}

// EXEC <filename>
//   Run a batch file containing MOS commands
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdEXEC(char *ptr) {
	FRESULT	fr;
	char *  filename;
	UINT24 	addr;
	char    buf[256];
	
	if(
		!mos_parseString(NULL, &filename)
	) {
		return FR_INVALID_PARAMETER;
	}
	fr = mos_EXEC(filename, buf, sizeof buf);
	return fr;
}

// SAVE <filename> <addr> <len> command
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdSAVE(char * ptr) {
	FRESULT	fr;
	char *  filename;
	UINT24 	addr;
	UINT24 	size;
	
	if(
		!mos_parseString(NULL, &filename) ||
		!mos_parseNumber(NULL, &addr) ||
		!mos_parseNumber(NULL, &size)
	) {
		return FR_INVALID_PARAMETER;
	}
	fr = mos_SAVE(filename, addr, size);
	return fr;
}

// DEL <filename> command
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdDEL(char * ptr) {
	FRESULT	fr;
	DIR dir;
	static FILINFO fno;
	char *dirPath = NULL;
	char *pattern = NULL;
	BOOL usePattern = FALSE;
	BOOL force = FALSE;
	char *filename;
	char *lastSeparator;
	char verify[7];

	if (
		!mos_parseString(NULL, &filename) 
	) {
		return FR_INVALID_PARAMETER;
	}

	if (strcasecmp(filename, "-f") == 0) {
		force = TRUE;
		if (!mos_parseString(NULL, &filename)) {
			return FR_INVALID_PARAMETER;
		}
	}

	fr = FR_INT_ERR;

	lastSeparator = strrchr(filename, '/');

	if (strchr(filename, '*') != NULL) {
		usePattern = TRUE;
		if (filename[0] == '/' && strchr(filename + 1, '/') == NULL) {
			dirPath = mos_strdup("/");
			if (!dirPath) return FR_INT_ERR;
			if (strchr(filename + 1, '*') != NULL) {
				pattern = mos_strdup(filename + 1);
				if (!pattern) goto cleanup;
			}
		} else if (lastSeparator != NULL) {
			dirPath = mos_strndup(filename, lastSeparator - filename);
			if (!dirPath) return FR_INT_ERR;

			pattern = mos_strdup(lastSeparator + 1);
			if (!pattern) {
				umm_free(dirPath);
				return FR_INT_ERR;
			}
        } else {
			dirPath = mos_strdup(".");
			pattern = mos_strdup(filename);
			if (!dirPath || !pattern) {
				if (dirPath) umm_free(dirPath);
				if (pattern) umm_free(pattern);
				return FR_INT_ERR;
			}
        }
	} else {
		dirPath = mos_strdup(filename);
		if (!dirPath) return FR_INT_ERR;
	}	

	if (usePattern) {
		fr = f_opendir(&dir, dirPath);
		if (fr != FR_OK) goto cleanup;

		fr = f_findfirst(&dir, &fno, dirPath, pattern);
		while (fr == FR_OK && fno.fname[0] != '\0') {
			size_t fullPathLen = strlen(dirPath) + strlen(fno.fname) + 2;
			char *fullPath = umm_malloc(fullPathLen);
			if (!fullPath) {
				fr = FR_INT_ERR;
				break;
			}

			sprintf(fullPath, "%s/%s", dirPath, fno.fname);  // Construct full path

			if (!force) {
				INT24 retval;
				// we could potentially support "All" here, and when detected changing `force` to true
				printf("Delete %s? (Yes/No/Cancel) ", fullPath);
				retval = mos_EDITLINE(&verify, sizeof(verify), 13);
				printf("\n\r");
				if (retval == 13) {
					if (strcasecmp(verify, "Cancel") == 0 || strcasecmp(verify, "C") == 0) {
						printf("Cancelled.\r\n");
						umm_free(fullPath);
						break;
					}
					if (strcasecmp(verify, "Yes") == 0 || strcasecmp(verify, "Y") == 0) {
						printf("Deleting %s.\r\n", fullPath);
						fr = f_unlink(fullPath);
					}
				} else {
					printf("Cancelled.\r\n");
					umm_free(fullPath);
					break;
				}
			} else {
				printf("Deleting %s\r\n", fullPath);
				fr = f_unlink(fullPath);
			}
			umm_free(fullPath);

			if (fr != FR_OK) break;
			fr = f_findnext(&dir, &fno);
		}

		f_closedir(&dir);
		printf("\r\n");
	} else {
		fr = f_unlink(filename);
	}

	cleanup:
		if (dirPath) umm_free(dirPath);
		if (pattern) umm_free(pattern);
		return fr;
}

// JMP <addr> command
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdJMP(char *ptr) {
	UINT24 	addr;
	void (* dest)(void) = 0;
	if(!mos_parseNumber(NULL, &addr)) {
		return FR_INVALID_PARAMETER;
	};
	dest = (void *)addr;
	dest();
	return 0;
}

// RUN <addr> command
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdRUN(char *ptr) {
	UINT24 	addr;
	
	if(!mos_parseNumber(NULL, &addr)) {
		addr = MOS_defaultLoadAddress;
	}
	return mos_runBin(addr);
}

// CD <path> command
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdCD(char * ptr) {
	char *  path;
	
	FRESULT	fr;
	
	if(
		!mos_parseString(NULL, &path) 
	) {
		return FR_INVALID_PARAMETER;
	}
	fr = f_chdir(path);
	f_getcwd(cwd, sizeof(cwd)); //Update full path.
	return fr;
}

// REN <filename1> <filename2> command
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdREN(char *ptr) {
	FRESULT	fr;
	char *  filename1;
	char *	filename2;
	
	if(
		!mos_parseString(NULL, &filename1) ||
		!mos_parseString(NULL, &filename2)
	) {
		return FR_INVALID_PARAMETER;
	}
	fr = mos_REN(filename1, filename2, TRUE);
	return fr;
}

// COPY <filename1> <filename2> command
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdCOPY(char *ptr) {
	FRESULT	fr;
	char *  filename1;
	char *	filename2;
	
	if(
		!mos_parseString(NULL, &filename1) ||
		!mos_parseString(NULL, &filename2)
	) {
		return FR_INVALID_PARAMETER;
	}
	fr = mos_COPY(filename1, filename2, TRUE);
	return fr;
}

// MKDIR <filename> command
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdMKDIR(char * ptr) {
	char *  filename;
	
	FRESULT	fr;
	
	if(
		!mos_parseString(NULL, &filename) 
	) {
		return FR_INVALID_PARAMETER;
	}
	fr = mos_MKDIR(filename);
	return fr;
}

// SET <option> <value> command
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdSET(char * ptr) {
	char *	command;
	UINT24 	value;
	
	if(
		!mos_parseString(NULL, &command) ||
		!mos_parseNumber(NULL, &value)
	) {
		return FR_INVALID_PARAMETER;
	}
	if(strcasecmp(command, "KEYBOARD") == 0) {
		putch(23);
		putch(0);
		putch(VDP_keycode);
		putch(value & 0xFF);
		return 0;
	}
	if(strcasecmp(command, "CONSOLE") == 0 && value <= 1) {
		putch(23);
		putch(0);
		putch(VDP_consolemode);
		putch(value & 0xFF);
		return 0;
	}
	return FR_INVALID_PARAMETER;
}

// VDU <char1> <char2> ... <charN>
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdVDU(char *ptr) {
    char *value_str;
    UINT24 value = 0;
    
    while (mos_parseString(NULL, &value_str)) {
        UINT8 is_word = 0;
        UINT8 base = 10;
        char *endPtr;
        size_t len = strlen(value_str);

        //Strip semicolon notation and set as word
        if (len > 0 && value_str[len - 1] == ';') {
            value_str[len - 1] = '\0';
            len--;
            is_word = 1;
        }

        // Check for '0x' or '0X' prefix
        if (len > 2 && (value_str[0] == '0' && tolower(value_str[1] == 'x'))) {
            base = 16;
        }
		
        // Check for '&' prefix
        if (value_str[0] == '&') {
            base = 16;
            value_str++;
            len--;
        }
        // Check for 'h' suffix
        if (len > 0 && tolower(value_str[len - 1]) == 'h') {
            value_str[len - 1] = '\0';
            base = 16;
        }

        value = strtol(value_str, &endPtr, base);

        if (*endPtr != '\0' || value > 65535) {
            return FR_INVALID_PARAMETER;
        }
		
        if (value > 255) {
            is_word = 1;
        }

        if (is_word) {
            putch(value & 0xFF); // write LSB
            putch(value >> 8);   // write MSB
        } else {
            putch(value);
        }
    }

    return 0;
}

// TIME
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - 0
//
int mos_cmdTIME(char *ptr) {
	UINT24	yr, mo, da, ho, mi, se;
	char	buffer[64];

	// If there is a first parameter
	//
	if(mos_parseNumber(NULL, &yr)) {
		//
		// Fetch the rest of the parameters
		//
		if(
			!mos_parseNumber(NULL, &mo) ||
			!mos_parseNumber(NULL, &da) ||
			!mos_parseNumber(NULL, &ho) ||
			!mos_parseNumber(NULL, &mi) ||
			!mos_parseNumber(NULL, &se) 
		) {
			return FR_INVALID_PARAMETER;
		}
		buffer[0] = yr - EPOCH_YEAR;
		buffer[1] = mo;
		buffer[2] = da;
		buffer[3] = ho;
		buffer[4] = mi;
		buffer[5] = se;
		mos_SETRTC((UINT24)buffer);
	}
	// Return the new time
	//
	mos_GETRTC((UINT24)buffer);
	printf("%s\n\r", buffer);
	return 0;
}

extern void sysvars[];

// MEM
// Returns:
// - MOS error code
//
int mos_cmdMEM(char * ptr) {
	int try_len = HEAP_LEN;

	printf("ROM      &000000-&01ffff     %2d%% used\r\n", ((int)_low_romdata) / 1311);
	printf("USER:LO  &%06x-&%06x %6d bytes\r\n", 0x40000, (int)_low_data-1, (int)_low_data - 0x40000);
	// data and bss together
	printf("MOS:DATA &%06x-&%06x %6d bytes\r\n", _low_data, (int)_heapbot - 1, (int)_heapbot - (int)_low_data);
	printf("MOS:HEAP &%06x-&%06x %6d bytes\r\n", _heapbot, (int)_stack - SPL_STACK_SIZE - 1, HEAP_LEN);
	printf("STACK24  &%06x-&%06x %6d bytes\r\n", (int)_stack - SPL_STACK_SIZE, _stack-1, SPL_STACK_SIZE);
	printf("USER:HI  &b7e000-&b7ffff   8192 bytes\r\n");
	printf("\r\n");

	// find largest kmalloc contiguous region
	for (; try_len > 0; try_len-=8) {
		void *p = umm_malloc(try_len);
		if (p) {
			umm_free(p);
			break;
		}
	}

	printf("Largest free MOS:HEAP fragment: %d bytes\r\n", try_len);
	printf("Sysvars at &%06x\r\n", sysvars);
	printf("\r\n");

	return 0;
}

// CREDITS
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdCREDITS(char *ptr) {
	printf("FabGL 1.0.8 (c) 2019-2022 by Fabrizio Di Vittorio\n\r");
	printf("FatFS R0.14b (c) 2021 ChaN\n\r");
	printf("umm_malloc Copyright (c) 2015 Ralph Hempel\n\r");
	printf("\n\r");
	return 0;
}

// TYPE <filename>
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdTYPE(char * ptr) {
	FRESULT	fr;
	char *  filename;

	if(!mos_parseString(NULL, &filename))
		return FR_INVALID_PARAMETER;

	fr = mos_TYPE(filename);
	return fr;
}

// CLS
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int	mos_cmdCLS(char *ptr) {
	putchar(12);
	return 0;
}

// MOUNT
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int	mos_cmdMOUNT(char *ptr) {
	int fr;

	fr = mos_mount();
	if (fr != FR_OK)
		mos_error(fr);
	f_getcwd(cwd, sizeof(cwd)); //Update full path.
	return 0;
}

void printCommandInfo(t_mosCommand * cmd, BOOL full) {
	int aliases = 0;
	int i;

	if (cmd->help == NULL) return;

	printf("%s", cmd->name);
	if (cmd->args != NULL)
		printf(" %s", cmd->args);
	
	// find aliases
	for (i = 0; i < mosCommands_count; ++i) {
		if (mosCommands[i].func == cmd->func && mosCommands[i].name != cmd->name) {
			aliases++;
		}
	}
	if (aliases > 0) {
		// print the aliases
		printf(" (Aliases: ");
		for (i = 0; i < mosCommands_count; ++i) {
			if (mosCommands[i].func == cmd->func && mosCommands[i].name != cmd->name) {
				printf("%s", mosCommands[i].name);
				if (aliases == 2) {
					printf(" and ");
				} else if (aliases > 1) {
					printf(", ");
				}
				aliases--;
			}
		}
		printf(")");
	}

	printf("\r\n");
	if (full) {
		printf("%s\r\n", cmd->help);
	}
}

// HELP
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// -  0: Success
//
int mos_cmdHELP(char *ptr) {
	int i;
	char *cmd;

	BOOL hasCmd = mos_parseString(NULL, &cmd);
	if (!hasCmd) {
		cmd = "help";
	}

	for (i = 0; i < mosCommands_count; ++i) {
		if (strcasecmp(cmd, mosCommands[i].name) == 0) {
			printCommandInfo(&mosCommands[i], TRUE);
			if (!hasCmd) {
				// must be showing "help" command with no args, so show list of all commands
				int col = 0;
				int maxCol = scrcols;
				printf("List of commands:\r\n");
				for (i = 1; i < mosCommands_count; ++i) {
					if (mosCommands[i].help == NULL) continue;
					if (col + strlen(mosCommands[i].name) + 2 >= maxCol) {
						printf("\r\n");
						col = 0;
					}
					printf("%s", mosCommands[i].name);
					if (i < mosCommands_count - 1) {
						printf(", ");
					}
					col += strlen(mosCommands[i].name) + 2;
				}
				printf("\r\n");
			}
			return 0;
		}
	}

	if (hasCmd && strcasecmp(cmd, "all") == 0) {
		for (i = 0; i < mosCommands_count; ++i) {
			printCommandInfo(&mosCommands[i], FALSE);
		}
		return 0;
	}

	printf("Command not found: %s\r\n", cmd);
	return 0;
}

// Load a file from SD card to memory
// Parameters:
// - filename: Path of file to load
// - address: Address in RAM to load the file into
// - size: Number of bytes to load, 0 for maximum file size
// Returns:
// - FatFS return code
// 
UINT24 mos_LOAD(char * filename, UINT24 address, UINT24 size) {
	FRESULT	fr;
	FIL	   	fil;
	UINT   	br;	
	FSIZE_t fSize;
	
	fr = f_open(&fil, filename, FA_READ);
	if(fr == FR_OK) {
		fSize = f_size(&fil);
		if(size) {
			// Maximize load according to size parameter
			if(fSize < size) size = fSize;
		}
		else {
			// Load the full file size
			size = fSize;
		}
		// Check potential system area overlap
		if((address <= MOS_externLastRAMaddress) && ((address + size) > MOS_systemAddress)) {
			fr = MOS_OVERLAPPING_SYSTEM;
		}
		else {
			fr = f_read(&fil, (void *)address, size, &br);		
		}		
	}
	f_close(&fil);	
	return fr;
}

// Save a file from memory to SD card
// Parameters:
// - filename: Path of file to save
// - address: Address in RAM to save the file from
// - size: Number of bytes to save
// Returns:
// - FatFS return code
// 
UINT24	mos_SAVE(char * filename, UINT24 address, UINT24 size) {
	FRESULT	fr;
	FIL	   	fil;
	UINT   	br;	
	
	fr = f_open(&fil, filename, FA_WRITE | FA_CREATE_NEW);
	if(fr == FR_OK) {
		fr = f_write(&fil, (void *)address, size, &br);
	}
	f_close(&fil);	
	return fr;
}

// Display a file from SD card on the screen
// Parameters:
// - filename: Path of file to load
// Returns:
// - FatFS return code
//
UINT24 mos_TYPE(char * filename) {
	FRESULT	fr;
	FIL		fil;
	UINT   	br;
	char	buf[512];
	int		i;

	fr = f_open(&fil, filename, FA_READ);
	if (fr != FR_OK) {
		return fr;
	}

	while (1) {
		fr = f_read(&fil, (void *)buf, sizeof buf, &br);
		if (br == 0)
			break;
		for (i = 0; i < br; ++i)
			putchar(buf[i]);
	}

	f_close(&fil);
	return FR_OK;
}

// Change directory
// Parameters:
// - filename: Path of file to save
// Returns:
// - FatFS return code
// 
UINT24	mos_CD(char *path) {
	FRESULT	fr;

	fr = f_chdir(path);
	return fr;
}


// Check if a path is a directory
BOOL isDirectory(char *path) {
	FILINFO fil;
	FRESULT fr;

	if (strcmp(path, ".") == 0 || strcmp(path, "..") == 0 || strcmp(path, "/") == 0) {
		return TRUE;
	}

	// check if destination is a directory
	fr = f_stat(path, &fil);

	return (fr == FR_OK) && fil.fname[0] && (fil.fattrib & AM_DIR);
}

static UINT24 get_num_dirents(const char* path, int* cnt) {
    FRESULT        fr;
    DIR            dir;
    static FILINFO fno;

    *cnt = 0;

    fr = f_opendir(&dir, path);

    if (fr == FR_OK) {
        for (;;) {
            fr = f_readdir(&dir, &fno);
            if (fr != FR_OK || fno.fname[0] == 0) {
                if (*cnt == 0 && fr == FR_DISK_ERR) {
                    fr = FR_NO_PATH;
                }
                break; // Break on error or end of dir
            }
            *cnt = *cnt + 1;
        }
    }

    f_closedir(&dir);

    return fr;
}

typedef struct SmallFilInfo {
    FSIZE_t fsize;   /* File size */
    WORD    fdate;   /* Modified date */
    WORD    ftime;   /* Modified time */
    BYTE    fattrib; /* File attribute */
    char*   fname;   /* umm_malloc'ed */
} SmallFilInfo;

static int cmp_filinfo(const SmallFilInfo* a, const SmallFilInfo* b) {
    if ((a->fattrib & AM_DIR) == (b->fattrib & AM_DIR)) {
        return strcasecmp(a->fname, b->fname);
    } else if (a->fattrib & AM_DIR) {
        return -1;
    } else {
        return 1;
    }
}

// Directory listing, for MOS API compatibility
// Returns:
// - FatFS return code
//
UINT24 mos_DIR_API(char* inputPath) {
    return mos_DIR(inputPath, TRUE);
}

UINT24	mos_DIRFallback(char * path, BOOL longListing, BOOL hideVolumeInfo) {
	FRESULT	fr;
	DIR	  	dir;
	static 	FILINFO  fno;
	int		yr, mo, da, hr, mi;
	char 	str[12];
	int 	col = 0;

	if (!hideVolumeInfo) {
		fr = f_getlabel("", str, 0);
		if(fr != 0) {
			return fr;
		}	
		printf("Volume: ");
		if(strlen(str) > 0) {
			printf("%s", str);
		}
		else {
			printf("<No Volume Label>");
		}
		printf("\n\r\n\r");
	}

	fr = f_opendir(&dir, path);
	if (fr == FR_OK) {
		for (;;) {
			fr = f_readdir(&dir, &fno);
			if (fr != FR_OK || fno.fname[0] == 0) {
				break;  // Break on error or end of dir
			}
			if (longListing) {
				yr = (fno.fdate & 0xFE00) >>  9;	// Bits 15 to  9, from 1980
				mo = (fno.fdate & 0x01E0) >>  5;	// Bits  8 to  5
				da = (fno.fdate & 0x001F);			// Bits  4 to  0
				hr = (fno.ftime & 0xF800) >> 11;	// Bits 15 to 11
				mi = (fno.ftime & 0x07E0) >>  5;	// Bits 10 to  5

				printf("%04d/%02d/%02d\t%02d:%02d %c %*lu %s\n\r", yr + 1980, mo, da, hr, mi, fno.fattrib & AM_DIR ? 'D' : ' ', 8, fno.fsize, fno.fname);
			} else {
				if (col + strlen(fno.fname) + 2 >= scrcols) {
					printf("\r\n");
					col = 0;
				}
                printf("%s  ", fno.fname);
				col += strlen(fno.fname) + 2;
			}
		}
	}
	if (!longListing) {
		printf("\r\n");
	}

	f_closedir(&dir);
	return fr;
}


// Directory listing
// Returns:
// - FatFS return code
//
UINT24 mos_DIR(char* inputPath, BOOL longListing) {
    FRESULT        fr;
    DIR            dir;
    char *         dirPath = NULL, *pattern = NULL;
    BOOL           usePattern = FALSE;
    BOOL           useColour = scrcolours > 2 && vdpSupportsTextPalette;
    char           str[12]; // Buffer for volume label
    int            yr, mo, da, hr, mi;
    int            longestFilename = 0;
    int            filenameLength = 0;
    static FILINFO filinfo;
    BYTE           textBg;
    BYTE           textFg = 15;
    BYTE           dirColour = 2;
    BYTE           fileColour = 15;
    SmallFilInfo * fnos = NULL, *fno = NULL;
    int            num_dirents, fno_num;

    fr = f_getlabel("", str, 0);
    if (fr != FR_OK) {
        return fr;
    }

    if (strchr(inputPath, '/') == NULL && strchr(inputPath, '*') != NULL) {
        dirPath = mos_strdup(".");
        if (!dirPath) {
			fr = mos_DIRFallback(inputPath, longListing, FALSE);
            goto cleanup;
		}
        pattern = mos_strdup(inputPath);
        if (!pattern) {
			fr = mos_DIRFallback(inputPath, longListing, FALSE);
            goto cleanup;
		}
        usePattern = TRUE;
    } else if (strcmp(inputPath, ".") == 0) {
        dirPath = mos_strdup(".");
        if (!dirPath) {
			fr = mos_DIRFallback(inputPath, longListing, FALSE);
            goto cleanup;
		}
    } else if (inputPath[0] == '/' && strchr(inputPath + 1, '/') == NULL) {
        dirPath = mos_strdup("/");
        if (!dirPath) {
			fr = mos_DIRFallback(inputPath, longListing, FALSE);
            goto cleanup;
		}
        if (strchr(inputPath + 1, '*') != NULL) {
            pattern = mos_strdup(inputPath + 1);
            if (!pattern) {
				fr = mos_DIRFallback(inputPath, longListing, FALSE);
                goto cleanup;
			}
            usePattern = TRUE;
        }
    } else {
        char* lastSeparator = strrchr(inputPath, '/');
        if (lastSeparator != NULL && *(lastSeparator + 1) != '\0') {
            dirPath = mos_strndup(inputPath, lastSeparator - inputPath + 1);
            if (!dirPath) {
				fr = mos_DIRFallback(inputPath, longListing, FALSE);
                goto cleanup;
			}
            dirPath[lastSeparator - inputPath + 1] = '\0';
            pattern = mos_strdup(lastSeparator + 1);
            if (!pattern) {
				fr = mos_DIRFallback(inputPath, longListing, FALSE);
                goto cleanup;
			}
            usePattern = TRUE;
        } else {
            dirPath = mos_strdup(inputPath);
            if (!dirPath) {
				fr = mos_DIRFallback(inputPath, longListing, FALSE);
                goto cleanup;
			}
        }
    }

    if (useColour) {
        readPalette(128, TRUE);
        textFg = scrpixelIndex;
        fileColour = textFg;
        readPalette(129, TRUE);
        textBg = scrpixelIndex;
        while (dirColour == textBg || dirColour == fileColour) {
            dirColour = (dirColour + 1) % scrcolours;
        }
    }

    fno_num = 0;
    fr = f_opendir(&dir, dirPath);

    if (fr == FR_OK) {
        printf("Volume: ");
        if (strlen(str) > 0) {
            printf("%s", str);
        } else {
            printf("<No Volume Label>");
        }
        printf("\n\r");

        if (strcmp(dirPath, ".") == 0) {
            f_getcwd(cwd, sizeof(cwd));
            printf("Directory: %s\r\n\r\n", cwd);
        } else
            printf("Directory: %s\r\n\r\n", dirPath);

		fr = get_num_dirents(dirPath, &num_dirents);

		if (num_dirents == 0) {
			printf("No files found\r\n");
			goto cleanup;
		}

		fnos = umm_malloc(sizeof(SmallFilInfo) * num_dirents);
		if (!fnos) {
			fr = mos_DIRFallback(inputPath, longListing, TRUE);
			goto cleanup;
		}

        if (usePattern) {
            fr = f_findfirst(&dir, &filinfo, dirPath, pattern);
        } else {
            fr = f_readdir(&dir, &filinfo);
        }

        while (fr == FR_OK && filinfo.fname[0]) {
            fnos[fno_num].fsize = filinfo.fsize;
            fnos[fno_num].fdate = filinfo.fdate;
            fnos[fno_num].ftime = filinfo.ftime;
            fnos[fno_num].fattrib = filinfo.fattrib;
            filenameLength = strlen(filinfo.fname) + 1;
            fnos[fno_num].fname = umm_malloc(filenameLength);
			if (!fnos[fno_num].fname) {
				fr = mos_DIRFallback(inputPath, longListing, TRUE);
				while (fno_num > 0) {
					umm_free(fnos[--fno_num].fname);
				}
				goto cleanup;
			}
            strcpy(fnos[fno_num].fname, filinfo.fname);
            if (filenameLength > longestFilename) {
                longestFilename = filenameLength;
            }
            fno_num++;

            if (usePattern) {
                fr = f_findnext(&dir, &filinfo);
            } else {
                fr = f_readdir(&dir, &filinfo);
            }

            if (!usePattern && filinfo.fname[0] == 0)
                break;
        }
	}
    f_closedir(&dir);

    if (fr == FR_OK) {
        int col = 0;
        int maxCols = scrcols / longestFilename;

		num_dirents = fno_num;
		qsort(fnos, num_dirents, sizeof(SmallFilInfo), cmp_filinfo);
        fno_num = 0;

        for (; fno_num < num_dirents; fno_num++) {
            fno = &fnos[fno_num];
            if (longListing) {
                yr = (fno->fdate & 0xFE00) >> 9;  // Bits 15 to  9, from 1980
                mo = (fno->fdate & 0x01E0) >> 5;  // Bits  8 to  5
                da = (fno->fdate & 0x001F);       // Bits  4 to  0
                hr = (fno->ftime & 0xF800) >> 11; // Bits 15 to 11
                mi = (fno->ftime & 0x07E0) >> 5;  // Bits 10 to  5

                if (useColour) {
                    BOOL isDir = fno->fattrib & AM_DIR;
                    printf("\x11%c%04d/%02d/%02d\t%02d:%02d %c %*lu \x11%c%s\n\r", textFg, yr + 1980, mo, da, hr, mi, isDir ? 'D' : ' ', 8, fno->fsize, isDir ? dirColour : fileColour, fno->fname);
                } else {
                    printf("%04d/%02d/%02d\t%02d:%02d %c %*lu %s\n\r", yr + 1980, mo, da, hr, mi, fno->fattrib & AM_DIR ? 'D' : ' ', 8, fno->fsize, fno->fname);
                }
            } else {
                if (col == maxCols) {
                    col = 0;
                    printf("\r\n");
                }

                if (useColour) {
                    printf("\x11%c%-*s", fno->fattrib & AM_DIR ? dirColour : fileColour, col == (maxCols - 1) ? longestFilename - 1 : longestFilename, fno->fname);
                } else {
                    printf("%-*s", col == (maxCols - 1) ? longestFilename - 1 : longestFilename, fno->fname);
                }
                col++;
            }
            umm_free(fno->fname);
        }
    }

    if (!longListing) {
        printf("\r\n");
    }
    umm_free(fnos);

    if (useColour) {
        printf("\x11%c", textFg);
    }

cleanup:
    if (pattern) umm_free(pattern);
    if (dirPath) umm_free(dirPath);
    return fr;
}


// Delete file
// Parameters:
// - filename: Path of file to delete
// Returns:
// - FatFS return code
// 
UINT24 mos_DEL(char * filename) {
	FRESULT	fr;	

	fr = f_unlink(filename);
	return fr;
}


// Rename file
// Parameters:
// - srcPath: Source path of file to rename
// - dstPath: Destination file path
// Returns:
// - FatFS return code
// 
UINT24 mos_REN_API(char *srcPath, char *dstPath) {
	return mos_REN(srcPath, dstPath, FALSE);
}


// Rename file
// Parameters:
// - srcPath: Source path of file to rename
// - dstPath: Destination file path
// Returns:
// - FatFS return code
// 
UINT24 mos_REN(char *srcPath, char *dstPath, BOOL verbose) {
    FRESULT fr;
    DIR dir;
    static FILINFO fno;
    char *srcDir = NULL, *pattern = NULL, *fullSrcPath = NULL, *fullDstPath = NULL, *srcFilename = NULL;
	char *asteriskPos, *lastSeparator;
    BOOL usePattern = FALSE;

    if (strchr(dstPath, '*') != NULL) {
        // printf("Wildcards permitted in source only.\r\n");
        return FR_INVALID_PARAMETER;
    }

    asteriskPos = strchr(srcPath, '*');
    lastSeparator = asteriskPos ? strrchr(srcPath, '/') : NULL;

    if (asteriskPos != NULL) {
        if (lastSeparator != NULL) {
            srcDir = mos_strndup(srcPath, lastSeparator - srcPath + 1); // Include '/'
            pattern = mos_strdup(asteriskPos);
        } else {
            srcDir = mos_strdup(""); // Empty string for later use as a destination path
            pattern = mos_strdup(srcPath);
        }
        if (!srcDir || !pattern) {
            fr = FR_INT_ERR; // Out of memory
            goto cleanup;
        }
        usePattern = TRUE;
    } else {
        usePattern = FALSE;
    }

    if (usePattern) {
		if (!isDirectory(dstPath)) {
			fr = FR_INVALID_PARAMETER;
			goto cleanup;
		}

        fr = f_opendir(&dir, srcDir);
        if (fr != FR_OK) goto cleanup;

        fr = f_findfirst(&dir, &fno, srcDir, pattern);
        while (fr == FR_OK && fno.fname[0] != '\0') {
            size_t srcPathLen = strlen(srcDir) + strlen(fno.fname) + 1;
            size_t dstPathLen = strlen(dstPath) + strlen(fno.fname) + 2; // +2 for '/' and null terminator
			fullSrcPath = umm_malloc(srcPathLen);
            fullDstPath = umm_malloc(dstPathLen);

            if (!fullSrcPath || !fullDstPath) {
                fr = FR_INT_ERR; // Out of memory
                if (fullSrcPath) umm_free(fullSrcPath);
                if (fullDstPath) umm_free(fullDstPath);
                break;
            }

            sprintf(fullSrcPath, "%s%s", srcDir, fno.fname);
            sprintf(fullDstPath, "%s%s%s", dstPath, (dstPath[strlen(dstPath) - 1] == '/' ? "" : "/"), fno.fname);

            if (verbose) printf("Moving %s to %s\r\n", fullSrcPath, fullDstPath);
			fr = f_rename(fullSrcPath, fullDstPath);
            umm_free(fullSrcPath);
            umm_free(fullDstPath);
            fullSrcPath = NULL;
            fullDstPath = NULL;

            if (fr != FR_OK) break;
            fr = f_findnext(&dir, &fno);
        }

        f_closedir(&dir);
		
    } else {
		if (isDirectory(dstPath)) {
			// copy into a directory, keeping name
			size_t fullDstPathLen = strlen(dstPath) + strlen(srcPath) + 2; // +2 for potential '/' and null terminator
			fullDstPath = umm_malloc(fullDstPathLen);
			if (!fullDstPath) {
				fr = FR_INT_ERR;
				goto cleanup;
			}
			srcFilename = strrchr(srcPath, '/');
			srcFilename = (srcFilename != NULL) ? srcFilename + 1 : srcPath;
			sprintf(fullDstPath, "%s%s%s", dstPath, (dstPath[strlen(dstPath) - 1] == '/' ? "" : "/"), srcFilename);

			fr = f_rename(srcPath, fullDstPath);
			umm_free(fullDstPath);
		} else {
			fr = f_rename(srcPath, dstPath);
		}
		
    }

cleanup:
    if (srcDir) umm_free(srcDir);
    if (pattern) umm_free(pattern);
    return fr;
}

// Copy file
// Parameters:
// - srcPath: Source path of file to copy
// - dstPath: Destination file path
// Returns:
// - FatFS return code
// 
UINT24 mos_COPY_API(char *srcPath, char *dstPath) {
	return mos_COPY(srcPath, dstPath, FALSE);
}

// Copy file
// Parameters:
// - srcPath: Source path of file to copy
// - dstPath: Destination file path
// - verbose: Print progress messages
// Returns:
// - FatFS return code
// 
UINT24 mos_COPY(char *srcPath, char *dstPath, BOOL verbose) {
    FRESULT fr;
    FIL fsrc, fdst;
    DIR dir;
    static FILINFO fno;
    BYTE buffer[1024];
    UINT br, bw;
    char *srcDir = NULL, *pattern = NULL, *fullSrcPath = NULL, *fullDstPath = NULL, *srcFilename = NULL;
	char *asteriskPos, *lastSeparator;
    BOOL usePattern = FALSE;

    if (strchr(dstPath, '*') != NULL) {
        return FR_INVALID_PARAMETER; // Wildcards not allowed in destination path
    }

    asteriskPos = strchr(srcPath, '*');
    lastSeparator = asteriskPos ? strrchr(srcPath, '/') : NULL;

    if (asteriskPos != NULL) {
        usePattern = TRUE;
        if (lastSeparator != NULL) {
            srcDir = mos_strndup(srcPath, lastSeparator - srcPath + 1); // Include '/'
            pattern = mos_strdup(asteriskPos);
        } else {
            srcDir = mos_strdup("");
            pattern = mos_strdup(srcPath);
        }
        if (!srcDir || !pattern) {
            fr = FR_INT_ERR;
            goto cleanup;
        }
    } else {
        srcDir = mos_strdup(srcPath);
        if (!srcDir) return FR_INT_ERR;
    }

    if (usePattern) {
		if (!isDirectory(dstPath)) {
			fr = FR_INVALID_PARAMETER;
			goto cleanup;
		}
        fr = f_opendir(&dir, srcDir);
        if (fr != FR_OK) goto cleanup;

        fr = f_findfirst(&dir, &fno, srcDir, pattern);
        while (fr == FR_OK && fno.fname[0] != '\0') {
            size_t srcPathLen = strlen(srcDir) + strlen(fno.fname) + 1;
            size_t dstPathLen = strlen(dstPath) + strlen(fno.fname) + 2; // +2 for '/' and null terminator
            fullSrcPath = umm_malloc(srcPathLen);
            fullDstPath = umm_malloc(dstPathLen);

            if (!fullSrcPath || !fullDstPath) {
                fr = FR_INT_ERR;
                goto file_cleanup;
            }

            sprintf(fullSrcPath, "%s%s", srcDir, fno.fname);
            sprintf(fullDstPath, "%s%s%s", dstPath, (dstPath[strlen(dstPath) - 1] == '/' ? "" : "/"), fno.fname);

            fr = f_open(&fsrc, fullSrcPath, FA_READ);
            if (fr != FR_OK) goto file_cleanup;
            fr = f_open(&fdst, fullDstPath, FA_WRITE | FA_CREATE_NEW);
            if (fr != FR_OK) {
                f_close(&fsrc);
                goto file_cleanup;
            }

			if (verbose) printf("Copying %s to %s\r\n", fullSrcPath, fullDstPath);
            while (1) {
                fr = f_read(&fsrc, buffer, sizeof(buffer), &br);
                if (br == 0 || fr != FR_OK) break;
                fr = f_write(&fdst, buffer, br, &bw);
                if (bw < br || fr != FR_OK) break;
            }

            f_close(&fsrc);
            f_close(&fdst);

        file_cleanup:
            if (fullSrcPath) umm_free(fullSrcPath);
            if (fullDstPath) umm_free(fullDstPath);
            fullSrcPath = NULL;
            fullDstPath = NULL;

            if (fr != FR_OK) break;
            fr = f_findnext(&dir, &fno);
        }

        f_closedir(&dir);
    } else {
        size_t fullDstPathLen = strlen(dstPath) + strlen(srcPath) + 2; // +2 for potential '/' and null terminator
        fullDstPath = umm_malloc(fullDstPathLen);
        if (!fullDstPath) {
			fr = FR_INT_ERR;
			goto cleanup;
		}
        srcFilename = strrchr(srcPath, '/');
        srcFilename = (srcFilename != NULL) ? srcFilename + 1 : srcPath;
		if (isDirectory(dstPath)) {
			sprintf(fullDstPath, "%s%s%s", dstPath, (dstPath[strlen(dstPath) - 1] == '/' ? "" : "/"), srcFilename);
		} else {
			strcpy(fullDstPath, dstPath);
		}

        fr = f_open(&fsrc, srcPath, FA_READ);
        if (fr != FR_OK) {
			goto cleanup;
        }
        fr = f_open(&fdst, fullDstPath, FA_WRITE | FA_CREATE_NEW);
        if (fr != FR_OK) {
            f_close(&fsrc);
			goto cleanup;
        }

		if (verbose) printf("Copying %s to %s\r\n", srcPath, fullDstPath);
        while (1) {
            fr = f_read(&fsrc, buffer, sizeof(buffer), &br);
            if (br == 0 || fr != FR_OK) break;
            fr = f_write(&fdst, buffer, br, &bw);
            if (bw < br || fr != FR_OK) break;
        }

        f_close(&fsrc);
        f_close(&fdst);
    }

cleanup:
    if (srcDir) umm_free(srcDir);
    if (pattern) umm_free(pattern);
    if (fullSrcPath) umm_free(fullSrcPath);
    if (fullDstPath) umm_free(fullDstPath);
    return fr;
}


// Make a directory
// Parameters:
// - filename: Path of file to delete
// Returns:
// - FatFS return code
// 
UINT24 mos_MKDIR(char * filename) {
	FRESULT	fr;	
	
	fr = f_mkdir(filename);
	return fr;
}

// Load and run a batch file of MOS commands.
// Parameters:
// - filename: The batch file to execute
// - buffer: Storage for each line to be loaded into and executed from (recommend 256 bytes)
// - size: Size of buffer (in bytes)
// Returns:
// - FatFS return code (of the last command)
//
UINT24 mos_EXEC(char * filename, char * buffer, UINT24 size) {
	FRESULT	fr;
	FIL	   	fil;
	int     line =  0;
	
	fr = f_open(&fil, filename, FA_READ);
	if (fr == FR_OK) {
		while (!f_eof(&fil)) {
			line++;
			f_gets(buffer, size, &fil);
			fr = mos_exec(buffer, TRUE);
			if (fr != FR_OK) {
				printf("\r\nError executing %s at line %d\r\n", filename, line);
				break;
			}
		}
	}
	f_close(&fil);	
	return fr;	
}

// Open a file
// Parameters:
// - filename: Path of file to open
// - mode: File open mode (r, r/w, w, etc) - see FatFS documentation for more details
// Returns:
// - File handle, or 0 if the file cannot be opened
// 
UINT24 mos_FOPEN(char * filename, UINT8 mode) {
	FRESULT fr;
	int		i;
	
	for(i = 0; i < MOS_maxOpenFiles; i++) {
		if(mosFileObjects[i].free == 0) {
			fr = f_open(&mosFileObjects[i].fileObject, filename, mode);
			if(fr == FR_OK) {
				mosFileObjects[i].free = 1;
				return i + 1;
			}
		}
	}
	return 0;
}

// Close file(s)
// Parameters:
// - fh: File handle, or 0 to close all open files
// Returns:
// - File handle passed in function args
//
UINT24 mos_FCLOSE(UINT8 fh) {
	FRESULT fr;
	int 	i;
	
	if(fh > 0 && fh <= MOS_maxOpenFiles) {
		i = fh - 1;
		if(mosFileObjects[i].free > 0) {
			fr = f_close(&mosFileObjects[i].fileObject);
			mosFileObjects[i].free = 0;
		}
	}
	else {
		for(i = 0; i < MOS_maxOpenFiles; i++) {
			if(mosFileObjects[i].free > 0) {
				fr = f_close(&mosFileObjects[i].fileObject);
				mosFileObjects[i].free = 0;
			}
		}
	}	
	return fh;	
}

// Read a byte from a file
// Parameters:
// - fh: File handle
// Returns:
// - Byte read in lower 8 bits
// - EOF in upper 8 bits (1 = EOF)
//
UINT24	mos_FGETC(UINT8 fh) {
	FRESULT fr;
	FIL	*	fo;
	UINT	br;
	char	c;

	fo = (FIL *)mos_GETFIL(fh);
	if(fo > 0) {
		fr = f_read(fo, &c, 1, &br); 
		if(fr == FR_OK) {
			return	c | (fat_EOF(fo) << 8);
		}		
	}
	return 0;
}

// Write a byte to a file
// Parameters:
// - fh: File handle
// - c: Byte to write
//
void	mos_FPUTC(UINT8 fh, char c) {
	FIL * fo = (FIL *)mos_GETFIL(fh);

	if(fo > 0) {
		f_putc(c, fo);
	}
}

// Read a block of data into a buffer
// Parameters:
// - fh: File handle
// - buffer: Address to write the data into
// - btr: Number of bytes to read
// Returns:
// - Number of bytes read
//
UINT24	mos_FREAD(UINT8 fh, UINT24 buffer, UINT24 btr) {
	FRESULT fr;
	FIL *	fo = (FIL *)mos_GETFIL(fh);
	UINT	br = 0;

	if(fo > 0) {
		fr = f_read(fo, (const void *)buffer, btr, &br);
		if(fr == FR_OK) {
			return br;
		}
	}
	return 0;
}

// Write a block of data from a buffer
// Parameters:
// - fh: File handle
// - buffer: Address to read the data from
// - btw: Number of bytes to write
// Returns:
// - Number of bytes written
//
UINT24	mos_FWRITE(UINT8 fh, UINT24 buffer, UINT24 btw) {
	FRESULT fr;
	FIL *	fo = (FIL *)mos_GETFIL(fh);
	UINT	bw = 0;

	if(fo > 0) {
		fr = f_write(fo, (const void *)buffer, btw, &bw);
		if(fr == FR_OK) {
			return bw;
		}
	}
	return 0;
}

// Move the read/write pointer in a file
// Parameters:
// - offset: Position of the pointer relative to the start of the file
// Returns:
// - FRESULT
// 
UINT8  	mos_FLSEEK(UINT8 fh, UINT32 offset) {
	FIL * fo = (FIL *)mos_GETFIL(fh);

	if(fo > 0) {
		return f_lseek(fo, offset);
	}
	return FR_INVALID_OBJECT;
}

// Check whether file is at EOF (end of file)
// Parameters:
// - fh: File handle
// Returns:
// - 1 if EOF, otherwise 0
//
UINT8	mos_FEOF(UINT8 fh) {
	FIL * fo = (FIL *)mos_GETFIL(fh);

	if(fo > 0) {
		return fat_EOF(fo);
	}
	return 0;
}

// Copy an error string to RAM
// Parameters:
// - errno: The error number
// - address: Address of the buffer to copy the error code to
// - size: Size of buffer
//
void mos_GETERROR(UINT8 errno, UINT24 address, UINT24 size) {
	strncpy((char *)address, mos_errors[errno], size - 1);
}

// OSCLI
// Parameters
// - cmd: Address of the command entered
// Returns:
// - MOS error code
//
UINT24 mos_OSCLI(char * cmd) {
	UINT24 fr;
	fr = mos_exec(cmd, FALSE);
	return fr;
}

// Get the RTC
// Parameters:
// - address: Pointer to buffer to store time in
// Returns:
// - size of string
//
UINT8 mos_GETRTC(UINT24 address) {
	vdp_time_t t;

	rtc_update();
	rtc_unpack(&rtc, &t);
	rtc_formatDateTime((char *)address, &t);

	return strlen((char *)address);
}

// Set the RTC
// Parameters:
// - address: Pointer to buffer that contains the time data
// Returns:
// - size of string
//
void mos_SETRTC(UINT24 address) {
	BYTE * p = (BYTE *)address;

	putch(23);				// Set the ESP32 time
	putch(0);
	putch(VDP_rtc);
	putch(1);				// 1: Set time (6 byte buffer mode)
	//
	putch(*p++);			// Year
	putch(*p++);			// Month
	putch(*p++);			// Day
	putch(*p++);			// Hour
	putch(*p++);			// Minute
	putch(*p);				// Second
}

// Set an interrupt vector
// Parameters:
// - vector: The interrupt vector to set
// - address: Address of the interrupt handler
// Returns:
// - address: Address of the previous interrupt handler
//
UINT24 mos_SETINTVECTOR(UINT8 vector, UINT24 address) {
	void (* handler)(void) = (void *)address;
	#if DEBUG > 0
	printf("@mos_SETINTVECTOR: %02X,%06X\n\r", vector, address);
	#endif
	return (UINT24)set_vector(vector, handler);
}

// Get a FIL struct from a filehandle
// Parameters:
// - fh: The filehandle (indexed from 1)
// Returns:
// - address of the file structure, or 0 if invalid fh
//
UINT24	mos_GETFIL(UINT8 fh) {
	t_mosFileObject	* mfo;

	if(fh > 0 && fh <= MOS_maxOpenFiles) {
		mfo = &mosFileObjects[fh - 1];
		if(mfo->free > 0) {
			return (UINT24)(&mfo->fileObject);
		}
	}
	return 0;
}

// Check whether file is at EOF (end of file)
// Parameters:
// - fp: Pointer to file structure
// Returns:
// - 1 if EOF, otherwise 0
//
UINT8 fat_EOF(FIL * fp) {
	if(f_eof(fp) != 0) {
		return 1;
	}
	return 0;
}

// (Re-)mount the MicroSD card
// Parameters:
// - None
// Returns:
// - fatfs error code
//
int mos_mount(void) {
	int ret = f_mount(&fs, "", 1);			// Mount the SD card
	f_getcwd(cwd, sizeof(cwd)); //Update full path.
	return ret;
}

