#include <isr.h>

#include <idt.h>
#include <cpu.h>

#include <macro.h>

#include <stdint.h>
#include <stdio.h>

static const char* exception_strings[32] = {
    "Divide by zero",
    "Debug",
    "Non-maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "BOUND Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-segment Fault",
    "General Protection",
    "Page Fault",
    "[INTEL_RESERVED]",
    "x87 FPU Floating-Point Error",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "[INTEL_RESERVED]",
    "[INTEL_RESERVED]",
    "[INTEL_RESERVED]",
    "[INTEL_RESERVED]",
    "[INTEL_RESERVED]",
    "[INTEL_RESERVED]",
    "[INTEL_RESERVED]",
    "[INTEL_RESERVED]",
    "[INTEL_RESERVED]",
    "[INTEL_RESERVED]",
};

static isr_handler_t handlers[IDT_MAX_DESCRIPTORS];

void isr_install_handler(uint16_t interrupt, isr_handler_t handler) {
    if (interrupt >= IDT_MAX_DESCRIPTORS) {
        return;
    }

    handlers[interrupt] = handler;
}

void isr_handler(struct interrupt_ctx* ctx) {
    if (handlers[ctx->interrupt]){
        handlers[ctx->interrupt](ctx);
        return;
    }

    switch (ctx->interrupt) {
        case 0 ... 31:
            _disable_interrupts();
            debugf_panic("Exception %#llx (%s) errcode=%#llx\n", ctx->interrupt, exception_strings[ctx->interrupt], ctx->error);
            _hcf();
        default:
            debugf_warn("Unhandled interrupt %#llx\n", ctx->interrupt);
    }
}
