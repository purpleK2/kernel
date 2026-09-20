#include <paging.h>
#include <page.h>

#include <stddef.h>
#include <stdint.h>
#include <x86_64.h>
#include <cpuid.h>
#include <limine.h>

#include <macro.h>
#include <assert.h>

#include <kprintf.h>
#include <mm/pmm.h>

LIMINEREQ static volatile struct limine_executable_address_request executable_address_request = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
    .revision = 0
};

LIMINEREQ static volatile struct limine_paging_mode_request paging_mode_request = {
    .id = LIMINE_PAGING_MODE_REQUEST_ID,
    .revision = 0,
    .mode = 4
};

extern char __k_start;
extern char __k_limreq_start;
extern char __k_limreq_end;
extern char __k_text_start;
extern char __k_text_end;
extern char __k_rodata_start;
extern char __k_rodata_end;
extern char __k_data_start;
extern char __k_data_end;
extern char __k_end;

static struct page* page_sizes = NULL;

/*
 * Register a supported page size.
 * @param page page size structure to add to the list of page sizes
 */
void register_pagesz(struct page* page) {
    if (!page_sizes) {
        page_sizes = page;
        return;
    }

    struct page* p = page_sizes;
    for (; p->next != NULL; p = p->next) {
        if (p == page) return;
    }
    p->next = page;
}

uintptr_t pg_get_create_pmle(struct page* p, uintptr_t table, size_t idx, size_t flags) {
    uint64_t* t = (uint64_t*)hhdm_virtual(table);

    if (!(t[idx] & PG_PRESENT)) {
        t[idx] = (uint64_t)palloc(p->ps / PAGESZ) | PG_PRESENT | (flags & ~(p->addr_mask));
    } else {
        /*
         * Widen intermediate entry permissions: on x86-64, if a higher-level
         * entry (PDP/PDIR) lacks WRITE, all pages beneath it are read-only
         * regardless of their page-level flags.
         */
        t[idx] |= PG_PRESENT | (flags & ~(p->addr_mask));
    }

    return t[idx] & p->addr_mask;
}

/*
 * Map a 4KiB page.
 * @param p pointer to a "page size" structure
 * @param root_table the root page table (e.g. CR3 register)
 * @param phys physical address
 * @param virt virtual address to be mapped to
 * @param flags flags to be set for all page entries for each level.
 */
void pg_map4kib(struct page* p, uintptr_t root_table, uintptr_t phys, uintptr_t virt, size_t flags) {
    size_t pml4e = PG_PML4E(virt);
    size_t pdpte = PG_PDPTE(virt);
    size_t pde = PG_PDE(virt);
    size_t pte = PG_PTE(virt);

    uintptr_t pdpt = pg_get_create_pmle(p, root_table, pml4e, flags & PG_PML4_FLAGS_MASK);
    uintptr_t pd = pg_get_create_pmle(p, pdpt, pdpte, flags & PG_FLAGS_MASK);
    uintptr_t pt = pg_get_create_pmle(p, pd, pde, flags & PG_FLAGS_MASK);
    ((uint64_t*)hhdm_virtual(pt))[pte] = phys | (flags & ~(p->addr_mask)) | PG_PRESENT;
}

/*
 * Map a 2MiB page.
 * @param p pointer to a "page size" structure
 * @param root_table the root page table (e.g. CR3 register)
 * @param phys physical address
 * @param virt virtual address to be mapped to
 * @param flags flags to be set for all page entries for each level.
 */
void pg_map2mib(struct page* p, uintptr_t root_table, uintptr_t phys, uintptr_t virt, size_t flags) {
    size_t pml4e = PG_PML4E(virt);
    size_t pdpte = PG_PDPTE(virt);
    size_t pde = PG_PDE(virt);

    uintptr_t pdpt = pg_get_create_pmle(p, root_table, pml4e, flags & PG_PML4_FLAGS_MASK);
    uintptr_t pd = pg_get_create_pmle(p, pdpt, pdpte, flags & PG_FLAGS_MASK);
    ((uint64_t*)hhdm_virtual(pd))[pde] = phys | PG_PRESENT | PG_PAT_PS | (flags & ~(p->addr_mask)) | (flags & PG_PAT_PS ? PG_PAT_LARGE : 0);
}

/*
 * Map a 1GiB page.
 * @param p pointer to a "page size" structure
 * @param root_table the root page table (e.g. CR3 register)
 * @param phys physical address
 * @param virt virtual address to be mapped to
 * @param flags flags to be set for all page entries for each level.
 */
void pg_map1gib(struct page* p, uintptr_t root_table, uintptr_t phys, uintptr_t virt, size_t flags) {
    size_t pml4e = PG_PML4E(virt);
    size_t pdpte = PG_PDPTE(virt);

    uintptr_t pdpt = pg_get_create_pmle(p, root_table, pml4e, flags & PG_PML4_FLAGS_MASK);
    ((uint64_t*)hhdm_virtual(pdpt))[pdpte] = phys | PG_PRESENT | PG_PAT_PS | (flags & ~(p->addr_mask)) | (flags & PG_PAT_PS ? PG_PAT_LARGE : 0);
}

static struct page page_4kib = {.ps = PG_4KIB, .addr_mask = PG_4KIB_ADDR_MASK, .map = pg_map4kib};
static struct page page_2mib = {.ps = PG_2MIB, .addr_mask = PG_2MIB_ADDR_MASK, .map = pg_map2mib};
static struct page page_1gib = {.ps = PG_1GIB, .addr_mask = PG_1GIB_ADDR_MASK, .map = pg_map1gib};

struct page* largest_pagesz(size_t s, uintptr_t v) {
    for (struct page* p = page_sizes; p != NULL; p = p->next) {
        if (s >= p->ps && (v & (p->ps - 1)) == 0) {
            return p;
        }
    }

    return &page_4kib;
}

void pg_map(uintptr_t root_table, uintptr_t phys, uintptr_t virt, size_t len, size_t flags) {
    uintptr_t end = virt + len;
    while (virt < end) {

        struct page* p = largest_pagesz(len, virt);
        p->map(p, root_table, phys, virt, flags);

        kprintf_trace("Mapped %#llx->%#llx ps=%zu flags=%#zx\n", virt, phys, p->ps, flags);

        phys += p->ps;
        virt += p->ps;
    }
}

void ptable_setup(LIMINE_PTR(struct limine_memmap_response*) memmap, uint64_t hhdm_offset, uintptr_t* ptable_out) {
    assert(memmap != NULL);
    assert(paging_mode_request.response != NULL);

    struct limine_paging_mode_response* paging_mode = paging_mode_request.response;
    assert(paging_mode->mode == LIMINE_PAGING_MODE_X86_64_4LVL);

    kprintf_trace("[KERNEL] %#p\n", &__k_start);
    kprintf_trace("[KERNEL:LIMINE_REQUESTS] %#p-%#p\n", &__k_limreq_start, &__k_limreq_end);
    kprintf_trace("[KERNEL:TEXT] %#p-%#p\n", &__k_text_start, &__k_text_end);
    kprintf_trace("[KERNEL:RODATA] %#p-%#p\n", &__k_rodata_start, &__k_rodata_end);
    kprintf_trace("[KERNEL:DATA] %#p-%#p\n", &__k_data_start, &__k_data_end);
    kprintf_trace("[KERNEL] %#p\n", &__k_end);

    // PAT setup
    uint64_t custom_pat = PAT_WRITEBACK |
                          PAT_WRITE_THROUGH << 8 |
                          PAT_UNCACHEABLE << 16 |
                          PAT_UNCACHEABLE << 24 |
                          PAT_WRITEBACK << 32 |
                          PAT_WRITE_THROUGH << 40 |
                          PAT_WRITE_COMBINING << 48 |
                          PAT_UNCACHEABLE << 56;
    _wrmsr(CPU_PAT_MSR, custom_pat);

    assert(executable_address_request.response != NULL);
    struct limine_executable_address_response* executable_address = executable_address_request.response;
    kprintf_trace("[KERNELBASE_PHYS] %#llx\n", executable_address->physical_base);
    kprintf_trace("[KERNELBASE_VIRT] %#llx\n", executable_address->virtual_base);

    struct cpuid_ctx ctx;
    if (_cpuid(0x80000001, &ctx) != 0) {
        kprintf_error("Couldn't check for extended processor features!\n");
    }

    if (ctx.edx & (1 << 26)) {
        register_pagesz(&page_1gib);
        kprintf_trace("CPU supports 1GiB pages\n");
    }

    register_pagesz(&page_2mib);
    register_pagesz(&page_4kib);



    // map the kernel
    uintptr_t limreq_start_offs = &__k_limreq_start - &__k_start;
    uintptr_t limreq_end_offs = &__k_limreq_end - &__k_start;
    size_t limreq_len = limreq_end_offs - limreq_start_offs;

    uintptr_t text_start_offs = &__k_text_start - &__k_start;
    uintptr_t text_end_offs = &__k_text_end - &__k_start;
    size_t text_len = text_end_offs - text_start_offs;

    uintptr_t rodata_start_offs = &__k_rodata_start - &__k_start;
    uintptr_t rodata_end_offs = &__k_rodata_end - &__k_start;
    size_t rodata_len = rodata_end_offs - rodata_start_offs;

    uintptr_t data_start_offs = &__k_data_start - &__k_start;
    uintptr_t data_end_offs = &__k_data_end - &__k_start;
    size_t data_len = data_end_offs - data_start_offs;

    // page tables are all 4KiB, 8 bytes per entry = 512 entries
    uintptr_t root_table = (uintptr_t)palloc(1);

    // .limine_requests coincides with kernel start
    pg_map(root_table, executable_address->physical_base + limreq_start_offs,
        executable_address->virtual_base + limreq_start_offs,
        limreq_len, PG_KERNEL_READ | PG_XD);
    // .text
    pg_map(root_table, executable_address->physical_base + text_start_offs,
        executable_address->virtual_base + text_start_offs,
        text_len, PG_KERNEL_READ);
    // .rodata
    pg_map(root_table, executable_address->physical_base + rodata_start_offs,
        executable_address->virtual_base + rodata_start_offs,
        rodata_len, PG_KERNEL_READ | PG_XD);
    // .data
    pg_map(root_table, executable_address->physical_base + data_start_offs,
        executable_address->virtual_base + data_start_offs,
        data_len, PG_KERNEL_READ_WRITE | PG_XD);

    kprintf_info("Kernel mapped OK\n");


    // HHDmapping
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];

        switch (entry->type) {
            case LIMINE_MEMMAP_RESERVED: continue;
            case LIMINE_MEMMAP_FRAMEBUFFER:
                pg_map(root_table, entry->base, entry->base + hhdm_offset, entry->length,
                    PG_KERNEL_READ_WRITE | PG_PAT_PS | PG_PCD); // PAT entry 6 (0b110) is WC
                break;
            default:
                pg_map(root_table, entry->base, entry->base + hhdm_offset, entry->length, PG_KERNEL_READ_WRITE);
                break;
        }

        kprintf_trace("Region %#llx-%#llx mapped OK\n", entry->base, entry->base + entry->length);
    }

    if (ptable_out) *ptable_out = root_table;
}
