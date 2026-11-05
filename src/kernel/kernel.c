#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <kprintf.h>

#include <limine.h>

#include <cpu.h>
#include <arch.h>

#include <assert.h>
#include <macro.h>

#include <mm/pmm.h>
#include <paging.h>
#include <mm/vmm.h>
#include <mm/kvmm.h>
#include <mm/kheap.h>

#include <elf/elf.h>
#include <elf/loader.h>
#include <string.h>

LIMINEREQ static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

LIMINEREQ static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

LIMINEREQ static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

LIMINEREQ static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

LIMINEREQ static volatile struct limine_executable_file_request kernel_file_request = {
    .id = LIMINE_EXECUTABLE_FILE_REQUEST_ID,
    .revision = 0
};

LIMINEREQ static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

static const char *memmap_entry_types[9] = {"USABLE",
                                            "RESERVED",
                                            "ACPI_RECLAIMABLE",
                                            "ACPI_NVS",
                                            "BAD",
                                            "BOOTLOADER_RECLAIMABLE",
                                            "KERNEL_AND_MODULES",
                                            "FRAMEBUFFER",
                                            "RESERVED_MAPPED"};

static struct limine_file* kernel_file;

uintptr_t elf_resolve_ksym(const char* sym) {
    return elf_resolve_sym(kernel_file->address, sym);
}

void kmain(void) {
    _disable_interrupts();

    // Ensure the bootloader actually understands our base revision (see spec).
    assert(LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) != false);
    // Check for framebuffers
    assert(framebuffer_request.response != NULL);
    assert(framebuffer_request.response->framebuffer_count >= 1);

    // Fetch the first framebuffer.
    struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];

    kprintf_info("Hello from pk2!\n");
    kprintf_error("ERROR\n");
    kprintf_warn("WARNING\n");
    kprintf_ok("SUCCESS\n");
    kprintf_trace("trace\n");

    arch_entry();

    assert(memmap_request.response != NULL);
    assert(memmap_request.response->entries != NULL);

    struct limine_memmap_response* memmap = memmap_request.response;
    kprintf_trace("Limine memory map\n");
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap->entries[i];
        kprintf_trace("\t%#llx-%#llx (%s)\n", entry->base, entry->base + entry->length, memmap_entry_types[entry->type]);
    }

    assert(hhdm_request.response != NULL);
    struct limine_hhdm_response* hhdm = hhdm_request.response;

    pmm_init(memmap, hhdm->offset);
    kprintf_ok("PMM init\n");
    uintptr_t table;
    ptable_setup(memmap, hhdm->offset, &table);
    kprintf_trace("Switching w/ table @ %p\n", table);
    pg_switch(table);
    kprintf_ok("paging\n");

    // Print a nice pattern to screen as an example.
    // Note: we assume the framebuffer model is RGB with 32-bit pixels.
    volatile uint32_t *fb_ptr = framebuffer->address;
    for (size_t y = 0; y < framebuffer->height; y++) {
        for (size_t x = 0; x < framebuffer->width; x++) {
            uint32_t nX = x * 255 / framebuffer->width;
            uint32_t nY = y * 255 / framebuffer->height;
            fb_ptr[y * (framebuffer->pitch / 4) + x] = (nY << 8) | nX;
        }
    }

    struct vmc* kvmc = kvmm_init(table);
    kheap_init(kvmc);
    kprintf_ok("Kernel heap init\n");

    assert(kernel_file_request.response != NULL);
    struct limine_executable_file_response* kernel_file_response = kernel_file_request.response;
    kernel_file = kernel_file_response->executable_file;
    kprintf_trace("Kernel executable loaded @ %p size=%llu\n", kernel_file->address, kernel_file->size);

    assert(elf_read(kernel_file->address) == 0);

    if (module_request.response) {
        assert(module_request.response->modules);
        struct limine_file** modules = module_request.response->modules;
        for (uint64_t m = 0; m < module_request.response->module_count; m++) {
            struct limine_file* mod = modules[m];
            kprintf_trace("Loaded module %s cmdline=%s\n", mod->path, mod->string);

            if (strcmp(mod->string, "elf_test") == 0) {
                void* load;
                elf_load(mod->address, &load);
                uintptr_t entry = elf_resolve_sym(mod->address, "main");
                if (!entry) {
                    kprintf_warn("main not found!\n");
                } else {
                    kprintf_trace("Found entry @ offset %#llx\n", entry);
                    void (*main)(void) = load + entry;
                    kprintf_trace("Loading ELF @ %#p\n", main);
                    main();
                }
            }
        }
    }

    // We're done, just hang...
    _hcf();
}
