#include "uaccess.h"
#include "memory/pmm/pmm.h"
#include "paging/paging.h"
#include "scheduler/scheduler.h"
#include <string.h>

struct fault_ctx *current_fault_ctx;

static void *user_to_kernel_addr(uint64_t user_addr) {
    pcb_t *pcb = get_current_pcb();
    if (!pcb || !pcb->vmc || !pcb->vmc->pml4_table)
        return NULL;

    uint64_t *pml4_virt = (uint64_t*)PHYS_TO_VIRTUAL(pcb->vmc->pml4_table);
    uint64_t page_offset = user_addr & 0xFFF;
    uint64_t phys = pg_virtual_to_phys(pml4_virt, user_addr);
    
    if (phys == 0)
        return NULL;

    return (void*)(uintptr_t)(PHYS_TO_VIRTUAL(phys) + page_offset);
}

static void *user_to_kernel_addr_for_write(uint64_t user_addr) {
    pcb_t *pcb = get_current_pcb();
    if (!pcb || !pcb->vmc || !pcb->vmc->pml4_table)
        return NULL;

    uint64_t *pml4 = (uint64_t*)PHYS_TO_VIRTUAL(pcb->vmc->pml4_table);
    uint64_t virt = user_addr & ~(uint64_t)(PFRAME_SIZE - 1);
    uint64_t page_offset = user_addr & 0xFFF;
    
    uint64_t pml4_index = PML4_INDEX(virt);
    uint64_t pdp_index  = PDP_INDEX(virt);
    uint64_t pdir_index = PDIR_INDEX(virt);
    uint64_t ptab_index = PTAB_INDEX(virt);
    
    if (!(pml4[pml4_index] & PMLE_PRESENT))
        return NULL;
    
    uint64_t *pdp = (uint64_t *)PHYS_TO_VIRTUAL(PG_GET_ADDR(pml4[pml4_index]));
    if (!(pdp[pdp_index] & PMLE_PRESENT))
        return NULL;
    
    uint64_t *pdir = (uint64_t *)PHYS_TO_VIRTUAL(PG_GET_ADDR(pdp[pdp_index]));
    if (!(pdir[pdir_index] & PMLE_PRESENT))
        return NULL;
    
    uint64_t *ptab = (uint64_t *)PHYS_TO_VIRTUAL(PG_GET_ADDR(pdir[pdir_index]));
    uint64_t entry = ptab[ptab_index];
    
    if (!(entry & PMLE_PRESENT))
        return NULL;
    
    if (entry & PMLE_COW) {
        uint64_t old_phys = PG_GET_ADDR(entry);
        int refcnt = pmm_page_ref_count((void *)old_phys);
        
        if (refcnt <= 1) {
            entry |= PMLE_WRITE;
            entry &= ~PMLE_COW;
            ptab[ptab_index] = entry;
            _invalidate(virt);
        } else {
            void *new_phys = pmm_alloc_page();
            if (!new_phys)
                return NULL;
            
            memcpy((void *)PHYS_TO_VIRTUAL((uint64_t)new_phys),
                   (void *)PHYS_TO_VIRTUAL(old_phys),
                   PFRAME_SIZE);
            
            pmm_page_ref_dec((void *)old_phys);
            
            uint64_t new_flags = (PG_FLAGS(entry) | PMLE_WRITE) & ~PMLE_COW;
            uint64_t new_entry = PG_GET_ADDR((uint64_t)new_phys) | new_flags;
            ptab[ptab_index] = new_entry;
            _invalidate(virt);
        }
    }
    
    uint64_t phys = PG_GET_ADDR(ptab[ptab_index]);
    if (phys == 0)
        return NULL;

    return (void*)(uintptr_t)(PHYS_TO_VIRTUAL(phys) + page_offset);
}

size_t copy_from_user(void *dst, const void *user_src, size_t n) {
    if (!dst || !user_src || n == 0)
        return n;
    
    if (!user_range_ok(user_src, n))
        return n;

    uint8_t *dst_ptr = (uint8_t *)dst;
    uint64_t user_addr = (uint64_t)user_src;
    size_t remaining = n;

    while (remaining > 0) {
        size_t page_offset = user_addr & 0xFFF;
        size_t bytes_in_page = 0x1000 - page_offset;
        size_t to_copy = (remaining < bytes_in_page) ? remaining : bytes_in_page;

        void *kernel_addr = user_to_kernel_addr(user_addr);
        if (!kernel_addr)
            return remaining;

        memcpy(dst_ptr, kernel_addr, to_copy);

        dst_ptr += to_copy;
        user_addr += to_copy;
        remaining -= to_copy;
    }

    return 0;
}

size_t copy_to_user(void *user_dst, const void *src, size_t n) {
    if (!user_dst || !src || n == 0)
        return n;
        
    if (!user_range_ok(user_dst, n))
        return n;

    const uint8_t *src_ptr = (const uint8_t *)src;
    uint64_t user_addr = (uint64_t)user_dst;
    size_t remaining = n;

    while (remaining > 0) {
        size_t page_offset = user_addr & 0xFFF;
        size_t bytes_in_page = 0x1000 - page_offset;
        size_t to_copy = (remaining < bytes_in_page) ? remaining : bytes_in_page;

        void *kernel_addr = user_to_kernel_addr_for_write(user_addr);
        if (!kernel_addr)
            return remaining;

        memcpy(kernel_addr, src_ptr, to_copy);

        src_ptr += to_copy;
        user_addr += to_copy;
        remaining -= to_copy;
    }

    return 0;
}

size_t strncpy_from_user(char *dst, const char __user *user_src, size_t max_len) {
    if (!dst || !user_src || max_len == 0)
        return -1;
    
    if (!user_range_ok(user_src, 1))
        return -1;

    for (size_t i = 0; i < max_len; i++) {
        void *kernel_addr = user_to_kernel_addr((uint64_t)(user_src + i));
        if (!kernel_addr) {
            dst[i] = '\0';
            return -1;
        }
        
        dst[i] = *(char *)kernel_addr;
        if (dst[i] == '\0')
            return i;
    }

    dst[max_len - 1] = '\0';
    return max_len - 1;
}
