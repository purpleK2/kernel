#include <arch.h>
#include <gdt.h>
#include <idt.h>
#include <cpuid.h>

#include <e9.h>
#include <kprintf.h>
#include <memory.h>

void arch_entry() {
    struct cpuid_ctx ctx;
    _cpuid(CPUID_LEAF_VENDOR, &ctx);
    switch (ctx.ebx) {
        case CPUID_VENDOR_QEMU:
            e9_init();
            kprintf_ok("E9 port\n");
            break;
        default:
            break;
    }

    gdt_init();
    kprintf_ok("GDT setup\n");

    idt_init();
    kprintf_ok("IDT setup\n");
}
