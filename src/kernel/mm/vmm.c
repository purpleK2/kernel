#include <mm/vmm.h>
#include <mm/vmo.h>
#include <mm/pmm.h>
#include <paging.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <macro.h>
#include <kprintf.h>

void* valloc(struct vmc* vmc, size_t pages, size_t flags, uintptr_t phys) {
    size_t base;
    if (vmc_new_base(vmc, pages, &base) != 0) {
        return NULL;
    }

    if (!phys) {
        phys = (uintptr_t)palloc(pages);
        flags |= VMO_PALLOC;
    }

    pg_map(vmc->root_table, phys, base, pages * PAGESZ, pgflags_from_vflags(flags));

    struct vmo* v = vmo_new();
    v->base = base;
    v->pages = pages;
    v->flags = flags;

    kprintf_trace("Allocated %zu pages @ %#llx->%#llx\n", v->pages, v->base, phys);

    vmc_append(vmc, v);
    return (void*)v->base;
}

void vfree(struct vmc* vmc, void* p) {
    if (!vmc || !p) return;

    struct vmo* v = vmc->root_vmo;
    struct vmo* prev = NULL;
    for (; v != NULL; v = v->next) {
        if (v->base == (uintptr_t)p) break;
        prev = v;
    }

    if (!v) {
        kprintf_trace("Non existent virtual address %#p\n", p);
        return;
    }

    if (v->flags & VMO_PALLOC) {
        uintptr_t phys = pg_phys(vmc->root_table, v->base);
        pfree((void*)phys, v->pages);
    }
    pg_unmap(vmc->root_table, v->base);

    kprintf_trace("Reclaimed %zu pages @ %#llx\n", v->pages, v->base);
    if (prev) {
        prev->next = v->next;
    } else {
        vmc->root_vmo = v->next;
    }
    vmo_free(v);

}
