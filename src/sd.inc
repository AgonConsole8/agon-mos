;
; Title:	AGON MOS - Low level SD card assembler defines
; Author:	Leigh Brown
; Created:	28/05/2023
; Last Updated:	28/05/2023
;
; Modinfo:


CMD0:			.EQU        0
CMD0_ARG:		.EQU    0x00000000
CMD0_CRC:		.EQU    0x94

CMD8:			.EQU        8
CMD8_ARG:		.EQU    0x0000001AA
CMD8_CRC:		.EQU    0x86 ;(1000011 << 1)

CMD17:			.EQU       17
CMD17_CRC:		.EQU   0x00

CMD24:			.EQU       24
CMD24_CRC:		.EQU   0x00

CMD55:			.EQU       55
CMD55_ARG:		.EQU   0x00000000
CMD55_CRC:		.EQU   0x00

CMD58:			.EQU       58
CMD58_ARG:		.EQU   0x00000000
CMD58_CRC:		.EQU   0x00

ACMD41:			.EQU      41
ACMD41_ARG:		.EQU  0x40000000
ACMD41_CRC:		.EQU  0x00

