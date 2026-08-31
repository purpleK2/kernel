#include <gdt.h>
#include <tss.h>

#include <stdint.h>
#include <stdio.h>

static struct gdtr gdtr;
static struct gdt_entry gdt[6];
static struct tss tss;

void gdt_init() {
    gdt[0] = (struct gdt_entry)GDT_ENTRY(0, 0, 0, 0);
    gdt[1] = (struct gdt_entry)GDT_ENTRY(0, 0xFFFFF, 0x9A, 0xA);    // 64-bit kernel code
    gdt[2] = (struct gdt_entry)GDT_ENTRY(0, 0xFFFFF, 0x92, 0xC);    // 64-bit kernel data
    gdt[3] = (struct gdt_entry)GDT_ENTRY(0, 0xFFFFF, 0xFA, 0xA);    // 64-bit user code
    gdt[4] = (struct gdt_entry)GDT_ENTRY(0, 0xFFFFF, 0xF2, 0xC);    // 64-bit user data
    gdt[5] = (struct gdt_entry)GDT_ENTRY((uint64_t)&tss, (sizeof(tss) - 1), 0x89, 0);    // 64-bit TSS

    gdtr.size = sizeof(gdt) - 1;
    gdtr.pointer = (uint64_t)&gdt;
    debugf_trace("Loading GDTR %#p\n", &gdtr);
    _lgdt(&gdtr);
    _reload_segments(GDT_CODE_SEGMENT, GDT_DATA_SEGMENT);
}
