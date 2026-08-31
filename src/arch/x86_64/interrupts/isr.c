#include <isr.h>

#include <idt.h>
#include <cpu.h>

#include <macro.h>

#include <stdint.h>
#include <stdio.h>

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

            debugf(ANSI_COLOR_BLUE);
            debugf("PANIC: Exception %#llx\n", ctx->interrupt);
            debugf("\terrcode=%#llx\n", ctx->error);
            debugf(ANSI_COLOR_RESET);

            _hcf();
        case 32 ... 255:
            debugf_warn("Unhandled interrupt %#llx\n", ctx->interrupt);
    }
}
