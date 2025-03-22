/*
 * Title:			AGON MOS - MOS line editor
 * Author:			Dean Belfield
 * Created:			18/09/2022
 * Last Updated:	31/03/2023
 *
 * Modinfo:
 * 28/09/2022:		Added clear parameter to mos_EDITLINE
 * 20/02/2023:		Fixed mos_EDITLINE to handle the full CP-1252 character set
 * 09/03/2023:		Added support for virtual keys; improved editing functionality
 * 14/03/2023:		Tweaks ready for command history
 * 21/03/2023:		Improved backspace, and editing of long lines, after scroll, at bottom of screen
 * 22/03/2023:		Added a single-entry command line history
 * 31/03/2023:		Added timeout for VDP protocol
 */

#include <eZ80.h>
#include <defines.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "defines.h"
#include "mos.h"
#include "uart.h"
#include "timer.h"
#include "mos_editor.h"
#include "mos_file.h"
#include "umm_malloc.h"

extern volatile BYTE vpd_protocol_flags;		// In globals.asm
extern volatile BYTE keyascii;					// In globals.asm
extern volatile BYTE keycode;					// In globals.asm
extern volatile BYTE keydown;					// In globals.asm
extern volatile BYTE keycount;					// In globals.asm

extern volatile BYTE history_no;
extern volatile BYTE history_size;

extern BYTE scrcols;

// Storage for the command history
//
static char	* cmd_history[cmd_historyDepth];

// Get the current screen dimensions from the VDU
//
void getModeInformation() {
	vpd_protocol_flags &= 0xEF;					// Clear the semaphore flag
	putch(23);
	putch(0);
	putch(VDP_mode);
	wait_VDP(0x10);								// Wait until the semaphore has been set, or a timeout happens
}

// Get palette entry
//
void readPalette(BYTE entry, BOOL wait) {
	vpd_protocol_flags &= 0xFB;					// Clear the semaphore flag
	putch(23);
	putch(0);
	putch(VDP_palette);
	putch(entry);
	if (wait) {
		wait_VDP(0x04);							// Wait until the semaphore has been set, or a timeout happens
	}
}

// Move cursor left
//
void doLeftCursor() {
	putch(0x08);
}

// Move Cursor Right
//
void doRightCursor() {
	putch(0x09);
}

// Insert a character in the input string
// Parameters:
// - buffer: Pointer to the line edit buffer
// - c: Character to insert
// - insertPos: Position in the input string to insert the character
// - len: Length of the input string (before the character is inserted)
// - limit: Max number of characters to insert
// Returns:
// - true if the character was inserted, otherwise false
//
BOOL insertCharacter(char *buffer, char c, int insertPos, int len, int limit) {
	int	i;
	int count = 0;

	if (len < limit) {
		putch(c);
		for (i = len; i >= insertPos; i--) {
			buffer[i+1] = buffer[i];
		}
		buffer[insertPos] = c;
		for (i = insertPos + 1; i <= len; i++, count++) {
			putch(buffer[i]);
		}
		for (i = 0; i < count; i++) {
			doLeftCursor();
		}
		return 1;
	}
	return 0;
}

BOOL insertString(char * buffer, char * source, int sourceLen, int sourceOffset, int insertPos, int len, int limit, char addedChar) {
	int i;
	source += sourceOffset;
	sourceLen -= sourceOffset;
	if (len + sourceLen > limit) {
		return false;
	}
	if (addedChar != '\0') {
		sourceLen++;
	}

	// Move buffer contents to allow for new string
	for (i = len; i >= insertPos; i--) {
		buffer[i + sourceLen] = buffer[i];
	}
	strncpy(buffer + insertPos, source, sourceLen - 1);
	if (addedChar != '\0') {
		buffer[insertPos + sourceLen - 1] = addedChar;
	}

	// Overwrite what's on-screen with what we are inserting
	printf("%s", buffer + insertPos);
	return true;
}

// Remove a character from the input string
// Parameters:
// - buffer: Pointer to the line edit buffer
// - insertPos: Position in the input string of the character to be deleted
// - len: Length of the input string before the character is deleted
// Returns:
// - true if the character was deleted, otherwise false
//
BOOL deleteCharacter(char *buffer, int insertPos, int len) {
	int	i;
	int count = 0;
	if (insertPos > 0) {
		doLeftCursor();
		for (i = insertPos - 1; i < len; i++, count++) {
			BYTE b = buffer[i+1];
			buffer[i] = b;
			putch(b ? b : ' ');
		}
		for (i = 0; i < count; i++) {
			doLeftCursor();
		}
		return 1;
	}
	return 0;
}

// Wait for a key to be pressed
//
void waitKey() {
	BYTE	c;
	do {
		c = keycount;
		while (c == keycount);		// Wait for a key event
	} while (keydown == 0);			// Loop until we get a key down value (keydown = 1)
}

// handle HOME
//
int gotoEditLineStart(int insertPos) {
	while (insertPos > 0) {
		doLeftCursor();
		insertPos--;
	}
	return insertPos;
}

// handle END
//
int gotoEditLineEnd(int insertPos, int len) {
	while (insertPos < len) {
		doRightCursor();
		insertPos++;
	}
	return insertPos;
}

// remove current edit line
//
void removeEditLine(char * buffer, int insertPos, int len) {
	// goto start of line
	insertPos = gotoEditLineStart(insertPos);
	// set buffer to be spaces up to len
	memset(buffer, ' ', len);
	// print the buffer to erase old line from screen
	printf("%s", buffer);
	// clear the buffer
	buffer[0] = 0;
	gotoEditLineStart(len);
}

// Handle hotkey, if defined
// Returns:
// - 0 if the hotkey doesn't exist
// - 1 if the hotkey was handled
// - 2 if the hotkey was handled and we should auto-return
//
BYTE handleHotkey(UINT8 fkey, char * buffer, int bufferLength, int insertPos, int len) {
	char label[10];
	t_mosSystemVariable *hotkeyVar = NULL;
	BOOL autoReturn = false;
	char * srcPtr;
	char * srcEnd;
	char * destPtr;
	char * destEnd;

	sprintf(label, "Hotkey$%d", fkey + 1);
	if (getSystemVariable(label, &hotkeyVar) == 0) {
		char * substitutedString = NULL;
		char * hotkeyString = expandVariable(hotkeyVar, false);
		if (!hotkeyString) {
			// Variable couldn't be read for some reason
			return 0;
		}

		substitutedString = substituteArguments(hotkeyString, buffer, true);
		umm_free(hotkeyString);
		if (!substitutedString) {
			return 0;
		}
		if (strlen(substitutedString) > bufferLength) {
			// Exceeds max length
			umm_free(substitutedString);
			putch(0x07); // Beep
			return 0;
		}

		removeEditLine(buffer, insertPos, len);
		// Copy in the new string, ignoring any control characters, up to buffer length or first CR
		srcPtr = substitutedString;
		srcEnd = srcPtr + strlen(substitutedString);
		destPtr = buffer;
		destEnd = buffer + bufferLength - 1;
		while (srcPtr < srcEnd && destPtr < destEnd) {
			if (*srcPtr == 0x0D) {
				autoReturn = true;
				break;
			}
			if (*srcPtr >= 0x20 && *srcPtr != 0x7F) {
				*destPtr++ = *srcPtr;
			}
			srcPtr++;
		}
		*destPtr = 0;

		// strcpy(buffer, substitutedString);
		printf("%s", buffer);
		umm_free(substitutedString);
		return autoReturn ? 2 : 1;
	}
	return 0;
}

// The main line edit function
// Parameters:
// - buffer: Pointer to the line edit buffer
// - bufferLength: Size of the buffer in bytes
// - flags: Set bit0 to 0 to not clear, 1 to clear on entry
// Returns:
// - The exit key pressed (ESC or CR)
//
UINT24 mos_EDITLINE(char * buffer, int bufferLength, UINT8 flags) {
	BOOL	clear = flags & 0x01;		// Clear the buffer on entry
	BOOL	enableTab = flags & 0x02;	// Enable tab completion (default off)
	BOOL	enableHotkeys = !(flags & 0x04); // Enable hotkeys (default on)
	BOOL	enableHistory = !(flags & 0x08); // Enable history (default on)
	BYTE	keya = 0;					// The ASCII key
	BYTE	keyc = 0;					// The FabGL keycode
	BYTE	keyr = 0;					// The ASCII key to return back to the calling program
	char	*path = NULL;				// used for tab completion
	int		limit = bufferLength - 1;	// Max # of characters that can be entered
	int		insertPos;					// The insert position
	int		len = 0;					// Length of current input

	history_no = history_size;			// Ensure our current "history" is the end of the list
	getModeInformation();				// Get the current screen dimensions

	if (clear) {						// Clear the buffer as required
		buffer[0] = 0;
		insertPos = 0;
	} else {
		printf("%s", buffer);			// Otherwise output the current buffer
		insertPos = strlen(buffer);		// And set the insertpos to the end
	}

	if (enableTab) {
		path = umm_malloc(bufferLength);
		if (!path) enableTab = false;
	}

	// Loop until an exit key is pressed
	//
	while (keyr == 0) {
		BYTE historyAction = 0;
		len = strlen(buffer);
		waitKey();
		keya = keyascii;
		keyc = keycode;
		switch (keyc) {
			//
			// First any extended (non-ASCII keys)
			//
			case 0x85: {	// HOME
				insertPos = gotoEditLineStart(insertPos);
			} break;
			case 0x87: {	// END
				insertPos = gotoEditLineEnd(insertPos, len);
			} break;

			case 0x92: {	// PgUp
				historyAction = 2;
			} break;

			case 0x94: {	// PgDn
				historyAction = 3;
			} break;

			case 0x9F: // F1
			case 0xA0: // F2
			case 0xA1: // F3
			case 0xA2: // F4
			case 0xA3: // F5
			case 0xA4: // F6
			case 0xA5: // F7
			case 0xA6: // F8
			case 0xA7: // F9
			case 0xA8: // F10
			case 0xA9: // F11
			case 0xAA: // F12
			{
				if (enableHotkeys) {
					BYTE handled = handleHotkey(keyc - 0x9F, buffer, bufferLength, insertPos, len);
					if (handled != 0) {
						len = strlen(buffer);
						insertPos = len;
						if (handled == 2) {
							keya = 0x0D;	// auto-return for inserted hotkey
						} else {
							// no auto-return so we can skip the rest of the loop
							break;
						}
						// Key was present, so drop through to ASCII key handling
					} else break; // key not defined/handled, so do nothing
				} else break; // function keys disabled, so do nothing
			}

			//
			// Now the ASCII keys
			//
			default: {
				if (keya > 0) {
					if (keya >= 0x20 && keya != 0x7F) {
						if (insertCharacter(buffer, keya, insertPos, len, limit)) {
							insertPos++;
						}
					} else {
						switch (keya) {
							case 0x0D:		// Enter
								historyAction = 1;
								// fall through to...
							case 0x1B:	{	// Escape
								keyr = keya;
							} break;
							case 0x08:	{	// Cursor Left
								if (insertPos > 0) {
									doLeftCursor();
									insertPos--;
								}
							} break;
							case 0x15:	{	// Cursor Right
								if (insertPos < len) {
									doRightCursor();
									insertPos++;
								}
							} break;
							case 0x0A: {	// Cursor Down
								// if we've got a line longer than our screen width, and our insertion pos can go down a line, then move cursor
								if (len > scrcols && (insertPos + scrcols) < len) {
									putch(0x0A);
									insertPos += scrcols;
								} else if (insertPos < len) {
									// otherwise if our insertion pos < len then move to end of line
									insertPos = gotoEditLineEnd(insertPos, len);
								} else {
									// otherwise do history thing
									historyAction = 3;
								}
							} break;
							case 0x0B:	{	// Cursor Up
								// if we've got a line longer than our screen width, and our insertion pos can go up a line, then move cursor
								if (len > scrcols && (insertPos - scrcols) > 0) {
									putch(0x0B);
									insertPos -= scrcols;
								} else if (insertPos > 0) {
									// otherwise if our insertion pos > 0 then move to start of line
									insertPos = gotoEditLineStart(insertPos);
								} else {
									historyAction = 2;
								}
							} break;

							case 0x09: if (enableTab) { // Tab completion
								FRESULT fr;
								char *searchTerm = NULL;
								const char *termStart = buffer + insertPos;
								int termLength = 0;
								int resolveLength;

								// With tab-completion we are completing a "term" in the buffer
								// start is last space before the insert position, end is the insert position
								while (termStart > buffer && *(termStart - 1) != ' ') {
									termStart--;
								}
								termLength = buffer + insertPos - termStart;

								if (termStart == buffer + mos_strspn(buffer, "* ") && termLength == 0) {
									// don't attempt to complete a zero-length command
									putch(0x07); // Beep
									break;
								}

								// if we're at the start of the buffer, then we're looking for a command, or executable
								// TODO consider skipping auto-complete for commands if there's no term yet
								if (
									termStart == buffer + mos_strspn(buffer, "* ") &&
									memchr(termStart, '/', termLength) == NULL
								) {
									t_mosSystemVariable *var = NULL;
									t_mosCommand *cmd;
									bool matched = false;
									bool success = false;

									searchTerm = (char*) umm_malloc(termLength + 10);
									if (!searchTerm) {
										// umm_malloc failed, so no tab completion for us today
										break;
									}

									sprintf(searchTerm, "Alias$%.*s*", termLength, termStart);
									if (getSystemVariable(searchTerm, &var) == 0) {
										// Matching alias found
										matched = true;
										success = insertString(buffer, var->label + 6, strlen(var->label + 6), termLength, insertPos, len, limit, ' ');
									}

									if (!matched) {
										// Internal command?
										sprintf(searchTerm, "%.*s.", termLength, termStart);
										cmd = mos_getCommand(searchTerm, MATCH_COMMANDS_AUTO);
										if (cmd != NULL) {
											// Matching command found
											matched = true;
											success = insertString(buffer, cmd->name, strlen(cmd->name), termLength, insertPos, len, limit, ' ');
										}
									}

									if (!matched) {
										// Find command in runpath, or given path
										// TODO think more on this `:` detection once we support runtypes
										if (memchr(termStart, ':' , termLength) != NULL) {
											sprintf(searchTerm, "%.*s*.bin", termLength, termStart);
										} else {
											sprintf(searchTerm, "run:%.*s*.bin", termLength, termStart);
										}
										resolveLength = bufferLength;
										fr = resolvePath(searchTerm, path, &resolveLength, NULL, NULL);
										if (fr == FR_OK) {
											char * sourceLeaf = getFilepathLeafname(searchTerm);
											int sourceOffset = sourceLeaf - searchTerm;
											char * leafname = getFilepathLeafname(path);
											matched = true;
											success = insertString(buffer, leafname, strlen(leafname) - 4, strlen(sourceLeaf) - 5, insertPos, len, limit, isDirectory(path) ? '/' : ' ');
										}
									}
									umm_free(searchTerm);
									if (success) {
										len = strlen(buffer);
										insertPos = len;
									}
									if (matched) {
										break;
									}
								}

								// if not at start of buffer, then we're doing filename completion
								searchTerm = (char*) umm_malloc(termLength + 2);
								if (!searchTerm) {
									// umm_malloc failed, so no tab completion for us today
									break;
								}

								sprintf(searchTerm, "%.*s*", termLength, termStart);
								resolveLength = bufferLength;
								fr = resolvePath(searchTerm, path, &resolveLength, NULL, NULL);
								if (fr == FR_OK) {
									char * sourceLeaf = getFilepathLeafname(searchTerm);
									int sourceOffset = sourceLeaf - searchTerm;
									char * leafname = getFilepathLeafname(path);
									if (insertString(buffer, leafname, strlen(leafname), strlen(sourceLeaf) - 1, insertPos, len, limit, isDirectory(path) ? '/' : ' ')) {
										len = strlen(buffer);
										insertPos = len;
									}
								} else {
									putch(0x07); // Beep
								}

								umm_free(searchTerm);
							}
							break;

							case 0x7F: {	// Backspace
								if (deleteCharacter(buffer, insertPos, len)) {
									insertPos--;
								}
							} break;
						}
					}
				}
			}
		}

		if (enableHistory) {
			BOOL lineChanged = FALSE;
			switch (historyAction) {
				case 1: { // Push new item to stack
					editHistoryPush(buffer);
				} break;
				case 2: { // Move up in history
					lineChanged = editHistoryUp(buffer, insertPos, len, limit);
				} break;
				case 3: { // Move down in history
					lineChanged = editHistoryDown(buffer, insertPos, len, limit);
				} break;
			}

			if (lineChanged) {
				printf("%s", buffer);							// Output the buffer
				insertPos = strlen(buffer);						// Set cursor to end of string
				len = strlen(buffer);
			}
		}
	}
	len -= insertPos;				// Now just need to cursor to end of line; get # of characters to cursor

	while (len >= scrcols) {		// First cursor down if possible
		putch(0x0A);
		len -= scrcols;
	}
	while (len-- > 0) putch(0x09);	// Then cursor right for the remainder

	if (enableTab) umm_free(path);
	return keyr;					// Finally return the keycode
}

void editHistoryInit() {
	int i;
	history_no = 0;
	history_size = 0;

	for (i = 0; i < cmd_historyDepth; i++) {
		cmd_history[i] = NULL;
	}
}

void editHistoryPush(char *buffer) {
	int len = strlen(buffer);

	if (len > 0) {		// If there is data in the buffer
		char * newEntry = NULL;

		// if the new entry is the same as the last entry, then don't save it
		if (history_size > 0 && strcmp(buffer, cmd_history[history_size - 1]) == 0) {
			return;
		}

		newEntry = umm_malloc(len + 1);
		if (newEntry == NULL) {
			// Memory allocation failed so we can't save history
			return;
		}
		strcpy(newEntry, buffer);

		// If we're at the end of the history, then we need to shift all our entries up by one
		if (history_size == cmd_historyDepth) {
			int i;
			umm_free(cmd_history[0]);
			for (i = 1; i < history_size; i++) {
				cmd_history[i - 1] = cmd_history[i];
			}
			history_size--;
		}
		cmd_history[history_size++] = newEntry;
	}
}

BOOL editHistoryUp(char *buffer, int insertPos, int len, int limit) {
	int index = -1;
	if (history_no > 0) {
		index = history_no - 1;
	} else if (history_size > 0) {
		// we're at the top of our history list
		// replace current line (which may have been edited) with first entry
		index = 0;
	}
	return editHistorySet(buffer, insertPos, len, limit, index);
}

BOOL editHistoryDown(char *buffer, int insertPos, int len, int limit) {
	if (history_no < history_size) {
		if (history_no == history_size - 1) {
			// already at most recent entry - just leave an empty line
			removeEditLine(buffer, insertPos, len);
			history_no = history_size;
			return TRUE;
		}
		return editHistorySet(buffer, insertPos, len, limit, ++history_no);
	}
	return FALSE;
}

BOOL editHistorySet(char *buffer, int insertPos, int len, int limit, int index) {
	if (index >= 0 && index < history_size) {
		removeEditLine(buffer, insertPos, len);
		if (strlen(cmd_history[index]) > limit) {
			// if the history entry is longer than the buffer, then we need to truncate it
			strncpy(buffer, cmd_history[index], limit);
			buffer[limit] = '\0';
		} else {
			strcpy(buffer, cmd_history[index]);			// Copy from the history to the buffer
		}
		history_no = index;
		return TRUE;
	}
	return FALSE;
}
