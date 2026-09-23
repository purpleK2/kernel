/*
 * x86_64-specific paging properties and functions.
 * Page flags and arch-specific functions go here.
 */
#ifndef PAGE_H
#define PAGE_H

#include <stdint.h>
#include <stddef.h>

// Page entries flags
// They are by default used by ALL page entries for all the structure's entries (PML5E, PML4E, PDPTE, PDE, PT),
// unless specified otherwise.
#define PG_PRESENT  1
#define PG_WRITE    (1 << 1)
#define PG_USER     (1 << 2)
#define PG_PWT      (1 << 3)
#define PG_PCD      (1 << 4)
#define PG_ACCESSED (1 << 5)
#define PG_DIRTY    (1 << 6)        // used only on the last level of paging: PDPTE for 1GiB pages, PDE for 2MiB, PTE for 4KiB, ignored elsewhere
#define PG_PAT_PS   (1 << 7)        // the PAT bit, for 1GiB or 2MiB pages it's PS (Page Size, PDPTE and PDE respectively)
#define PG_XD       (1ull << 63)    // Instructions shouldn't be fetched here.

#define PG_PAT_LARGE    (1 << 12)   // the PAT bit only in PDPTE/PDE for 1GiB and 2MiB pages respectively

// useful flag helpers
#define PG_KERNEL_READ          PG_PRESENT
#define PG_KERNEL_READ_WRITE    PG_KERNEL_READ | PG_WRITE
#define PG_USER_READ            PG_PRESENT | PG_USER
#define PG_USER_READ_WRITE      PG_USER_READ | PG_WRITE

// PAT
#define PAT_UNCACHEABLE     0ULL
#define PAT_WRITE_COMBINING 1ULL
#define PAT_WRITE_THROUGH   4ULL
#define PAT_WRITE_PROTECTED 5ULL
#define PAT_WRITEBACK       6ULL
#define PAT_UNCACHED        7ULL

#define CPU_PAT_MSR 0x277

#define PMLE_MASK   0x1ff
#define PG_PML5E(a) (((a) >> 48) & PMLE_MASK)   // 5-level only (obviously), bits 56:48
#define PG_PML4E(a) (((a) >> 39) & PMLE_MASK)   // 4 & 5-level, bits 47:39
#define PG_PDPTE(a) (((a) >> 30) & PMLE_MASK)   // 4 & 5, bits 38:30
#define PG_PDE(a)   (((a) >> 21) & PMLE_MASK)   // PAE & 4 & 5, bits 29:21
#define PG_PTE(a)   (((a) >> 12) & PMLE_MASK)   // PAE & 4 & 5, bits 20:12

#define PG_4KIB_ADDR_MASK   0xfffffffff000
#define PG_2MIB_ADDR_MASK   0xfffffff00000
#define PG_1GIB_ADDR_MASK   0xfffff0000000

#define PG_FLAGS_MASK       0xfff

enum page_size {
    PG_4KIB = 0x1000,
    PG_2MIB = 0x200000,
    PG_1GIB = 0x40000000
};

// for future use. Only 4 level paging is supported.
enum paging_lvl {
    PG_4LVL,
    PG_5LVL
};

/* A page size struct */
struct page {
    size_t ps;
    size_t addr_mask;
    /*
     * Pointer to a "map page" function.
     * @param 1 "page size" structure pointer
     * @param 2 root table
     * @param 3 physical address
     * @param 4 virtual address
     * @param 5 flags
     */
    void (*map)(struct page*, uintptr_t, uintptr_t, uintptr_t, size_t);

    struct page* next;
};

/*
 * Get the Page Map Level Entry.
 * @param p pointer to a "page size" structure
 * @param table a Page Map Level Table
 * @param idx the index to access
 * @param flags flags to assign to the entry
 * @returns the PHYSICAL PMLE address (without the flags). If the entry is not PRESENT, an empty one with PG_PRESENT set is created.
 */
uintptr_t pg_get_create_pmle(struct page* p, uintptr_t table, size_t idx, size_t flags);

/*
 * Invalidate a single page in the TLB.
 * @param pg page to invalidate
 */
extern void _invlpg(uint64_t pg);

#endif
