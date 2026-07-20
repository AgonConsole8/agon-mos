;* Return status codes to read/write caller
RET_OK					.equ	00h	; ok - caller sets this at entry
RET_NORESPONSE			.equ	01h	; address sent, nack received
RET_DATA_NACK			.equ	02h ; data sent, nack received
RET_ARB_LOST			.equ	04h ; arbitration lost
RET_BUS_ERROR			.equ	08h ; Bus error

;* I2C ROLE STATUS
I2C_IDLE				.equ	00h
I2C_MTX					.equ	01h
I2C_MRX					.equ	02h
I2C_SRX					.equ	04h
I2C_STX					.equ	08h

;* I2C_CTL bits
I2C_CTL_IEN				.equ	0b10000000
I2C_CTL_ENAB				.equ	0b01000000
I2C_CTL_STA				.equ	0b00100000
I2C_CTL_STP				.equ	0b00010000
I2C_CTL_IFLG				.equ	0b00001000
I2C_CTL_AAK				.equ	0b00000100
