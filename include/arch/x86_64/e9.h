#ifndef DEBUG_H
#define DEBUG_H

/*
 * Initialize the E9 port driver. Supposes we're running on QEMU
 * or any emulator that supports the E9 port output.
 */
void e9_init();

/*
 * Prints a character to E9 port.
 * @param c the character to print
 * @param ctx unused.
 */
void dputc(int c, void* ctx);

#endif
