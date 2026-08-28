#ifndef CPU_H
#define CPU_H

/*
 * Halt and catch fire. We should NOT return from this function.
 */
__attribute__((noreturn)) extern void _hcf();

/*
 * Disable interrupts.
 */
extern void _disable_interrupts();
/*
 * Enable interrupts.
 */
extern void _enable_interrupts();

#endif
