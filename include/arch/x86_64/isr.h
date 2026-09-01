#ifndef ISR_H
#define ISR_H

#include <stdint.h>

enum EXCEPTIONS {
    EXC_DE = 0,
    EXC_DB = 1,
    EXC_BP = 3,
    EXC_OF = 4,
    EXC_BR = 5,
    EXC_UD = 6,
    EXC_NM = 7,
    EXC_DF = 8,
    EXC_TS = 10,
    EXC_NP = 11,
    EXC_SS = 12,
    EXC_GP = 13,
    EXC_PF = 14,
    EXC_MF = 16,
    EXC_AC = 17,
    EXC_MC = 18,
    EXC_XM = 19,
    EXC_VE = 20,
    EXC_CP = 21
};

struct interrupt_ctx {
    uint64_t ds;
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;

    uint64_t interrupt;
    uint64_t error;

    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    // these are pushed when CPL changes
    uint64_t rsp;
    uint64_t ss;
} PACKED;

typedef void (*isr_handler_t)(struct interrupt_ctx*);

/*
 * Install a handler when an interrupt is fired.
 * @param interrupt the interrupt
 * @param handler the handler to be called when interrupt is fired.
 */
void isr_install_handler(uint16_t interrupt, isr_handler_t handler);

#endif
