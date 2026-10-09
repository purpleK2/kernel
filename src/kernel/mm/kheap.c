#include "mm/heap.h"
#include <mm/kheap.h>
#include <mm/vmm.h>
#include <mm/pmm.h>

#include <stddef.h>

#include <macro.h>
#include <memory.h>
#include <kprintf.h>
#include <cpu.h>

static struct heap_cache g_kcaches[KHEAP_CACHES];
// static struct heap g_heap;
static struct vmc* g_kvmc;

static struct heap_cache* get_cache(size_t s) {
    for (size_t o = KHEAP_CACHE_MIN_ORDER; o < KHEAP_CACHE_MAX_ORDER; o++) {
        if ((size_t)(1 << o) >= s) {
            return &g_kcaches[o - KHEAP_CACHE_MIN_ORDER];
        }
    }

    return NULL;
}

static struct heap_cache* get_cache_by_size(size_t addr, size_t base) {
    // Find which cache this belongs to by size
    // (simple: iterate caches and check obj_size fits within page)
    uintptr_t offset = addr - base;
    for (size_t i = 0; i < KHEAP_CACHES; i++) {
        if (g_kcaches[i].obj_size <= PAGESZ) {
            if (offset % g_kcaches[i].obj_size == 0) {
                return &g_kcaches[i];
            }
        }
    }

    return NULL;
}


void refill_cache(struct heap_cache* cache) {
    void *p = valloc(g_kvmc, 1, VMO_KERNEL_RW | VMO_NX, 0);
    if (!p) {
        kprintf_panic("OUT OF MEMORY!\n");
        _hcf();
    }

    size_t obj_size = cache->obj_size;
    size_t capacity = PAGESZ / obj_size;
    for (size_t i = 0; i < capacity; i++) {
        struct heap_obj *obj      = p + (i * obj_size);
        // push onto freelist
        obj->next  = cache->objects;
        cache->objects = obj;
    }
}

void* kmalloc(size_t s) {
    if (!s) return NULL;

    struct heap_cache* cache = get_cache(s);
    if (!cache) {
        // Large object: fall back to page allocator
        size_t pages = ROUND_UP(s, PAGESZ) / PAGESZ;
        void *ptr = valloc(g_kvmc, pages, VMO_KERNEL_RW | VMO_NX, 0);
        if (!ptr)
            return NULL;
        return ptr;
    }

    if (!cache->objects) refill_cache(cache);

    void *obj      = cache->objects;
    cache->objects = cache->objects->next;
    return obj;
}

void kfree(void* p) {
    if (!p) return;

    uintptr_t addr      = (uintptr_t)p;
    uintptr_t page_base = addr & ~(PAGESZ - 1);

    struct heap_cache *cache = get_cache_by_size(addr, page_base);
    if (!cache) {
        // Large allocation: free whole pages
        vfree(g_kvmc, (void *)page_base);
        return;
    }

    ((struct heap_obj*)p)->next  = cache->objects;
    cache->objects = p;
}

void* kcalloc(size_t n, size_t s) {
    if (n != 0 && s > SIZE_MAX / n) return NULL;

    size_t total = n * s;
    void *p = kmalloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *krealloc(void *p, size_t new) {
    if (!p) return NULL;
    if (new == 0) {
        kfree(p);
        return NULL;
    }

    uintptr_t addr      = (uintptr_t)p;
    uintptr_t page_base = addr & ~(PAGESZ - 1);
    struct heap_cache *cache = get_cache_by_size(addr, page_base);
    size_t old_size = PAGESZ;
    if (!cache) {
        vfree(g_kvmc, (void *)page_base);
    } else {
        old_size = cache->obj_size;
        kfree(p);
    }

    // crude: allocate new and copy
    void *new_p = kmalloc(new);
    if (new_p) {
        memcpy(new_p, p, old_size);
        kfree(p);
    }
    return p;
}

void kheap_init(struct vmc* kvmc) {
    g_kvmc = kvmc;

    for (size_t i = 0; i < KHEAP_CACHES; i++) {
        struct heap_cache* cache = &g_kcaches[i];
        cache->obj_size = (1 << (i + KHEAP_CACHE_MIN_ORDER));
    }
}
