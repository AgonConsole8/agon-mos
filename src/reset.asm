		.assume	adl = 1	

		include "equs.inc"
		include "macros.inc"

		; Reset vector (org $0)
		section .init

		.global _reset
		.global __vector_table

		.org 0
_reset:	
_rst0:		di
		stmix
		; enter ADL (24-bit) mode
		jp.lil __init
	
		.org 8
_rst8:		jp ram_rst_08_handler
	
		.org 0x10
_rst10:		jp ram_rst_10_handler

		.org 0x18
_rst18:		jp ram_rst_18_handler
	
		.org 0x20
_rst20:		jp ram_rst_20_handler
	
		.org 0x28
_rst28:		jp ram_rst_28_handler

		.org 0x30
_rst30:		jp ram_rst_30_handler

		.org 0x38
_rst38:		jp ram_rst_38_handler
	
		.org 0x66
_nmi:		jp ram_nmi_handler

		; ROM descriptor table
		.org	0x6b
		.ascii 	"MOS"
		.db  2			; Descriptor table version
        	.d24 _f_chdir
        	.d24 _f_chdrive
        	.d24 _f_close
        	.d24 _f_closedir
        	.d24 _f_getcwd
        	.d24 _f_getfree
        	.d24 _f_getlabel
        	.d24 _f_gets
        	.d24 _f_lseek
        	.d24 _f_mkdir
        	.d24 _f_mount
        	.d24 _f_open
        	.d24 _f_opendir
        	.d24 _f_printf
        	.d24 _f_putc
        	.d24 _f_puts
        	.d24 _f_read
        	.d24 _f_readdir
        	.d24 _f_rename
        	.d24 _f_setlabel
        	.d24 _f_stat
        	.d24 _f_sync
        	.d24 _f_truncate
        	.d24 _f_unlink
        	.d24 _f_write
		.d24 _SD_readBlocks
		.d24 _SD_writeBlocks

; Interrupt Vector Table
;  - this segment must be aligned on a 256 byte boundary anywhere below
;    the 64K byte boundry
;  - each 2-byte entry is a 2-byte vector address
;
		.org	0x100
__vector_table:
		.dw __1st_jump_table + 0x00
		.dw __1st_jump_table + 0x04
		.dw __1st_jump_table + 0x08
		.dw __1st_jump_table + 0x0c
		.dw __1st_jump_table + 0x10
		.dw __1st_jump_table + 0x14
		.dw __1st_jump_table + 0x18
		.dw __1st_jump_table + 0x1c
		.dw __1st_jump_table + 0x20
		.dw __1st_jump_table + 0x24
		.dw __1st_jump_table + 0x28
		.dw __1st_jump_table + 0x2c
		.dw __1st_jump_table + 0x30
		.dw __1st_jump_table + 0x34
		.dw __1st_jump_table + 0x38
		.dw __1st_jump_table + 0x3c
		.dw __1st_jump_table + 0x40
		.dw __1st_jump_table + 0x44
		.dw __1st_jump_table + 0x48
		.dw __1st_jump_table + 0x4c
		.dw __1st_jump_table + 0x50
		.dw __1st_jump_table + 0x54
		.dw __1st_jump_table + 0x58
		.dw __1st_jump_table + 0x5c
		.dw __1st_jump_table + 0x60
		.dw __1st_jump_table + 0x64
		.dw __1st_jump_table + 0x68
		.dw __1st_jump_table + 0x6c
		.dw __1st_jump_table + 0x70
		.dw __1st_jump_table + 0x74
		.dw __1st_jump_table + 0x78
		.dw __1st_jump_table + 0x7c
		.dw __1st_jump_table + 0x80
		.dw __1st_jump_table + 0x84
		.dw __1st_jump_table + 0x88
		.dw __1st_jump_table + 0x8c
		.dw __1st_jump_table + 0x90
		.dw __1st_jump_table + 0x94
		.dw __1st_jump_table + 0x98
		.dw __1st_jump_table + 0x9c
		.dw __1st_jump_table + 0xa0
		.dw __1st_jump_table + 0xa4
		.dw __1st_jump_table + 0xa8
		.dw __1st_jump_table + 0xac
		.dw __1st_jump_table + 0xb0
		.dw __1st_jump_table + 0xb4
		.dw __1st_jump_table + 0xb8
		.dw __1st_jump_table + 0xbc

; 1st Interrupt Vector Jump Table
;  - this table must reside in the first 64K bytes of memory
;  - each 4-byte entry is a jump to the 2nd jump table plus offset
;
__1st_jump_table:
		JP __2nd_jump_table + 0x00
		JP __2nd_jump_table + 0x04
		JP __2nd_jump_table + 0x08
		JP __2nd_jump_table + 0x0c
		JP __2nd_jump_table + 0x10
		JP __2nd_jump_table + 0x14
		JP __2nd_jump_table + 0x18
		JP __2nd_jump_table + 0x1c
		JP __2nd_jump_table + 0x20
		JP __2nd_jump_table + 0x24
		JP __2nd_jump_table + 0x28
		JP __2nd_jump_table + 0x2c
		JP __2nd_jump_table + 0x30
		JP __2nd_jump_table + 0x34
		JP __2nd_jump_table + 0x38
		JP __2nd_jump_table + 0x3c
		JP __2nd_jump_table + 0x40
		JP __2nd_jump_table + 0x44
		JP __2nd_jump_table + 0x48
		JP __2nd_jump_table + 0x4c
		JP __2nd_jump_table + 0x50
		JP __2nd_jump_table + 0x54
		JP __2nd_jump_table + 0x58
		JP __2nd_jump_table + 0x5c
		JP __2nd_jump_table + 0x60
		JP __2nd_jump_table + 0x64
		JP __2nd_jump_table + 0x68
		JP __2nd_jump_table + 0x6c
		JP __2nd_jump_table + 0x70
		JP __2nd_jump_table + 0x74
		JP __2nd_jump_table + 0x78
		JP __2nd_jump_table + 0x7c
		JP __2nd_jump_table + 0x80
		JP __2nd_jump_table + 0x84
		JP __2nd_jump_table + 0x88
		JP __2nd_jump_table + 0x8c
		JP __2nd_jump_table + 0x90
		JP __2nd_jump_table + 0x94
		JP __2nd_jump_table + 0x98
		JP __2nd_jump_table + 0x9c
		JP __2nd_jump_table + 0xa0
		JP __2nd_jump_table + 0xa4
		JP __2nd_jump_table + 0xa8
		JP __2nd_jump_table + 0xac
		JP __2nd_jump_table + 0xb0
		JP __2nd_jump_table + 0xb4
		JP __2nd_jump_table + 0xb8
		JP __2nd_jump_table + 0xbc

		.data
		; Software-modifiable reset vectors
ram_rst_08_handler:
		jp rst_08_handler
ram_rst_10_handler:
		jp rst_10_handler
ram_rst_18_handler:
		jp rst_18_handler
ram_rst_20_handler:
		jp __default_mi_handler
ram_rst_28_handler:
		jp __default_mi_handler
ram_rst_30_handler:
		jp __default_mi_handler
ram_rst_38_handler:
		jp rst_38_handler
ram_nmi_handler:
		jp __default_nmi_handler
