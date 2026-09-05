#include <mm/pmm.h>

#include <memory.h>
#include <assert.h>

#include <cpu.h>

#include <stdio.h>

#include <datatypes/llist.h>

static struct pmm pmm = {0};

uintptr_t hhdm_physical(uintptr_t v) {
    return v >= pmm.limine_hhdm_offset ? v - pmm.limine_hhdm_offset : v;
}

uintptr_t hhdm_virtual(uintptr_t p) {
    return p < pmm.limine_hhdm_offset ? p + pmm.limine_hhdm_offset : p;
}

void pmm_init(LIMINE_PTR(struct limine_memmap_response*) memmap, uint64_t limine_hhdm_offset) {
    assert(memmap != NULL);
    pmm.limine_hhdm_offset = limine_hhdm_offset;
    debugf_trace("HHDM offset @ %#llx\n", pmm.limine_hhdm_offset);

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* e = memmap->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) continue;
        pmm.total_usable_mem += e->length;

        uint64_t base_virt = e->base + pmm.limine_hhdm_offset;
        struct ll_node* node = (struct ll_node*)base_virt;
        node->len = e->length;
        node->next = NULL;
        ll_append(&pmm.head, node);
    }

    debugf_trace("Total available memory: %zu\n", pmm.total_usable_mem);
    debugf_trace("PMM init stats:\n");
    for (struct ll_node* n = pmm.head; n != NULL; n = n->next) {
        debugf_trace("\tbase:%#llx length:%#zu\n", (uint64_t)n - pmm.limine_hhdm_offset, n->len);
    }
}

void* palloc(size_t pages) {
    if (pages == 0) return NULL;

    size_t s = pages * PAGESZ;
    void* p = llalloc(&pmm.head, s, NULL);
    if (!p) {
        debugf_panic("OUT OF MEMORY!\n");
        _hcf();
    }

    memset(p, 0, s);
    pmm.used_mem += s;
    debugf_trace("Allocated %zu page%s @ %#llx\n", pages, pages > 1 ? "s": "", (uintptr_t)p - pmm.limine_hhdm_offset);
    return (void*)(p - pmm.limine_hhdm_offset);
}

void pfree(void* p, size_t pages) {
    void* p_virt = (void*)((uintptr_t)p + pmm.limine_hhdm_offset);
    size_t s = pages * PAGESZ;

    llfree(&pmm.head, p_virt, s);
    pmm.used_mem -= s;
    debugf_trace("Reclaimed %zu page%s\n", pages, pages > 1 ? "s": "");
}
