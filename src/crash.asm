	XDEF		_on_crash

	.ASSUME ADL = 1

PUTS:	MACRO str
	ld hl,str
	ld bc,0
	xor a
	rst.lil 018h
	ENDMACRO

PUTC:	MACRO chr
	ld a,chr
	rst.lil 010h
	ENDMACRO

_on_crash:
	di
	push iy
	push ix
	push hl
	push de
	push bc
	push af

	PUTC 17
	PUTC 129
	PUTC 17
	PUTC 0

	PUTS _debug_intro

	ld ix,0
	add ix,sp

	PUTS _debug_af
	ld hl,(ix+0)
	call print_hex24

	PUTS _debug_bc
	ld hl,(ix+3)
	call print_hex24

	PUTS _debug_de
	ld hl,(ix+6)
	call print_hex24

	PUTS _debug_hl
	ld hl,(ix+9)
	call print_hex24

	call print_crlf

	PUTS _debug_ix
	ld hl,(ix+12)
	call print_hex24

	PUTS _debug_iy
	ld hl,(ix+15)
	call print_hex24

	call print_crlf

	PUTS _debug_pc
	ld hl,(ix+18)
	call print_hex24

	PUTS _debug_spl
	lea hl,ix-18
	call print_hex24

	PUTS _debug_sps
	ld hl,0
	add.s hl,sp
	call print_hex24

	PUTS _debug_mb
	ld a,mb
	call print_hex8

	call print_crlf

	PUTS _debug_stack
	; dump top of stack
	ld hl,18
	add hl,sp
	ld b, 32
$$:
	ld a, b
	and 7
	call z, print_crlf

	ld a,(hl)
	inc hl
	call print_hex8
	ld a, ' '
	rst.lil 010h
	djnz $B

	call print_crlf
	PUTS _debug_outro
	PUTC 17
	PUTC 128
	PUTC 17
	PUTC 15

	ei 		; enable interrupts so user can make keypresses
$$:	xor a 		; wait for 'r'esume
	rst.lil 08h
	cp 'r'
	jr z, $F
	cp 'R'
	jr z, $F
	jr $B

	; restore everything and return to userspace
$$:	pop af
	pop bc
	pop de
	pop hl
	pop ix
	pop iy
	ret.lil

print_hex8:
	push af
	push bc
	ld b, a
	srl a
	srl a
	srl a
	srl a
	and 0fh
	; print high nibble
	call __print_hex_nibble
	ld a, b
	and 0fh
	; print low nibble
	call __print_hex_nibble
	pop bc
	pop af
	ret
__print_hex_nibble:
	add a, 48
	cp 58
	jr c, $F
	add a, 39
$$:	rst.lil 010h
	ret

print_hex24: ; print hex u24 in `hl`
	push hl
	push bc
	ld b,l
	push hl
	inc sp
	pop hl
	dec sp
	ld a,h
	call print_hex8
	ld a,l
	call print_hex8
	ld a,b
	call print_hex8
	pop bc
	pop hl
	ret

print_crlf:
	ld a,13
	rst.lil 010h
	ld a,10
	rst.lil 010h
	ret

_debug_intro: db "== RST $38 panic. Guru meditation    ==",13,10,0
_debug_outro: db "== [r] resume, [ctrl-alt-del] reboot ==",13,10,0
_debug_stack: db "SPL stack top: ",0
_debug_mb: db " MB:",0
_debug_pc: db "PC:",0
_debug_spl: db " SPL:",0
_debug_sps: db " SPS:",0
_debug_af: db "AF:",0
_debug_bc: db " BC:",0
_debug_de: db " DE:",0
_debug_hl: db " HL:",0
_debug_ix: db "IX:",0
_debug_iy: db " IY:",0
