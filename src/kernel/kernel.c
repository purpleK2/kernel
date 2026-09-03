#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

#include <limine.h>

#include <cpu.h>
#include <arch.h>

#include <assert.h>
#include <macro.h>

#include <mm/pmm.h>

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

void kmain(void) {
    _disable_interrupts();

    // Ensure the bootloader actually understands our base revision (see spec).
    assert(LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) != false);
    // Check for framebuffers
    assert(framebuffer_request.response != NULL);
    assert(framebuffer_request.response->framebuffer_count >= 1);

    // Fetch the first framebuffer.
    struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];

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

    debugf("Hello from pk2!\n");
    debugf_error("ERROR\n");
    debugf_warn("WARNING\n");
    debugf_ok("SUCCESS\n");
    debugf_trace("trace\n");

    arch_entry();

    assert(memmap_request.response != NULL);
    assert(memmap_request.response->entries != NULL);

    struct limine_memmap_response* memmap = memmap_request.response;
    debugf_trace("Limine memory map\n");
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap->entries[i];
        debugf_trace("\t%#llx-%#llx (%s)\n", entry->base, entry->base + entry->length, memmap_entry_types[entry->type]);
    }

    assert(hhdm_request.response != NULL);
    struct limine_hhdm_response* hhdm = hhdm_request.response;

    pmm_init(memmap, hhdm->offset);

    // We're done, just hang...
    _hcf();
}
