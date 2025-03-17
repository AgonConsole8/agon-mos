;
; Title:	AGON MOS - API code
; Author:	Dean Belfield
; Created:	24/07/2022
; Last Updated:	10/11/2023
;
; Modinfo:
; 03/08/2022:	Added a handful of MOS API calls and stubbed FatFS calls
; 05/08/2022:	Added mos_FEOF, saved affected registers in fopen, fclose, fgetc, fputc and feof
; 09/08/2022:	mos_api_sysvars now returns pointer to _sysvars
; 05/09/2022:	Added mos_REN
; 24/09/2022:	Error codes returned for MOS commands
; 13/10/2022:	Added mos_OSCLI and supporting code
; 20/10/2022:	Tweaked error handling
; 13/03/2023:	Renamed keycode to keyascii, fixed mos_api_getkey, added parameter to mos_api_dir
; 15/03/2023:	Added mos_api_copy, mos_api_getrtc, mos_api_setrtc
; 21/03/2023:	Added mos_api_setintvector
; 24/03/2023:	Fixed bugs in mos_api_setintvector
; 28/03/2023:	Function mos_api_setintvector now only accepts a 24-bit pointer
; 29/03/2023:	Added mos_api_uopen, mos_api_uclose, mos_api_ugetc, mos_api_uputc
; 14/04/2023:	Added ffs_api_fopen, ffs_api_fclose, ffs_api_stat, ffs_api_fread, ffs_api_fwrite, ffs_api_feof, ffs_api_flseek
; 15/04/2023:	Added mos_api_getfil, mos_api_fread, mos_api_fwrite and mos_api_flseek
; 30/05/2023:	Fixed mos_api_fgetc to set carry if at end of file
; 03/08/2023:	Added mos_api_setkbvector
; 10/08/2023:	Added mos_api_getkbmap
; 10/11/2023:	Added mos_api_i2c_close, mos_api_i2c_open, mos_api_i2c_read, mos_api_i2c_write


			.ASSUME	ADL = 1

			DEFINE .STARTUP, SPACE = ROM
			SEGMENT .STARTUP

			XDEF	mos_api

			XREF	SWITCH_A		; In misc.asm
			XREF	SET_AHL24
			XREF	GET_AHL24
			XREF	SET_ADE24
			XREF	SET_ABC24
			XREF	SET_AIX24

			XREF	_mos_OSCLI		; In mos.c
			XREF	_mos_EDITLINE
			XREF	_mos_LOAD_API
			XREF	_mos_SAVE_API
			XREF	_mos_CD_API
			XREF	_mos_DIR_API
			XREF	_mos_DEL
			XREF	_mos_REN_API
			XREF	_mos_FOPEN
			XREF	_mos_FCLOSE
			XREF	_mos_FGETC
			XREF	_mos_FPUTC
			XREF	_mos_FEOF
			XREF	_mos_GETERROR
			XREF	_mos_MKDIR_API
			XREF	_mos_COPY_API
			XREF	_mos_GETRTC
			XREF	_mos_UNPACKRTC
			XREF	_mos_SETRTC
			XREF	_mos_SETINTVECTOR
			XREF	_mos_GETFIL
			XREF	_mos_FREAD
			XREF	_mos_FWRITE
			XREF	_mos_FLSEEK
			XREF	_mos_I2C_OPEN
			XREF	_mos_I2C_CLOSE
			XREF	_mos_I2C_WRITE
			XREF	_mos_I2C_READ

			XREF	_fat_EOF		; In mos.c

			XREF	_open_UART1		; In uart.c
			XREF	_close_UART1

			XREF	UART1_serial_GETCH	; In serial.asm
			XREF	UART1_serial_PUTCH

			XREF	_keyascii		; In globals.asm
			XREF	_keycount
			XREF	_keydown
			XREF	_sysvars
			XREF	_scratchpad
			XREF	_vpd_protocol_flags
			XREF	_user_kbvector
			XREF	_keymap

			XREF	_f_open			; In ff.c
			XREF	_f_close
			XREF	_f_read
			XREF	_f_write
			XREF	_f_stat
			XREF	_f_lseek
			XREF	_f_truncate
			XREF	_f_opendir
			XREF	_f_closedir
			XREF	_f_readdir
			XREF	_f_getcwd

			XREF	_pmatch			; In strings.c

			XREF	_getArgument		; In mos_sysvars.c
			XREF	_extractString
			XREF	_extractNumber
			XREF	_escapeString
			XREF	_setVarVal
			XREF	_readVarVal
			XREF	_gsInit
			XREF	_gsRead
			XREF	_gsTrans
			XREF	_substituteArgs
			; XREF	_evaluateExpression

			XREF	_resolvePath		; In mos_file.c
			XREF	_getDirectoryForPath
			XREF	_getFilepathLeafname
			XREF	_isDirectory
			XREF	_resolveRelativePath

; Call a MOS API function
; 00h - 7Fh: Reserved for high level MOS calls
; 80h - FFh: Reserved for low level calls to FatFS
;  A: function to call
;
mos_api:		CP	80h			; Check if it is a FatFS command
			JR	NC, $F			; Yes, so jump to next block
			CP	mos_api_block1_size	; Check if out of bounds
			JP	NC, mos_api_not_implemented
			CALL	SWITCH_A		; Switch on this table
;
mos_api_block1_start:	DW	mos_api_getkey		; 0x00
			DW	mos_api_load		; 0x01
			DW	mos_api_save		; 0x02
			DW	mos_api_cd		; 0x03
			DW	mos_api_dir		; 0x04
			DW	mos_api_del		; 0x05
			DW	mos_api_ren		; 0x06
			DW	mos_api_mkdir		; 0x07
			DW	mos_api_sysvars		; 0x08
			DW	mos_api_editline	; 0x09
			DW	mos_api_fopen		; 0x0A
			DW	mos_api_fclose		; 0x0B
			DW	mos_api_fgetc		; 0x0C
			DW	mos_api_fputc		; 0x0D
			DW	mos_api_feof		; 0x0E
			DW	mos_api_getError	; 0x0F
			DW	mos_api_oscli		; 0x10
			DW	mos_api_copy		; 0x11
			DW	mos_api_getrtc		; 0x12
			DW	mos_api_setrtc		; 0x13
			DW	mos_api_setintvector	; 0x14
			DW	mos_api_uopen		; 0x15
			DW 	mos_api_uclose		; 0x16
			DW	mos_api_ugetc		; 0x17
			DW	mos_api_uputc		; 0x18
			DW	mos_api_getfil		; 0x19
			DW	mos_api_fread		; 0x1A
			DW	mos_api_fwrite		; 0x1B
			DW	mos_api_flseek		; 0x1C
			DW	mos_api_setkbvector	; 0x1D
			DW	mos_api_getkbmap	; 0x1E
			DW	mos_api_i2c_open	; 0x1F
			DW	mos_api_i2c_close	; 0x20
			DW	mos_api_i2c_write	; 0x21
			DW	mos_api_i2c_read	; 0x22
			DW	mos_api_unpackrtc	; 0x23

			DW	mos_api_not_implemented	; 0x24
			DW	mos_api_not_implemented	; 0x25
			DW	mos_api_not_implemented	; 0x26
			DW	mos_api_not_implemented	; 0x27

			DW	mos_api_pmatch		; 0x28
			DW	mos_api_getargument	; 0x29
			DW	mos_api_extractstring	; 0x2a
			DW	mos_api_extractnumber	; 0x2b
			DW	mos_api_escapestring	; 0x2c
			DW	mos_api_not_implemented	; 0x2d
			DW	mos_api_not_implemented	; 0x2e
			DW	mos_api_not_implemented	; 0x2f

			DW	mos_api_setvarval	; 0x30
			DW	mos_api_readvarval	; 0x31
			DW	mos_api_gsinit		; 0x32
			DW	mos_api_gsread		; 0x33
			DW	mos_api_gstrans		; 0x34
			DW	mos_api_substituteargs	; 0x35
			DW	mos_api_not_implemented	; 0x36   reserved for mos_api_evaluateexpression
			DW	mos_api_not_implemented	; 0x37   reserved for something else :)
			DW	mos_api_resolvepath	; 0x38
			DW	mos_api_getdirectoryforpath	; 0x39
			DW	mos_api_getfilepathleafname	; 0x3a
			DW	mos_api_isdirectory	; 0x3b
			DW	mos_api_getabsolutepath	; 0x3c
			DW	mos_api_not_implemented	; 0x3d
			DW	mos_api_not_implemented	; 0x3e
			DW	mos_api_not_implemented	; 0x3f

			DW	mos_api_not_implemented	; 0x40
			DW	mos_api_not_implemented	; 0x41
			DW	mos_api_not_implemented	; 0x42
			DW	mos_api_not_implemented	; 0x43
			DW	mos_api_not_implemented	; 0x44
			DW	mos_api_not_implemented	; 0x45
			DW	mos_api_not_implemented	; 0x46
			DW	mos_api_not_implemented	; 0x47
			DW	mos_api_not_implemented	; 0x48
			DW	mos_api_not_implemented	; 0x49
			DW	mos_api_not_implemented	; 0x4a
			DW	mos_api_not_implemented	; 0x4b
			DW	mos_api_not_implemented	; 0x4c
			DW	mos_api_not_implemented	; 0x4d
			DW	mos_api_not_implemented	; 0x4e
			DW	mos_api_not_implemented	; 0x4f

			DW	mos_api_not_implemented	; 0x50
			DW	mos_api_not_implemented	; 0x51
			DW	mos_api_not_implemented	; 0x52
			DW	mos_api_not_implemented	; 0x53
			DW	mos_api_not_implemented	; 0x54
			DW	mos_api_not_implemented	; 0x55
			DW	mos_api_not_implemented	; 0x56
			DW	mos_api_not_implemented	; 0x57
			DW	mos_api_not_implemented	; 0x58
			DW	mos_api_not_implemented	; 0x59
			DW	mos_api_not_implemented	; 0x5a
			DW	mos_api_not_implemented	; 0x5b
			DW	mos_api_not_implemented	; 0x5c
			DW	mos_api_not_implemented	; 0x5d
			DW	mos_api_not_implemented	; 0x5e
			DW	mos_api_not_implemented	; 0x5f

			DW	mos_api_not_implemented	; 0x60
			DW	mos_api_not_implemented	; 0x61
			DW	mos_api_not_implemented	; 0x62
			DW	mos_api_not_implemented	; 0x63
			DW	mos_api_not_implemented	; 0x64
			DW	mos_api_not_implemented	; 0x65
			DW	mos_api_not_implemented	; 0x66
			DW	mos_api_not_implemented	; 0x67
			DW	mos_api_not_implemented	; 0x68
			DW	mos_api_not_implemented	; 0x69
			DW	mos_api_not_implemented	; 0x6a
			DW	mos_api_not_implemented	; 0x6b
			DW	mos_api_not_implemented	; 0x6c
			DW	mos_api_not_implemented	; 0x6d
			DW	mos_api_not_implemented	; 0x6e
			DW	mos_api_not_implemented	; 0x6f

			DW	mos_api_not_implemented	; 0x70
			DW	mos_api_not_implemented	; 0x71
			DW	mos_api_not_implemented	; 0x72
			DW	mos_api_not_implemented	; 0x73
			DW	mos_api_not_implemented	; 0x74
			DW	mos_api_not_implemented	; 0x75
			DW	mos_api_not_implemented	; 0x76
			DW	mos_api_not_implemented	; 0x77
			DW	mos_api_not_implemented	; 0x78
			DW	mos_api_not_implemented	; 0x79
			DW	mos_api_not_implemented	; 0x7a
			DW	mos_api_not_implemented	; 0x7b
			DW	mos_api_not_implemented	; 0x7c
			DW	mos_api_not_implemented	; 0x7d
			DW	mos_api_not_implemented	; 0x7e
			DW	mos_api_not_implemented	; 0x7f

mos_api_block1_size:	EQU 	($ - mos_api_block1_start) / 2
;
$$:			AND	7Fh			; Else remove the top bit
			CP	mos_api_block2_size	; Check if out of bounds
			JP	NC, mos_api_not_implemented
			CALL	SWITCH_A		; And switch on this table

mos_api_block2_start:	DW	ffs_api_fopen		; 0x80
			DW	ffs_api_fclose		; 0x81
			DW	ffs_api_fread		; 0x82
			DW	ffs_api_fwrite		; 0x83
			DW	ffs_api_flseek		; 0x84
			DW	ffs_api_ftruncate	; 0x85
			DW	ffs_api_fsync		; 0x86
			DW	ffs_api_fforward	; 0x87
			DW	ffs_api_fexpand		; 0x88
			DW	ffs_api_fgets		; 0x89
			DW	ffs_api_fputc		; 0x8A
			DW	ffs_api_fputs		; 0x8B
			DW	ffs_api_fprintf		; 0x8C
			DW	ffs_api_ftell		; 0x8D
			DW	ffs_api_feof		; 0x8E
			DW	ffs_api_fsize		; 0x8F
			DW	ffs_api_ferror		; 0x90
			DW	ffs_api_dopen		; 0x91
			DW	ffs_api_dclose		; 0x92
			DW	ffs_api_dread		; 0x93
			DW	ffs_api_dfindfirst	; 0x94
			DW	ffs_api_dfindnext	; 0x95
			DW	ffs_api_stat		; 0x96
			DW	ffs_api_unlink		; 0x97
			DW	ffs_api_rename		; 0x98
			DW	ffs_api_chmod		; 0x99
			DW	ffs_api_utime		; 0x9A
			DW	ffs_api_mkdir		; 0x9B
			DW	ffs_api_chdir		; 0x9C
			DW	ffs_api_chdrive		; 0x9D
			DW	ffs_api_getcwd		; 0x9E
			DW	ffs_api_mount		; 0x9F
			DW	ffs_api_mkfs		; 0xA0
			DW	ffs_api_fdisk		; 0xA1
			DW	ffs_api_getfree		; 0xA2
			DW	ffs_api_getlabel	; 0xA3
			DW	ffs_api_setlabel	; 0xA4
			DW	ffs_api_setcp		; 0xA5

mos_api_block2_size:	EQU 	($ - mos_api_block2_start) / 2

mos_api_not_implemented:
			LD	HL, 23			; MOS_NOT_IMPLEMENTED
			LD	A, 23			; MOS_NOT_IMPLEMENTED
			RET

; Get keycode
; Returns:
;  A: ASCII code of key pressed, or 0 if no key pressed
;
mos_api_getkey:		PUSH	HL
			LD	HL, _keycount
mos_api_getkey_1:	LD	A, (HL)			; Wait for a key to be pressed
$$:			CP	(HL)
			JR	Z, $B
			LD	A, (_keydown)		; Check if key is down
			OR	A
			JR	Z, mos_api_getkey_1	; No, so loop
			POP	HL
			LD	A, (_keyascii)		; Get the key code
			RET

; Load an area of memory from a file.
; HLU: Address of filename (zero terminated)
; DEU: Address at which to load
; BCU: Maximum allowed size (bytes)
; Returns:
; - A: File error, or 0 if OK
; - F: Carry reset indicates no room for file.
;
mos_api_load:		LD	A, MB		; Check if MBASE is 0
			OR	A, A
			JR	Z, $F		; If it is, we can assume HL and DE are 24 bit
;
; Now we need to mod HLU and DEU to include the MBASE in the U byte
;
			CALL	SET_AHL24
			CALL	SET_ADE24
;
; Finally, we can do the load
;
$$:			PUSH	BC		; UINT24   size
			PUSH	DE		; UNIT24   address
			PUSH	HL		; char   * filename
			CALL	_mos_LOAD_API	; Call the C function mos_LOAD_API
			LD	A, L		; Return value in HLU, put in A
			POP	HL
			POP	DE
			POP	BC
			SCF			; Flag as successful
			RET

; Save a file to the SD card from RAM
; HLU: Address of filename (zero terminated)
; DEU: Address to save from
; BCU: Number of bytes to save
; Returns:
; - A: File error, or 0 if OK
; - F: Carry reset indicates no room for file
;
mos_api_save:		LD	A, MB		; Check if MBASE is 0
			OR	A, A
			JR	Z, $F		; If it is, we can assume HL and DE are 24 bit
;
; Now we need to mod HLU and DEU to include the MBASE in the U byte
;
			CALL	SET_AHL24
			CALL	SET_ADE24
;
; Finally, we can do the save
;
$$:			PUSH	BC		; UINT24   size
			PUSH	DE		; UNIT24   address
			PUSH	HL		; char   * filename
			CALL	_mos_SAVE_API	; Call the C function mos_SAVE_API
			LD	A, L		; Return value in HLU, put in A
			POP	HL
			POP	DE
			POP	BC
			SCF			; Flag as successful
			RET

; Change directory
; HLU: Address of path (zero terminated)
; Returns:
; - A: File error, or 0 if OK
;
mos_api_cd:		LD	A, MB		; Check if MBASE is 0
			OR	A, A
;
; Now we need to mod HLU to include the MBASE in the U byte
;
			CALL	NZ, SET_AHL24	; If it is running in classic Z80 mode, set U to MB
;
; Finally, we can do the load
;
			PUSH	HL		; char   * filename
			CALL	_mos_CD_API
			LD	A, L		; Return vaue in HLU, put in A
			POP	HL
			RET

; Directory listing
; HLU: Address of path (zero terminated)
; Returns:
; - A: File error, or 0 if OK
;
mos_api_dir:		LD	A, MB		; Check if MBASE is 0
			OR	A, A
;
; Now we need to mod HLU to include the MBASE in the U byte
;
			CALL	NZ, SET_AHL24	; If it is running in classic Z80 mode, set U to MB
;
; Finally, we can run the command
;
			PUSH	HL		; char * path
			CALL	_mos_DIR_API
			LD	A, L		; Return value in HLU, put in A
			POP	HL
			RET

; Delete a file from the SD card
; HLU: Address of filename (zero terminated)
; Returns:
; - A: File error, or 0 if OK
;
mos_api_del:		LD	A, MB		; Check if MBASE is 0
			OR	A, A
;
; Now we need to mod HLU to include the MBASE in the U byte
;
			CALL	NZ, SET_AHL24	; If it is running in classic Z80 mode, set U to MB
;
; Finally, we can do the delete
;
			PUSH	HL		; char   * filename
			CALL	_mos_DEL	; Call the C function mos_DEL
			LD	A, L		; Return vaue in HLU, put in A
			POP	HL
			RET

; Rename a file on the SD card
; HLU: Address of filename1 (zero terminated)
; DEU: Address of filename2 (zero terminated)
; Returns:
; - A: File error, or 0 if OK
;
mos_api_ren:		LD	A, MB		; Check if MBASE is 0
			OR	A, A
			JR	Z, $F		; If it is, we can assume HL and DE are 24 bit
;
; Now we need to mod HLU and DEu to include the MBASE in the U byte
;
			CALL	SET_AHL24
			CALL	SET_ADE24
;
; Finally we can do the rename
;
$$:			PUSH	DE		; char * filename2
			PUSH	HL		; char * filename1
			CALL	_mos_REN_API	; Call the C function mos_REN_API
			LD	A, L		; Return vaue in HLU, put in A
			POP	HL
			POP	DE
			RET

; Copy a file on the SD card
; HLU: Address of filename1 (zero terminated)
; DEU: Address of filename2 (zero terminated)
; Returns:
; - A: File error, or 0 if OK
;
mos_api_copy:		LD	A, MB		; Check if MBASE is 0
			OR	A, A
			JR	Z, $F		; If it is, we can assume HL and DE are 24 bit
;
; Now we need to mod HLU and DEu to include the MBASE in the U byte
;
			CALL	SET_AHL24
			CALL	SET_ADE24
;
; Finally we can do the rename
;
$$:			PUSH	DE		; char * filename2
			PUSH	HL		; char * filename1
			CALL	_mos_COPY_API	; Call the C function mos_COPY_API
			LD	A, L		; Return vaue in HLU, put in A
			POP	HL
			POP	DE
			RET

; Make a folder on the SD card
; HLU: Address of filename (zero terminated)
; Returns:
; - A: File error, or 0 if OK
;
mos_api_mkdir:		LD	A, MB		; Check if MBASE is 0
			OR	A, A
;
; Now we need to mod HLU to include the MBASE in the U byte
;
			CALL	NZ, SET_AHL24	; If it is running in classic Z80 mode, set U to MB
;
; Finally, we can do the load
;
			PUSH	HL		; char   * filename
			CALL	_mos_MKDIR_API	; Call the C function mos_MKDIR_API
			LD	A, L		; Return vaue in HLU, put in A
			POP	HL
			RET

; Get a pointer to a system variable
; Returns:
; IXU: Pointer to system variables (see mos_api.asm for more details)
;
mos_api_sysvars:	LD	IX, _sysvars
			RET

; Invoke the line editor
; HLU: Address of the buffer
; BCU: Buffer length
;   E: flags
; Returns:
;   A: Key that was used to exit the input loop (CR=13, ESC=27)
;
mos_api_editline:	LD	A, MB		; Check if MBASE is 0
			OR	A, A
;
; Now we need to mod HLU to include the MBASE in the U byte
;
			CALL	NZ, SET_AHL24	; If it is running in classic Z80 mode, set U to MB
;
			PUSH	DE		; UINT8	  flags
			PUSH	BC		; int 	  bufferLength
			PUSH	HL		; char	* buffer
			CALL	_mos_EDITLINE
			LD	A, L		; return value, only interested in lowest byte
			POP	HL
			POP	BC
			POP	DE
			RET

; Open a file
; HLU: Filename
;   C: Mode
; Returns:
;   A: Filehandle, or 0 if couldn't open
;
; TODO: why the push/pop of HL, DE, IX and IY?
mos_api_fopen:		PUSH	BC
			PUSH	DE
			PUSH	HL
			PUSH	IX
			PUSH	IY
;
			LD	A, MB		; Check if MBASE is 0
			OR	A, A
;
; Now we need to mod HLU and DEU to include the MBASE in the U byte
;
			CALL	NZ, SET_AHL24	; If it is running in classic Z80 mode, set U to MB
;
			LD	A, C
			LD	BC, 0
			LD	C, A
			PUSH	BC		; byte	  mode
			PUSH	HL		; char	* buffer
			CALL	_mos_FOPEN
			LD	A, L		; Return fh
			POP	HL
			POP	BC
;
			POP	IY
			POP	IX
			POP	HL
			POP	DE
			POP	BC
			RET

; Close a file
;   C: Filehandle
; Returns
;   A: Number of files still open
;
mos_api_fclose:		PUSH	BC
			PUSH	DE
			PUSH	HL
			PUSH	IX
			PUSH	IY
;
			LD	A, C
			LD	BC, 0
			LD	C, A
			PUSH	BC		; byte 	  fh
			CALL	_mos_FCLOSE
			LD	A, L		; Return # files still open
			POP	BC
;
			POP	IY
			POP	IX
			POP	HL
			POP	DE
			POP	BC
			RET

; Get a character from a file
;   C: Filehandle
; Returns:
;   A: Character read
;   F: C set if last character in file, otherwise NC
;
mos_api_fgetc:		PUSH	BC
			PUSH	DE
			PUSH	HL
			PUSH	IX
			PUSH	IY
;
			LD	DE, 0
			LD	E, C
			PUSH	DE		; byte	  fh
			CALL	_mos_FGETC	; Read the character
			POP	DE
			LD	A, L 		; A: Character read
			SRL	H 		; F: C = EOF
;
			POP	IY
			POP	IX
			POP	HL
			POP	DE
			POP	BC
			RET

; Write a character to a file
;   C: Filehandle
;   B: Character to write
;
mos_api_fputc:		PUSH	AF
			PUSH	BC
			PUSH	DE
			PUSH	HL
			PUSH	IX
			PUSH	IY
;
			LD	DE, 0
			LD	E, B
			PUSH	DE		; byte	  char
			LD	E, C
			PUSH	DE		; byte	  fh
			CALL	_mos_FPUTC
			POP	DE
			POP	DE
;
			POP	IY
			POP	IX
			POP	HL
			POP	DE
			POP	BC
			POP	AF
			RET

; Check whether we're at the end of the file
;   C: Filehandle
; Returns:
;   A: 1 if at end of file, otherwise 0
;
mos_api_feof:		PUSH	BC
			PUSH	DE
			PUSH	HL
			PUSH	IX
			PUSH	IY
;
			LD	DE, 0
			LD	E, C
			PUSH	DE		; byte	  fh
			CALL	_mos_FEOF
			POP	DE
;
			POP	IY
			POP	IX
			POP	HL
			POP	DE
			POP	BC
			RET

; Copy an error message
;   E: The error code
; HLU: Address of buffer to copy message into
; BCU: Size of buffer
;
mos_api_getError:	LD	A, MB		; Check if MBASE is 0
			OR	A, A
			CALL	NZ, SET_AHL24	; If it is running in classic Z80 mode, set U to MB
;
; Now copy the error message
;
			PUSH	BC		; UINT24 size
			PUSH	HL		; UINT24 address
			PUSH	DE		; byte   errno
			CALL	_mos_GETERROR
			POP	DE
			POP	HL
			POP	BC
			RET

; Execute a MOS command
; HLU: Pointer the the MOS command string
; DEU: Pointer to additional command structure
; BCU: Number of additional commands
; Returns:
;   A: MOS error code
;
mos_api_oscli:		LD	A, MB		; Check if MBASE is 0
			OR	A, A
			CALL	NZ, SET_AHL24	; If it is running in classic Z80 mode, set U to MB
;
; Now execute the MOS command
;
			PUSH	HL		; char * buffer
			CALL	_mos_OSCLI
			LD	A, L		; Return vaue in HLU, put in A
			POP	HL
			RET

; Fetch a RTC string
; HLU: Pointer to a buffer to copy the string to
; Returns:
;   A: Length of time
;
mos_api_getrtc:		LD	A, MB		; Check if MBASE is 0
			OR	A, A
			CALL	NZ, SET_AHL24	; If it is running in classic Z80 mode, set U to MB
;
; Now fetch the time
;
			PUSH	HL		; UINT24 address
			CALL	_mos_GETRTC
			POP	HL
			RET

; Unpack RTC data
; HLU: Pointer to a buffer to copy the RTC data to
; C: Flags (bit 0 = refresh RTC before unpacking, bit 1 = refresh RTC after unpacking)
;
mos_api_unpackrtc:	LD	A, MB		; Check if MBASE is 0
			OR	A, A
			CALL	NZ, SET_AHL24	; If it is running in classic Z80 mode, set U to MB
			PUSH	BC		; UINT8 flags
			PUSH 	HL		; UINT24 address
			CALL	_mos_UNPACKRTC
			POP	HL
			POP	BC
			RET

; Set the RTC
; HLU: Pointer to a buffer with the time data in
;
mos_api_setrtc:		LD	A, MB		; Check if MBASE is 0
			OR	A, A
			CALL	NZ, SET_AHL24	; If it is running in classic Z80 mode, set U to MB
;
; Now fetch the time
;
			PUSH	HL		; UINT24 address
			CALL	_mos_SETRTC
			POP	HL
			RET

; Set an interrupt vector
; HLU: Pointer to the interrupt vector (24-bit pointer)
;   E: Vector # to set
; Returns:
; HLU: Pointer to the previous vector
;
mos_api_setintvector:	LD	A, E
			LD	DE, 0 		; Clear DE
			LD	E, A 		; Store the vector #
			PUSH	HL		; void(*handler)(void)
			PUSH	DE 		; byte vector
			CALL	_mos_SETINTVECTOR
			POP	DE
			POP	DE
			RET

; Set a VDP keyboard packet receiver callback
;   C: If non-zero then set the top byte of HLU(callback address)  to MB (for ADL=0 callers)
; HLU: Pointer to callback
;
mos_api_setkbvector:	PUSH	DE
			XOR	A
			OR	C		; If C!=0 set top byte (bits 16:23) to MB
			JR	Z, $F
			LD	A, MB
			CALL	SET_AHL24
$$:			PUSH	HL
			POP	DE
			LD	HL, _user_kbvector
			LD	(HL),DE
			POP	DE
			RET

; Get the address of the keyboard map
; Returns:
; IXU: Base address of the keymap
;
mos_api_getkbmap:	LD	IX, _keymap
			RET

; Open the I2C bus as master
;   C: Frequency ID
;
mos_api_i2c_open:	PUSH	BC
			PUSH	DE
			PUSH	HL
			PUSH	IX
			PUSH	IY
;
			LD	HL,0
			LD	L, C
			PUSH	HL
			CALL	_mos_I2C_OPEN
			POP	HL
;
			POP	IY
			POP	IX
			POP	HL
			POP	DE
			POP	BC
			RET

; Close the I2C bus
;
mos_api_i2c_close:	PUSH	BC
			PUSH	DE
			PUSH	HL
			PUSH	IX
			PUSH	IY
;
			CALL	_mos_I2C_CLOSE
;
			POP	IY
			POP	IX
			POP	HL
			POP	DE
			POP	BC
			RET

; Write n bytes to the I2C bus
;   C: I2C address
;   B: Number of bytes to write, maximum 32
; HLU: Address of buffer containing the bytes to send
;
mos_api_i2c_write:	PUSH	DE
			PUSH	IX
			PUSH	IY
;
			LD	A, MB		; Check if MBASE is 0
			OR	A, A
			CALL	NZ, SET_AHL24	; If it is running in classic Z80 mode, set U to MB
;
			PUSH	HL		; Address of buffer
			LD	HL,0
			LD	L, B
			PUSH	HL		; Count
			LD	L, C
			PUSH	HL		; I2C address
			CALL	_mos_I2C_WRITE
			POP	HL
			POP	HL
			POP	HL
;
			POP	IY
			POP	IX
			POP	DE
			RET

; Read n bytes from the I2C bus
;   C: I2C address
;   B: Number of bytes to read, maximum 32
; HLU: Address of buffer to read bytes to
;
mos_api_i2c_read:	PUSH	DE
			PUSH	IX
			PUSH	IY
;
			LD	A, MB		; Check if MBASE is 0
			OR	A, A
			CALL	NZ, SET_AHL24	; If it is running in classic Z80 mode, set U to MB
;
			PUSH	HL		; Address of buffer
			LD	HL,0
			LD	L, B
			PUSH	HL		; Count
			LD	L, C
			PUSH	HL		; I2C address
			CALL	_mos_I2C_READ
			POP	HL
			POP	HL
			POP	HL
;
			POP	IY
			POP	IX
			POP	DE
			RET

; Open UART1
; IXU: Pointer to UART struct
;	+0: Baud rate (24-bit, little endian)
;	+3: Data bits
;	+4: Stop bits
;	+5: Parity bits
;	+6: Flow control (0: None, 1: Hardware)
;	+7: Enabled interrupts
; Returns:
;   A: Error code (0 = no error)
;
mos_api_uopen:		LEA	HL, IX + 0	; HLU: Pointer to struct
			LD	A, MB 		; If in 64K segment when
			OR	A, A 		; MB != 0 then
			CALL	NZ, SET_AHL24 	; Convert to a 24-bit absolute pointer
			PUSH	HL		; UART * pUART
			CALL	_open_UART1	; Initialise the UART port
			LD	A, L 		; The return value is in HLU
			POP	HL 		; Tidy up the stack
			RET

; Close UART1
;
mos_api_uclose:		JP	_close_UART1

; Get a character from UART1
; Returns:
;   A: Character read
;   F: C if successful
;   F: NC if the UART is not open
;
mos_api_ugetc		JP	UART1_serial_GETCH

; Write a character to UART1
;   C: Character to write
; Returns:
;   F: C if successful
;   F: NC if the UART is not open
;
mos_api_uputc:		LD	A, C
			JP	UART1_serial_PUTCH

; Convert a file handle to a FIL structure pointer
;   C: Filehandle
; Returns:
; HLU: Pointer to a FIL struct
;
mos_api_getfil:		PUSH	BC		; UINT8 fh
			CALL	_mos_GETFIL
			POP	BC
			RET

; Read a block of data from a file
;   C: Filehandle
; HLU: Pointer to where to write the data to
; DEU: Number of bytes to read
; Returns:
; DEU: Number of bytes read
;
mos_api_fread:		LD	A, MB		; Check if MBASE is 0
			OR	A, A
			CALL	NZ, SET_AHL24
			PUSH	DE		; UINT24 btr
			PUSH	HL		; UINT24 buffer
			PUSH	BC		; UINT8 fh
			CALL	_mos_FREAD
			LD	(_scratchpad), HL
			POP	BC
			POP	HL
			POP	DE
			LD	DE, (_scratchpad)
			RET

; Write a block of data to a file
;  C: Filehandle
; HLU: Pointer to where the data is
; DEU: Number of bytes to write
; Returns:
; DEU: Number of bytes read
;
mos_api_fwrite:		LD	A, MB		; Check if MBASE is 0
			OR	A, A
			CALL	NZ, SET_AHL24
			PUSH	DE		; UINT24 btr
			PUSH	HL		; UINT24 buffer
			PUSH	BC		; UINT8 fh
			CALL	_mos_FWRITE
			LD	(_scratchpad), HL
			POP	BC
			POP	HL
			POP	DE
			LD	DE, (_scratchpad)
			RET

; Move the read/write pointer in a file
;   C: Filehandle
; HLU: Least significant 3 bytes of the offset from the start of the file (DWORD)
;   E: Most significant byte of the offset
; Returns:
;   A: FRESULT
;
mos_api_flseek:		PUSH 	DE		; UINT32 offset (msb)
			PUSH	HL 		; UINT32 offset (lsb)
			PUSH	BC		; UINT8 fh
			CALL	_mos_FLSEEK
			LD	A, L 		; FRESULT
			POP	BC
			POP	HL
			POP	DE
			RET


; MOS String functions
;
; Pattern matching
; HLU: Address of pattern (zero terminated)
; DEU: Address at string to compare against pattern (zero terminated)
; C: Flags
; Returns:
; - A: File error, or 0 if OK
;
mos_api_pmatch:		PUSH	BC
			LD	A, MB		; Check if MBASE is 0
			OR	A, A
			JR	Z, $F		; If it is, we can assume HL and DE are 24 bit
			CALL	SET_AHL24
			CALL	SET_ADE24
$$:			LD	A, C
			LD	BC, 0
			LD	C, A
			PUSH	BC		; BYTE flags  (altho we'll push all 3 bytes)
			PUSH	DE		; char * string
			PUSH	HL		; char * pattern
			CALL	_pmatch		; Call the C function pmatch
			LD	A, L		; Return value in HLU, put in A
			POP	HL
			POP	DE
			POP	BC
			POP	BC
			RET

; Extract a (numbered) argument from a string
; HLU: Pointer to source string
; BCU: Argument number
; Returns:
; - HLU: Address of the argument or zero if not found
; - DEU: Address of the next character after the argument
;
; char * getArgument(char * source, int argNo, char ** end)
mos_api_getargument:	LD	A, MB		; Check if MBASE is 0
			OR	A, A
			CALL	NZ, SET_AHL24
			PUSH	HL
			LD	HL, _scratchpad
			EX	(SP), HL	; char ** end
			PUSH	BC		; UINT8 argNo
			PUSH	HL		; char * source
			CALL	_getArgument	; Call the C function getArgument
			; HL now contains the argument address
			POP	BC
			POP	BC
			EX	(SP), HL
			POP	HL		; Return start of argument in HLU
			LD	DE, (_scratchpad)	; Return end of argument in DEU
			RET

; Extract a string, using a given divider
; HLU: Pointer to source string to extract from
; DEU: Pointer to string for divider matching, or 0 for default (space)
; C: Flags
; Depending on flags, the result string will be zero terminated or not
; Returns:
; - A: status code
; - HLU: Address of the result string
; - DEU: Address of next character after end of result string
;
; int extractString(char * source, char ** end, char * divider, char ** result, BYTE flags)
mos_api_extractstring:
			PUSH	BC		; BYTE flags
			LD	A, MB		; Check if MBASE is 0
			OR	A, A
			JR	Z, $F		; If it is, we can assume addresses are 24 bit
			CALL	SET_AHL24
			LD	A, D		; Check if DE is zero
			OR	A, E
			JR	Z, $F		; DE is zero so no need to set U to MB
			LD	A, MB
			CALL	SET_ADE24	; DE not zero, so set U to MB
$$:			PUSH	HL
			LD	HL, _scratchpad
			EX	(SP), HL	; char ** result
			PUSH	DE		; char * divider
			PUSH	HL
			LD	HL, _scratchpad + 3
			EX	(SP), HL	; char ** end
			PUSH	HL		; char * source
			CALL	_extractString	; Call the C function extractString
			LD	A, L		; Save return value in HLU, in A
			POP	HL
			POP	HL
			POP	HL
			POP	HL
			LD	L, A		; keep result so we can
			POP 	BC		; unpop the BC
			LD 	A, L		; return status code in A
			LD	HL, (_scratchpad)	; return result in HLU
			LD	DE, (_scratchpad + 3)	; return end in DEU
			RET

; Extract a number, using given divider
; HLU: Pointer to source string to extract from
; DEU: Pointer to string for divider matching, or 0 for default (space)
; C: Flags
; Returns:
; - A: status code
; - HLU: Number extracted
; - DEU: Address of next character after end of number
;
; bool	extractNumber(char * source, char ** end, char * divider, int * number, BYTE flags)
mos_api_extractnumber:
			PUSH	BC		; BYTE flags
			LD	A, MB		; Check if MBASE is 0
			OR	A, A
			JR	Z, $F		; If it is, we can assume addresses are 24 bit
			CALL	SET_AHL24
			LD	A, D
			OR	A, E
			JR	Z, $F		; DE is zero, so no need to set U to MB
			LD	A, MB
			CALL	SET_ADE24
$$:			PUSH	HL
			LD	HL, _scratchpad
			EX	(SP), HL	; int * number
			PUSH	DE		; char * divider
			PUSH	HL
			LD	HL, _scratchpad + 3
			EX	(SP), HL	; char ** end
			PUSH	HL		; char * source
			CALL	_extractNumber	; Call the C function extractNumber
			LD	A, L		; Save return value in HLU, in A
			POP	HL
			POP	HL
			POP	HL
			POP	HL
			LD	L, A		; keep result so we can
			POP 	BC		; unpop the BC
			LD 	A, L		; move return value back to A
			LD	HL, (_scratchpad)	; return number in HLU
			LD	DE, (_scratchpad + 3)	; return end in DEU
			; Return value in A will be true/false
			; so we need to change to 0 for success, and 19 (invalid parameter) for failure
			OR	A, A		; Was status value false?
			JR	Z, $F		; If it is, we need to replace with 19
			LD	A, 0		; Otherwise, return 0 FR_OK
			RET
$$:			LD	A, 19		; Return 19 FR_INVALID_PARAMETER
			RET

; Escape a string, converting control characters to be pipe-prefixed
; HLU: Pointer to source string
; DEU: Pointer to destination buffer (optional)
; BCU: Length of destination buffer
; Returns:
; - A: Status code
; - BCU: Length of escaped string
;
; int escapeString(char * source, char * dest, int * length)
mos_api_escapestring:
			LD	(_scratchpad), BC 	; Save the length
			LD	A, MB		; Check if MBASE is 0
			OR	A, A
			JR	Z, $F		; If it is, we can assume addresses are 24 bit
			CALL	SET_AHL24
			LD	A, D
			OR	A, E
			JR	Z, $F		; DE is zero, so no need to set U to MB
			LD	A, MB
			CALL	SET_ADE24
$$:			PUSH	HL
			LD	HL, _scratchpad
			EX	(SP), HL	; int * length
			PUSH	DE		; char * dest
			PUSH	HL		; char * source
			CALL	_escapeString	; Call the C function escapeString
			LD	A, L		; Save return value in HLU, in A
			POP	HL
			POP	DE
			POP	BC
			LD	BC, (HL)	; Return the length
			RET

; Set a variable value
; HLU: Pointer to variable name (can include wildcards)
; IXU: Variable value (number, or pointer to zero-terminated string)
; IYU: Pointer to variable name (0 for first call)
; C: Variable type, or -1 (255) to delete the variable
; Returns:
; - A: Status code
; - C: Actual variable type
; - IYU: Pointer to variable name (for next call)
;
; int setVarVal(char * name, void * value, char ** actualName, BYTE * type);
mos_api_setvarval:
			LD	A, C
			LD	(_scratchpad + 3), A	; Save the type
			LD	(_scratchpad), IY	; Save the actualName pointer
			LD	A, MB		; Check if MBASE is 0
			OR	A, A
			JR	Z, $F		; If it is, we can assume addresses are 24 bit
			CALL	SET_AHL24
			LD	A, C
			CP	1		; Is the type a number?
			CALL	NZ, SET_AIX24	; Only set U if type is not a number
$$:			PUSH	HL		; Temporary storage
			LD	HL, _scratchpad + 3
			EX	(SP), HL	; BYTE * type
			LD	IY, _scratchpad
			PUSH	IY		; char ** actualName
			PUSH	IX		; void * value
			PUSH	HL		; char * name
			CALL	_setVarVal	; Call the C function setVarVal
			LD	A, L		; Save return value in HLU, in A
			POP	HL
			POP	IY		; To be replaced
			POP	IY
			POP	IY
			LD 	IY, _scratchpad
			LD	C, (IY + 3)	; Return the actual type
			LD	IY, (IY)	; Return the actual name
			RET

; Read a variable value
; HLU: Pointer to variable name (can include wildcards)
; IXU: Pointer to buffer to store the value (null/0 to read length only)
; DEU: Length of buffer
; IYU: Pointer to variable name (0 for first call)
; C: Flags (3 = expand value into string)
; Returns:
; - A: Status code
; - C: Actual variable type
; - DEU: Length of variable value
; - IYU: Pointer to variable name (for next call)
;
; int readVarVal(char * namePattern, void * value, char ** actualName, int * length, BYTE * typeFlag)
mos_api_readvarval:
			LD	A, C
			LD	(_scratchpad + 6), A	; Save the flags
			LD	(_scratchpad + 3), DE	; Save the length
			LD	(_scratchpad), IY	; Save the actualName pointer
			LD	DE, IX		; move optional target buffer into DE
			LD	A, MB		; Check if MBASE is 0
			OR	A, A
			JR	Z, $F		; If it is, we can assume addresses are 24 bit
			CALL	SET_AHL24
			LD	A, D		; check it target buffer is zero
			OR	A, E
			JR	Z, $F		; DE is zero, so no need to set U to MB
			LD	A, MB
			CALL	SET_ADE24
$$:			PUSH	HL		; Temporary storage
			LD	HL, _scratchpad + 6
			EX	(SP), HL	; BYTE * typeFlag
			PUSH	HL
			LD	HL, _scratchpad + 3
			EX	(SP), HL	; int * length
			LD	IY, _scratchpad
			PUSH	IY		; char ** actualName
			PUSH	DE		; void * value
			PUSH	HL		; char * namePattern
			CALL	_readVarVal	; Call the C function readVarVal
			LD	A, L		; Save return value in HLU, in A
			POP	HL		; (variable name pattern)
			POP	IX		; (value pointer)
			POP	IY		; To be replaced
			POP	IY
			POP	IY
			LD 	IY, _scratchpad
			LD	C, (IY + 6)	; Return the actual type
			LD	DE, (IY + 3)	; Return the length
			LD	IY, (IY)	; Return the actual name
			RET

; Initialise a GS Trans operation
; HLU: Pointer to source buffer to translate
; DEU: Address of pointer used to store trans info
; A: Flags
; Returns:
; - A: Status code
;
mos_api_gsinit:
			PUSH	AF		; BYTE flags
			LD	A, MB		; Check if MBASE is 0
			OR	A, A
			JR	Z, $F		; If it is, we can assume HL and DE are 24 bit
			CALL	SET_AHL24
			CALL	SET_ADE24
$$:			PUSH	DE		; t_mosTransInfo ** transInfoPtr
			PUSH	HL		; char * source
			CALL	_gsInit		; Call the C function gsInit
			LD	A, L		; Return value in HLU, put in A
			LD	(_scratchpad), A	; Save the result
			POP	HL
			POP	DE
			POP	AF
			LD	A, (_scratchpad)
			RET

; Perform a GS Trans "read" operation
; HLU: Pointer to a char (byte) to store the result
; DEU: Address of pointer used to store trans info (same pointer as used with gsInit)
; Returns:
; - A: Status code
;
mos_api_gsread:
			LD	A, MB		; Check if MBASE is 0
			OR	A, A
			JR	Z, $F		; If it is, we can assume HL and DE are 24 bit
			CALL	SET_AHL24
			CALL	SET_ADE24
$$:			PUSH	HL		; char * read
			PUSH	DE		; t_mosTransInfo ** transInfoPtr
			CALL	_gsRead		; Call the C function gsRead
			LD	A, L		; Return value in HLU, put in A
			POP	DE
			POP	HL
			RET

; Perform a complete GSTrans operation from source into dest buffer
; HLU: Pointer to source buffer
; DEU: Pointer to destination buffer (can be null to just count size)
; BCU: Length of destination buffer
; A: Flags
; Returns:
; - A: Status code
; - BCU: Calculated total length of destination string
;
; int gsTrans(char * source, char * dest, int destLen, int * read, BYTE flags)
mos_api_gstrans:
			PUSH	AF		; BYTE flags
			LD	A, MB		; Check if MBASE is 0
			OR	A, A
			JR	Z, $F		; If it is, we can assume addresses are 24 bit
			CALL	SET_AHL24
			LD	A, D		; Check if DE is zero
			OR	A, E
			JR	Z, $F		; DE is zero, so no need to set U to MB
			LD	A, MB
			CALL	SET_ADE24
$$:			PUSH 	HL
			LD	HL, _scratchpad
			EX	(SP), HL	; int * read
			PUSH	BC		; UINT24 destLength
			PUSH	DE		; char * dest
			PUSH	HL		; char * source
			CALL	_gsTrans	; Call the C function gstrans
			LD	A, L		; Return value in HLU, put in A
			LD	(_scratchpad + 3), A	; Save the result
			POP	HL
			POP	DE
			POP	BC
			POP	BC
			POP	AF
			LD	A, (_scratchpad + 3)
			LD	BC, (_scratchpad)
			RET

; Substitute arguments into a string from template
; HLU: Pointer to template string
; DEU: Pointer to arguments string
; BCU: Length of destination buffer
; IXU: Pointer to destination buffer (can be null to just count size)
; A: Flags
; Returns:
; - BCU: Calculated length of destination string
;
; int substituteArgs(char * template, char * args, char * dest, int length, bool omitRest)
mos_api_substituteargs:
			PUSH	AF		; BYTE flags (bool omitRest)
			PUSH	BC		; UINT24 length
			PUSH	IX		; char * dest
			LD	A, MB		; Check if MBASE is 0
			OR	A, A
			JR	Z, sub_args_contd	; If it is, we can assume addresses are 24 bit
			CALL	SET_AHL24
			CALL	SET_ADE24
			EX	(SP), HL	; Swap IX (on stack) with HL, as IX is optional
			LD	A, L
			OR	A, H
			JR	Z, $F		; HL was zero, so jump ahead
			LD	A, MB
			CALL	SET_AHL24	; HL (IX) not zero, so set U to MB
$$:			EX	(SP), HL
sub_args_contd:		PUSH	DE		; char * args
			PUSH	HL		; char * template
			CALL	_substituteArgs	; Call the C function substituteArgs
			LD	(_scratchpad), HL	; Save the result
			POP	HL
			POP	DE
			POP	IX
			POP	BC
			POP	AF
			LD	BC, (_scratchpad)
			RET

; All these optional address things need an MB load into A before calling SET_Axx24

; Resolves a path, replacing prefixes and leafnames with actual values
; HLU: Pointer to the path to resolve
; DEU: Pointer to the resolved path (optional - omit for count only)
; BCU: Length of the resolved path buffer
; IXU: Pointer to the index (integer) of the resolved path
; IYU: Pointer to a directory object to persist between calls (optional)
; Returns:
; - A: Status code
; - BCU: Length of the resolved path
; - Index pointed to at IXU, if set, will be updated to the next path index
;
; int resolvePath(char * filepath, char * resolvedPath, int * length, BYTE * index, DIR * dir)
mos_api_resolvepath:
			LD	(_scratchpad), BC	; Save the length
			LD	A, MB		; Check if MBASE is 0
			OR	A, A
			JR	Z, res_path_contd	; If it is, we can assume addresses are 24 bit
			CALL	SET_AHL24	; HL is required, so set it
			PUSH	HL		; push HL so we can use register for working, if we need to
			; DE is optional, so check if it's zero
			LD	A, D
			OR	A, E
			JR	Z, $F		; DE is zero, so no need to set U to MB
			LD	A, MB
			CALL	SET_ADE24	; DE not zero, so set U to MB
$$:			; IX is optional, so check if it's zero
			PUSH	IX
			POP	HL
			LD	A, H
			OR	A, L
			JR	Z, $F		; IX is zero, so no need to set U to MB
			LD	A, MB
			CALL	SET_AHL24	; IX not zero, so set U to MB
$$:			PUSH	HL
			POP	IX
			; IY is optional, so check if it's zero
			PUSH	IY
			POP	HL
			LD	A, H
			OR	A, L
			JR	Z, $F		; IY is zero, so no need to set U to MB
			LD	A, MB
			CALL	SET_AHL24	; IY not zero, so set U to MB
$$:			PUSH	HL
			POP	IY
			POP	HL
			; OK so we should now have all the addresses set up
res_path_contd:		PUSH	IY		; DIR * dir
			PUSH	IX		; BYTE * index
			LD	IX, _scratchpad
			PUSH	IX		; int * length
			PUSH	DE		; char * resolvedPath
			PUSH	HL		; char * filepath
			CALL	_resolvePath	; Call the C function resolvePath
			LD	A, L		; Return value in HLU, put in A
			POP	HL
			POP	DE
			POP	BC
			POP	IX
			POP	IY
			LD	BC, (_scratchpad)
			RET

; Get the directory for a given path
; String only - resolves path prefixes for the given index
; HLU: Pointer to the path to get the directory for
; DEU: Pointer to the buffer to store the directory in (optional - omit for count only)
; BCU: Length of the buffer
; A: Search index
; Returns:
; - A: Status code
; - BCU: Length of the directory
;
; int getDirectoryForPath(char * srcPath, char * dir, int * length, BYTE searchIndex)
mos_api_getdirectoryforpath:
			PUSH	AF		; BYTE searchIndex
			LD	(_scratchpad), BC	; Save the length
			LD	A, MB		; Check if MBASE is 0
			OR	A, A
			JR	Z, $F		; If it is, we can assume addresses are 24 bit
			CALL	SET_AHL24	; HL is required, so set it
			; DE is optional, so check if it's zero
			LD	A, D
			OR	A, E
			JR	Z, $F		; DE is zero, so no need to set U to MB
			LD	A, MB
			CALL	SET_ADE24	; DE not zero, so set U to MB
$$:			LD	BC, _scratchpad
			PUSH	BC		; int * length
			PUSH	DE		; char * dir
			PUSH	HL		; char * srcPath
			CALL	_getDirectoryForPath	; Call the C function getDirectoryForPath
			LD	A, L		; Return value in HLU, put in A
			POP	HL
			POP	DE
			POP	BC
			POP	BC		; (AF push - we will replace)
			LD	BC, (_scratchpad)
			RET

; Get the leafname for a given path
; HLU: Pointer to the path to get the leafname for
; Returns:
; - HLU: Pointer to the leafname
;
; char * getFilepathLeafname(char * filepath);
mos_api_getfilepathleafname:
			LD	A, MB		; Check if MBASE is 0
			OR	A, A
			JR	Z, $F		; If it is, we can assume HL is 24 bit
			CALL	SET_AHL24	; HL is required, so set it
$$:			PUSH	HL		; char * filepath
			CALL	_getFilepathLeafname	; Call the C function getFilepathLeafname
			EX	(SP), HL	; Return value in HLU
			POP	HL
			RET

; Check if a given path points to a directory
; NB this does not do path resolution
; HLU: Pointer to the path to check
; Returns:
; - A: Status code
mos_api_isdirectory:
			LD	A, MB		; Check if MBASE is 0
			OR	A, A
			JR	Z, $F		; If it is, we can assume HL is 24 bit
			CALL	SET_AHL24	; HL is required, so set it
$$:			PUSH	HL		; char * filepath
			CALL	_isDirectory	; Call the C function isDirectory
			LD	A, L		; Return value in HLU, put in A
			POP	HL
			; return value is true/false, so we need to change to 0 for success, and 19 (invalid parameter) for failure
			OR	A, A		; Was status value false?
			JR	Z, $F		; If it is, we need to replace with 5
			LD	A, 0		; Otherwise, return 0 FR_OK
			RET
$$:			LD	A, 5		; Return 5 FR_NO_PATH
			RET

; Get the absolute version of a (relative) path
; HLU: Pointer to the path to get the absolute version of
; DEU: Pointer to the buffer to store the absolute path in
; BCU: Length of the buffer
; Returns:
; - A: Status code
;
; int resolveRelativePath(char * path, char * resolved, int length);
; For now, we will not support returning back length, or calculating length
mos_api_getabsolutepath:
			LD	A, MB		; Check if MBASE is 0
			OR	A, A
			JR	Z, $F		; If it is, we can assume pointers are 24 bit
			CALL	SET_AHL24
			CALL	SET_ADE24
$$:			PUSH	BC		; int length
			PUSH	DE		; char * resolved
			PUSH	HL		; char * path
			CALL	_resolveRelativePath	; Call the C function resolveRelativePath
			LD	A, L		; Return value in HLU, put in A
			POP	HL
			POP	DE
			POP	BC
			RET

; Open a file
; HLU: Pointer to a blank FIL struct
; DEU: Pointer to the filename (0 terminated)
;   C: File mode
; Returns:
;   A: FRESULT
;
ffs_api_fopen:		LD	A, MB		; A: MB
			OR	A, A 		; Check whether MB is 0, i.e. in 24-bit mode
			JR	Z, $F		; It is, so skip as all addresses can be assumed to be 24-bit
			CALL 	SET_ADE24	; Convert DE to an address in segment A (MB)
			CALL	GET_AHL24	; Get MSB of HL
			OR	A, A 		; Does it already contain a value? (fetched using mos_api_getfil?)
			LD	A, MB		; A: MB
			CALL	Z, SET_AHL24	; No it's zero, so convert HL to an address in segment A (MB)
;
$$:			PUSH	BC		; BYTE mode
			PUSH	DE		; const TCHAR * path
			PUSH	HL		; FIL * fp
			CALL	_f_open
			LD	A, L 		; FRESULT
			POP	HL
			POP	DE
			POP	BC
			RET

; Close a file
; HLU: Pointer to a blank FIL struct
; Returns:
;   A: FRESULT
;
ffs_api_fclose:		LD	A, MB
			OR	A, A
			JR	Z, $F
			CALL	GET_AHL24
			OR 	A, A
			LD	A, MB
			CALL	Z, SET_AHL24
;
$$:			PUSH	HL		; FIL * fp
			CALL	_f_close
			LD	A, L		; FRESULT
			POP	HL
			RET

; Read data from a file
; HLU: Pointer to a FIL struct
; DEU: Pointer to where to write the file out
; BCU: Number of bytes to read
; Returns:
;   A: FRESULT
; BCU: Number of bytes read
;
ffs_api_fread:		LD	A, MB		; A: MB
			OR	A, A 		; Check whether MB is 0, i.e. in 24-bit mode
			JR	Z, $F		; It is, so skip as all addresses can be assumed to be 24-bit
			CALL 	SET_ADE24	; Convert DE to an address in segment A (MB)
			CALL	GET_AHL24	; Get MSB of HL
			OR	A, A 		; Does it already contain a value? (fetched using mos_api_getfil?)
			LD	A, MB		; A: MB
			CALL	Z, SET_AHL24	; No it's zero, so convert HL to an address in segment A (MB)
;
$$:			PUSH	HL
			LD	HL, _scratchpad
			EX	(SP), HL	; UINT * br
			PUSH	BC		; UINT btr
			PUSH	DE		; void * buff
			PUSH	HL		; FILE * fp
			CALL	_f_read
			LD	A, L 		; FRESULT
			POP	HL
			POP	DE
			POP	BC
			POP	BC
			LD	BC, (_scratchpad)
			RET

; Write data to a file
; HLU: Pointer to a FIL struct
; DEU: Pointer to the data to write out
; BCU: Number of bytes to write
; Returns:
;   A: FRESULT
; BCU: Number of bytes written
;
ffs_api_fwrite:		LD	A, MB		; A: MB
			OR	A, A 		; Check whether MB is 0, i.e. in 24-bit mode
			JR	Z, $F		; It is, so skip as all addresses can be assumed to be 24-bit
			CALL 	SET_ADE24	; Convert DE to an address in segment A (MB)
			CALL	GET_AHL24	; Get MSB of HL
			OR	A, A 		; Does it already contain a value? (fetched using mos_api_getfil?)
			LD	A, MB		; A: MB
			CALL	Z, SET_AHL24	; No it's zero, so convert HL to an address in segment A (MB)
;
$$:			PUSH	HL
			LD	HL, _scratchpad
			EX	(SP), HL	; UINT * bw
			PUSH	BC		; UINT btw
			PUSH	DE		; void * buff
			PUSH	HL		; FILE * fp
			CALL	_f_write
			LD	A, L 		; FRESULT
			POP	HL
			POP	DE
			POP	BC
			POP	BC
			LD	BC, (_scratchpad)
			RET

; Check file exists
; HLU: Pointer to a FILINFO struct
; DEU: Pointer to the filename (0 terminated)
; Returns:
;   A: FRESULT
;
ffs_api_stat:		LD	A, MB		; A: MB
			OR	A, A 		; Check whether MB is 0, i.e. in 24-bit mode
			JR	Z, $F		; It is, so skip as all addresses can be assumed to be 24-bit
			CALL 	SET_ADE24	; Convert DE to an address in segment A (MB)
			CALL	GET_AHL24	; Get MSB of HL
			OR	A, A 		; Does it already contain a value? (fetched using mos_api_getfil?)
			LD	A, MB		; A: MB
			CALL	Z, SET_AHL24	; No it's zero, so convert HL to an address in segment A (MB)
;
$$:			PUSH	HL		; FILEINFO * fil
			PUSH	DE		; const TCHAR * path
			CALL	_f_stat
			LD	A, L 		; FRESULT
			POP	DE
			POP	HL
			RET

; Check for EOF
; HLU: Pointer to a FILINFO struct
; Returns:
;   A: 1 if end of file, otherwise 0
;
ffs_api_feof:		LD	A, MB
			OR	A, A
			JR	Z, $F
			CALL	GET_AHL24
			OR 	A, A
			LD	A, MB
			CALL	Z, SET_AHL24
;
$$:			PUSH	HL		; FILEINFO * fil
			CALL	_fat_EOF
			LD	A, L 		; EOF
			POP	HL
			RET

; Move the read/write pointer in a file
; HLU: Pointer to a FIL struct
; DEU: Least significant 3 bytes of the offset from the start of the file (DWORD)
;   C: Most significant byte of the offset
; Returns:
;   A: FRESULT
;
ffs_api_flseek:		LD	A, MB
			OR	A, A
			JR	Z, $F
			CALL	GET_AHL24
			OR 	A, A
			LD	A, MB
			CALL	Z, SET_AHL24
;
$$:			PUSH	BC 		; FSIZE_t ofs (msb)
			PUSH	DE		; FSIZE_t ofs (lsw)
			PUSH	HL		; FIL * fp
			CALL	_f_lseek
			LD	A, L
			POP	HL
			POP	DE
			POP	BC
			RET

; Truncate a file
; HLU: Pointer to a FIL struct
; Returns:
;   A: FRESULT
;
ffs_api_ftruncate:
			LD	A, MB
			OR	A, A
			JR	Z, $F
			CALL	GET_AHL24
			OR 	A, A
			LD	A, MB
			CALL	Z, SET_AHL24
;
$$:			PUSH	HL		; FIL * fp
			CALL	_f_truncate
			LD	A, L
			POP	HL
			RET

;
; Commands that have not been implemented yet
;
ffs_api_fsync:
			JP mos_api_not_implemented
ffs_api_fforward:
			JP mos_api_not_implemented
ffs_api_fexpand:
			JP mos_api_not_implemented
ffs_api_fgets:
			JP mos_api_not_implemented
ffs_api_fputc:
			JP mos_api_not_implemented
ffs_api_fputs:
			JP mos_api_not_implemented
ffs_api_fprintf:
			JP mos_api_not_implemented
ffs_api_ftell:
			JP mos_api_not_implemented
ffs_api_fsize:
			JP mos_api_not_implemented
ffs_api_ferror:
			JP mos_api_not_implemented

; Open a directory
; HLU: Pointer to a blank DIR struct
; DEU: Pointer to the directory path
; Returns:
; A: FRESULT
ffs_api_dopen:		LD	A, MB		; A: MB
			OR	A, A 		; Check whether MB is 0, i.e. in 24-bit mode
			JR	Z, $F		; It is, so skip as all addresses can be assumed to be 24-bit
			CALL 	SET_ADE24	; Convert DE to an address in segment A (MB)
			CALL	SET_AHL24	; Convert HL to an address in segment A (MB)
$$:
			PUSH	DE 		; const TCHAR *path
			PUSH    HL		; DIR *dp
			CALL	_f_opendir
			LD	A, L		; FRESULT
			POP	HL
			POP	DE
			RET

; Close a directory
; HLU: Pointer to an open DIR struct
; Returns:
; A: FRESULT
ffs_api_dclose:		LD	A, MB		; A: MB
			OR	A, A 		; Check whether MB is 0, i.e. in 24-bit mode
			JR	Z, $F		; It is, so skip as all addresses can be assumed to be 24-bit
			CALL	SET_AHL24	; Convert HL to an address in segment A (MB)
$$:
			PUSH    HL		; DIR *dp
			CALL	_f_closedir
			LD	A, L		; FRESULT
			POP	HL
			RET

; Read the next FILINFO from an open DIR
; HLU: Pointer to an open DIR struct
; DEU: Pointer to an empty FILINFO struct
; Returns:
; A: FRESULT
ffs_api_dread:		LD	A, MB		; A: MB
			OR	A, A 		; Check whether MB is 0, i.e. in 24-bit mode
			JR	Z, $F		; It is, so skip as all addresses can be assumed to be 24-bit
			CALL 	SET_ADE24	; Convert DE to an address in segment A (MB)
			CALL	SET_AHL24	; Convert HL to an address in segment A (MB)
$$:
			PUSH	DE 		; FILINFO *fno
			PUSH    HL		; DIR *dp
			CALL	_f_readdir
			LD	A, L		; FRESULT
			POP	HL
			POP	DE
			RET

ffs_api_dfindfirst:
			JP mos_api_not_implemented
ffs_api_dfindnext:
			JP mos_api_not_implemented
ffs_api_unlink:
			JP mos_api_not_implemented
ffs_api_rename:
			JP mos_api_not_implemented
ffs_api_chmod:
			JP mos_api_not_implemented
ffs_api_utime:
			JP mos_api_not_implemented
ffs_api_mkdir:
			JP mos_api_not_implemented
ffs_api_chdir:
			JP mos_api_not_implemented
ffs_api_chdrive:
			JP mos_api_not_implemented
; Copy the current directory (string) into buffer (hl)
; HLU: Pointer to a buffer
; BCU: Maximum length of buffer
; Returns:
; A: FRESULT
ffs_api_getcwd:		LD	A, MB		; A: MB
			OR	A, A 		; Check whether MB is 0, i.e. in 24-bit mode
			JR	Z, $F		; It is, so skip as all addresses can be assumed to be 24-bit
			CALL	SET_AHL24	; Convert HL to an address in segment A (MB)
$$:
			PUSH	BC 		; sizeof(buffer)
			PUSH    HL		; buffer
			CALL	_f_getcwd
			LD	A, L		; FRESULT
			POP	HL
			POP	BC
			RET

ffs_api_mount:
			JP mos_api_not_implemented
ffs_api_mkfs:
			JP mos_api_not_implemented
ffs_api_fdisk
			JP mos_api_not_implemented
ffs_api_getfree:
			JP mos_api_not_implemented
ffs_api_getlabel:
			JP mos_api_not_implemented
ffs_api_setlabel:
			JP mos_api_not_implemented
ffs_api_setcp:
			JP mos_api_not_implemented
