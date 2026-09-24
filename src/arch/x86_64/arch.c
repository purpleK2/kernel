#include <arch.h>
#include <gdt.h>
#include <idt.h>
#include <cpuid.h>

#include <sinks.h>
#include <kprintf.h>
#include <debug.h>
#include <memory.h>

static const struct sink e9_sink = {
    .putc = dputc,
    .level_mask = LOG_INFO | LOG_WARN | LOG_CRIT | LOG_TRACE
};

void arch_entry() {
    struct cpuid_ctx ctx;
    _cpuid(0x40000000, &ctx);
    switch (ctx.ebx) {
        case 0x54474354:    // "TGCT", QEMU vendor little-endian
            register_sink(&e9_sink);
            kprintf_trace("E9 Port detected\n");
            break;
        default:
            break;
    }


    gdt_init();
    kprintf_ok("GDT setup\n");

    idt_init();
    kprintf_ok("IDT setup\n");
}
