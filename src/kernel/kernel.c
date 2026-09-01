#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

#include <limine.h>

#include <cpu.h>
#include <arch.h>

#include <macro.h>

// Set the base revision to 6, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// See specification for further info.

LIMINEREQ static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, usually, they should
// be made volatile or equivalent, _and_ they should be accessed at least
// once or marked as used with the "used" attribute as done here.

LIMINEREQ static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

LIMINEREQ static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

// Finally, define the start and end markers for the Limine requests.
// These can also be moved anywhere, to any .c file, as seen fit.

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
    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
        debugf_error("Limine revision %d not supported!\n", limine_base_revision[2]);
        _hcf();
    }

    // Ensure we got a framebuffer.
    if (framebuffer_request.response == NULL
     || framebuffer_request.response->framebuffer_count < 1) {
         debugf_error("No framebuffers available!\n");
        _hcf();
    }

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

    if (!memmap_request.response || !memmap_request.response->entries) {
        debugf_panic("No memory map!\n");
        _hcf();
    }
    struct limine_memmap_response* memmap = memmap_request.response;
    debugf_trace("Limine memory map\n");
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap->entries[i];
        debugf_trace("\t%#llx-%#llx (%s)\n", entry->base, entry->base + entry->length, memmap_entry_types[entry->type]);
    }

    // We're done, just hang...
    _hcf();
}
