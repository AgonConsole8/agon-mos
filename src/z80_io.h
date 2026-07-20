#ifndef Z80_IO_H
#define Z80_IO_H

#include <stdint.h>

/**
 * The following magical incantations: __attribute__((address_space(3))
 * cause the emission of IO in/out instructions.
 */

static inline uint8_t io_in(int addr)
{
	return *((volatile uint8_t __attribute__((address_space(3)))*)addr);
}

static inline void io_out(int addr, uint8_t value)
{
	*((volatile uint8_t __attribute__((address_space(3)))*)addr) = value;
}

static inline void io_setreg(int addr, uint8_t bits)
{
	io_out(addr, io_in(addr) | bits);
}

static inline void io_resetreg(int addr, uint8_t bits)
{
	io_out(addr, io_in(addr) & (0xff ^ bits));
}

#endif /* Z80_IO_H */
