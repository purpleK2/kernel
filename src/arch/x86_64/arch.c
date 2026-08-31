#include <arch.h>
#include <gdt.h>

#include <stdio.h>

void arch_entry() {
    gdt_init();
    debugf_ok("GDT setup\n");
}
