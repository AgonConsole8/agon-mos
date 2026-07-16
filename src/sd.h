/*
 * Title:			AGON MOS - Low level SD card functionality
 * Author:			RJH
 * Modified By:		Dean Belfield
 * Created:			19/06/2022
 * Last Updated:	08/11/2023
 *
 * Modinfo:
 * 08/11/2023:		Removed redundant defines and function prototypes
 */

#ifndef SD_H
#define SD_H

#include <stdlib.h>

#define SD_SUCCESS	0
#define SD_ERROR	1
#define SD_LOCKED	2
#define SD_READY	0

extern int quickrand(void);

typedef struct {
	DWORD		address;
	UINT24	code;
} SD_safe_access;

//safety to throw an error if the length of code defined above is anything other than 3 bytes
typedef char assert_sd_safe_access_size[(sizeof(SD_safe_access) == 7) ? 1 : -1];

UINT24	unlockCode = 0;

BYTE	SD_readBlocks(DWORD addr, BYTE *buf, WORD count);
BYTE	SD_writeBlocks(DWORD addr, BYTE *buf, WORD count);

BYTE	SD_init();

void	SD_getUnlockCode(UINT24 * code) {
	if (code == NULL) {
		return;
	}
	while (unlockCode == 0) {
		// Generate an unlock code
		BYTE * codePtr = (BYTE *)&unlockCode;
		codePtr[0] = ((BYTE)quickrand()) ^ 0xDE;
		codePtr[1] = ((BYTE)quickrand()) ^ 0xAD;
		codePtr[2] = ((BYTE)quickrand()) ^ 0x5D;
		// unlockCode = rand() ^ 0xDEAD5D;
	}
	*code = unlockCode;
}

BYTE	SD_init_API(UINT24 * code) {
	if ((code == NULL) || (*code != unlockCode)) {
		return SD_LOCKED;
	}
	return SD_init();
}

BYTE	SD_readBlocks_API(SD_safe_access * addr_w_code, BYTE *buf, WORD count) {
	// Check that the code provided matches unlockCode
	if (addr_w_code == NULL) {
		return SD_ERROR;
	}
	if ((unlockCode == 0) || (addr_w_code->code != unlockCode)) {
		return SD_LOCKED;
	}
	// Read the blocks from the SD card
	return SD_readBlocks(addr_w_code->address, buf, count);
}

BYTE	SD_writeBlocks_API(SD_safe_access * addr_w_code, BYTE *buf, WORD count) {
	// Check that the code provided matches unlockCode to prevent accidental SDcard writes
	if (addr_w_code == NULL) {
		return SD_ERROR;
	}
	if ((unlockCode == 0) || (addr_w_code->code != unlockCode)) {
		return SD_LOCKED;
	}
	return SD_writeBlocks(addr_w_code->address, buf, count);
}

#endif SD_H
