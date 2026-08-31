#ifndef IDT_H
#define IDT_H

#include <stdint.h>

#include <macro.h>

#define IDT_MAX_DESCRIPTORS 256

enum IDT_FLAGS {
    IDT_FLAG_GATE_TASK       = 0x5,
    IDT_FLAG_GATE_16BIT_INT  = 0x6,
    IDT_FLAG_GATE_16BIT_TRAP = 0x7,
    IDT_FLAG_GATE_32BIT_INT  = 0xE,
    IDT_FLAG_GATE_32BIT_TRAP = 0xF,

    IDT_FLAG_RING0 = (0 << 5),
    IDT_FLAG_RING1 = (1 << 5),
    IDT_FLAG_RING2 = (2 << 5),
    IDT_FLAG_RING3 = (3 << 5),

    IDT_FLAG_PRESENT = 0x80,

};

#define IDT_BASE_LOW(b)     (b & 0xFFFF)
#define IDT_BASE_MID(b)     ((b >> 16) & 0xFFFF)
#define IDT_BASE_HIGH(b)     ((b >> 32) & 0xFFFFFFFF)

#define IDT_ENTRY(base, sel, flags) \
    {IDT_BASE_LOW(base),            \
     sel,                           \
     0,                             \
     flags,                         \
     IDT_BASE_MID(base),            \
     IDT_BASE_HIGH(base),           \
     0}

struct idt_entry {
    uint16_t base_low;  // The lower 16 bits of the ISR's address
    uint16_t kernel_cs; // The GDT segment selector that the CPU will load into CS before calling the ISR
    uint8_t ist;        // The IST in the TSS that the CPU will load into RSP; set to zero for now
    uint8_t flags;      // Type and attributes; see the IDT page
    uint16_t base_mid;  // The higher 16 bits of the lower 32 bits of the ISR's address
    uint32_t base_high; // The higher 32 bits of the ISR's address
    uint32_t reserved;  // Set to zero
} PACKED;

struct idtr {
    uint16_t limit;
    uint64_t base;
} PACKED;

/*
 * Loads an IDT pointer.
 * @param idtr the IDT pointer
 */
extern void _lidt(struct idtr* idtr);

/*
 * Initialize and load a default IDT.
 */
void idt_init();

#endif
