#ifndef GDT_H
#define GDT_H

#include <macro.h>

#include <stdint.h>

#define GDT_CODE_SEGMENT      0x08
#define GDT_DATA_SEGMENT      0x10
#define GDT_USER_CODE_SEGMENT 0x18
#define GDT_USER_DATA_SEGMENT 0x20

#define GDT_LIMIT_LOW(limit)                (limit & 0xFFFF)
#define GDT_BASE_LOW(base)                  (base & 0xFFFF)
#define GDT_BASE_MIDDLE(base)               ((base >> 16) & 0xFF)
#define GDT_FLAGS_HI_LIMIT(limit, flags)    (((limit >> 16) & 0xF) | ((flags << 4) & 0xF0))
#define GDT_BASE_HIGH(base)                 ((base >> 24) & 0xFF)
#define GDT_ENTRY(base, limit, access, flags)                                  \
    {GDT_LIMIT_LOW(limit),                                                     \
     GDT_BASE_LOW(base),                                                       \
     GDT_BASE_MIDDLE(base),                                                    \
     access,                                                                   \
     GDT_FLAGS_HI_LIMIT(limit, flags),                                         \
     GDT_BASE_HIGH(base)}

struct gdt_entry {
    uint16_t limit_low;           // limit & 0xFF
    uint16_t base_low;            // base & 0xFF
    uint8_t base_middle;          // (base >> 16) & 0xFF
    uint8_t access;               // access
    uint8_t limit_high_and_flags; // ((limit >> 16) & 0xF) | (flags & 0xF0)
    uint8_t base_high;            // (base >> 24) & 0xF
} PACKED;

struct gdtr {
    uint16_t size;
    uint64_t pointer;
} PACKED;

/*
 * Loads the given GDT pointer struct
 * @param gdtr address of the GDT pointer
 */
extern void _lgdt(struct gdtr *gdtr);
/*
 * Loads segment selectors
 * @param cs code segment
 * @param ds data segment
 */
extern void _reload_segments(uint64_t cs, uint64_t ds);

/*
 * Initialize and load a default GDT
 */
void gdt_init();

#endif
