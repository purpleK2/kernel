#include "stdio.h"
#include <idt.h>
#include <gdt.h>
#include <cpu.h>

#include <macro.h>

#include <stdint.h>

ALIGNED(0x10) static struct idt_entry idt[IDT_MAX_DESCRIPTORS];
static struct idtr idtr;

extern void* isr_stub_table[];

void idt_init() {
    for (uint16_t vector = 0; vector < IDT_MAX_DESCRIPTORS; vector++) {
        idt[vector] = (struct idt_entry)IDT_ENTRY((uint64_t)isr_stub_table[vector], GDT_CODE_SEGMENT, IDT_FLAG_PRESENT | IDT_FLAG_GATE_32BIT_INT);
    }

    idtr.base = (uint64_t)&idt[0];
    idtr.limit = ((uint16_t)sizeof(struct idt_entry) * IDT_MAX_DESCRIPTORS) - 1;

    debugf_trace("Loading IDTR @ %#p\n", &idtr);

    _lidt(&idtr);
    _enable_interrupts();
}
