/*
 * Kernel heap allocator configuration
 */
#ifndef KHEAP_H
#define KHEAP_H

#include <mm/heap.h>

#define KHEAP_CACHE_MIN_ORDER    3   // 8 bytes
#define KHEAP_CACHE_MAX_ORDER    12  // 4096 bytes (1 page frame)
#define KHEAP_CACHES             ((KHEAP_CACHE_MAX_ORDER) - (KHEAP_CACHE_MIN_ORDER))

/*
 * The kernel malloc function (woah)
 * @param s bytes to allocate
 * @returns pointer to allocate memory
 */
void* kmalloc(size_t s);

/*
 * Allocate a number of items.
 * @param n items
 * @param s size of item
 * @returns pointer to region.
 */
void* kcalloc(size_t n, size_t s);

/*
 * Reallocate a pointer.
 * @param p pointer
 * @param new new size to allocate
 * @returns pointer to new region
 */
void *krealloc(void *p, size_t new);

/*
 * Kernel free function.
 * @param p pointer to reclaim
 */
void kfree(void* p);

/*
 * Kernel heap init.
 * @param kvmc kernel VMC
 */
void kheap_init(struct vmc* kvmc);

#endif
