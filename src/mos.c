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
#include "mos_file.h"
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
	{ ".",			&mos_cmdDIR,		true,	HELP_CAT_ARGS,		HELP_CAT },
	{ "Cat",		&mos_cmdDIR,		true,	HELP_CAT_ARGS,		HELP_CAT },
	{ "CD",			&mos_cmdCD,			true,	HELP_CD_ARGS,		HELP_CD },
	{ "CDir",		&mos_cmdCD,			true,	HELP_CD_ARGS,		HELP_CD },
	{ "CLS",		&mos_cmdCLS,		false,	NULL,				HELP_CLS },
	{ "Copy",		&mos_cmdCOPY,		true,	HELP_COPY_ARGS,		HELP_COPY },
	{ "CP",			&mos_cmdCOPY,		true,	HELP_COPY_ARGS,		HELP_COPY },
	{ "Credits",	&mos_cmdCREDITS,	false,	NULL,				HELP_CREDITS },
	{ "Delete",		&mos_cmdDEL,		true,	HELP_DELETE_ARGS,	HELP_DELETE },
	{ "Dir",		&mos_cmdDIR,		true,	HELP_CAT_ARGS,		HELP_CAT },
	{ "Disc",		&mos_cmdDISC,		false,	NULL,				NULL },
	{ "Do",			&mos_cmdDO,			true,	HELP_DO_ARGS,		HELP_DO },
	{ "Echo",		&mos_cmdECHO,		false,	HELP_ECHO_ARGS,		HELP_ECHO },
	{ "Erase",		&mos_cmdDEL,		true,	HELP_DELETE_ARGS,	HELP_DELETE },
	{ "Exec",		&mos_cmdEXEC,		true,	HELP_EXEC_ARGS,		HELP_EXEC },
	{ "Help",		&mos_cmdHELP,		false,	HELP_HELP_ARGS,		HELP_HELP },
	{ "Hotkey",		&mos_cmdHOTKEY,		false,	HELP_HOTKEY_ARGS,	HELP_HOTKEY },
	{ "If",			&mos_cmdIF,			false,	HELP_IF_ARGS,		HELP_IF },
	{ "IfThere",	&mos_cmdIFTHERE,	false,	HELP_IFTHERE_ARGS,	HELP_IFTHERE },
	{ "JMP",		&mos_cmdJMP,		true,	HELP_JMP_ARGS,		HELP_JMP },
	{ "Load",		&mos_cmdLOAD,		true,	HELP_LOAD_ARGS,		HELP_LOAD },
	{ "LoadFile",	&mos_cmdLOADFILE,	true,	HELP_LOADFILE_ARGS,	HELP_LOADFILE },
	{ "LS",			&mos_cmdDIR,		true,	HELP_CAT_ARGS,		HELP_CAT },
	{ "Mem",		&mos_cmdMEM,		false,	NULL,				HELP_MEM },
	{ "MkDir",		&mos_cmdMKDIR,		true,	HELP_MKDIR_ARGS,	HELP_MKDIR },
	{ "Mount",		&mos_cmdMOUNT,		false,	NULL,				HELP_MOUNT },
	{ "Move",		&mos_cmdREN,		true,	HELP_RENAME_ARGS,	HELP_RENAME },
	{ "MV",			&mos_cmdREN,		true,	HELP_RENAME_ARGS,	HELP_RENAME },
	{ "Obey",		&mos_cmdOBEY,		true,	HELP_OBEY_ARGS,		HELP_OBEY },
	{ "PrintF",		&mos_cmdPRINTF,		false,	HELP_PRINTF_ARGS,	HELP_PRINTF },
	{ "Rename",		&mos_cmdREN,		true,	HELP_RENAME_ARGS,	HELP_RENAME },
	{ "RM",			&mos_cmdDEL,		true,	HELP_DELETE_ARGS,	HELP_DELETE },
	{ "Run",		&mos_cmdRUN,		true,	HELP_RUN_ARGS,		HELP_RUN },
	{ "RunBin",		&mos_cmdRUNBIN,		true,	HELP_RUNBIN_ARGS,	HELP_RUNBIN },
	{ "RunFile",	&mos_cmdRUNFILE,	true,	HELP_RUNFILE_ARGS,	HELP_RUNFILE },
	{ "Save",		&mos_cmdSAVE,		true,	HELP_SAVE_ARGS,		HELP_SAVE },
	{ "Set",		&mos_cmdSET,		false,	HELP_SET_ARGS,		HELP_SET },
	{ "SetEval",	&mos_cmdSETEVAL,	false,	HELP_SETEVAL_ARGS,	HELP_SETEVAL },
	{ "SetMacro",	&mos_cmdSETMACRO,	false,	HELP_SETMACRO_ARGS,	HELP_SETMACRO },
	{ "Show",		&mos_cmdSHOW,		false,	HELP_SHOW_ARGS,		HELP_SHOW },
	{ "Time",		&mos_cmdTIME,		true,	HELP_TIME_ARGS,		HELP_TIME },
	{ "Try",		&mos_cmdTRY,		false,	HELP_TRY_ARGS,		HELP_TRY },
	{ "Type",		&mos_cmdTYPE,		true,	HELP_TYPE_ARGS,		HELP_TYPE },
	{ "Unset",		&mos_cmdUNSET,		false,	HELP_UNSET_ARGS,	HELP_UNSET },
	{ "VDU",		&mos_cmdVDU,		true,	HELP_VDU_ARGS,		HELP_VDU },
#if DEBUG > 0
	{ "RUN_MOS_TESTS",		&mos_cmdTEST,		false,	NULL,		"Run the MOS OS test suite" },
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
	char * prompt = expandVariableToken("CLI$Prompt");

	printf("%s", prompt ? prompt : "*");
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
t_mosCommand *mos_getCommand(char * ptr, uint8_t flags) {
	int i;
	t_mosCommand * cmd;
	for (i = 0; i < mosCommands_count; i++) {
		cmd = &mosCommands[i];
		if (pmatch(ptr, cmd->name, flags) == 0) {
			return cmd;
		}
	}
	return NULL;
}

// String trim function
// NB: This also includes the asterisk character as whitespace
// Parameters:
// - s: Pointer to the string to trim
// - removeLeadingAsterisks: If true, remove leading asterisks
// Returns:
// - s: Pointer to the start of the new string
//
char * mos_trim(char * s, bool removeLeadingAsterisks) {
	char * ptr;

	if (!s) {					// Return NULL if a null string is passed
		return NULL;
	}
	if (!*s) {
		return s;				// Handle empty string
	}
	// skip leading spaces and asterisks
	while (isspace(*s) || removeLeadingAsterisks && (*s == '*')) {
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

int mos_runBinFile(char * filepath, char * args) {
	char * resolvedPath = NULL;
	char * fullyResolvedPath = NULL;
	int pathLen = 0;
	UINT24 addr = MOS_defaultLoadAddress;
	int result = getResolvedPath(filepath, &resolvedPath);

	#if DEBUG > 0
	createOrUpdateSystemVariable("LastBin$RunPath", MOS_VAR_STRING, filepath);
	#endif /* DEBUG */
	if (result != FR_OK) {
		return result;
	}

	#if DEBUG > 0
	createOrUpdateSystemVariable("LastBin$UnresolvedRun", MOS_VAR_STRING, resolvedPath);
	#endif /* DEBUG */

	// Fully resolved path allocation - size is very conservative
	pathLen = strlen(resolvedPath) + strlen(cwd) + 1;
	fullyResolvedPath = umm_malloc(pathLen);
	if (fullyResolvedPath == NULL) {
		umm_free(resolvedPath);
		return MOS_OUT_OF_MEMORY;
	}

	result = resolveRelativePath(resolvedPath, fullyResolvedPath, pathLen);
	if (result == FR_OK) {
		if (isMoslet(fullyResolvedPath)) {
			addr = MOS_starLoadAddress;
		}
		result = mos_LOAD(fullyResolvedPath, addr, 0);
		if (result == FR_OK) {
			createOrUpdateSystemVariable("LastBin$Run", MOS_VAR_STRING, fullyResolvedPath);
			result = mos_runBin(addr, args);
		}
	}

	umm_free(fullyResolvedPath);
	umm_free(resolvedPath);
	return result;
}

// runOrLoadFile needs to be called with args _including_ the filepath
int mos_runOrLoadFile(char * ptr, bool run) {
	char *	filepath = NULL;
	char *	args = NULL;
	char * 	leafname = NULL;
	char *	resolvedPath = NULL;
	char *	extension = NULL;
	int		result = extractString(ptr, &args, NULL, &filepath, EXTRACT_FLAG_AUTO_TERMINATE);

	if (result != FR_OK) {
		return result;
	}
	result = getResolvedPath(filepath, &resolvedPath);

	#if DEBUG > 0
	createOrUpdateSystemVariable(run ? "LastFile$RunPath" : "LastFile$LoadPath", MOS_VAR_STRING, filepath);
	#endif /* DEBUG */
	if (result != FR_OK) {
		return result;
	}

	leafname = getFilepathLeafname(resolvedPath);
	if (leafname == NULL) {
		umm_free(resolvedPath);
		return MOS_INVALID_COMMAND;
	}

	extension = strrchr(leafname, '.');
	if (extension == NULL) {
		result = MOS_INVALID_COMMAND;
	} else {
		// Look up a runtype for the file extension
		char * runtype = NULL;
		char * token = umm_malloc(strlen(extension) + 17);
		if (token == NULL) {
			result = MOS_OUT_OF_MEMORY;
		} else {
			extension++;
			sprintf(token, "Alias$@%sType_%s", run ? "Run" : "Load", extension);
			if (args && *(args - 1) == '\0') {
				*(args - 1) = ' ';	// Replace the terminator with a space
			}
			runtype = expandVariableToken(token);
			umm_free(token);
			if (runtype != NULL) {
				// We have a run/load type, so use substituteArguments to build the command
				char * command = substituteArguments(runtype, ptr, false);
				if (command != NULL) {
					createOrUpdateSystemVariable(run ? "LastFile$Run" : "LastFile$Load", MOS_VAR_STRING, command);
					result = mos_exec(command, true, 0);
					umm_free(command);
				} else {
					result = MOS_OUT_OF_MEMORY;
				}
				umm_free(runtype);
			} else {
				result = MOS_INVALID_COMMAND;
			}
		}
	}

	umm_free(resolvedPath);
	return result;
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

	ptr = mos_trim(buffer, true);
	if (ptr != NULL && (*ptr == '#' || *ptr == '\0' || (*ptr == '|' && *(ptr+1) == ' '))) {
		return FR_OK;
	}

	// TODO the code here to separate command from arguments needs reworking
	// it has become messy, especially with the addition of quoted strings
	// Will be revisited when we have runtypes, as they will require a different interpretation of `.`
	// With runtypes, we can look to see if we have a command terminated by `.`, and if so
	// if the argument does not start with a space, and matches a runtype, then we can run the command

	if (ptr != NULL) {
		int (*func)(char * ptr);
		t_mosCommand *cmd;
		t_mosSystemVariable *alias = NULL;
		char * aliasToken;
		char * commandPtr;
		char * command = NULL;
		char * args = NULL;
		int cmdLen = 0;

		result = extractString(ptr, &ptr, " .", &commandPtr, EXTRACT_FLAG_OMIT_LEADSKIP | EXTRACT_FLAG_INCLUDE_QUOTES);
		if (result == FR_INVALID_PARAMETER && *commandPtr == '.') {
			// single dot (which is interpreted as an empty string) is a valid command
			result = FR_OK;
		}
		if (result != FR_OK) {
			// This shouldn't really happen, unless our command string is invalid somehow
			return result;
		}
		if (*ptr == '.') {
			ptr++;
		}
		cmdLen = ptr - commandPtr;
		if (*commandPtr == '"' && *(ptr - 1) == '"') {
			// We have a quoted command, so strip the quotes
			commandPtr++;
			*(ptr - 1) = '\0';
			cmdLen -= 2;
		}
		ptr = mos_trim(ptr, false);
		// ptr will now point to the arguments
		// printf("command is '%.*s', args '%s', cmdLen %d\n\r", cmdLen, commandPtr, ptr ? ptr : "<not found>", cmdLen);

		if (*commandPtr == '%') {
			// Skip alias expansion for commands that start with %
			commandPtr++;
			cmdLen--;
		} else {
			// Check if this command has an alias
			aliasToken = umm_malloc(cmdLen + 7);
			if (aliasToken == NULL) {
				return MOS_OUT_OF_MEMORY;
			}
			sprintf(aliasToken, "Alias$%.*s", cmdLen, commandPtr);
			if (cmdLen > 1 && aliasToken[strlen(aliasToken) - 1] == '.') {
				aliasToken[strlen(aliasToken) - 1] = '*';
			}
			if (getSystemVariable(aliasToken, &alias) == 0) {
				char * aliasTemplate;
				umm_free(aliasToken);
				aliasTemplate = expandVariable(alias, false);
				if (!aliasTemplate) {
					return FR_INT_ERR;
				}
				command = substituteArguments(aliasTemplate, ptr, false);
				umm_free(aliasTemplate);
				if (!command) {
					return FR_INT_ERR;
				}
				result = mos_exec(command, in_mos, depth + 1);
				umm_free(command);
				return result;
			}

			umm_free(aliasToken);
		}

		command = umm_malloc(cmdLen + 1);
		if (command == NULL) {
			return MOS_OUT_OF_MEMORY;
		}
		strncpy(command, commandPtr, cmdLen);
		command[cmdLen] = '\0';
		// printf("searching for command '%s' (cmdLen is %d)\n\r", command, cmdLen);

		cmd = mos_getCommand(command, MATCH_COMMANDS);
		umm_free(command);
		command = commandPtr;
		func = cmd->func;
		if (cmd != NULL && func != 0) {
			if (cmd->expandArgs) {
				args = expandMacro(ptr);
			}
			// printf("command is '%s', args '%s'\n\r", cmd->name, args ? args : ptr);
			result = func(args ? args : ptr);
			if (cmd->expandArgs) {
				umm_free(args);
			}
			return result;
		} else {
			// Command not built-in, so see if it's a file
			char * path;
			// OK - with the logic as it was, `./` would match an arbitrary moslet
			// this is not what we want
			// and `./filename.bin` would fail to find the file to run it
			if (*command == '.') {
				// Single dot can't match
				// printf("cmdLen is 1\n\r");
				return MOS_INVALID_COMMAND;
			}
			if (cmdLen < 2) {
				// Command is too short to be a valid command
				return MOS_INVALID_COMMAND;
			}
			path = umm_malloc(cmdLen + 12);
			if (path == NULL) {
				// Out of memory, but report it as an invalid command
				return MOS_INVALID_COMMAND;
			}
			if (*(command + cmdLen - 1) == '.') {
				// If we have a trailing dot on our command, replace with * for wilcard matching
				*(command + cmdLen - 1) = '*';
			}
			// TODO when we have support for runtypes, we should omit the ".bin" extension
			// and use `runFile` instead of `runBinFile`
			if (memchr(command, ':', cmdLen) != NULL) {
				// Command has a path prefix, so we use it as-is
				sprintf(path, "%.*s.bin", cmdLen, command);
			} else {
				// If "in_mos" is true we use full run path, otherwise restrict to moslets only
				sprintf(path, "%s:%.*s.bin", in_mos ? "run" : "moslet", cmdLen, command);
			}

			// expand any variables in our arguments, if we can
			args = expandMacro(ptr);

			// Once we have runtype support we should `runFile`
			// Run the command as a binary file
			result = mos_runBinFile(path, args ? args : ptr);

			if (result == FR_NO_FILE || result == FR_NO_PATH || result == FR_DISK_ERR) {
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
	BYTE	flags = 0;
	char *	path;
	int		result;

	for (;;) {
		result = extractString(ptr, &ptr, NULL, &path, EXTRACT_FLAG_AUTO_TERMINATE);
		if (result == FR_INVALID_PARAMETER) {
			path = ".";
			break;
		} else if (result != FR_OK) {
			return result;
		}
		if (path[0] == '-') {
			// we have flag(s)
			path++;
			while (path[0]) {
				switch(path[0]) {
					case 'l':
						flags |= MOS_DIR_LONG_LISTING;
						break;
					case 'a':
						flags |= MOS_DIR_SHOW_HIDDEN;
						break;
					case 's':
						flags |= MOS_DIR_SHOW_SYSTEM;
						break;
					case 'v':
						flags |= MOS_DIR_HIDE_VOLUME_INFO;
						break;
					default:
						printf("Invalid flag: %c\n\r", path[0]);
				}
				path++;
			}
		} else {
			break;
		}
	}
	return mos_DIR(path, flags);
}

// DO command
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdDO(char * ptr) {
	// Call mos_exec with in_mos set to true, which allows for OSCLI commands to use full run path
	return mos_exec(ptr, true, 0);
}

// TRY command
// This command is broadly similar to `X` in RISC OS, just named nicer, and with a return code variable
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdTRY(char * ptr) {
	// Call mos_exec with in_mos set to true, which allows for OSCLI commands to use full run path
	int result = mos_exec(ptr, true, 0);

	createOrUpdateSystemVariable("Try$ReturnCode", MOS_VAR_NUMBER, (void *)result);
	if (result > 0) {
		createOrUpdateSystemVariable("Try$Error", MOS_VAR_STRING, result < mos_errors_count ? mos_errors[result] : "Unknown error");
	}
	return FR_OK;
}

// ECHO command
//
int mos_cmdECHO(char *ptr) {
	t_mosTransInfo * transInfo;
	char read;
	int result = gsInit(ptr, &transInfo, GSTRANS_FLAG_NO_DOUBLEQUOTE | GSTRANS_FLAG_NO_TRACE);

	if (result != FR_OK) {
		return result;
	}

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
	int result;

	if (!extractNumber(ptr, &ptr, NULL, &fn_number, 0)) {
		UINT8 key;

		if (*ptr != '\0') {
			return FR_INVALID_PARAMETER;
		}

		printf("Hotkey assignments:\r\n\r\n");
		for (key = 1; key <= 12; key++) {
			hotkeyVar = NULL;
			sprintf(label, "Hotkey$%d", key);
			printf("F%d: %s", key, key < 10 ? " " : "");
			if (getSystemVariable(label, &hotkeyVar) == 0) {
				printEscapedString(hotkeyVar->value);
			} else {
				printf("N/A");
			}
			printf("\r\n");
		}
		printf("\r\n");
		return FR_OK;
	}

	ptr = mos_trim(ptr, false);

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
		return FR_OK;
	}

	// Remove surrounding quotes
	// TODO consider whether this should be an optional part of expandMacro?
	if (ptr[0] == '\"' && ptr[strlen(ptr) - 1] == '\"') {
		ptr[strlen(ptr) - 1] = '\0';
		ptr++;
	}
	// We need to add a `|M` to the end of the string...
	// For now we'll use a crude length check on our ptr (args)
	// ptr is at an unknown offset into our command buffer, but `*hotkey 12 ` is 10 characters
	// This is imperfect and may fail in some edge-cases
	// TODO This can more safely be done as part of expandMacro
	if (strlen(ptr) > 242) {
		return MOS_BAD_STRING;
	}
	strcat(ptr, "|M");

	hotkeyString = expandMacro(ptr);
	if (!hotkeyString) return FR_INT_ERR;
	result = createOrUpdateSystemVariable(label, MOS_VAR_STRING, hotkeyString);
	umm_free(hotkeyString);

	return result;
}

// IF <condition> THEN <command> [ELSE <command>] command
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdIF(char * ptr) {
	char * condition = ptr;
	char * then = NULL;
	char * elseCmd = NULL;
	int result = FR_OK;
	t_mosEvalResult * conditionResult = NULL;
	bool conditionResultBool = true;

	then = stristr(ptr, " THEN ");
	if (!then) {
		return FR_INVALID_PARAMETER;
	}
	*then = '\0';
	then += 6;

	elseCmd = stristr(then, " ELSE ");
	if (elseCmd) {
		*elseCmd = '\0';
		elseCmd += 6;
	}

	conditionResult = evaluateExpression(condition);
	if (conditionResult == NULL) {
		return FR_INT_ERR;
	}

	if (conditionResult->status == FR_INVALID_PARAMETER) {
		conditionResultBool = false;
	} else if (conditionResult->status != FR_OK) {
		result = conditionResult->status;
		conditionResultBool = false;
	} else if (conditionResult->type == MOS_VAR_STRING) {
		if (strlen(conditionResult->result) == 0) {
			conditionResultBool = false;
		}
		umm_free(conditionResult->result);
	} else if (conditionResult->type == MOS_VAR_NUMBER) {
		conditionResultBool = conditionResult->result != 0;
	} else {
		// Invalid type - shouldn't happen
		result = FR_INT_ERR;
	}

	umm_free(conditionResult);

	if (result == FR_OK) {
		if (conditionResultBool) {
			result = mos_exec(then, true, 0);
		} else if (elseCmd) {
			result = mos_exec(elseCmd, true, 0);
		}
	}

	return result;
}

// IFTHERE <condition> THEN <command> [ELSE <command>] command
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdIFTHERE(char * ptr) {
	char * filepath = NULL;
	char * then = NULL;
	char * elseCmd = NULL;
	int result = FR_OK;
	int pathLength = 0;	

	then = stristr(ptr, " THEN ");
	if (!then) {
		return FR_INVALID_PARAMETER;
	}
	*then = '\0';
	then += 6;

	elseCmd = stristr(then, " ELSE ");
	if (elseCmd) {
		*elseCmd = '\0';
		elseCmd += 6;
	}

	// TODO this probably needs to be called with the double-quotes flag cleared
	filepath = expandMacro(ptr);
	if (!filepath) {
		return FR_INVALID_PARAMETER;
	}

	if (strlen(filepath) == 0) {
		// No path to check, which we will interpret as "false"
		result = FR_INVALID_PARAMETER;
	} else {
		// check if the file exists
		result = resolvePath(filepath, NULL, &pathLength, NULL, NULL);
	}
	umm_free(filepath);

	if (result == FR_OK) {
		result = mos_exec(then, true, 0);
	} else if (elseCmd) {
		result = mos_exec(elseCmd, true, 0);
	} else {
		// No ELSE command, so return OK
		result = FR_OK;
	}

	return result;
}

// LOAD <filename> <addr> command
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdLOAD(char * ptr) {
	char *	filename;
	UINT24	addr;
	int result = extractString(ptr, &ptr, NULL, &filename, EXTRACT_FLAG_AUTO_TERMINATE);

	if (result != FR_OK) {
		return result;
	}
	if (!extractNumber(ptr, &ptr, NULL, (int *)&addr, 0)) {
		addr = MOS_defaultLoadAddress;
	}
	return mos_LOAD(filename, addr, 0);
}

// EXEC <filename>
//	Run a batch file containing MOS commands
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdEXEC(char *ptr) {
	char *	filename;
	int result = extractString(ptr, &ptr, NULL, &filename, EXTRACT_FLAG_AUTO_TERMINATE);

	if (result == FR_OK) result = mos_EXEC(filename);
	return result;
}

// Load and run a batch file of MOS commands.
// Parameters:
// - filename: The batch file to execute
// - buffer: Storage for each line to be loaded into and executed from (recommend 256 bytes)
// - size: Size of buffer (in bytes)
// Returns:
// - FatFS return code (of the last command)
//
int mos_cmdOBEY(char *ptr) {
	FRESULT	fr;
	FIL		fil;
	int		size = 256;
	char *	expandedPath = NULL;
	char *	filename = NULL;
	char *	directory = NULL;
	char *	tempDirectory = NULL;
	char *	absoluteDirectory = NULL;
	char *	substituted = NULL;
	char *	buffer = (char *)umm_malloc(size);
	int		line = 0;
	int		dirLength = 0;
	int		absoluteDirLength = 0;
	bool 	verbose = false;

	if (!buffer) {
		return MOS_OUT_OF_MEMORY;
	}

	fr = extractString(ptr, &ptr, NULL, &filename, EXTRACT_FLAG_AUTO_TERMINATE);
	if (fr == FR_OK && strcasecmp(filename, "-v") == 0) {
		verbose = true;
		fr = extractString(ptr, &ptr, NULL, &filename, EXTRACT_FLAG_AUTO_TERMINATE);
	}
	if (fr != FR_OK) {
		umm_free(buffer);
		return fr;
	}

	// TODO Consider merging with mos_EXEC - below is very similar

	fr = getResolvedPath(filename, &expandedPath);
	if (fr == FR_OK) {
		fr = f_open(&fil, expandedPath, FA_READ);
	}

	if (fr == FR_OK) fr = getDirectoryForPath(expandedPath, NULL, &dirLength, 0);
	if (fr == FR_OK) {
		directory = umm_malloc(dirLength);
		absoluteDirectory = umm_malloc(dirLength + strlen(cwd) + 1);
		if (directory && absoluteDirectory) {
			fr = getDirectoryForPath(expandedPath, directory, &dirLength, 0);
			if (fr == FR_OK) fr = resolveRelativePath(directory, absoluteDirectory, dirLength + strlen(cwd) + 1);
			if (fr == FR_OK) {
				createOrUpdateSystemVariable("Obey$Dir", MOS_VAR_STRING, absoluteDirectory);
			}
		} else {
			// Failed to allocate memory for obey directory
			fr = MOS_OUT_OF_MEMORY;
		}
		umm_free(absoluteDirectory);
		umm_free(directory);
	}

	if (fr == FR_OK) {
		while (!f_eof(&fil)) {
			line++;
			f_gets(buffer, size, &fil);
			substituted = substituteArguments(buffer, ptr, true);
			if (!substituted) {
				fr = MOS_OUT_OF_MEMORY;
				break;
			}
			if (verbose) {
				printf("Obey: %s", substituted);
				if (strrchr(substituted, '\n') == NULL) printf("\n");
				if (strrchr(substituted, '\r') == NULL) printf("\r");
			}
			fr = mos_exec(substituted, TRUE, 0);
			umm_free(substituted);
			if (fr != FR_OK) {
				printf("\r\nError executing %s at line %d\r\n", expandedPath, line);
				break;
			}
		}
	}
	f_close(&fil);
	umm_free(expandedPath);
	umm_free(buffer);
	return fr;	
}


// SAVE <filename> <addr> <len> command
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdSAVE(char * ptr) {
	char *	filename;
	UINT24	addr;
	UINT24	size;
	int result = extractString(ptr, &ptr, NULL, &filename, EXTRACT_FLAG_AUTO_TERMINATE);

	if (result != FR_OK) {
		return result;
	}
	if (!extractNumber(ptr, &ptr, NULL, (int *)&addr, 0) || !extractNumber(ptr, &ptr, NULL, (int *)&size, 0)) {
		return FR_INVALID_PARAMETER;
	}
	return mos_SAVE(filename, addr, size);
}

// DEL [-f] <filename> command
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdDEL(char * ptr) {
	FRESULT	fr;
	FRESULT	unlinkResult;
	DIR		dir;
	BOOL	verbose;
	BOOL	force = FALSE;
	char *	filename;
	char	verify[7];
	char *	resolvedPath;
	int		maxLength = 0;
	int		length = 0;
	BYTE	index = 0;

	fr = extractString(ptr, &ptr, NULL, &filename, EXTRACT_FLAG_AUTO_TERMINATE);

	if (fr == FR_OK && (strcasecmp(filename, "-f") == 0)) {
		force = TRUE;
		fr = extractString(ptr, &ptr, NULL, &filename, EXTRACT_FLAG_AUTO_TERMINATE);
	}
	if (fr != FR_OK) {
		return fr;
	}

	// Verbose kicks in when we our filename can potentially match multiple files
	verbose = mos_strcspn(filename, "*?:") != strlen(filename);
	if (!force) {
		// set force if we are matching single files
		force = !verbose;
	}

	// Work out our maximum path length
	fr = resolvePath(filename, NULL, &maxLength, NULL, NULL);
	if (!(fr == FR_OK || fr == FR_NO_FILE)) {
		return fr;
	}

	resolvedPath = umm_malloc(maxLength);
	if (!resolvedPath) {
		return MOS_OUT_OF_MEMORY;
	}
	*resolvedPath = '\0';

	length = maxLength;
	fr = resolvePath(filename, resolvedPath, &length, &index, &dir);
	unlinkResult = fr;

	while (fr == FR_OK) {
		// We have a resolved path - either to a file or a directory
		if (!force) {
			INT24 retval;
			// we could potentially support "All" here, and when detected changing `force` to true
			printf("Delete %s? (Yes/No/Cancel) ", resolvedPath);
			retval = mos_EDITLINE(&verify, sizeof(verify), 13);
			printf("\n\r");
			if (retval == 13) {
				if (strcasecmp(verify, "Cancel") == 0 || strcasecmp(verify, "C") == 0) {
					printf("Cancelled.\r\n");
					break;
				}
				if (strcasecmp(verify, "Yes") == 0 || strcasecmp(verify, "Y") == 0) {
					printf("Deleting %s\r\n", resolvedPath);
					unlinkResult = f_unlink(resolvedPath);
				}
			} else {
				printf("Cancelled.\r\n");
				break;
			}
		} else {
			if (verbose) {
				printf("Deleting %s\r\n", resolvedPath);
			}
			unlinkResult = f_unlink(resolvedPath);
		}

		// On any unlink error, break out of the loop
		if (unlinkResult != FR_OK) break;
		length = maxLength;
		fr = resolvePath(filename, resolvedPath, &length, &index, &dir);
	}

	umm_free(resolvedPath);
	return unlinkResult;
}

// JMP <addr> command
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdJMP(char *ptr) {
	UINT24	addr;
	void (* dest)(void) = 0;

	if (!extractNumber(ptr, &ptr, NULL, (int *)&addr, 0)) {
		return FR_INVALID_PARAMETER;
	}
	dest = (void *)addr;
	dest();
	return 0;
}

// LOADFILE <filename> [<arguments>] command
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdLOADFILE(char *ptr) {
	return mos_runOrLoadFile(ptr, false);
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

	if (!extractNumber(ptr, &ptr, NULL, (int *)&addr, 0)) {
		addr = MOS_defaultLoadAddress;
	}
	ptr = mos_trim(ptr, false);
	result = mos_runBin(addr, ptr);
	return result;
}

// RUNBIN <filename> [<arguments>] command
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdRUNBIN(char *ptr) {
	char *	filename = NULL;
	int 	result = extractString(ptr, &ptr, NULL, &filename, EXTRACT_FLAG_AUTO_TERMINATE);

	if (result != FR_OK) {
		return result;
	}
	ptr = mos_trim(ptr, false);
	return mos_runBinFile(filename, ptr);
}

// RUNFILE <filename> [<arguments>] command
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdRUNFILE(char *ptr) {
	return mos_runOrLoadFile(ptr, true);
}

// CD <path> command
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdCD(char * ptr) {
	char *	path;
	int		result = extractString(ptr, &ptr, NULL, &path, EXTRACT_FLAG_AUTO_TERMINATE);

	if (result != FR_OK) {
		// TODO should we default to the root directory if no path is provided?
		return result;
	}
	return mos_CD(path);
}

// REN <filename1> <filename2> command
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdREN(char *ptr) {
	char *	filename1;
	char *	filename2;
	int 	result = extractString(ptr, &ptr, NULL, &filename1, EXTRACT_FLAG_AUTO_TERMINATE);

	if (result == FR_OK) result = extractString(ptr, &ptr, NULL, &filename2, EXTRACT_FLAG_AUTO_TERMINATE);
	if (result == FR_OK) result = mos_REN(filename1, filename2, TRUE);
	return result;
}

// COPY <filename1> <filename2> command
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdCOPY(char *ptr) {
	char *	filename1;
	char *	filename2;
	int 	result = extractString(ptr, &ptr, NULL, &filename1, EXTRACT_FLAG_AUTO_TERMINATE);

	if (result == FR_OK) result = extractString(ptr, &ptr, NULL, &filename2, EXTRACT_FLAG_AUTO_TERMINATE);
	if (result == FR_OK) result = mos_COPY(filename1, filename2, TRUE);
	return result;
}

// MKDIR <filename> command
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdMKDIR(char * ptr) {
	char *	filename;
	int result = extractString(ptr, &ptr, NULL, &filename, EXTRACT_FLAG_AUTO_TERMINATE);

	if (result == FR_OK) result = mos_MKDIR(filename);
	return result;
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
	int 	result = extractString(ptr, &ptr, NULL, &token, EXTRACT_FLAG_AUTO_TERMINATE);

	if (result != FR_OK) {
		return result;
	}

	mos_trim(ptr, false);
	if (*ptr == '\0') {
		return FR_INVALID_PARAMETER;
	}

	newValue = expandMacro(ptr);
	if (!newValue) return FR_INT_ERR;

	result = createOrUpdateSystemVariable(token, MOS_VAR_STRING, newValue);
	umm_free(newValue);
	return result;
}

// SetEval <varname> <expression> command
//
int mos_cmdSETEVAL(char * ptr) {
	char *	token;
	t_mosEvalResult * evaluation = NULL;
	int 	result = extractString(ptr, &ptr, NULL, &token, EXTRACT_FLAG_AUTO_TERMINATE);

	if (result != FR_OK) {
		return result;
	}

	while (isspace(*ptr)) ptr++;
	if (*ptr == '\0') {
		return FR_INVALID_PARAMETER;
	}

	evaluation = evaluateExpression(ptr);
	if (evaluation == NULL) {
		return FR_INT_ERR;
	}

	if (evaluation->status != FR_OK) {
		result = evaluation->status;
	} else {
		result = createOrUpdateSystemVariable(token, evaluation->type, evaluation->result);
		if (evaluation->type == MOS_VAR_STRING) {
			umm_free(evaluation->result);
		}
	}
	umm_free(evaluation);
	return result;
}

// SETMACRO <varname> <value> command
//
int mos_cmdSETMACRO(char * ptr) {
	char *	token;
	int 	result = extractString(ptr, &ptr, NULL, &token, EXTRACT_FLAG_AUTO_TERMINATE);

	if (result != FR_OK) {
		return result;
	}

	// make sure we have a value
	while (isspace(*ptr)) ptr++;
	if (*ptr == '\0') {
		return FR_INVALID_PARAMETER;
	}
	result = createOrUpdateSystemVariable(token, MOS_VAR_MACRO, ptr);

	return result;
}

void printEscapedString(char * value) {
	while (*value) {
		if ((unsigned char)*value < 0x20) {
			putch('|');
			putch(*value + 0x40);
		} else if ((unsigned char)*value == 0x7F) {
			putch('|');
			putch('?');
		} else if ((unsigned char)*value == '|') {
			putch('|');
			putch('|');
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
	t_mosSystemVariable * var = NULL;
	char *	token;
	int		searchResult = extractString(ptr, &ptr, NULL, &token, EXTRACT_FLAG_AUTO_TERMINATE);

	if (searchResult != FR_OK) {
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

	return FR_OK;
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
	int searchResult = extractString(ptr, &ptr, NULL, &token, EXTRACT_FLAG_AUTO_TERMINATE);

	if (searchResult != FR_OK) {
		return searchResult;
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
	char *	token = NULL;
	char *	delimiter = ", ";

	while (!extractString(ptr, &ptr, NULL, &token, EXTRACT_FLAG_AUTO_TERMINATE | EXTRACT_FLAG_NO_DOUBLEQUOTE)) {
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
			return FR_INVALID_PARAMETER;
		}

		if ((endPtr != NULL && endPtr < token + len) || value > 65535) {
			// Did not consume all of the string, or value too large
			return FR_INVALID_PARAMETER;
		}

		if (value > 255 || value < -255) {
			is_word = true;
		}

		if (is_word) {
			putch(value & 0xFF);	// write LSB
			putch(value >> 8);		// write MSB
		} else {
			putch(value);
		}
	}

	return FR_OK;
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
	return FR_OK;
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

	return FR_OK;
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
	return FR_OK;
}

// TYPE <filename>
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int mos_cmdTYPE(char * ptr) {
	char *	filename;
	int		result = extractString(ptr, &ptr, NULL, &filename, EXTRACT_FLAG_AUTO_TERMINATE);

	if (result == FR_OK) result = mos_TYPE(filename);
	return result;
}

// CLS
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int	mos_cmdCLS(char *ptr) {
	putch(12);
	return 0;
}

// MOUNT
// Parameters:
// - ptr: Pointer to the argument string in the line edit buffer
// Returns:
// - MOS error code
//
int	mos_cmdMOUNT(char *ptr) {
	return mos_mount();
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
// - 0: Success
//
int mos_cmdHELP(char *ptr) {
	char *	cmd;
	int		i;
	int		result = extractString(ptr, &ptr, NULL, &cmd, EXTRACT_FLAG_AUTO_TERMINATE);
	bool	hasCmd = result == FR_OK;

	if (result == FR_INVALID_PARAMETER) {
		cmd = "help";
	} else if (result != FR_OK) {
		return result;
	}

	if (strcasecmp(cmd, "all") == 0) {
		for (i = 0; i < mosCommands_count; ++i) {
			printCommandInfo(&mosCommands[i], FALSE);
		}
		return FR_OK;
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
		result = extractString(ptr, &ptr, NULL, &cmd, EXTRACT_FLAG_AUTO_TERMINATE);
	} while (result == FR_OK);

	return result == FR_INVALID_PARAMETER ? FR_OK : result;
}


#if DEBUG > 0
int mos_cmdTEST(char *ptr) {
	char * testName;
	bool ran = false;
	bool verbose = false;

	while (extractString(ptr, &ptr, NULL, &testName, EXTRACT_FLAG_AUTO_TERMINATE) == FR_OK) {
		if (strcasecmp(testName, "verbose") == 0) {
			verbose = true;
		}
		if (strcasecmp(testName, "-v") == 0) {
			verbose = true;
		}
		if (strcasecmp(testName, "mem") == 0) {
			ran = true;
			malloc_grind(verbose);
		}
		if (strcasecmp(testName, "path") == 0) {
			ran = true;
			path_tests(verbose);
		}
		if (strcasecmp(testName, "string") == 0) {
			ran = true;
			string_tests(verbose);
		}
		if (strcasecmp(testName, "all") == 0) {
			ran = true;
			malloc_grind(verbose);
			path_tests(verbose);
			break;
		}
	}
	if (!ran) {
		printf("No tests run.\n\r");
		printf("Available tests are 'mem' and 'path', or 'all' to run all.\r\n");
		printf("Run with 'verbose' or '-v' to get more detailed output.\r\n");
	}
	return FR_OK;
}
#endif /* DEBUG */


// Load a file from SD card to memory
// Parameters:
// - filename: Path of file to load
// - address: Address in RAM to load the file into
// - size: Number of bytes to load, 0 for maximum file size
// Returns:
// - FatFS return code
// 
UINT24 mos_LOAD(char * filename, UINT24 address, UINT24 size) {
	FIL		fil;
	UINT	br;	
	FSIZE_t	fSize;
	char *	expandedFilename = NULL;
	FRESULT	fr = getResolvedPath(filename, &expandedFilename);
	
	if (fr != FR_OK) {
		return fr;
	}

	fr = f_open(&fil, expandedFilename, FA_READ);
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
	umm_free(expandedFilename);
	return fr;
}

UINT24 mos_LOAD_API(char * filename, UINT24 address, UINT24 size) {
	FRESULT	fr;
	char *	expandedFilename = expandMacro(filename);

	if (!expandedFilename) return FR_INT_ERR;
	fr = mos_LOAD(expandedFilename, address, size);
	umm_free(expandedFilename);
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
UINT24 mos_SAVE(char * filename, UINT24 address, UINT24 size) {
	FIL		fil;
	UINT	br;	
	char *	expandedFilename = NULL;
	FRESULT	fr;

	// if the filename contains a wildcard then return an error
	if (mos_strcspn(filename, "*?") != strlen(filename)) {
		return FR_INVALID_PARAMETER;
	}

	fr = getResolvedPath(filename, &expandedFilename);

	if (fr == FR_OK || fr == FR_NO_FILE) {
		fr = f_open(&fil, expandedFilename, FA_WRITE | FA_CREATE_NEW);
		if (fr == FR_OK) {
			fr = f_write(&fil, (void *)address, size, &br);
		}
		f_close(&fil);
	}
	umm_free(expandedFilename);
	return fr;
}

UINT24 mos_SAVE_API(char * filename, UINT24 address, UINT24 size) {
	FRESULT	fr;
	char *	expandedFilename = expandMacro(filename);

	if (!expandedFilename) return FR_INT_ERR;
	fr = mos_SAVE(expandedFilename, address, size);
	umm_free(expandedFilename);
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
	int		i;
	char *	expandedFilename = NULL;
	char *	buffer = umm_malloc(size);

	if (!buffer) {
		return MOS_OUT_OF_MEMORY;
	}

	fr = getResolvedPath(filename, &expandedFilename);
	if (fr == FR_OK) {
		fr = f_open(&fil, expandedFilename, FA_READ);
	}
	if (fr == FR_OK) {
		while (1) {
			fr = f_read(&fil, (void *)buffer, size, &br);
			if (br == 0)
				break;
			for (i = 0; i < br; ++i) {
				char c = buffer[i];
				if (c < 0x20 && c != '\n' && c != '\r') {
					putch('|');
					putch(c + 0x40);
				} else if (c == 0x7F) {
					putch('|');
					putch('?');
				} else if (c == '|') {
					putch('|');
					putch('|');
				} else {
					putch(c);
					if (c == '\n') {
						putch('\r');
					}
				}
			}
		}
		f_close(&fil);
	}

	umm_free(expandedFilename);
	umm_free(buffer);
	return fr;
}

// Change directory
// Parameters:
// - filename: Path of file to save
// Returns:
// - FatFS return code
// 
UINT24	mos_CD(char *path) {
	FRESULT	fr;
	char *	expandedPath = NULL;

	fr = getResolvedPath(path, &expandedPath);
	if (fr == FR_OK || fr == FR_NO_FILE) {
		fr = f_chdir(expandedPath);
		f_getcwd(cwd, sizeof(cwd)); // Update full path.
	}
	umm_free(expandedPath);
	return fr;
}

UINT24 mos_CD_API(char *path) {
	FRESULT	fr;
	char *	expandedPath = expandMacro(path);

	if (!expandedPath) return FR_INT_ERR;
	fr = mos_CD(expandedPath);
	umm_free(expandedPath);
	return fr;
}

UINT24 countDirEntries(const char * path, const char * pattern, BYTE flags, int * count) {
	FRESULT		fr;
	DIR			dir;
	FILINFO		file;
	bool		usePattern = pattern != NULL;
	bool		showHidden = flags & MOS_DIR_SHOW_HIDDEN;
	bool		showSystem = flags & MOS_DIR_SHOW_SYSTEM;

	*count = 0;
	if (usePattern) {
		fr = f_findfirst(&dir, &file, path, pattern);
	} else {
		fr = f_opendir(&dir, path);
		if (fr == FR_OK) fr = f_readdir(&dir, &file);
	}
	while (fr == FR_OK && file.fname[0] != 0) {
		if ((showHidden || !(file.fattrib & AM_HID)) && (showSystem || !(file.fattrib & AM_SYS))) {
			(*count)++;
		}
		fr = usePattern ? f_findnext(&dir, &file) : f_readdir(&dir, &file);
	}
	f_closedir(&dir);
	return fr;
}

typedef struct SmallFilInfo {
	FSIZE_t	fsize;		/* File size */
	WORD	fdate;		/* Modified date */
	WORD	ftime;		/* Modified time */
	BYTE	fattrib;	/* File attribute */
	char *	fname;		/* umm_malloc'ed */
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
UINT24 mos_DIR_API(char* path) {
	FRESULT	fr;
	char *	expandedPath = expandMacro(path);

	if (!expandedPath) return FR_INT_ERR;
	fr = mos_DIR(expandedPath, MOS_DIR_LONG_LISTING | MOS_DIR_SHOW_HIDDEN);
	umm_free(expandedPath);
	return fr;
}

UINT24	mos_DIRFallback(char * dirPath, char * pattern, BYTE flags) {
	FRESULT	fr;
	DIR		dir;
	static	FILINFO	file;
	char	volume[12];
	int		col = 0;
	bool	showingCWD = dirPath[0] == 0 || strcmp(dirPath, ".") == 0 || strcmp(dirPath, "./") == 0;
	bool	longListing = flags & MOS_DIR_LONG_LISTING;
	bool	showHidden = flags & MOS_DIR_SHOW_HIDDEN;
	bool	showSystem = flags & MOS_DIR_SHOW_SYSTEM;
	bool	hideVolumeInfo = flags & MOS_DIR_HIDE_VOLUME_INFO;
	bool	usePattern = pattern != NULL;

	if (!hideVolumeInfo) {
		fr = f_getlabel("", volume, 0);
		if (fr != FR_OK) {
			return fr;
		}
		printf("Volume: %s\n\r", volume[0] ? volume : "<No Volume Label>");
		if (showingCWD) f_getcwd(cwd, sizeof(cwd));
		printf("Directory: %s\r\n\r\n", showingCWD ? cwd : dirPath);
	}

	fr = f_opendir(&dir, dirPath);
	if (fr != FR_OK) {
		return fr;
	}

	fr = usePattern ? f_findfirst(&dir, &file, dirPath, pattern) : f_readdir(&dir, &file);
	while (fr == FR_OK && file.fname[0]) {
		if ((showHidden || !(file.fattrib & AM_HID)) && (showSystem || !(file.fattrib & AM_SYS))) {
			if (longListing) {
				int yr = (file.fdate & 0xFE00) >>  9;	// Bits 15 to  9, from 1980
				int mo = (file.fdate & 0x01E0) >>  5;	// Bits  8 to  5
				int da = (file.fdate & 0x001F);			// Bits  4 to  0
				int hr = (file.ftime & 0xF800) >> 11;	// Bits 15 to 11
				int mi = (file.ftime & 0x07E0) >>  5;	// Bits 10 to  5

				printf("%04d/%02d/%02d\t%02d:%02d %c%c%c %*lu %s\n\r",
					yr + 1980, mo, da, hr, mi,
					file.fattrib & AM_DIR ? 'D' : ' ', file.fattrib & AM_HID ? 'H' : ' ', file.fattrib & AM_SYS ? 'S' : ' ',
					8, file.fsize, file.fname
				);
			} else {
				if (col + strlen(file.fname) + 2 >= scrcols) {
					printf("\r\n");
					col = 0;
				}
				printf("%s  ", file.fname);
				col += strlen(file.fname) + 2;
			}
		}
		fr = usePattern ? f_findnext(&dir, &file) : f_readdir(&dir, &file);
	}
	if (!longListing) {
		printf("\r\n");
	}

	f_closedir(&dir);
	return fr;
}

UINT24 displayDirectory(char * dirPath, char * pattern, BYTE flags) {
	FRESULT		fr;
	DIR			dir;
	FILINFO		file;
	char		volume[12]; // Buffer for volume label
	int			longestFilename = 0;
	int			filenameLength = 0;
	BYTE		textBg;
	BYTE		textFg = 15;
	BYTE		dirColour = 2;
	BYTE		fileColour = 15;
	SmallFilInfo *	filesInfo = NULL, *fileInfo = NULL;
	int			entryCount = 0;
	int			fileNum = 0;
	bool		usePattern = pattern != NULL;
	bool		useColour = scrcolours > 2 && vdpSupportsTextPalette;
	bool		showingCWD = dirPath[0] == 0 || strcmp(dirPath, ".") == 0 || strcmp(dirPath, "./") == 0;
	bool		longListing = flags & MOS_DIR_LONG_LISTING;
	bool		showHidden = flags & MOS_DIR_SHOW_HIDDEN;
	bool		showSystem = flags & MOS_DIR_SHOW_SYSTEM;
	bool		hideVolumeInfo = flags & MOS_DIR_HIDE_VOLUME_INFO;

	fr = f_getlabel("", volume, 0);
	if (fr != FR_OK) {
		return fr;
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

	fr = f_opendir(&dir, dirPath);
	if (fr != FR_OK) {
		return fr;
	}

	if (!hideVolumeInfo) {
		printf("Volume: %s\n\r", volume[0] ? volume : "<No Volume Label>");
		if (showingCWD) f_getcwd(cwd, sizeof(cwd));
		printf("Directory: %s\r\n\r\n", showingCWD ? cwd : dirPath);
	}

	fr = countDirEntries(dirPath, pattern, flags, &entryCount);
	if (entryCount == 0) {
		printf("No files found\r\n");
		return FR_OK;
	}
	filesInfo = umm_malloc(sizeof(SmallFilInfo) * entryCount);
	if (!filesInfo) {
		return mos_DIRFallback(dirPath, pattern, flags | MOS_DIR_HIDE_VOLUME_INFO);
	}

	fr = usePattern ? f_findfirst(&dir, &file, dirPath, pattern) : f_readdir(&dir, &file);
	while (fr == FR_OK && file.fname[0]) {
		if ((showHidden || !(file.fattrib & AM_HID)) && (showSystem || !(file.fattrib & AM_SYS))) {
			filesInfo[fileNum].fsize = file.fsize;
			filesInfo[fileNum].fdate = file.fdate;
			filesInfo[fileNum].ftime = file.ftime;
			filesInfo[fileNum].fattrib = file.fattrib;
			filenameLength = strlen(file.fname) + 1;
			filesInfo[fileNum].fname = umm_malloc(filenameLength);
			if (!filesInfo[fileNum].fname) {
				// Couldn't store filename, so fall back to stack-only directory listing
				fr = mos_DIRFallback(dirPath, pattern, flags | MOS_DIR_HIDE_VOLUME_INFO);
				while (fileNum > 0) {
					umm_free(filesInfo[--fileNum].fname);
				}
				umm_free(filesInfo);
				return fr;
			}
			strcpy(filesInfo[fileNum].fname, file.fname);
			if (filenameLength > longestFilename) {
				longestFilename = filenameLength;
			}
			fileNum++;
		}

		fr = usePattern ? f_findnext(&dir, &file) : f_readdir(&dir, &file);
	}
	f_closedir(&dir);

	if (fileNum == 0) {
		printf("No files found\r\n");
	} else if (fr == FR_OK) {
		// All file info gathered, so sort and display
		int column = 0;
		int maxCols = scrcols / longestFilename;

		qsort(filesInfo, entryCount, sizeof(SmallFilInfo), cmp_filinfo);
		fileNum = 0;

		while (fileNum < entryCount) {
			bool isDir;
			fileInfo = &filesInfo[fileNum];
			isDir = fileInfo->fattrib & AM_DIR;
			if (longListing) {
				int yr = (fileInfo->fdate & 0xFE00) >> 9;	// Bits 15 to  9, from 1980
				int mo = (fileInfo->fdate & 0x01E0) >> 5;	// Bits  8 to  5
				int da = (fileInfo->fdate & 0x001F);		// Bits  4 to  0
				int hr = (fileInfo->ftime & 0xF800) >> 11;	// Bits 15 to 11
				int mi = (fileInfo->ftime & 0x07E0) >> 5;	// Bits 10 to  5

				if (useColour) {
					printf("\x11%c%04d/%02d/%02d\t%02d:%02d %c%c%c %*lu \x11%c%s\n\r",
						textFg, yr + 1980, mo, da, hr, mi,
						isDir ? 'D' : ' ', fileInfo->fattrib & AM_HID ? 'H' : ' ', fileInfo->fattrib & AM_SYS ? 'S' : ' ',
						8, fileInfo->fsize, isDir ? dirColour : fileColour, fileInfo->fname
					);
				} else {
					printf("%04d/%02d/%02d\t%02d:%02d %c%c%c %*lu %s\n\r",
						yr + 1980, mo, da, hr, mi,
						isDir ? 'D' : ' ', fileInfo->fattrib & AM_HID ? 'H' : ' ', fileInfo->fattrib & AM_SYS ? 'S' : ' ',
						8, fileInfo->fsize, fileInfo->fname
					);
				}
			} else {
				if (column == maxCols) {
					column = 0;
					printf("\r\n");
				}
				if (useColour) {
					printf("\x11%c%-*s", isDir ? dirColour : fileColour, column == (maxCols - 1) ? longestFilename - 1 : longestFilename, fileInfo->fname);
				} else {
					printf("%-*s", column == (maxCols - 1) ? longestFilename - 1 : longestFilename, fileInfo->fname);
				}
				column++;
			}
			fileNum++;
		}
		if (!longListing) printf("\r\n");
		if (useColour) {
			printf("\x11%c", textFg);
		}
	}

	// Clear up our memory
	while (fileNum > 0) {
		umm_free(filesInfo[--fileNum].fname);
	}
	umm_free(filesInfo);

	return fr;
}


// Directory listing
// Returns:
// - FatFS return code
//
UINT24 mos_DIR(char* inputPath, BYTE flags) {
	char *	leafname = getFilepathLeafname(inputPath);
	int		pathLength = 0;
	BYTE	pathIndex = 0;
	FRESULT	fr = FR_NO_PATH;
	FRESULT pathResult = getDirectoryForPath(inputPath, NULL, &pathLength, pathIndex);

	while (pathResult == FR_OK) {
		// Get the current path - use a larger buffer to allow for leafname to be appended for testing
		bool showingPath = false;
		char * currentPath = umm_malloc(pathLength + strlen(leafname) + 1);
		if (!currentPath) {
			return MOS_OUT_OF_MEMORY;
		}
		pathResult = getDirectoryForPath(inputPath, currentPath, &pathLength, pathIndex);

		showingPath = pathResult == FR_OK && isDirectory(currentPath);
		if (showingPath) {
			// Work out if we are targetting an explicit directory with our leafname
			strcat(currentPath, leafname);
			if (isDirectory(currentPath)) {
				// Display directory at sourcePath, no pattern
				pathResult = displayDirectory(currentPath, NULL, flags);
			} else {
				// Display directory at currentPath, using leafname as pattern
				currentPath[pathLength - 1] = '\0';
				pathResult = displayDirectory(currentPath, leafname, flags);
			}
			if (fr != FR_OK) {
				fr = pathResult;
			}
		}

		umm_free(currentPath);
		pathIndex++;
		pathResult = getDirectoryForPath(inputPath, NULL, &pathLength, pathIndex);
		if (showingPath && pathResult == FR_OK) {
			printf("\r\n\n\r");
		}
	}

	return fr;
}


// Delete file
// Parameters:
// - filename: Path of file to delete
// Returns:
// - FatFS return code
//
UINT24 mos_DEL(char * filename) {
	char * expandedPath = NULL;
	FRESULT	fr;
	if (mos_strcspn(filename, "*?") == strlen(filename)) {
		fr = expandPath(filename, &expandedPath);
		if (fr == FR_OK) {
			fr = f_unlink(expandedPath);
		}
		umm_free(expandedPath);
	} else {
		fr = FR_INVALID_PARAMETER;
	}
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
	FRESULT	fr = FR_INT_ERR;
	char *	expandedSrcPath = expandMacro(srcPath);
	char *	expandedDstPath = expandMacro(dstPath);

	if (expandedSrcPath && expandedDstPath) {
		fr = mos_REN(expandedSrcPath, expandedDstPath, FALSE);
	}
	umm_free(expandedSrcPath);
	umm_free(expandedDstPath);
	return fr;
}


// Rename file
// Parameters:
// - srcPath: Source path of file to rename
// - dstPath: Destination file path
// Returns:
// - FatFS return code
// 
UINT24 mos_REN(char *srcPath, char *dstPath, BOOL verbose) {
	FRESULT	fr;
	FRESULT	renResult;
	DIR		dir;
	char *	resolvedDestPath = NULL;
	char *	fullSrcPath = NULL;
	int		maxLength = 0;
	int		length = 0;
	BYTE	index = 0;
	BOOL	usePattern = FALSE;
	BOOL	targetIsDir = FALSE;
	BOOL	addSlash = FALSE;

	if (mos_strcspn(dstPath, "*?") != strlen(dstPath)) {
		// Destination path cannot include wildcards
		return FR_INVALID_PARAMETER;
	}
	// establish whether src has a pattern
	// NB if target is not a directory only first match will be copied
	usePattern = mos_strcspn(srcPath, "*?:") != strlen(srcPath);

	fr = getResolvedPath(dstPath, &resolvedDestPath);
	if (fr != FR_OK && fr != FR_NO_FILE) {
		// Destination path must either be a directory, or a non-existant file
		umm_free(resolvedDestPath);
		return fr;
	}
	targetIsDir = isDirectory(resolvedDestPath);
	if (!targetIsDir && fr == FR_OK) {
		// Destination path (file) already exists - we don't support overwriting, yet
		umm_free(resolvedDestPath);
		return FR_EXIST;
	}
	if (targetIsDir) {
		addSlash = dstPath[strlen(dstPath) - 1] != '/';
	}

	fr = resolvePath(srcPath, NULL, &maxLength, NULL, NULL);
	if (fr != FR_OK) {
		// source couldn't be resolved, so no file to move
		umm_free(resolvedDestPath);
		return fr;
	}
	fullSrcPath = umm_malloc(maxLength + 1);
	if (!fullSrcPath) {
		umm_free(resolvedDestPath);
		return MOS_OUT_OF_MEMORY;
	}

	length = maxLength;
	fr = resolvePath(srcPath, fullSrcPath, &length, &index, &dir);
	renResult = fr;

	while (fr == FR_OK) {
		// Build our destination path in fullDstPath
		char * srcLeafname = getFilepathLeafname(fullSrcPath);
		int dstLen = strlen(resolvedDestPath) + (targetIsDir ? strlen(srcLeafname) : 0) + 2;
		char * fullDstPath = umm_malloc(dstLen);
		if (!fullDstPath) {
			fr = MOS_OUT_OF_MEMORY;
			break;
		}
		sprintf(fullDstPath, "%s%s%s", resolvedDestPath, addSlash ? "/" : "", targetIsDir ? srcLeafname : "");
		// Rename the file
		if (verbose) printf("Moving %s to %s\r\n", fullSrcPath, fullDstPath);
		renResult = f_rename(fullSrcPath, fullDstPath);
		umm_free(fullDstPath);
		if (renResult != FR_OK) break;
		if (usePattern && targetIsDir) {
			// get next matching source, if there is one
			length = maxLength;
			fr = resolvePath(srcPath, fullSrcPath, &length, &index, &dir);
		} else {
			break;
		}
	}

	umm_free(fullSrcPath);
	umm_free(resolvedDestPath);
	return renResult;
}

// Copy file
// Parameters:
// - srcPath: Source path of file to copy
// - dstPath: Destination file path
// Returns:
// - FatFS return code
// 
UINT24 mos_COPY_API(char *srcPath, char *dstPath) {
	FRESULT	fr = FR_INT_ERR;
	char *	expandedSrcPath = expandMacro(srcPath);
	char *	expandedDstPath = expandMacro(dstPath);

	if (expandedSrcPath && expandedDstPath) {
		fr = mos_COPY(expandedSrcPath, expandedDstPath, FALSE);
	}
	umm_free(expandedSrcPath);
	umm_free(expandedDstPath);
	return fr;
}

// Copy file
// Parameters:
// - srcPath: Source path of file to copy
// - dstPath: Destination file path
// - verbose: Print progress messages
// Returns:
// - FatFS return code
// - MOS_OUT_OF_MEMORY if memory allocation fails
// 
UINT24 mos_COPY(char *srcPath, char *dstPath, BOOL verbose) {
	FRESULT	fr;
	FRESULT	copyResult;
	DIR		dir;
	char *	resolvedDestPath = NULL;
	char *	fullSrcPath = NULL;
	int		maxLength = 0;
	int		length = 0;
	BYTE	index = 0;
	BOOL	usePattern = FALSE;
	BOOL	targetIsDir = FALSE;
	BOOL	addSlash = FALSE;

	// TODO refactor to use a common function for resolving paths
	// as this is identical to mos_REN - only the contents of the `while` loop differ
	if (mos_strcspn(dstPath, "*?") != strlen(dstPath)) {
		// Destination path cannot include wildcards
		return FR_INVALID_PARAMETER;
	}
	// establish whether src has a pattern
	// NB if target is not a directory only first match will be copied
	usePattern = mos_strcspn(srcPath, "*?:") != strlen(srcPath);

	fr = getResolvedPath(dstPath, &resolvedDestPath);
	if (fr != FR_OK && fr != FR_NO_FILE) {
		// Destination path must either be a directory, or a non-existant file
		umm_free(resolvedDestPath);
		return fr;
	}
	targetIsDir = isDirectory(resolvedDestPath);
	if (!targetIsDir && fr == FR_OK) {
		// Destination path (file) already exists - we don't support overwriting, yet
		umm_free(resolvedDestPath);
		return FR_EXIST;
	}
	if (targetIsDir) {
		addSlash = dstPath[strlen(dstPath) - 1] != '/';
	}

	fr = resolvePath(srcPath, NULL, &maxLength, NULL, NULL);
	if (fr != FR_OK) {
		// we only support copying files - a resolved source path returning `no file` or `no path` is an error
		umm_free(resolvedDestPath);
		return fr;
	}
	fullSrcPath = umm_malloc(maxLength + 1);
	if (!fullSrcPath) {
		umm_free(resolvedDestPath);
		return MOS_OUT_OF_MEMORY;
	}

	length = maxLength;
	fr = resolvePath(srcPath, fullSrcPath, &length, &index, &dir);
	copyResult = fr;

	while (fr == FR_OK) {
		// Build our destination path in fullDstPath
		char * srcLeafname = getFilepathLeafname(fullSrcPath);
		int dstLen = strlen(resolvedDestPath) + (targetIsDir ? strlen(srcLeafname) : 0) + 2;

		// skip copying if source is a directory (possibly encountered via a pattern match)
		if (!isDirectory(fullSrcPath)) {
			char * fullDstPath = umm_malloc(dstLen);
			if (!fullDstPath) {
				fr = MOS_OUT_OF_MEMORY;
				break;
			}
			sprintf(fullDstPath, "%s%s%s", resolvedDestPath, addSlash ? "/" : "", targetIsDir ? srcLeafname : "");

			// Copy the file
			if (verbose) printf("Copying %s to %s\r\n", fullSrcPath, fullDstPath);
			copyResult = copyFile(fullSrcPath, fullDstPath);
			umm_free(fullDstPath);
			if (copyResult != FR_OK) break;
		} else if (verbose) {
			// Copying directories is not supported, so just print a message
			printf("Skipping directory %s\r\n", fullSrcPath);
		}

		if (usePattern && targetIsDir) {
			// get next matching source, if there is one
			length = maxLength;
			fr = resolvePath(srcPath, fullSrcPath, &length, &index, &dir);
		} else {
			break;
		}
	}

	umm_free(fullSrcPath);
	umm_free(resolvedDestPath);
	return copyResult;
}


// Make a directory
// Parameters:
// - filename: Path of file to delete
// Returns:
// - FatFS return code
// 
UINT24 mos_MKDIR(char * filename) {
	FRESULT	fr;
	char *	resolved = NULL;

	fr = getResolvedPath(filename, &resolved);
	if (fr == FR_NO_FILE) {
		fr = f_mkdir(resolved);
	}
	umm_free(resolved);
	return fr;
}

UINT24 mos_MKDIR_API(char * filename) {
	FRESULT	fr;
	char *	expandedFilename = expandMacro(filename);

	if (!expandedFilename) return FR_INT_ERR;
	fr = mos_MKDIR(expandedFilename);
	umm_free(expandedFilename);
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
	char *	expandedPath = NULL;
	char *	buffer = (char *)umm_malloc(size);
	int		line = 0;

	// TODO consider merging this implementation with Obey
	// it would just need some flags to disable the Obey$Dir and argument substitution
	if (!buffer) {
		return MOS_OUT_OF_MEMORY;
	}
	fr = getResolvedPath(filename, &expandedPath);
	if (fr == FR_OK) {
		fr = f_open(&fil, expandedPath, FA_READ);
	}
	if (fr == FR_OK) {
		while (!f_eof(&fil)) {
			line++;
			f_gets(buffer, size, &fil);
			fr = mos_exec(buffer, TRUE, 0);
			if (fr != FR_OK) {
				printf("\r\nError executing %s at line %d\r\n", expandedPath, line);
				break;
			}
		}
		f_close(&fil);
	}
	umm_free(expandedPath);
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
	char *	expandedFilename = NULL;
	int		i;
	FRESULT	fr = expandPath(filename, &expandedFilename);

	if (fr == FR_OK || fr == FR_NO_FILE) {
		for (i = 0; i < MOS_maxOpenFiles; i++) {
			if (mosFileObjects[i].free == 0) {
				fr = f_open(&mosFileObjects[i].fileObject, expandedFilename, mode);
				if (fr == FR_OK) {
					mosFileObjects[i].free = 1;
					umm_free(expandedFilename);
					return i + 1;
				}
			}
		}
	}
	umm_free(expandedFilename);
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
UINT8	mos_FLSEEK(UINT8 fh, UINT32 offset) {
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

	mos_UNPACKRTC((UINT24)&t, 1);
	rtc_formatDateTime((char *)address, &t);

	return strlen((char *)address);
}

void mos_UNPACKRTC(UINT24 address, UINT8 flags) {
	if (flags & 1) {
		rtc_update();
	}
	if (address != 0) {
		rtc_unpack(&rtc, (vdp_time_t *)address);
	}
	if (flags & 2) {
		rtc_update();
	}
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
	int ret = f_mount(&fs, "", 1);		// Mount the SD card
	if (ret == FR_OK) {
		f_getcwd(cwd, sizeof(cwd)); 	// Update current working directory
	} else {
		strcpy(cwd, "No SD card present");
	}
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
	writeBuffer[1] = t.month + 1;
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

// Write the date
int writeDate(char * buffer) {
	vdp_time_t t;
	char * end = buffer;
	char * arg = NULL;
	int	day = -1;
	int mon = -1;
	int weekday = -1;
	char writeBuffer[6];

	// Write date will iterate over input buffer to extract day and month, and optional weekday
	// weekday will be discarded
	// repeated elements will trigger an error
	// string beyond the date will be ignored

	while (day == -1 || mon == -1) {
		if (extractString(end, &end, ", ", &arg, EXTRACT_FLAG_AUTO_TERMINATE) != FR_OK) {
			return FR_INVALID_PARAMETER;
		}
		if (weekday == -1) {
			weekday = rtc_dayFromName(arg);
			if (weekday != -1) {
				continue;
			}
		}
		if (mon == -1) {
			mon = rtc_monthFromName(arg);
			if (mon != -1) {
				continue;
			}
		}
		if (day == -1) {
			if (extractNumber(arg, &arg, " ,", &day, EXTRACT_FLAG_DECIMAL_ONLY | EXTRACT_FLAG_POSITIVE_ONLY)) {
				continue;
			}
		}
		// If we get here, our date string is invalid
		return FR_INVALID_PARAMETER;
	}

	rtc_update();
	rtc_unpack(&rtc, &t);

	writeBuffer[0] = t.year - EPOCH_YEAR;
	writeBuffer[1] = mon + 1;
	writeBuffer[2] = day;
	writeBuffer[3] = t.hour;
	writeBuffer[4] = t.minute;
	writeBuffer[5] = t.second;
	mos_SETRTC((UINT24)writeBuffer);

	rtc_update();
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
	writeBuffer[1] = t.month + 1;
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
	&writeDate
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

	// Default paths
	createOrUpdateSystemVariable("Moslet$Path", MOS_VAR_STRING, "/mos/");
	createOrUpdateSystemVariable("Run$Path", MOS_VAR_MACRO, "<Moslet$Path>, ./, /bin/");

	// Keyboard and console settings
	createOrUpdateSystemVariable("Keyboard", MOS_VAR_CODE, &keyboardVar);
	createOrUpdateSystemVariable("Console", MOS_VAR_CODE, &consoleVar);

	// RunRypes
	createOrUpdateSystemVariable("Alias$@RunType_obey", MOS_VAR_STRING, "Obey %*0");
	createOrUpdateSystemVariable("Alias$@RunType_exec", MOS_VAR_STRING, "Exec %*0");
	createOrUpdateSystemVariable("Alias$@RunType_bin", MOS_VAR_STRING, "RunBin %*0");
	createOrUpdateSystemVariable("Alias$@RunType_bas", MOS_VAR_STRING, "BBCBasic %*0");
	createOrUpdateSystemVariable("Alias$@RunType_bbc", MOS_VAR_STRING, "BBCBasic %*0");
	// LoadTypes
	createOrUpdateSystemVariable("Alias$@LoadType_obey", MOS_VAR_STRING, "Type %*0");
	createOrUpdateSystemVariable("Alias$@LoadType_bin", MOS_VAR_STRING, "Load %*0");
}
