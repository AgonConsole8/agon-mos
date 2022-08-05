;
; Title:	AGON MOS - API code
; Author:	Dean Belfield
; Created:	24/07/2022
; Last Updated:	03/08/2022
;
; Modinfo:
; 03/08/2022:	Added a handful of MOS API calls and stubbed FatFS calls

			.ASSUME	ADL = 1
			
			DEFINE .STARTUP, SPACE = ROM
			SEGMENT .STARTUP
			
			XDEF	mos_api		

			XREF	SWITCH_A
			XREF	SET_AHL24
			XREF	SET_ADE24

			XREF	_mos_EDITLINE
			
			XREF	_mos_LOAD
			XREF	_mos_SAVE
			XREF	_mos_CD
			XREF	_mos_DIR
			XREF	_mos_DEL
			XREF	_mos_FOPEN
			XREF	_mos_FCLOSE
			XREF	_mos_FGETC
			XREF	_mos_FPUTC

			XREF	_keycode
			XREF	_clock
			
; Call a MOS API function
; 00h - 7Fh: Reserved for high level MOS calls
; 80h - FFh: Reserved for low level calls to FatFS
;  A: function to call
;
mos_api:		CP	80h			; Check if it is a FatFS command
			JR	NC, $F			; Yes, so jump to next block
			CALL	SWITCH_A		; Switch on this table
			DW	mos_api_getkey		; 0x00
			DW	mos_api_load		; 0x01
			DW	mos_api_save		; 0x02
			DW	mos_api_cd		; 0x03
			DW	mos_api_dir		; 0x04
			DW	mos_api_del		; 0x05
			DW	mos_api_sysvars		; 0x06
			DW	mos_api_editline	; 0x07
			DW	mos_api_fopen		; 0x08
			DW	mos_api_fclose		; 0x09
			DW	mos_api_fgetc		; 0x0A
			DW	mos_api_fputc		; 0x0B
;			
$$:			AND	7Fh			; Else remove the top bit
			CALL	SWITCH_A		; And switch on this table
			DW	ffs_api_fopen
			DW	ffs_api_fclose
			DW	ffs_api_fread
			DW	ffs_api_fwrite
			DW	ffs_api_fseek
			DW	ffs_api_ftruncate
			DW	ffs_api_fsync
			DW	ffs_api_fforward
			DW	ffs_api_fexpand
			DW	ffs_api_fgets
			DW	ffs_api_fputc
			DW	ffs_api_fputs
			DW	ffs_api_fprintf
			DW	ffs_api_ftell
			DW	ffs_api_feof
			DW	ffs_api_fsize
			DW	ffs_api_ferror
			DW	ffs_api_dopen
			DW	ffs_api_dclose
			DW	ffs_api_dread
			DW	ffs_api_dfindfirst
			DW	ffs_api_dfindnext
			DW	ffs_api_stat
			DW	ffs_api_unlink
			DW	ffs_api_rename
			DW	ffs_api_chmod
			DW	ffs_api_utime
			DW	ffs_api_mkdir
			DW	ffs_api_chdir
			DW	ffs_api_chdrive
			DW	ffs_api_getcwd
			DW	ffs_api_mount
			DW	ffs_api_mkfs
			DW	ffs_api_fdisk		
			DW	ffs_api_getfree
			DW	ffs_api_getlabel
			DW	ffs_api_setlabel
			DW	ffs_api_setcp

; Get keycode
; Returns:
;  A: ASCII code of key pressed, or 0 if no key pressed
;
mos_api_getkey:		LD	A, (_keycode)
			PUSH	AF
			XOR	A
			LD	(_keycode), A
			POP	AF
			RET

; Load an area of memory from a file.
; HLU: Address of filename (zero terminated)
; DEU: Address at which to load
; BCU: Maximum allowed size (bytes)
; Returns:
; - Carry reset indicates no room for file.
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
			CALL	_mos_LOAD	; Call the C function mos_LOAD
			POP	HL
			POP	DE
			POP	BC
			SCF			; Flag as successful
			RET

; Save a file to the SD card from RAM
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
; Finally, we can do the load
;
$$:			PUSH	BC		; UINT24   size
			PUSH	DE		; UNIT24   address
			PUSH	HL		; char   * filename
			CALL	_mos_SAVE	; Call the C function mos_LOAD
			POP	HL
			POP	DE
			POP	BC
			SCF			; Flag as successful
			RET
			
; Change directory
; HLU: Address of path (zero terminated)
;			
mos_api_cd:		LD	A, MB		; Check if MBASE is 0
			OR	A, A
			JR	Z, $F		; If it is, we can assume HL and DE are 24 bit
;
; Now we need to mod HLU to include the MBASE in the U byte
;
			CALL	SET_AHL24
;
; Finally, we can do the load
;
$$:			PUSH	HL		; char   * filename	
			CALL	_mos_CD
			POP	HL
			RET

; Directory listing
;	
mos_api_dir:		PUSH	HL
			CALL	_mos_DIR
			POP	HL
			RET
			
; Delete a file from the SD card
; HLU: Address of filename (zero terminated)
;
mos_api_del:		LD	A, MB		; Check if MBASE is 0
			OR	A, A
			JR	Z, $F		; If it is, we can assume HL and DE are 24 bit
;
; Now we need to mod HLU to include the MBASE in the U byte
;
			CALL	SET_AHL24
;
; Finally, we can do the load
;
$$:			PUSH	HL		; char   * filename
			CALL	_mos_DEL	; Call the C function mos_DEL
			POP	HL
			SCF			; Flag as successful
			RET
	

; Get a pointer to a system variable
;   C: System variable to return
; Returns:
; IXU: Pointer to system variable
;
mos_api_sysvars:	LD	IX, _clock
			RET
			
; Invoke the line editor
; HLU: Address of the buffer
; BCU: Buffer length
; Returns:
;   A: Key that was used to exit the input loop (CR=13, ESC=27)
;
mos_api_editline:	LD	A, MB		; Check if MBASE is 0
			OR	A, A
			JR	Z, $F		; If it is, we can assume HL and DE are 24 bit
;
; Now we need to mod HLU to include the MBASE in the U byte
;
			CALL	SET_AHL24
;
$$:			PUSH	BC		; int 	  bufferLength
			PUSH	HL		; char	* buffer
			CALL	_mos_EDITLINE
			LD	A, L		; return value, only interested in lowest byte
			POP	HL
			POP	BC
			RET

; Open a file
; HLU: Filename
;   C: Mode
; Returns:
;   A: Filehandle, or 0 if couldn't open
;
mos_api_fopen:		LD	A, MB		; Check if MBASE is 0
			OR	A, A
			JR	Z, $F		; If it is, we can assume HL and DE are 24 bit
;
; Now we need to mod HLU and DEU to include the MBASE in the U byte
;
			CALL	SET_AHL24
;
$$:			LD	A, C	
			LD	BC, 0
			LD	C, A
			PUSH	BC		; byte	  mode
			PUSH	HL		; char	* buffer
			CALL	_mos_FOPEN
			LD	A, L		; Return fh
			POP	HL
			POP	BC			
			RET

; Close a file
;   C: Filehandle
; Returns
;   A: Number of files still open
;
mos_api_fclose:		LD	A, C
			LD	BC, 0
			LD	C, A
			PUSH	BC		; byte 	  fh
			CALL	_mos_FCLOSE
			LD	A, L		; Return # files still open
			POP	BC
			RET
			
; Get a character from a file
;   C: Filehandle
; Returns:
;   A: Character read
;   F: C set if last character in file, otherwise NC
;
mos_api_fgetc:		LD	DE, 0
			LD	E, C
			PUSH	DE		; byte	  fh
			CALL	_mos_FGETC
			POP	BC
			OR	A		; TODO: Need to set C if EOF
			RET
	
; Write a character to a file
;   C: Filehandle
;   B: Character to write
;
mos_api_fputc:		LD	DE, 0
			LD	E, B		
			PUSH	DE		; byte	  char
			LD	E, C
			PUSH	DE		; byte	  fh
			CALL	_mos_FPUTC
			POP	DE
			POP	DE
			RET
			

; Commands that have not been implemented yet
;
ffs_api_fopen:
ffs_api_fclose:
ffs_api_fread:		
ffs_api_fwrite:		
ffs_api_fseek:		
ffs_api_ftruncate:	
ffs_api_fsync:		
ffs_api_fforward:	
ffs_api_fexpand:	
ffs_api_fgets:		
ffs_api_fputc:		
ffs_api_fputs:		
ffs_api_fprintf:	
ffs_api_ftell:		
ffs_api_feof:		
ffs_api_fsize:		
ffs_api_ferror:		
ffs_api_dopen:		
ffs_api_dclose:		
ffs_api_dread:		
ffs_api_dfindfirst:	
ffs_api_dfindnext:	
ffs_api_stat:		
ffs_api_unlink:		
ffs_api_rename:		
ffs_api_chmod:		
ffs_api_utime:		
ffs_api_mkdir:		
ffs_api_chdir:		
ffs_api_chdrive:	
ffs_api_getcwd:		
ffs_api_mount:		
ffs_api_mkfs:		
ffs_api_fdisk		
ffs_api_getfree:	
ffs_api_getlabel:	
ffs_api_setlabel:	
ffs_api_setcp:		
			RET