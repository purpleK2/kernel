#ifndef PMM_H
#define PMM_H

#include <stddef.h>
#include <stdint.h>

#include <limine.h>

#include <datatypes/llist.h>

#define PAGESZ  0x1000      // 4KiB page frames

struct pmm {
    uint64_t limine_hhdm_offset;
    struct ll_node* head;
    // stats
    size_t total_usable_mem;
    size_t used_mem;
};

/*
 * Get the physical address from a virtual HH-directly-mapped one.
 * @param virt virtual HH-directly-mapped address
 * @returns the virtual address minus the HHDM offset, if virt is larger than it.
 */
uintptr_t hhdm_physical(uintptr_t virt);
/*
 * Get the HH-directly-mapped address of a physical one.
 * @param phys physical address
 * @returns the physical address plus the HHDM offset. It's not guaranteed for it to be present in the page tables.
 */
uintptr_t hhdm_virtual(uintptr_t phys);

/*
 * Prints PMM stats.
 */
void print_pmm_stats();

/*
 * Initialize the required PMM data.
 * @param memmap Limine's memmap response structure.
 * @param limine_hhdm_offset Limine's HHDM offset, used for reading/writing to structs in physical memory.
 */
void pmm_init(LIMINE_PTR(struct limine_memmap_response*) memmap, uint64_t limine_hhdm_offset);

/*
 * Allocates a multiple of 4KiB physical pages.
 * @param pages number of pages to allocate
 * @returns a (PHYSICAL) pointer to the allocated region.
 */
void* palloc(size_t pages);
/*
 * Reclaim a physicall allocated region
 * @param p pointer to the (PHYSICALLY) allocated region
 * @param pages number of pages to reclaim
 */
void pfree(void* p, size_t pages);
#endif
