#include <arch.h>
#include <gdt.h>
#include <idt.h>

#include <stdio.h>

void arch_entry() {
    gdt_init();
    debugf_ok("GDT setup\n");

    idt_init();
    debugf_ok("IDT setup\n");
}
