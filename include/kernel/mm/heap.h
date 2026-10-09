/*
 * SLUB heap allocator. Abstract implementation.
 */
#ifndef HEAP_H
#define HEAP_H

#include <mm/vmo.h>
#include <stddef.h>

struct heap_obj {
    struct heap_obj* next;
};

struct heap_cache {
    size_t obj_size;
    size_t blocks;      // objects count
    struct heap_obj* objects;
};

struct heap {
  struct vmc* vmc;
  struct heap_cache* caches;
};

#endif
