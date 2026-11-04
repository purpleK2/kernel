#ifndef KVMM_H
#define KVMM_H

#include <mm/vmm.h>
#include <mm/vmo.h>

#include <stdint.h>

/*
 * Initializes a kernel-level VMM.
 * @param root_pt root page table
 * @returns the VMC struct for kernel-level VMM
 */
struct vmc* kvmm_init(uintptr_t root_pt);

/*
 * Allocate a virtual memory region in the kernel-level VMM
 * @param pages pages
 * @param flags flags that should be converted to arch-specific page-flags for mapping.
 * @param phys A physical address to map this virtual region. Can be NULL.
 * @returns a pointer to the newly created region.
 */
void* kvalloc(size_t pages, size_t flags, uintptr_t phys);
/*
 * Reclaim a virtual address to the kernel-level VMM.
 * @param p pointer to virtual address
 */
void kvfree(void* p);

#endif
