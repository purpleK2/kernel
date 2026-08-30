#ifndef IO_H
#define IO_H

#include <stdint.h>

/*
 * Sends an 8-bit value (byte) to a port.
 */
extern void _outb(uint16_t port, uint8_t val);
/*
 * Sends a 16-bit value (word) to a port.
 */
extern void _outw(uint16_t port, uint16_t val);
/*
 * Sends a 32-bit value (doubleword) to a port.
 */
extern void _outd(uint16_t port, uint32_t val);

/*
 * Reads an 8-bit value (byte) from a port.
 */
extern uint8_t _inb(uint16_t port);
/*
 * Reads a 16-bit value (word) from a port.
 */
extern uint16_t _inw(uint16_t port);
/*
 * Reads a 32-bit value (doubleword) from a port.
 */
extern uint32_t _ind(uint16_t port);

# define IO_WAIT()  _outb(0x80, 0)

#endif
