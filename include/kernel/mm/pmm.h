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
