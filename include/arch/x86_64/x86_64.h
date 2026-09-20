/*
 * General x86_64 related declarations that don't fit anywhere else.
 * Actual source for functions may sit in sources like cpu.asm and the likes.
 */

#ifndef X86_64_H
#define X86_64_H

#include <stdint.h>

/*
 * Get the value of CR3.
 * @returns the value of CR3
 * @notes check cpu.asm for implementation
 */
extern uint64_t _get_cr3();

/*
 * Sets an MSR value.
 * @param m the MSR to write onto
 * @param v the value to write onto the MSR
 * @notes check cpu.asm for implementation
 */
extern void _wrmsr(uint64_t m, uint64_t v);
/*
 * Read an MSR.
 * @param m the MSR to read
 * @returns the value in the read MSR
 * @notes check cpu.asm for implementation
 */
extern uint64_t _rdmsr(uint64_t m);

#endif
