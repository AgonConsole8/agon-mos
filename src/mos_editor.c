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
#include "umm_malloc.h"

extern volatile BYTE vpd_protocol_flags;		// In globals.asm
extern volatile BYTE keyascii;					// In globals.asm
extern volatile BYTE keycode;					// In globals.asm
extern volatile BYTE keydown;					// In globals.asm
extern volatile BYTE keycount;					// In globals.asm

extern volatile BYTE history_no;
extern volatile BYTE history_size;

extern BYTE cursorX;
extern BYTE cursorY;
extern BYTE scrcols;

// Storage for the command history
//
static char	* cmd_history[cmd_historyDepth];

char *hotkey_strings[12] = NULL; 

// Get the current cursor position from the VPD
//
void getCursorPos() {
	vpd_protocol_flags &= 0xFE;					// Clear the semaphore flag
	putch(23);									// Request the cursor position
	putch(0);
	putch(VDP_cursor);
	wait_VDP(0x01);								// Wait until the semaphore has been set, or a timeout happens
}

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
	getCursorPos();
	if(cursorX > 0) {
		putch(0x08);
	}
	else {
		while(cursorX < (scrcols - 1)) {
			putch(0x09);
			cursorX++;
		}
		putch(0x0B);
	}
}

// Move Cursor Right
// 
void doRightCursor() {
	getCursorPos();
	if(cursorX < (scrcols - 1)) {
		putch(0x09);
	}
	else {
		while(cursorX > 0) {
			putch(0x08);
			cursorX--;
		}
		putch(0x0A);
	}
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
	
	if(len < limit) {
		putch(c);
		for(i = len; i >= insertPos; i--) {
			buffer[i+1] = buffer[i];
		}
		buffer[insertPos] = c;
		for(i = insertPos + 1; i <= len; i++, count++) {
			putch(buffer[i]);
		}
		for(i = 0; i < count; i++) {
			doLeftCursor();
		}
		return 1;
	}	
	return 0;
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
		for(i = insertPos - 1; i < len; i++, count++) {
			BYTE b = buffer[i+1];
			buffer[i] = b;
			putch(b ? b : ' ');
		}
		for(i = 0; i < count; i++) {
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
		while(c == keycount);		// Wait for a key event
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
// - 1 if the hotkey was handled, otherwise 0
//
BOOL handleHotkey(UINT8 fkey, char * buffer, int bufferLength, int insertPos, int len) {
	if (hotkey_strings[fkey] != NULL) {
		char *wildcardPos = strstr(hotkey_strings[fkey], "%s");

		if (wildcardPos == NULL) { // No wildcard in the hotkey string
			removeEditLine(buffer, insertPos, len);
			strcpy(buffer, hotkey_strings[fkey]);
			printf("%s", buffer);
		} else {
			UINT8 prefixLength = wildcardPos - hotkey_strings[fkey];
			UINT8 replacementLength = strlen(buffer);
			UINT8 suffixLength = strlen(wildcardPos + 2);
			char *result;

			if (prefixLength + replacementLength + suffixLength + 1 >= bufferLength) {
				// Exceeds max command length (256 chars)
				putch(0x07); // Beep
				return 0;
			}

			result = umm_malloc(prefixLength + replacementLength + suffixLength + 1); // +1 for null terminator
			if (!result) {
				// Memory allocation failed
				return 0;
			}

			strncpy(result, hotkey_strings[fkey], prefixLength); // Copy the portion preceding the wildcard to the buffer
			result[prefixLength] = '\0'; // Terminate

			strcat(result, buffer);
			strcat(result, wildcardPos + 2);

			removeEditLine(buffer, insertPos, len);
			strcpy(buffer, result);
			printf("%s", buffer);

			umm_free(result);
		}
		return 1;
		// Key was present, so drop through to ASCII key handling
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
	BOOL clear = flags & 0x01;		// Clear the buffer on entry
	BOOL enableTab = flags & 0x02;	// Enable tab completion (default off)
	BOOL enableHotkeys = !(flags & 0x04); // Enable hotkeys (default on)
	BOOL enableHistory = !(flags & 0x08); // Enable history (default on)
	BYTE keya = 0;					// The ASCII key	
	BYTE keyc = 0;					// The FabGL keycode
	BYTE keyr = 0;					// The ASCII key to return back to the calling program

	int  limit = bufferLength - 1;	// Max # of characters that can be entered
	int	 insertPos;					// The insert position
	int  len = 0;					// Length of current input
	history_no = history_size;		// Ensure our current "history" is the end of the list

	getModeInformation();			// Get the current screen dimensions
	
	if (clear) {					// Clear the buffer as required
		buffer[0] = 0;	
		insertPos = 0;
	} else {
		printf("%s", buffer);		// Otherwise output the current buffer
		insertPos = strlen(buffer);	// And set the insertpos to the end
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

			case 0x9F: //F1
			case 0xA0: //F2
			case 0xA1: //F3
			case 0xA2: //F4
			case 0xA3: //F5
			case 0xA4: //F6
			case 0xA5: //F7
			case 0xA6: //F8
			case 0xA7: //F9
			case 0xA8: //F10
			case 0xA9: //F11	
			case 0xAA: //F12
			{
				UINT8 fkey = keyc - 0x9F;
				if (enableHotkeys && handleHotkey(fkey, buffer, bufferLength, insertPos, len)) {
					len = strlen(buffer);
					insertPos = len;
					keya = 0x0D;
					// Key was present, so drop through to ASCII key handling
				} else break; // key wasn't present, so do nothing
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
							
							case 0x09: if (enableTab) { // Tab
								char *search_term = NULL;
								char *path = NULL;

								FRESULT fr;
								DIR dj;
								FILINFO fno;
								t_mosCommand *cmd;
								const char *searchTermStart;
								const char *lastSpace = strrchr(buffer, ' ');
								const char *lastSlash = strrchr(buffer, '/');
								
								if (lastSlash == NULL && lastSpace == NULL) { //Try commands first before fatfs completion
									
									search_term = (char*) umm_malloc(strlen(buffer) + 6);
									if (!search_term) {
										// umm_malloc failed, so no tab completion for us today
										break;
									}
									
									strcpy(search_term, buffer);
									strcat(search_term, ".");
									
									cmd = mos_getCommand(search_term);
									if (cmd != NULL) { //First try internal MOS commands
										
										printf("%s ", cmd->name + strlen(buffer));
										strcat(buffer, cmd->name + strlen(buffer));
										strcat(buffer, " ");
										len = strlen(buffer);
										insertPos = strlen(buffer);										
										umm_free(search_term);										
										break;
										
									}
									
									strcpy(search_term, buffer);
									strcat(search_term, "*.bin");
									fr = f_findfirst(&dj, &fno, "/mos/", search_term);
									if (fr == FR_OK && fno.fname[0]) { //Now try MOSlets
										
										printf("%.*s ", strlen(fno.fname) - 4 - strlen(buffer), fno.fname + strlen(buffer));
										strncat(buffer, fno.fname + strlen(buffer), strlen(fno.fname) - 4 - strlen(buffer));
										strcat(buffer, " ");
										len = strlen(buffer);
										insertPos = strlen(buffer);										
										umm_free(search_term);
										break;
										
									}
									
									//Try local .bin
									fr = f_findfirst(&dj, &fno, "", search_term);
									if ((fr == FR_OK && fno.fname[0])) {
										printf("%.*s ", strlen(fno.fname) - 4 - strlen(buffer), fno.fname + strlen(buffer));
										strncat(buffer, fno.fname + strlen(buffer), strlen(fno.fname) - 4 - strlen(buffer));
										strcat(buffer, " ");
										len = strlen(buffer);
										insertPos = strlen(buffer);										
										umm_free(search_term);
										break;									
									}									
									
									//Otherwise try /bin/
									fr = f_findfirst(&dj, &fno, "/bin/", search_term);
									if ((fr == FR_OK && fno.fname[0])) {
										printf("%.*s ", strlen(fno.fname) - 4 - strlen(buffer), fno.fname + strlen(buffer));
										strncat(buffer, fno.fname + strlen(buffer), strlen(fno.fname) - 4 - strlen(buffer));
										strcat(buffer, " ");
										len = strlen(buffer);
										insertPos = strlen(buffer);										
										umm_free(search_term);
										break;									
									}
								}
								
								if (lastSlash != NULL) {
									int pathLength = 1;
																		
									if (lastSpace != NULL && lastSlash > lastSpace) {
										pathLength = lastSlash - lastSpace; // Path starts after the last space and includes the slash
									}
									if (lastSpace == NULL) {
										lastSpace = buffer;
										pathLength = lastSlash - lastSpace;
									}

									path = (char*) umm_malloc(pathLength + 1); // +1 for null terminator
									if (path == NULL) {
										break;
									}
									strncpy(path, lastSpace + 1, pathLength); // Start after the last space
									path[pathLength] = '\0'; // Null-terminate the string

									// Determine the start of the search term
									searchTermStart = lastSlash + 1;
									if (lastSpace != NULL && lastSpace > lastSlash) {
										searchTermStart = lastSpace + 1;
									}
									search_term = (char*) umm_malloc(strlen(searchTermStart) + 2); // +2 for '*' and null terminator
								} else {
									path = (char*) umm_malloc(1);
									if (path == NULL) {
										break;
									}
									path[0] = '\0'; // Path is empty (current dir, essentially).

									searchTermStart = lastSpace ? lastSpace + 1 : buffer;
									search_term = (char*) umm_malloc(strlen(searchTermStart) + 2); // +2 for '*' and null terminator
								}

								if (search_term == NULL) {
									if (path) umm_free(path);
									break;
								}

								strcpy(search_term, lastSpace && lastSlash > lastSpace ? lastSlash + 1 : lastSpace ? lastSpace + 1 : buffer);
								strcat(search_term, "*");
								
								//printf("Path:\"%s\" Pattern:\"%s\"\r\n", path, search_term);
								fr = f_findfirst(&dj, &fno, path, search_term);
								
								if (fr == FR_OK && fno.fname[0]) {
									if (fno.fattrib & AM_DIR) printf("%s/", fno.fname + strlen(search_term) - 1);
									else printf("%s", fno.fname + strlen(search_term) - 1);

									strcat(buffer, fno.fname + strlen(search_term) - 1);
									if (fno.fattrib & AM_DIR) strcat(buffer, "/");

									len = strlen(buffer);
									insertPos = strlen(buffer);
								}

								// Free the allocated memory
								if (search_term) umm_free(search_term);
								if (path) umm_free(path);
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
