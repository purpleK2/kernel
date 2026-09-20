/*
 * Arch-agnostic paging functions (likely only mapping/unmapping pages).
 */
#ifndef PAGING_H
#define PAGING_H

#include <limine.h>

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * Creates a page table that contains the necessary mappings for the kernel to work.
 * @param memmap Limine memory map response
 * @param hhdm_offset the HHDM offset for mapping virtual memory.
 * @param ptable_out a pointer to save the root table. Can be NULL.
 */
void ptable_setup(LIMINE_PTR(struct limine_memmap_response*) memmap, uint64_t hhdm_offset, uintptr_t* ptable_out);

/*
 * Maps a physical region. The page entry(ies) should be created with a PRESENT flag already set.
 * @param root_table the root page table (e.g. CR3 register)
 * @param phys physical address
 * @param virt virtual address to be mapped to
 * @param len size of the region (implementation should eventually align it on its own)
 * @param flags flags to be set for all page entries for each level. Implementation should on page entries where those flags are unused/reserved, unset such flags.
 */
void pg_map(uintptr_t root_table, uintptr_t phys, uintptr_t virt, size_t len, size_t flags);

/*
 * Switches the current page table.
 * @param new the new page table
 */
void pg_switch(uintptr_t new);

#endif
