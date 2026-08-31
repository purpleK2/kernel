#include <isr.h>

#include <macro.h>

#include <stdio.h>

void isr_handler(struct interrupt_ctx* ctx) {
    debugf_trace("Oh some interrupt happened, we have a context @ %#p\n", ctx);
}
