#ifndef VMM_H
#define VMM_H

#include <mm/vmo.h>

#include <stddef.h>
#include <stdint.h>

/*
 * Allocate a virtual memory region.
 * @param vmc a virtual memory context.
 * @param s size of region
 * @param flags flags that should be converted to arch-specific page-flags for mapping.
 * @param phys A physical address to map this virtual region. Can be NULL.
 * @returns a pointer to the newly created region.
 */
void* valloc(struct vmc* vmc, size_t s, size_t flags, uintptr_t phys);
void vfree(struct vmc* vmc, void* p);

#endif
