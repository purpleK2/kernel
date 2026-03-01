#include "vmm.h"

#include <autoconf.h>
#include <cpu.h>
#include <kernel.h>
#include <fs/vfs/vfs.h>

#include "memory/pmm/pmm.h"
#include "vflags.h"
#include <paging/paging.h>
#include <scheduler/scheduler.h>

#include <smp/ipi.h>

#include <spinlock.h>

#include <util/util.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

atomic_flag VMM_LOCK = ATOMIC_FLAG_INIT;

vmc_t *current_vmc = NULL;
vmc_t *global_vmc  = NULL;
vmc_t *kernel_vmc  = NULL; // for kmalloc()

vmc_t *get_current_vmc() {
    return current_vmc;
}

void vmc_switch(vmc_t *new) {
    spinlock_acquire(&VMM_LOCK);

    current_vmc = new;

    spinlock_release(&VMM_LOCK);
}

void set_kernel_vmc(vmc_t *kvmc) {
    spinlock_acquire(&VMM_LOCK);
    // so that it only gets set once
    if (!kernel_vmc) {
        kernel_vmc = kvmc;
    }
    spinlock_release(&VMM_LOCK);
}

vmc_t *get_kernel_vmc() {
    return kernel_vmc;
}

void set_global_vmc(vmc_t *glob) {
    spinlock_acquire(&VMM_LOCK);
    global_vmc = glob;
    spinlock_release(&VMM_LOCK);
}

vmc_t *get_global_vmc() {
    return global_vmc;
}

// imagine making a function to print stuff that you're going to barely use LMAO
void vmo_dump(vmo_t *vmo) {
#ifdef CONFIG_VMM_DEBUG
    debugf_debug("VMO %p\n", vmo);
    debugf_debug("\tprev: %p\n", vmo->prev);
    debugf_debug("\tbase: %llx\n", vmo->base);
    debugf_debug("\tlen %zu\n", vmo->len);
    debugf_debug("\tflags: %llb\n", vmo->flags);
    debugf_debug("\tnext: %p\n", vmo->next);
#endif
}

static vmm_node_t *vmolist_root_node = NULL;
static vmm_node_t *vmclist_root_node = NULL;

// allocate a physical page
vmm_node_t *vlist_node_alloc(size_t item_size) {
    size_t itemsize_aligned = ROUND_UP(item_size, PFRAME_SIZE);

    vmm_node_t *v = (vmm_node_t *)PHYS_TO_VIRTUAL(
        pmm_alloc_pages(itemsize_aligned / PFRAME_SIZE));

    v->len  = itemsize_aligned;
    v->next = NULL;

    return v;
}

// we don't waste a whole page for a single VMO/VMC structure
void *vmmlist_alloc(vmm_node_t **root_node, size_t item_size) {
    void *item = NULL;

    if (!(*root_node)) {
        (*root_node) = (vmm_node_t *)vlist_node_alloc(item_size);
    }

    vmm_node_t *v_cur;
    vmm_node_t *v_prev = NULL;

    // find a large enough block for a VMO
    for (v_cur = (*root_node); v_cur != NULL; v_cur = v_cur->next) {
        if (v_cur->len >= item_size) {
            break;
        }

        if (!v_cur->next) {
            v_cur->next = vlist_node_alloc(item_size);
        }

        v_prev = v_cur; // 200 iq move
    }

    item        = v_cur;
    v_cur->len -= item_size;

    if (v_cur->len == 0) {
        if (v_cur == (*root_node)) {
            (*root_node) = v_cur->next;
        } else if (v_prev) {
            v_prev->next = v_cur->next;
        }
    } else if (v_cur != (*root_node)) {
        // shift the node (byte-level pointer arithmetic)
        v_prev->next = (vmm_node_t *)((void *)v_cur + item_size);
        memcpy(v_prev->next, v_cur, sizeof(vmm_node_t));
        v_cur = v_prev->next;
    } else {
        // just shift the root node
        (*root_node) = (vmm_node_t *)((void *)(*root_node) + item_size);
        memcpy((*root_node), v_cur, sizeof(vmm_node_t));
    }

    return item;
}

static bool vmo_overlaps(vmo_t *v, uint64_t base, size_t pages) {
    uint64_t len_bytes = pages * PFRAME_SIZE;
    uint64_t v_end = v->base + v->len * PFRAME_SIZE;
    uint64_t r_end = base + len_bytes;

    return !(r_end <= v->base || base >= v_end);
}

void vmmlist_free(vmm_node_t **root_node, size_t item_size, void *tofree) {
    // cases:
    // it was the first VMO allocated
    // it's somewhere in between two nodes
    //      check if we can merge something
    // it's at the end

    // tbf we can just do a for loop
    vmm_node_t *v_cur    = NULL;
    vmm_node_t *v_prev   = NULL;
    vmm_node_t *v_tofree = tofree;

    if (tofree < (void *)(*root_node)) {
        v_cur = (*root_node);

        if (((void *)v_tofree) + item_size == (void *)(*root_node)) {
            v_tofree->len  = (*root_node)->len + item_size;
            v_tofree->next = (*root_node)->next;
        } else {
            v_tofree->len  = item_size;
            v_tofree->next = v_cur;
        }

        (*root_node) = v_tofree;

        if (v_tofree->len % PFRAME_SIZE == 0) {
            (*root_node) = v_tofree->next;
            pmm_free((void *)VIRT_TO_PHYSICAL(v_tofree),
                     v_tofree->len / PFRAME_SIZE);
        }
        return;
    }

    vmm_node_t *modified_node = NULL;

    for (v_cur = (*root_node); v_cur != NULL; v_cur = v_cur->next) {
        vmm_node_t *v_next = v_cur->next;

        if (v_next != NULL && (void *)v_next < tofree) {
            v_prev = v_cur;
            continue;
        }

        // if tofree sits right after current node
        if (((void *)v_cur + v_cur->len) == tofree) {
            v_cur->len += item_size;

            if (v_next && ((void *)v_cur + v_cur->len) == (void *)v_next) {
                v_cur->len += v_next->len;
                v_cur->next = v_next->next;
            }
            modified_node = v_cur;
        } else if (v_next && ((void *)v_next - item_size) == tofree) {
            // here, if it sits right before the next node
            vmm_node_t *v_new = (vmm_node_t *)((void *)v_next - item_size);
            v_new->len        = v_next->len + item_size;
            v_new->next       = v_next->next;
            v_cur->next       = v_new;
            modified_node     = v_new;
        } else {
            // last case, just put it in the middle of the two xD
            // this might also work at the end of the list (v_next is NULL)
            v_tofree->len  = item_size;
            v_tofree->next = v_next;
            v_cur->next    = v_tofree;
            modified_node  = v_tofree;
        }

        break; // we don't need to go on with the loop
    }

    if (v_cur == NULL) {
        v_prev->next = v_tofree;
        v_tofree->len = item_size;
        v_tofree->next = NULL;
        modified_node = v_tofree;
        v_cur = v_tofree;
    }

    if (!modified_node || modified_node->len % PFRAME_SIZE != 0) {
        return;
    }

    v_tofree = modified_node;
    if (v_tofree == (*root_node)) {
        (*root_node) = (*root_node)->next;
    } else {
        vmm_node_t *prev_of_mod = NULL;
        for (vmm_node_t *n = (*root_node); n != NULL; n = n->next) {
            if (n->next == v_tofree) {
                prev_of_mod = n;
                break;
            }
        }
        if (prev_of_mod) {
            prev_of_mod->next = v_tofree->next;
        }
    }

    pmm_free((void *)VIRT_TO_PHYSICAL(v_tofree), v_tofree->len / PFRAME_SIZE);
}

// useful wrapper functions //

vmc_t *vmc_alloc() {
    return (vmc_t *)vmmlist_alloc(&vmclist_root_node, sizeof(vmc_t));
}

vmo_t *vmo_alloc() {
    return (vmo_t *)vmmlist_alloc(&vmolist_root_node, sizeof(vmo_t));
}

void vmc_free(vmc_t *v) {
    vmmlist_free(&vmclist_root_node, sizeof(vmc_t), v);
}

void vmo_free(vmo_t *v) {
    vmmlist_free(&vmolist_root_node, sizeof(vmo_t), v);
}

// @param length IT'S IN PAGEEEEES
vmo_t *vmo_init(uint64_t base, size_t length, uint64_t flags) {
    vmo_t *vmo = vmo_alloc();

    vmo->base  = base;
    vmo->len   = length;
    vmo->flags = flags;

    vmo->backing_vnode = NULL;
    vmo->file_offset   = 0;

    vmo->next = NULL;

    return vmo;
}

// @note We will not care if `pml4` is 0x0 :^)
// @param pml4_virt VIRTUAL address
vmc_t *vmc_init(uint64_t *pml4_virt, uint64_t flags) {
    spinlock_acquire(&VMM_LOCK);
    vmc_t *ctx = vmc_alloc();

    if (pml4_virt == NULL) {
        pml4_virt = (uint64_t *)PHYS_TO_VIRTUAL(pmm_alloc_page());
    }

    ctx->pml4_table = (uint64_t *)VIRT_TO_PHYSICAL(pml4_virt);
    ctx->root_vmo   = vmo_init(0x1000, 1, flags);


    spinlock_release(&VMM_LOCK);
    return ctx;
}

void vmc_destroy(vmc_t *ctx) {
    spinlock_acquire(&VMM_LOCK);
    if (ctx->pml4_table == (uint64_t *)cpu_get_cr(3)) {
        debugf_warn("Attempted to destroy a pagemap (%.16llx) that's currently in use\n", VIRT_TO_PHYSICAL(ctx->pml4_table));
        return;
    }

    // Free all VMOs and their associated physical memory
    for (vmo_t *v = ctx->root_vmo; v != NULL;) {
        vmo_t *next = v->next;

        // Only free physical memory if the VMO is mapped
        if (v->flags & VMO_PRESENT) {
            if (is_addr_mapped_in((uint64_t *)PHYS_TO_VIRTUAL(ctx->pml4_table), v->base)) {
                pmm_free((void *)PHYS_TO_VIRTUAL(v->base), v->len);
            }
        }

        // Free the VMO structure itself
        vmo_free(v);
        v = next;
    }

    // Unmap all pages in the page tables
    for (int pml4_idx = 0; pml4_idx < 512; pml4_idx++) {
        uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRTUAL(ctx->pml4_table);
        if (!(pml4[pml4_idx] & PMLE_PRESENT)) {
            continue;
        }

        uint64_t *pdpt = (uint64_t *)PHYS_TO_VIRTUAL(pml4[pml4_idx] & ~0xFFF);
        for (int pdpt_idx = 0; pdpt_idx < 512; pdpt_idx++) {
            if (!(pdpt[pdpt_idx] & PMLE_PRESENT)) {
                continue;
            }

            uint64_t *pd = (uint64_t *)PHYS_TO_VIRTUAL(pdpt[pdpt_idx] & ~0xFFF);
            for (int pd_idx = 0; pd_idx < 512; pd_idx++) {
                if (!(pd[pd_idx] & PMLE_PRESENT)) {
                    continue;
                }

                uint64_t *pt = (uint64_t *)PHYS_TO_VIRTUAL(pd[pd_idx] & ~0xFFF);

                // Free the page table
                if (is_addr_mapped((uint64_t)pt)) {
                    pmm_free((void *)pt, 1);
                }
            }

            // Free the page directory
            if (is_addr_mapped((uint64_t)pd)) {
                pmm_free((void *)pd, 1);
            }
        }

        // Free the PDPT
        if (is_addr_mapped((uint64_t)pdpt)) {
            pmm_free((void *)pdpt, 1);
        }
    }

    // Free the PML4 table and the context
    pmm_free(ctx->pml4_table, 1);
    vmc_free(ctx);

    ctx->pml4_table = NULL;
    spinlock_release(&VMM_LOCK);
}

// @param where after how many pages should we split the VMO
vmo_t *split_vmo_at(vmo_t *src_vmo, size_t where) {
    vmo_t *new_vmo;

    if (src_vmo->len - where <= 0) {
        return src_vmo; // we are not going to split it
    }

    size_t offset = (uint64_t)(where * PFRAME_SIZE);
    new_vmo =
        vmo_init(src_vmo->base + offset, src_vmo->len - where, src_vmo->flags);
    /*
    src_vmo		  new_vmo
    [     [                        ]
    0	  0+len					   X
    */
    /*debugf_debug("VMO %p has been split at (virt)%llx\n", src_vmo,
                 src_vmo->base + offset);*/

    src_vmo->len = where;

    if (src_vmo->next != NULL) {
        new_vmo->next = src_vmo->next;
        src_vmo->next = new_vmo;
    }

    /*
    src_vmo		  new_vmo
    [     ]<-->[                        ]-->...
    0	       0+len					X
    */

    return src_vmo;
}

void pagemap_copy_to(uint64_t *non_kernel_pml4) {

    uint64_t *k_pml4 = (uint64_t *)PHYS_TO_VIRTUAL(get_kernel_pml4());

    if ((uint64_t *)PHYS_TO_VIRTUAL(non_kernel_pml4) == k_pml4)
        return;

    for (int i = 255; i < 512; i++) {
        // debugf("Copying %p[%d](%llx) to %p[%d]\n", k_pml4, i, k_pml4[i],
        //        non_kernel_pml4, i);

        ((uint64_t *)PHYS_TO_VIRTUAL(non_kernel_pml4))[i] = k_pml4[i];
    }
}

static uint64_t *pt_clone(uint64_t *src) {
    uint64_t *dst = (uint64_t *)PHYS_TO_VIRTUAL(pmm_alloc_page());
    memset(dst, 0, PFRAME_SIZE);
    memcpy(dst, src, PFRAME_SIZE);
    return dst;
}


vmc_t *vmc_fork(vmc_t *parent) {
    if (!parent) {
        return NULL;
    }

    spinlock_acquire(&VMM_LOCK);

    uint64_t tls_start = 0;
    uint64_t tls_end   = 0;
    tcb_t *cur_tcb = get_current_tcb();
    if (cur_tcb && cur_tcb->tls.base_virt && cur_tcb->tls.size) {
        uint64_t fs_base   = (uint64_t)(uintptr_t)cur_tcb->tls.base_virt;
        size_t   total_sz  = cur_tcb->tls.size;
        size_t   tcb_sz    = sizeof(user_tls_t);
        if (total_sz >= tcb_sz) {
            uint64_t region_base = fs_base - (total_sz - tcb_sz);
            tls_start            = region_base & ~(PFRAME_SIZE - 1ULL);
            tls_end              = region_base + total_sz;
        }
    }

    vmc_t *child = vmc_alloc();
    if (!child) {
        spinlock_release(&VMM_LOCK);
        return NULL;
    }

    child->pml4_table = (uint64_t *)pmm_alloc_page();
    if (!child->pml4_table) {
        vmc_free(child);
        spinlock_release(&VMM_LOCK);
        return NULL;
    }

    uint64_t *parent_pml4 = (uint64_t *)PHYS_TO_VIRTUAL(parent->pml4_table);
    uint64_t *child_pml4  = (uint64_t *)PHYS_TO_VIRTUAL(child->pml4_table);

    // Initialize child PML4 - copy kernel half (entries 256-511), clear user half
    memset(child_pml4, 0, PFRAME_SIZE);
    for (int i = 256; i < 512; i++) {
        child_pml4[i] = parent_pml4[i];
    }

    child->root_vmo = NULL;
    vmo_t **dst     = &child->root_vmo;

    for (vmo_t *v = parent->root_vmo; v != NULL; v = v->next) {
        *dst = vmo_alloc();
        if (!*dst) {
            break;
        }

        memcpy(*dst, v, sizeof(vmo_t));
        (*dst)->next = NULL;
        dst          = &(*dst)->next;
    }

    // Deep copy user-space page tables (entries 0-255)
    // We need to allocate fresh page table pages for the child
    for (int pml4_idx = 0; pml4_idx < 256; pml4_idx++) {
        if (!(parent_pml4[pml4_idx] & PMLE_PRESENT)) {
            continue;
        }

        uint64_t *parent_pdpt = (uint64_t *)PHYS_TO_VIRTUAL(
            PG_GET_ADDR(parent_pml4[pml4_idx])
        );
        
        // Allocate new PDPT for child
        uint64_t *child_pdpt_phys = (uint64_t *)pmm_alloc_page();
        if (!child_pdpt_phys) {
            continue;
        }
        uint64_t *child_pdpt = (uint64_t *)PHYS_TO_VIRTUAL(child_pdpt_phys);
        memset(child_pdpt, 0, PFRAME_SIZE);
        child_pml4[pml4_idx] = (uint64_t)child_pdpt_phys | (parent_pml4[pml4_idx] & 0xFFF);

        for (int pdp_idx = 0; pdp_idx < 512; pdp_idx++) {
            if (!(parent_pdpt[pdp_idx] & PMLE_PRESENT)) {
                continue;
            }

            uint64_t *parent_pdir = (uint64_t *)PHYS_TO_VIRTUAL(
                PG_GET_ADDR(parent_pdpt[pdp_idx])
            );
            
            // Allocate new PDIR for child
            uint64_t *child_pdir_phys = (uint64_t *)pmm_alloc_page();
            if (!child_pdir_phys) {
                continue;
            }
            uint64_t *child_pdir = (uint64_t *)PHYS_TO_VIRTUAL(child_pdir_phys);
            memset(child_pdir, 0, PFRAME_SIZE);
            child_pdpt[pdp_idx] = (uint64_t)child_pdir_phys | (parent_pdpt[pdp_idx] & 0xFFF);

            for (int pdir_idx = 0; pdir_idx < 512; pdir_idx++) {
                if (!(parent_pdir[pdir_idx] & PMLE_PRESENT)) {
                    continue;
                }

                uint64_t *parent_pt = (uint64_t *)PHYS_TO_VIRTUAL(
                    PG_GET_ADDR(parent_pdir[pdir_idx])
                );
                
                // Allocate new PT for child
                uint64_t *child_pt_phys = (uint64_t *)pmm_alloc_page();
                if (!child_pt_phys) {
                    continue;
                }
                uint64_t *child_pt = (uint64_t *)PHYS_TO_VIRTUAL(child_pt_phys);
                memset(child_pt, 0, PFRAME_SIZE);
                child_pdir[pdir_idx] = (uint64_t)child_pt_phys | (parent_pdir[pdir_idx] & 0xFFF);

                for (int pt_idx = 0; pt_idx < 512; pt_idx++) {
                    uint64_t entry = parent_pt[pt_idx];
                    if (!(entry & PMLE_PRESENT)) {
                        continue;
                    }

                    uint64_t virt =
                        ((uint64_t)pml4_idx << 39) |
                        ((uint64_t)pdp_idx << 30) |
                        ((uint64_t)pdir_idx << 21) |
                        ((uint64_t)pt_idx << 12);

                    // Skip TLS region - will be allocated fresh for child
                    if (tls_start && virt >= tls_start && virt < tls_end) {
                        continue;
                    }

                    // Skip user stack region - will be allocated fresh for child
                    if (virt >= USER_STACK_TOP - SCHEDULER_STACKSZ && virt < USER_STACK_TOP) {
                        continue;
                    }

                    uint64_t phys = PG_GET_ADDR(entry);

                    // Only COW user-writable 4KiB pages
                    if ((entry & PMLE_USER) && (entry & PMLE_WRITE)) {
                        uint64_t cow_entry = (entry & ~PMLE_WRITE) | PMLE_COW;
                        parent_pt[pt_idx] = cow_entry;
                        child_pt[pt_idx]  = cow_entry;
                        pmm_page_ref_inc((void *)phys);

                        if (!(parent_pt[pt_idx] & PMLE_USER)) {
                            child_pt[pt_idx] = entry; // non-user pages are shared read-only
                            continue;
                        }

                        if (virt == 0x15000) {
                            debugf_debug("how");
                            child_pt[pt_idx] = entry;
                            continue;
                        }

                        bool in_kernel_vmc = false;
                        for (vmo_t *v = kernel_vmc->root_vmo; v != NULL; v = v->next) {
                            if (vmo_overlaps(v, virt, 1)) {
                                in_kernel_vmc = true;
                                break;
                            }
                        }

                        if (in_kernel_vmc) {
                            child_pt[pt_idx] = entry;
                            continue;
                        }

                        _invalidate(virt);
                        if (get_bootloader_data()->smp_enabled) {
                            tlb_shootdown(virt);
                        }
                    } else {
                        // Read-only or non-user pages: just copy the entry
                        child_pt[pt_idx] = entry;
                        // Increment refcount for shared physical pages
                        if (entry & PMLE_USER) {
                            pmm_page_ref_inc((void *)phys);
                        }
                    }
                }
            }
        }
    }

    spinlock_release(&VMM_LOCK);
    return child;
}

// Assumes the CTX has been initialized with vmc_init()
void vmm_init(vmc_t *ctx) {
    for (vmo_t *i = ctx->root_vmo; i != NULL; i = i->next) {
        // every VMO will have the same flags as the root one
        i->flags = ctx->root_vmo->flags;

        // mapping will be done on vma_alloc
    }

    pagemap_copy_to(ctx->pml4_table);
}

// to be used by scheduler
void process_vmm_init(vmc_t **proc_vmcr, uint64_t flags) {
    spinlock_acquire(&VMM_LOCK);
    if (!proc_vmcr) {
        spinlock_release(&VMM_LOCK);
        return;
    }

    if (!(*proc_vmcr)) {
        spinlock_release(&VMM_LOCK);
        *proc_vmcr = vmc_init(NULL, flags);
        spinlock_acquire(&VMM_LOCK);
    }

    vmc_t *proc_vmc = *proc_vmcr;
    if (proc_vmc->root_vmo) {
        vmo_free(proc_vmc->root_vmo);
    }

    vmo_t **proc_vmor = &proc_vmc->root_vmo;

    for (vmo_t *v = global_vmc->root_vmo; v != NULL; v = v->next) {
        *proc_vmor = vmo_alloc();
        memcpy(*proc_vmor, v, sizeof(vmo_t));

        proc_vmor = &(*proc_vmor)->next;
    }

    proc_vmc->pml4_table = (uint64_t *)pmm_alloc_page();
    memcpy((void *)PHYS_TO_VIRTUAL(proc_vmc->pml4_table),
           global_vmc->pml4_table, PFRAME_SIZE);
    spinlock_release(&VMM_LOCK);
}

void *valloc(vmc_t *ctx, size_t pages, uint8_t flags, void *phys) {
    spinlock_acquire(&VMM_LOCK);

    void *ptr = NULL;

    vmo_t *cur_vmo = ctx->root_vmo;
    vmo_t *new_vmo;

    for (; cur_vmo != NULL; cur_vmo = cur_vmo->next) {
        vmo_dump(cur_vmo);

        if ((cur_vmo->len >= pages) && (BIT_GET(cur_vmo->flags, 8) == 0)) {
            break;
        }

#ifdef CONFIG_VMM_DEBUG
        debugf_debug("Current VMO is either too small or already "
                     "allocated. Skipping...\n");
#endif

        if (!cur_vmo->next) {
            uint64_t offset = (uint64_t)(cur_vmo->len * PFRAME_SIZE);
            new_vmo         = vmo_init(cur_vmo->base + offset, pages,
                                       flags & ~(VMO_ALLOCATED));
            cur_vmo->next   = new_vmo;

#ifdef CONFIG_VMM_DEBUG
            debugf_debug("VMO %p created successfully. Proceeding to next "
                         "iteration\n",
                         new_vmo);
#endif
        }
    }

    if (cur_vmo == NULL) {
        kprintf_panic("VMM ran out of memory and is not able to request it "
                      "from the PMM.\n");
        _hcf();
    }

    cur_vmo = split_vmo_at(cur_vmo, pages);
    FLAG_SET(cur_vmo->flags, VMO_ALLOCATED);

    ptr = (void *)(cur_vmo->base);

    void *phys_al = NULL;
    size_t offset = 0;
    if (phys) {
        phys_al = (void *)ROUND_DOWN((size_t)phys, PFRAME_SIZE);
        offset  = (size_t)(phys_al - phys);
    }

    void *phys_to_map = phys_al ? phys_al : pmm_alloc_pages(pages);
    map_region((uint64_t *)PHYS_TO_VIRTUAL(ctx->pml4_table),
               (uint64_t)phys_to_map, (uint64_t)ptr, (uint64_t)pages,
               vmo_to_page_flags(flags));

#ifdef CONFIG_VMM_DEBUG
    debugf_debug("Returning pointer %p\n", ptr);
#endif

    spinlock_release(&VMM_LOCK);

    return (ptr + offset);
}

void *valloc_at(vmc_t *ctx, void *addr, size_t pages, uint8_t flags, void *phys) {
    spinlock_acquire(&VMM_LOCK);

    uint64_t base = (uint64_t)(uintptr_t)addr;

    if (base & (PFRAME_SIZE - 1)) {
        debugf_warn("valloc_at: Address %p is not page-aligned\n", addr);
        spinlock_release(&VMM_LOCK);
        return NULL;
    }

    vmo_t *prev_vmo = NULL;
    vmo_t *cur_vmo  = ctx->root_vmo;

    for (; cur_vmo; prev_vmo = cur_vmo, cur_vmo = cur_vmo->next) {
        if (vmo_overlaps(cur_vmo, base, pages)) {
            debugf_warn("valloc_at: Requested region %p - %p overlaps with "
                        "existing VMO %p (%p - %p)\n",
                        (void *)base, (void *)(base + pages * PFRAME_SIZE),
                        cur_vmo, (void *)cur_vmo->base,
                        (void *)(cur_vmo->base + cur_vmo->len * PFRAME_SIZE));
            spinlock_release(&VMM_LOCK);
            return NULL;
        }
        if (cur_vmo->base > base) {
            break;
        }
    }

    vmo_t *vmo = vmo_alloc();
    vmo->base  = base;
    vmo->len   = pages;
    vmo->flags = flags | VMO_ALLOCATED;
    vmo->backing_vnode = NULL;
    vmo->file_offset   = 0;

    if (!prev_vmo) {
        vmo->next = ctx->root_vmo;
        ctx->root_vmo = vmo;
    } else {
        vmo->next = prev_vmo->next;
        prev_vmo->next = vmo;
    }

    void *phys_al = NULL;
    size_t offset = 0;

    if (phys) {
        phys_al = (void *)ROUND_DOWN((size_t)phys, PFRAME_SIZE);
        offset  = (size_t)(phys - phys_al);
    }

    void *phys_to_map = phys_al ? phys_al : pmm_alloc_pages(pages);
    map_region(
        (uint64_t *)PHYS_TO_VIRTUAL(ctx->pml4_table),
        (uint64_t)phys_to_map,
        base,
        pages,
        vmo_to_page_flags(flags)
    );

#ifdef CONFIG_VMM_DEBUG
    debugf_debug("Created VMO %p at requested address %p (%zu pages) (range %p - %p)\n",
                 vmo, (void *)base, pages, (void *)base, (void *)(base + pages * PFRAME_SIZE));
#endif
    spinlock_release(&VMM_LOCK);
    return (void *)(base + offset);
}

void *valloc_at_lazy(vmc_t *ctx, void *addr, size_t pages, uint8_t flags,
                     struct vnode *vnode, size_t file_offset) {
    spinlock_acquire(&VMM_LOCK);

    uint64_t base = (uint64_t)(uintptr_t)addr;

    if (base & (PFRAME_SIZE - 1)) {
        debugf_warn("valloc_at_lazy: Address %p is not page-aligned\n", addr);
        spinlock_release(&VMM_LOCK);
        return NULL;
    }

    vmo_t *prev_vmo = NULL;
    vmo_t *cur_vmo  = ctx->root_vmo;

    for (; cur_vmo; prev_vmo = cur_vmo, cur_vmo = cur_vmo->next) {
        if (vmo_overlaps(cur_vmo, base, pages)) {
            debugf_warn("valloc_at_lazy: Requested region %p - %p overlaps\n",
                        (void *)base, (void *)(base + pages * PFRAME_SIZE));
            spinlock_release(&VMM_LOCK);
            return NULL;
        }
        if (cur_vmo->base > base) {
            break;
        }
    }

    vmo_t *vmo = vmo_alloc();
    vmo->base  = base;
    vmo->len   = pages;
    vmo->flags = flags | VMO_ALLOCATED | VMO_LAZY;
    vmo->backing_vnode = vnode;
    vmo->file_offset   = file_offset;

    if (vnode) {
        vnode_ref(vnode);
    }

    if (!prev_vmo) {
        vmo->next = ctx->root_vmo;
        ctx->root_vmo = vmo;
    } else {
        vmo->next = prev_vmo->next;
        prev_vmo->next = vmo;
    }
#ifdef CONFIG_VMM_DEBUG
    debugf_debug("Created lazy VMO %p at %p (%zu pages, vnode=%p offset=%zu)\n",
                 vmo, (void *)base, pages, vnode, file_offset);
#endif
    spinlock_release(&VMM_LOCK);
    return (void *)base;
}

vmo_t *vmo_find_by_addr(vmc_t *vmc, uint64_t addr) {
    if (!vmc) return NULL;

    for (vmo_t *v = vmc->root_vmo; v != NULL; v = v->next) {
        if (!(v->flags & VMO_ALLOCATED)) continue;

        uint64_t vmo_end = v->base + v->len * PFRAME_SIZE;
        if (addr >= v->base && addr < vmo_end) {
            return v;
        }
    }

    return NULL;
}

// @param free do you want to give back the physical address of `ptr` back to
// the PMM? (this will zero out that region on next allocation)
void vfree(vmc_t *ctx, void *ptr, bool free) {
    spinlock_acquire(&VMM_LOCK);

#ifdef CONFIG_VMM_DEBUG
    debugf_debug("Deallocating pointer %p\n", ptr);
#endif

    ptr = (void *)ROUND_DOWN((uint64_t)ptr, PFRAME_SIZE);

    vmo_t *d_prev = NULL;

    vmo_t *cur_vmo = ctx->root_vmo;
    for (; cur_vmo != NULL; cur_vmo = cur_vmo->next) {
        vmo_dump(cur_vmo);
        if ((uint64_t)ptr == cur_vmo->base) {
            break;
        }

        d_prev = cur_vmo;
    }

    if (cur_vmo == NULL) {
#ifdef CONFIG_VMM_DEBUG
        debugf_debug("Tried to deallocate a non-existing virtual address. "
                     "Quitting...\n");
#endif
        spinlock_release(&VMM_LOCK);
        return;
    }

    FLAG_UNSET(cur_vmo->flags, VMO_ALLOCATED);

    uint64_t *pml4_virt = (uint64_t *)PHYS_TO_VIRTUAL(ctx->pml4_table);

    if (cur_vmo->flags & VMO_LAZY) {
        for (size_t i = 0; i < cur_vmo->len; i++) {
            uint64_t virt = cur_vmo->base + i * PFRAME_SIZE;
            uint64_t phys = pg_virtual_to_phys(pml4_virt, virt);
            if (phys && free) {
                pmm_free((void *)phys, 1);
            }
        }
        if (cur_vmo->backing_vnode) {
            vnode_unref(cur_vmo->backing_vnode);
        }
    } else {
        uint64_t phys = pg_virtual_to_phys(pml4_virt, cur_vmo->base);
        if (free)
            pmm_free((void *)phys, cur_vmo->len);
    }
    unmap_region(pml4_virt, cur_vmo->base, cur_vmo->len);

    vmo_t *to_dealloc = cur_vmo;
    // d is deallocated
    vmo_t *d_next     = to_dealloc->next;

    if (cur_vmo == ctx->root_vmo) {
        ctx->root_vmo = ctx->root_vmo->next;
    } else {
        d_prev->next = d_next;
    }

    vmo_free(to_dealloc);

#ifdef CONFIG_VMM_DEBUG
    debugf("VM range %p deallocated OK!\n", to_dealloc);
#endif

    spinlock_release(&VMM_LOCK);
}

void global_vmc_init(vmc_t *kernel_vmc) {
    spinlock_acquire(&VMM_LOCK);
    if (!kernel_vmc) {
        spinlock_release(&VMM_LOCK);
        return;
    }

    if (!global_vmc) {
        global_vmc = vmc_alloc();
    }

    uint64_t *new_pml4 = (uint64_t *)PHYS_TO_VIRTUAL(pmm_alloc_page());
    uint64_t *src_pml4 = (uint64_t *)PHYS_TO_VIRTUAL(kernel_vmc->pml4_table);
    memcpy(new_pml4, src_pml4, PFRAME_SIZE);

    vmo_t **globvmo_cur = &global_vmc->root_vmo;

    for (vmo_t *v = kernel_vmc->root_vmo; v != NULL; v = v->next) {
        *globvmo_cur = vmo_alloc();
        memcpy(*globvmo_cur, v, sizeof(vmo_t));

        globvmo_cur = &(*globvmo_cur)->next;
    }

    global_vmc->pml4_table = new_pml4;

    spinlock_release(&VMM_LOCK);
    set_global_vmc(global_vmc);
}
