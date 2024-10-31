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
#include "mos_sysvars.h"
#if DEBUG > 0
# include "tests.h"
#endif /* DEBUG */

char	cmd[256];				// Array for the command line handler

extern void *	set_vector(unsigned int vector, void(*handler)(void));	// In vectors16.asm

extern int 		exec16(UINT24 addr, char * params);	// In misc.asm
extern int 		exec24(UINT24 addr, char * params);	// In misc.asm

extern BYTE scrcols, scrcolours, scrpixelIndex; // In globals.asm
extern volatile	BYTE keyascii;					// In globals.asm
extern volatile	BYTE vpd_protocol_flags;		// In globals.asm
extern BYTE 	rtc;							// In globals.asm

static FATFS	fs;					// Handle for the file system

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
	{ "Cat",		&mos_cmdDIR,		HELP_CAT_ARGS,		HELP_CAT },
	{ "CD", 		&mos_cmdCD,			HELP_CD_ARGS,		HELP_CD },
	{ "CDir", 		&mos_cmdCD,			HELP_CD_ARGS,		HELP_CD },
	{ "CLS",		&mos_cmdCLS,		NULL,			HELP_CLS },
	{ "Copy", 		&mos_cmdCOPY,		HELP_COPY_ARGS,		HELP_COPY },
	{ "CP", 		&mos_cmdCOPY,		HELP_COPY_ARGS,		HELP_COPY },
	{ "Credits",	&mos_cmdCREDITS,	NULL,			HELP_CREDITS },
	{ "Delete",		&mos_cmdDEL,		HELP_DELETE_ARGS,	HELP_DELETE },
	{ "Dir",		&mos_cmdDIR,		HELP_CAT_ARGS,		HELP_CAT },
	{ "Disc",		&mos_cmdDISC,		NULL,		NULL },
	{ "Echo",		&mos_cmdECHO,		HELP_ECHO_ARGS,		HELP_ECHO },
	{ "Erase",		&mos_cmdDEL,		HELP_DELETE_ARGS,	HELP_DELETE },
	{ "Exec",		&mos_cmdEXEC,		HELP_EXEC_ARGS,		HELP_EXEC },
	{ "Help",		&mos_cmdHELP,		HELP_HELP_ARGS,		HELP_HELP },
	{ "Hotkey",		&mos_cmdHOTKEY,		HELP_HOTKEY_ARGS,	HELP_HOTKEY },
	{ "JMP",		&mos_cmdJMP,		HELP_JMP_ARGS,		HELP_JMP },
	{ "Load",		&mos_cmdLOAD,		HELP_LOAD_ARGS,		HELP_LOAD },
	{ "LS",			&mos_cmdDIR,		HELP_CAT_ARGS,		HELP_CAT },
	{ "Mem",		&mos_cmdMEM,		NULL,		HELP_MEM },
	{ "MkDir", 		&mos_cmdMKDIR,		HELP_MKDIR_ARGS,	HELP_MKDIR },
	{ "Mount",		&mos_cmdMOUNT,		NULL,			HELP_MOUNT },
	{ "Move",		&mos_cmdREN,		HELP_RENAME_ARGS,	HELP_RENAME },
	{ "MV",			&mos_cmdREN,		HELP_RENAME_ARGS,	HELP_RENAME },
	{ "PrintF",		&mos_cmdPRINTF,		HELP_PRINTF_ARGS,	HELP_PRINTF },
	{ "Rename",		&mos_cmdREN,		HELP_RENAME_ARGS,	HELP_RENAME },
	{ "RM",			&mos_cmdDEL,		HELP_DELETE_ARGS,	HELP_DELETE },
	{ "Run", 		&mos_cmdRUN,		HELP_RUN_ARGS,		HELP_RUN },
	{ "Save", 		&mos_cmdSAVE,		HELP_SAVE_ARGS,		HELP_SAVE },
	{ "Set",		&mos_cmdSET,		HELP_SET_ARGS,		HELP_SET },
	{ "SetEval",	&mos_cmdSETEVAL,	HELP_SETEVAL_ARGS,	HELP_SETEVAL },
	{ "SetMacro",	&mos_cmdSETMACRO,	HELP_SETMACRO_ARGS,	HELP_SETMACRO },
	{ "Show",		&mos_cmdSHOW,		HELP_SHOW_ARGS,		HELP_SHOW },
	{ "Time", 		&mos_cmdTIME,		HELP_TIME_ARGS,		HELP_TIME },
	{ "Type",		&mos_cmdTYPE,		HELP_TYPE_ARGS,		HELP_TYPE },
	{ "Unset",		&mos_cmdUNSET,		HELP_UNSET_ARGS,	HELP_UNSET },
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
	"Internal error",
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
	"Too deep",
};

#define mos_errors_count (sizeof(mos_errors)/sizeof(char *))

// Output a file error
// Parameters:
// - error: The FatFS error number
//
void mos_error(int error) {
	if (error >= 0 && error < mos_errors_count) {
		printf("\n\r%s\n\r", mos_errors[error]);
	}
}

// Wait for a keycode character from the VPD
// Returns:
// - ASCII keycode
//
BYTE mos_getkey() {
	BYTE ch = 0;
	while (ch == 0) {		// Loop whilst no key pressed
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
	t_mosSystemVariable * promptVar = NULL;
	char * prompt = NULL;
	retval = getSystemVariable("CLI$Prompt", &promptVar);
	// Print our prompt
	if (retval == 0) {
		prompt = expandVariable(promptVar, false);
	}
	if (prompt == NULL) {
		prompt = "*\0";
	}
	printf("%s", prompt);
	umm_free(prompt);
	retval = mos_EDITLINE(buffer, bufferLength, 3);
	printf("\n\r");
	return retval;
}

// Parse a MOS command from the line edit buffer
// Parameters:
// - ptr: Pointer to the MOS command in the line edit buffer
// Returns:
// - Command pointer, or 0 if command not found
//
t_mosCommand *mos_getCommand(char * ptr) {
	int i;
	t_mosCommand * cmd;
	for (i = 0; i < mosCommands_count; i++) {
		cmd = &mosCommands[i];
		if (pmatch(ptr, cmd->name, MATCH_COMMANDS) == 0) {
			return cmd;
		}
	}
	return NULL;
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

	if (!s) {					// Return NULL if a null string is passed
		return NULL;
	}
	if (!*s) {
		return s;				// Handle empty string
	}
	// skip leading spaces and asterisks
	while (isspace(*s) || *s == '*') {
		s++;
	}
	// strip trailing spaces
	ptr = s + strlen(s) - 1;
	while (ptr > s && isspace(*ptr)) {
		ptr--;
	}
	ptr[1] = '\0';
	return s;
}

int mos_runBin(UINT24 addr, char * args) {
	UINT8 mode = mos_execMode((UINT8 *)addr);
	switch (mode) {
		case 0:		// Z80 mode
			return exec16(addr, args);
			break;
		case 1: 	// ADL mode
			return exec24(addr, args);
			break;	
		default:	// Unrecognised header
			return MOS_INVALID_EXECUTABLE;
			break;
	}
}

// Execute a MOS command
// Parameters:
// - buffer: Pointer to a zero terminated string that contains the MOS command with arguments
// - in_mos: Flag to indicate if the command is being run from within MOS CLI (and thus allow running non-moslet executables)
// Returns:
// - MOS error code
//
int mos_exec(char * buffer, BOOL in_mos, BYTE depth) {
	char * 	ptr;
	int 	result = 0;

	if (depth > 10) {
		return MOS_TOO_DEEP;
	}

	ptr = mos_trim(buffer);
	if (ptr != NULL && (*ptr == '#' || *ptr == '\0' || (*ptr == '|' && *(ptr+1) == ' '))) {
		return FR_OK;
	}

	if (ptr != NULL) {
		int (*func)(char * ptr);
		t_mosCommand *cmd;
		t_mosSystemVariable *alias = NULL;
		char * aliasToken = NULL;
		char * command;
		if (!extractString(&ptr, &command, NULL, 0)) {
			// This shouldn't happen
			return FR_INT_ERR;
		}
		// ptr will now point to the arguments

		// Check if this command has an alias
		aliasToken = umm_malloc(strlen(command) + 7);
		sprintf(aliasToken, "Alias$%s", command);
		if (getSystemVariable(aliasToken, &alias) == 0) {
			char * newCommand;
			char * aliasTemplate;
			umm_free(aliasToken);
			aliasTemplate = expandVariable(alias, false);
			if (!aliasTemplate) {
				return FR_INT_ERR;
			}
			newCommand = substituteArguments(aliasTemplate, ptr, true);
			if (!newCommand) {
				umm_free(aliasTemplate);
				return FR_INT_ERR;
			}

			umm_free(aliasTemplate);
			result = mos_exec(newCommand, in_mos, depth + 1);
			umm_free(newCommand);
			return result;
		}

		umm_free(aliasToken);

		cmd = mos_getCommand(command);
		func = cmd->func;
		if (cmd != NULL && func != 0) {
			return func(ptr);
		} else {
			char * args = NULL;
			char * path = umm_malloc(strlen(command) + 10);
			if (path == NULL) {
				// Out of memory, but report it as an invalid command
				return MOS_INVALID_COMMAND;
			}

			// Search for the command
			// Moslets should attempt to be loaded under `Moslet$Path`
			// with the general search path being `Run$Path`
			// if `in_mos` is _not_ set we should only search `Moslet$Path`

			// expand any variables in our arguments, if we can
			args = expandMacro(ptr);

			sprintf(path, "/mos/%s.bin", command);
			result = mos_LOAD(path, MOS_starLoadAddress, 0);
			if (result == FR_OK) {
				result = mos_runBin(MOS_starLoadAddress, args ? args : ptr);
			} else if (in_mos && result != MOS_OVERLAPPING_SYSTEM) {
				sprintf(path, "%s.bin", command);
				result = mos_LOAD(path, MOS_defaultLoadAddress, 0);
				if (result == FR_OK) {
					result = mos_runBin(MOS_defaultLoadAddress, args ? args : ptr);
				} else if (result != MOS_OVERLAPPING_SYSTEM) {
					sprintf(path, "/bin/%s.bin", command);
					result = mos_LOAD(path, MOS_defaultLoadAddress, 0);
					if (result == FR_OK) {
						result = mos_runBin(MOS_defaultLoadAddress, args ? args : ptr);
					}
				}
			}
			if (result == FR_NO_FILE || result == FR_NO_PATH) {
				result = MOS_INVALID_COMMAND;
			}
			umm_free(path);
			if (args) umm_free(args);
		}
	}
	return result;
}

// Get the MOS Z80 execution mode
// Parameters:
// - ptr: Pointer to the code block
// Returns:
// - 0: Z80 mode
// - 1: ADL mode
//
UINT8 mos_execMode(UINT8 * ptr) {
	if (
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
		if (!extractString(&ptr, &path, NULL, 0)) {
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
	char read;
	int result;
	t_mosTransInfo * transInfo = gsInit(ptr, NULL);

	while (transInfo != NULL) {
		result = gsRead(&transInfo, &read);
		if (result != FR_OK) {
			if (transInfo != NULL) {
				umm_free(transInfo);
			}
			return result;
		}
		if (transInfo == NULL) {
			break;
		}
		putch(read);
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
	const char *p = ptr;

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
	int fn_number = 0;
	char *hotkeyString;
	t_mosSystemVariable *hotkeyVar = NULL;
	char label[10];

	if (!extractNumber(ptr, &ptr, NULL, &fn_number, 0)) {
		UINT8 key;

		if (*ptr != '\0') {
			return FR_INVALID_PARAMETER;
		}

		printf("Hotkey assignments:\r\n\r\n");
		for (key = 1; key <= 12; key++) {
			hotkeyVar = NULL;
			sprintf(label, "Hotkey$%d", key);
			if (getSystemVariable(label, &hotkeyVar) == 0) {
				printf("F%d: %s%s\r\n", key, key < 10 ? " " : "", hotkeyVar->value);
			} else {
				printf("F%d: %sN/A\r\n", key, key < 10 ? " " : "");
			}
		}
		printf("\r\n");
		return FR_OK;
	}

	ptr = mos_trim(ptr);

	if (fn_number < 1 || fn_number > 12) {
		return FR_INVALID_PARAMETER;
	}

	sprintf(label, "Hotkey$%d", fn_number);

	if (strlen(ptr) < 1) {		
		if (getSystemVariable(label, &hotkeyVar) == 0) {
			removeSystemVariable(hotkeyVar);
			printf("F%u cleared.\r\n", fn_number);
		} else {
			printf("F%d already clear, no hotkey command provided.\r\n", fn_number);
		}
		return 0;
	}

	// Remove surrounding quotes
	// TODO consider whether this should be part of expandMacro?
	if (ptr[0] == '\"' && ptr[strlen(ptr) - 1] == '\"') {
		ptr[strlen(ptr) - 1] = '\0';
		ptr++;
	}

	hotkeyString = expandMacro(ptr);
	if (!hotkeyString) return FR_INT_ERR;
	createOrUpdateSystemVariable(label, MOS_VAR_STRING, hotkeyString);

	return FR_OK;
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
	
	if (!extractString(&ptr, &filename, NULL, 0)) {
		return FR_INVALID_PARAMETER;
	}
	if (!extractNumber(ptr, &ptr, NULL, (int *)&addr, 0)) {
		addr = MOS_defaultLoadAddress;
	}

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
	char *	filename;
	
	if (!extractString(&ptr, &filename, NULL, 0)) {
		return FR_INVALID_PARAMETER;
	}
	fr = mos_EXEC(filename);
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
	
	if (
		!extractString(&ptr, &filename, NULL, 0) ||
		!extractNumber(ptr, &ptr, NULL, (int *)&addr, 0) ||
		!extractNumber(ptr, &ptr, NULL, (int *)&size, 0)
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

	// TODO support path and variable expansion with expandPath

	if (!extractString(&ptr, &filename, NULL, 0)) {
		return FR_INVALID_PARAMETER;
	}

	if (strcasecmp(filename, "-f") == 0) {
		force = TRUE;
		if (!extractString(&ptr, &filename, NULL, 0)) {
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
	// TODO consider allowing macro expansion here
	if (!extractNumber(ptr, &ptr, NULL, (int *)&addr, 0)) {
		return FR_INVALID_PARAMETER;
	}
	dest = (void *)addr;
	dest();
	return 0;
}

// RUN [<addr>] [<arguments>] command
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdRUN(char *ptr) {
	UINT24	addr;
	int		result;

	ptr = expandMacro(ptr);
	if (!ptr) return FR_INT_ERR;
	if (!extractNumber(ptr, &ptr, NULL, (int *)&addr, 0)) {
		addr = MOS_defaultLoadAddress;
	}
	result = mos_runBin(addr, ptr);
	if (ptr) umm_free(ptr);
	return result;
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

	// TODO why doesn't this use mos_CD?

	ptr = expandPath(ptr);
	if (!extractString(&ptr, &path, NULL, 0)) {
		// TODO should we default to the root directory?
		return FR_INVALID_PARAMETER;
	}
	fr = f_chdir(path);
	f_getcwd(cwd, sizeof(cwd)); // Update full path.
	if (ptr) umm_free(ptr);
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
	
	if (
		!extractString(&ptr, &filename1, NULL, 0) ||
		!extractString(&ptr, &filename2, NULL, 0)
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
	
	if (
		!extractString(&ptr, &filename1, NULL, 0) ||
		!extractString(&ptr, &filename2, NULL, 0)
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

	ptr = expandPath(ptr);
	if (!extractString(&ptr, &filename, NULL, 0)) {
		return FR_INVALID_PARAMETER;
	}
	fr = mos_MKDIR(filename);
	if (ptr) umm_free(ptr);
	return fr;
}

// SET <varname> <value> command
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdSET(char * ptr) {
	char *	token;
	char *	newValue;
	t_mosSystemVariable * var = NULL;
	UINT24 	value;
	int searchResult;

	// "token" is first parameter, which is a string
	if (!extractString(&ptr, &token, NULL, 0)) {
		return FR_INVALID_PARAMETER;
	}

	while (isspace(*ptr)) ptr++;
	if (*ptr == '\0') {
		return FR_INVALID_PARAMETER;
	}

	newValue = expandMacro(ptr);
	if (!newValue) return FR_INT_ERR;

	// search for our token in the system variables
	searchResult = getSystemVariable(token, &var);

	// at this point var will either point to the variable we want,
	// or the last variable in the list before our token

	if (searchResult == 0) {
		return updateSystemVariable(var, MOS_VAR_STRING, newValue);
	} else {
		// we have not found a matching variable
		t_mosSystemVariable *newVar;
		newVar = createSystemVariable(token, MOS_VAR_STRING, newValue);
		if (newVar == NULL || newValue == NULL) {
			if (newValue) umm_free(newValue);
			if (newVar) umm_free(newVar);
			return FR_INT_ERR;
		}
		insertSystemVariable(newVar, var);
	}

	return FR_OK;
}

// SetEval <varname> <expression> command
//
int mos_cmdSETEVAL(char * ptr) {
	char *	token;
	t_mosSystemVariable * var = NULL;
	t_mosEvalResult * evaluation = NULL;
	int result;

	// "token" is first parameter, which is a string
	if (!extractString(&ptr, &token, NULL, 0)) {
		return FR_INVALID_PARAMETER;
	}

	// make sure we have a value
	while (isspace(*ptr)) ptr++;
	if (*ptr == '\0') {
		return FR_INVALID_PARAMETER;
	}

	// we need to work out the value of the expression at ptr
	// and what type it is
	evaluation = evaluateExpression(ptr);

	if (evaluation == NULL) {
		return FR_INT_ERR;
	}
	if (evaluation->status != FR_OK) {
		result = evaluation->status;
		umm_free(evaluation);
		return result;
	}

	// search for our token in the system variables
	result = getSystemVariable(token, &var);

	if (result == 0) {
		result = updateSystemVariable(var, evaluation->type, evaluation->result);
	} else {
		// we have not found a matching variable
		t_mosSystemVariable *newVar;
		newVar = createSystemVariable(token, evaluation->type, evaluation->result);
		if (newVar == NULL) {
			umm_free(evaluation);
			return FR_INT_ERR;
		}
		insertSystemVariable(newVar, var);
		result = FR_OK;
	}

	umm_free(evaluation);

	return result;
}

// SETMACRO <varname> <value> command
//
int mos_cmdSETMACRO(char * ptr) {
	char *	token;
	char *	newValue;
	t_mosSystemVariable * var = NULL;
	int searchResult;

	// "token" is first parameter, which is a string
	if (!extractString(&ptr, &token, NULL, 0)) {
		return FR_INVALID_PARAMETER;
	}

	// make sure we have a value
	while (isspace(*ptr)) ptr++;
	if (*ptr == '\0') {
		return FR_INVALID_PARAMETER;
	}

	newValue = mos_strdup(ptr);
	if (!newValue) return FR_INT_ERR;

	// search for our token in the system variables
	searchResult = getSystemVariable(token, &var);

	if (searchResult == 0) {
		return updateSystemVariable(var, MOS_VAR_MACRO, newValue);
	} else {
		// we have not found a matching variable
		t_mosSystemVariable *newVar;
		newVar = createSystemVariable(token, MOS_VAR_MACRO, newValue);
		if (newVar == NULL) {
			if (newValue) umm_free(newValue);
			return FR_INT_ERR;
		}
		insertSystemVariable(newVar, var);
	}

	return FR_OK;
}

void printEscapedString(char * value) {
	while (*value) {
		if ((unsigned char)*value < 0x20) {
			putch('|');
			putch(*value + 0x40);
		} else {
			putch(*value);
		}
		value++;
	}
}

// SHOW [<pattern>] command
// Will show all system variables if no pattern is provided
// or only those variables that match the given pattern
//
int mos_cmdSHOW(char * ptr) {
	char *	token;
	t_mosSystemVariable * var = NULL;
	int searchResult;

	if (!extractString(&ptr, &token, NULL, 0)) {
		// Show all variables
		token = "*";
	}

	while (getSystemVariable(token, &var) == 0) {
		printf("%s", var->label);
		switch (var->type) {
			case MOS_VAR_MACRO:
				printf("(Macro) : ");
				// Macros set via SETMACRO shouldn't contain characters that need to be escaped
				// but as they could be set via API, they potentially can, so we will escape them
				printEscapedString(var->value);
				printf("\r\n");
				break;
			case MOS_VAR_NUMBER:
				printf("(Number) : %d\r\n", var->value);
				break;
			case MOS_VAR_CODE: {
				char * value = expandVariable(var, true);
				if (value == NULL) {
					printf(" : Error fetching code-based variable\r\n");
					break;
				}
				printf(" : %s\r\n", value);
				umm_free(value);
				break;
			}
			default: {
				// Assume all other types are strings
				printf(" : ");
				printEscapedString(var->value);
				printf("\r\n");
				break;
			}
		}
	}

	return 0;
}

// UNSET <varname> command
// Removes variables matching the varname pattern
// NB "code" variables cannot be removed via this command
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdUNSET(char * ptr) {
	char *	token;
	t_mosSystemVariable * var = NULL;
	int searchResult;

	if (!extractString(&ptr, &token, NULL, 0)) {
		return FR_INVALID_PARAMETER;
	}

	searchResult = getSystemVariable(token, &var);

	while (searchResult == 0) {
		if (var->type != MOS_VAR_CODE) {
			removeSystemVariable(var);
		}
		searchResult = getSystemVariable(token, &var);
	}

	return FR_OK;
}


// VDU <char1> <char2> ... <charN>
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdVDU(char *ptr) {
	char * value_str;
	char * token = NULL;
	char * delimiter = ", ";

	value_str = expandMacro(ptr);

	while (extractString(&value_str, &token, delimiter, 0)) {
		bool is_word = false;
		int value;
		char *endPtr = NULL;
		size_t len = strlen(token);

		// Strip semicolon notation and set as word
		if (len > 0 && token[len - 1] == ';') {
			token[len - 1] = '\0';
			len--;
			is_word = true;
		}

		if (!extractNumber(token, &endPtr, delimiter, &value, EXTRACT_FLAG_H_SUFFIX_HEX)) {
			umm_free(value_str);
			return FR_INVALID_PARAMETER;
		}

		if ((endPtr != NULL && endPtr < token + len) || value > 65535) {
			// Did not consume all of the string, or value too large
			umm_free(value_str);
			return FR_INVALID_PARAMETER;
		}

		if (value > 255 || value < -255) {
			is_word = true;
		}

		if (is_word) {
			putch(value & 0xFF); // write LSB
			putch(value >> 8);   // write MSB
		} else {
			putch(value);
		}
	}

	umm_free(value_str);
	return 0;
}

// TIME
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - 0
//
int mos_cmdTIME(char *ptr) {
	int		yr, mo, da, ho, mi, se;
	char	buffer[64];

	if (strlen(ptr) != 0) {
		// look for yr, mo, da, ho, mi, se
		// if we find them all, set the time
		if (
			!extractNumber(ptr, &ptr, NULL, &yr, EXTRACT_FLAG_DECIMAL_ONLY | EXTRACT_FLAG_POSITIVE_ONLY) ||
			!extractNumber(ptr, &ptr, NULL, &mo, EXTRACT_FLAG_DECIMAL_ONLY | EXTRACT_FLAG_POSITIVE_ONLY) ||
			!extractNumber(ptr, &ptr, NULL, &da, EXTRACT_FLAG_DECIMAL_ONLY | EXTRACT_FLAG_POSITIVE_ONLY) ||
			!extractNumber(ptr, &ptr, NULL, &ho, EXTRACT_FLAG_DECIMAL_ONLY | EXTRACT_FLAG_POSITIVE_ONLY) ||
			!extractNumber(ptr, &ptr, NULL, &mi, EXTRACT_FLAG_DECIMAL_ONLY | EXTRACT_FLAG_POSITIVE_ONLY) ||
			!extractNumber(ptr, &ptr, NULL, &se, EXTRACT_FLAG_DECIMAL_ONLY | EXTRACT_FLAG_POSITIVE_ONLY)
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

	if (!extractString(&ptr, &filename, NULL, 0)) {
		return FR_INVALID_PARAMETER;
	}

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
	if (fr != FR_OK) {
		return fr;
	}
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

	BOOL hasCmd = extractString(&ptr, &cmd, NULL, 0);
	if (!hasCmd) {
		cmd = "help";
	}

	if (strcasecmp(cmd, "all") == 0) {
		for (i = 0; i < mosCommands_count; ++i) {
			printCommandInfo(&mosCommands[i], FALSE);
		}
		return 0;
	}

	do {
		BOOL found = false;
		for (i = 0; i < mosCommands_count; ++i) {
			if (pmatch(cmd, mosCommands[i].name, MATCH_COMMANDS) == 0) {
				found = true;
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
			}
		}
		if (!found) {
			printf("Command not found: %s\r\n", cmd);
		}
	} while (extractString(&ptr, &cmd, NULL, 0));

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
	FIL		fil;
	UINT	br;	
	FSIZE_t fSize;
	
	// TODO loop over filenames until we find a file, or have none left
	filename = expandPath(filename);
	if (!filename) return FR_INT_ERR;
	fr = f_open(&fil, filename, FA_READ);
	if (fr == FR_OK) {
		fSize = f_size(&fil);
		if (size) {
			// Maximize load according to size parameter
			if (fSize < size) {
				size = fSize;
			}
		} else {
			// Load the full file size
			size = fSize;
		}
		// Check potential system area overlap
		if ((address <= MOS_externLastRAMaddress) && ((address + size) > MOS_systemAddress)) {
			fr = MOS_OVERLAPPING_SYSTEM;
		} else {
			fr = f_read(&fil, (void *)address, size, &br);		
		}		
	}
	f_close(&fil);	
	umm_free(filename);
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
	FIL		fil;
	UINT	br;	

	// TODO loop over path options until we find a file, or have none left
	filename = expandPath(filename);
	if (!filename) return FR_INT_ERR;

	fr = f_open(&fil, filename, FA_WRITE | FA_CREATE_NEW);
	if (fr == FR_OK) {
		fr = f_write(&fil, (void *)address, size, &br);
	}
	f_close(&fil);
	umm_free(filename);
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
	UINT	br;
	int		size = 512;
	char *	buffer;
	int		i;

	// TODO loop over path options until we find a file, or have none left
	filename = expandPath(filename);
	if (!filename) return FR_INT_ERR;
	buffer = umm_malloc(size);
	if (!buffer) {
		umm_free(filename);
		return FR_INT_ERR;
	}
	fr = f_open(&fil, filename, FA_READ);
	if (fr != FR_OK) {
		umm_free(buffer);
		umm_free(filename);
		return fr;
	}

	while (1) {
		fr = f_read(&fil, (void *)buffer, size, &br);
		if (br == 0)
			break;
		for (i = 0; i < br; ++i) {
			putchar(buffer[i]);
			if (buffer[i] == '\n') {
				putchar('\r');
			}
		}
	}

	f_close(&fil);
	umm_free(buffer);
	umm_free(filename);
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

	path = expandPath(path);
	if (!path) return FR_INT_ERR;
	fr = f_chdir(path);
	f_getcwd(cwd, sizeof(cwd)); // Update full path.
	umm_free(path);
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
		if (fr != 0) {
			return fr;
		}	
		printf("Volume: ");
		if (strlen(str) > 0) {
			printf("%s", str);
		} else {
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

	// TODO use expandPath here to expand macros and paths in the inputPath

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

	// TODO loop over path options until we find a file, or have none left
	filename = expandPath(filename);
	fr = f_unlink(filename);
	umm_free(filename);
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

	// TODO add in path expansion using expandPath
	// that would need to do iteration over srcPath, but use first expanded dstPath

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

	// TODO add in path expansion using expandPath
	// that would need to do iteration over srcPath, but use first expanded dstPath

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

	// Expand path - we will only attempt to mkdir at first expansion
	filename = expandPath(filename);
	if (!filename) {
		return FR_INT_ERR;
	}
	fr = f_mkdir(filename);
	umm_free(filename);
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
UINT24 mos_EXEC(char * filename) {
	FRESULT	fr;
	FIL		fil;
	int		size = 256;
	char *	buffer = (char *)umm_malloc(size);
	int		line =  0;

	if (!buffer) {
		return FR_INT_ERR;
	}
	// TODO handle path expansion here
	// looping thru path
	filename = expandPath(filename);
	if (!filename) {
		umm_free(buffer);
		return FR_INT_ERR;
	}
	
	fr = f_open(&fil, filename, FA_READ);
	if (fr == FR_OK) {
		while (!f_eof(&fil)) {
			line++;
			f_gets(buffer, size, &fil);
			fr = mos_exec(buffer, TRUE, 0);
			if (fr != FR_OK) {
				printf("\r\nError executing %s at line %d\r\n", filename, line);
				break;
			}
		}
	}
	f_close(&fil);
	umm_free(filename);
	umm_free(buffer);
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
	
	filename = expandPath(filename);
	for (i = 0; i < MOS_maxOpenFiles; i++) {
		if (mosFileObjects[i].free == 0) {
			fr = f_open(&mosFileObjects[i].fileObject, filename, mode);
			if (fr == FR_OK) {
				mosFileObjects[i].free = 1;
				umm_free(filename);
				return i + 1;
			}
		}
	}
	umm_free(filename);
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
	
	if (fh > 0 && fh <= MOS_maxOpenFiles) {
		i = fh - 1;
		if (mosFileObjects[i].free > 0) {
			fr = f_close(&mosFileObjects[i].fileObject);
			mosFileObjects[i].free = 0;
		}
	} else {
		for (i = 0; i < MOS_maxOpenFiles; i++) {
			if (mosFileObjects[i].free > 0) {
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
	if (fo > 0) {
		fr = f_read(fo, &c, 1, &br); 
		if (fr == FR_OK) {
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

	if (fo > 0) {
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

	if (fo > 0) {
		fr = f_read(fo, (const void *)buffer, btr, &br);
		if (fr == FR_OK) {
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

	if (fo > 0) {
		fr = f_write(fo, (const void *)buffer, btw, &bw);
		if (fr == FR_OK) {
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

	if (fo > 0) {
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

	if (fo > 0) {
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
	if (errno >= 0 && errno < mos_errors_count) {
		strncpy((char *)address, mos_errors[errno], size - 1);
	} else {
		strncpy((char *)address, "Unknown error", size - 1);
	}
}

// OSCLI
// Parameters
// - cmd: Address of the command entered
// Returns:
// - MOS error code
//
UINT24 mos_OSCLI(char * cmd) {
	UINT24 fr;
	// NB OSCLI doesn't support automatic running of programs besides moslets
	fr = mos_exec(cmd, FALSE, 0);
	createOrUpdateSystemVariable("Sys$ReturnCode", MOS_VAR_NUMBER, (void *)fr);
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

	if (fh > 0 && fh <= MOS_maxOpenFiles) {
		mfo = &mosFileObjects[fh - 1];
		if (mfo->free > 0) {
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
	if (f_eof(fp) != 0) {
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

// Support functions for code-type system variables
//

// Read the current working directory
int readCWD(char * buffer, int * size) {
	int len = strlen(cwd) + 1;
	if (*size >= len) {
		if (buffer != NULL) {
			strncpy(buffer, cwd, len);
		}
	}
	*size = len;
	return FR_OK;
}

// Read the year
int readYear(char * buffer, int * size) {
	vdp_time_t t;
	int len = 5;	// 4 digits + null terminator = not y10k compliant ;)

	if (!buffer) {
		*size = len;
		return FR_OK;
	}

	// Assume that the RTC has been updated ?
	rtc_update();
	rtc_unpack(&rtc, &t);

	if (*size >= len) {
		if (buffer != NULL) {
			sprintf(buffer, "%04d\0", t.year);
		}
	}
	*size = len;
	return FR_OK;
}

// Write the year
int writeYear(char * buffer) {
	vdp_time_t t;
	int	yr;
	char writeBuffer[6];
	char * buffEnd = buffer + 4;

	// attempt to read the year
	if (!extractNumber(buffer, &buffEnd, NULL, &yr, EXTRACT_FLAG_DECIMAL_ONLY | EXTRACT_FLAG_POSITIVE_ONLY)) {
		return FR_INVALID_PARAMETER;
	}

	rtc_update();
	rtc_unpack(&rtc, &t);

	writeBuffer[0] = yr - EPOCH_YEAR;
	writeBuffer[1] = t.month;
	writeBuffer[2] = t.day;
	writeBuffer[3] = t.hour;
	writeBuffer[4] = t.minute;
	writeBuffer[5] = t.second;
	mos_SETRTC((UINT24)writeBuffer);

	rtc_update();
	return FR_OK;
}

// Read the date
int readDate(char * buffer, int * size) {
	vdp_time_t t;
	// Date format is Day,dd mmm
	// or Day, d mmm
	int len = 11;	// 10 characters + null terminator

	if (!buffer || *size < len) {
		*size = len;
		return FR_OK;
	}

	// Assume that the RTC has been updated ?
	rtc_update();
	rtc_unpack(&rtc, &t);

	rtc_formatDate((char *)buffer, &t);

	*size = len;
	return FR_OK;
}

// Read the time
int readTime(char * buffer, int * size) {
	vdp_time_t t;
	// Time format is hh:mm:ss
	int len = 9;	// 8 characters + null terminator

	if (!buffer || *size < len) {
		*size = len;
		return FR_OK;
	}

	// Assume that the RTC has been updated ?
	rtc_update();
	rtc_unpack(&rtc, &t);

	rtc_formatTime((char *)buffer, &t);

	*size = len;
	return FR_OK;
}

// Update the time
int writeTime(char * buffer) {
	vdp_time_t t;
	int	hr, min, sec;
	char writeBuffer[6];
	char * buffEnd = buffer + 2;

	// attempt to read the time
	if (!extractNumber(buffer, &buffEnd, ":", &hr, EXTRACT_FLAG_DECIMAL_ONLY | EXTRACT_FLAG_POSITIVE_ONLY)) {
		return FR_INVALID_PARAMETER;
	}
	buffer += 3;
	buffEnd += 3;
	if (!extractNumber(buffer, &buffEnd, ":", &min, EXTRACT_FLAG_DECIMAL_ONLY | EXTRACT_FLAG_POSITIVE_ONLY)) {
		return FR_INVALID_PARAMETER;
	}
	buffer += 3;
	buffEnd += 3;
	if (!extractNumber(buffer, &buffEnd, "\0", &sec, EXTRACT_FLAG_DECIMAL_ONLY | EXTRACT_FLAG_POSITIVE_ONLY)) {
		return FR_INVALID_PARAMETER;
	}

	rtc_update();
	rtc_unpack(&rtc, &t);

	writeBuffer[0] = t.year - EPOCH_YEAR;
	writeBuffer[1] = t.month;
	writeBuffer[2] = t.day;
	writeBuffer[3] = hr;
	writeBuffer[4] = min;
	writeBuffer[5] = sec;
	mos_SETRTC((UINT24)writeBuffer);

	rtc_update();
	return FR_OK;
}

// Simplistic VDU 23,0,setting,value wrapper, extracting value as a number from buffer
int writeVDPSetting(char * buffer, int setting) {
	int value;
	char * buffEnd = buffer + strlen(buffer);

	if (!extractNumber(buffer, &buffEnd, NULL, &value, 0)) {
		return FR_INVALID_PARAMETER;
	}

	putch(23);
	putch(0);
	putch(setting);
	putch(value & 0xFF);

	return FR_OK;
}

// Write the "keyboard" setting to the VDP
int writeKeyboard(char * buffer) {
	return writeVDPSetting(buffer, VDP_keycode);
}

// Write the "console" setting to the VDP
int writeConsole(char * buffer) {
	return writeVDPSetting(buffer, VDP_consolemode);
}

// Current$Dir variable definition - read-only
static t_mosCodeSystemVariable cwdVar = {
	&readCWD,
	NULL
};

// Sys$Year variable definition
static t_mosCodeSystemVariable yearVar = {
	&readYear,
	&writeYear
};

// Sys$Date variable definition - read-only (for now)
static t_mosCodeSystemVariable dateVar = {
	&readDate,
	NULL
};

// Sys$Time variable definition
static t_mosCodeSystemVariable timeVar = {
	&readTime,
	&writeTime
};

// Keyboard variable definition
static t_mosCodeSystemVariable keyboardVar = {
	NULL,
	&writeKeyboard
};

// Console variable definition
static t_mosCodeSystemVariable consoleVar = {
	NULL,
	&writeConsole
};

void mos_setupSystemVariables() {
	// Date/time variables:
	// Sys$Time
	// Sys$Date
	// Sys$Year
	// TODO consider how to handle reading these sysvars without spamming the VDP for updates
	// as using all three in a single command would result in three VDP RTC reads
	// A simplistic approach would be to only update the RTC sysvar when Sys$Time is read
	createOrUpdateSystemVariable("Sys$ReturnCode", MOS_VAR_NUMBER, (void *)0);

	createOrUpdateSystemVariable("Sys$Time", MOS_VAR_CODE, &timeVar);
	createOrUpdateSystemVariable("Sys$Date", MOS_VAR_CODE, &dateVar);
	createOrUpdateSystemVariable("Sys$Year", MOS_VAR_CODE, &yearVar);
	// Current working directory
	createOrUpdateSystemVariable("Current$Dir", MOS_VAR_CODE, &cwdVar);
	// Default CLI prompt
	createOrUpdateSystemVariable("CLI$Prompt", MOS_VAR_MACRO, "<Current$Dir> *");

	// Keyboard and console settings
	createOrUpdateSystemVariable("Keyboard", MOS_VAR_CODE, &keyboardVar);
	createOrUpdateSystemVariable("Console", MOS_VAR_CODE, &consoleVar);
}
