#include "fbdev.h"
#include "kernel.h"
#include "limine.h"
#include "memory/pmm/pmm.h"
#include "memory/vmm/vmm.h"
#include "scheduler/scheduler.h"
#include "util/macro.h"

#include <errors.h>
#include <memory/heap/kheap.h>
#include <stdint.h>
#include <string.h>

void dev_fb_init() {
    device_t *dev = kmalloc(sizeof(device_t));
    memcpy(dev->name, "fb0", DEVICE_NAME_MAX);
    dev->type          = DEVICE_TYPE_BLOCK;
    dev->write         = dev_fb_write;
    dev->read          = dev_fb_read;
    dev->ioctl         = dev_fb_ioctl;
    dev->mmap          = dev_fb_mmap;
    dev->dev_node_path = "fb0";
    dev->data          = get_bootloader_data()->framebuffer;
    register_device(dev);
}

int dev_fb_write(device_t *dev, const void *buffer, size_t size,
                 size_t offset) {
    struct limine_framebuffer *fb = (struct limine_framebuffer *)dev->data;
    size_t fb_size                = fb->pitch * fb->height;

    if (offset >= fb_size) {
        return -EINVAL;
    }

    if (offset + size > fb_size) {
        size = fb_size - offset;
    }

    size_t pixel_size = fb->bpp / 8;
    if (pixel_size < 3) {
        return -EINVAL; // unsupported format
    }

    uint8_t *dst       = (uint8_t *)fb->address + offset;
    const uint8_t *src = (const uint8_t *)buffer;

    size_t processed = 0;
    while (processed + 3 <= size) {
        dst[0] = src[2]; // B
        dst[1] = src[1]; // G
        dst[2] = src[0]; // R

        if (pixel_size == 4) {
            dst[3] = 0xFF;
        }

        dst       += pixel_size;
        src       += 3;
        processed += 3;
    }

    return EOK;
}

#include <memory/mmap.h>
#include <paging/paging.h>
#include <memory/pmm/pmm.h>

#define ALIGN_UP(addr, align)                                                  \
    ((((uint64_t)(addr)) + ((align) - 1)) & ~((align) - 1))

#define ALIGN_DOWN(addr, align)                                                \
    (((uint64_t)(addr)) & ~((align) - 1))

int dev_fb_mmap(device_t *dev, void *addr, size_t length, int prot, int flags,
                size_t offset) {
    struct limine_framebuffer *fb =
        (struct limine_framebuffer *)dev->data;

    size_t fb_size = fb->pitch * fb->height;

    if (offset != 0) {
        return -EINVAL;
    }

    if (!(flags & MAP_SHARED)) {
        return -EINVAL;
    }

    if (length > fb_size) {
        return -EINVAL;
    }

    uint64_t fb_phys = VIRT_TO_PHYSICAL((uintptr_t)fb->address);

    uint64_t phys_start = ALIGN_DOWN(fb_phys, PFRAME_SIZE);
    uint64_t phys_end =
        ALIGN_UP(fb_phys + length, PFRAME_SIZE);

    size_t map_len = phys_end - phys_start;

    uint64_t page_flags =
        PMLE_PRESENT |
        PMLE_USER;

    if (prot & PROT_WRITE) {
        page_flags |= PMLE_WRITE;
    }

    page_flags |= PMLE_PCD | PMLE_PWT;

    if (addr == 0) {
        valloc(get_current_pcb()->vmc, map_len / PFRAME_SIZE, page_to_vmo_flags(page_flags), (void*)fb_phys);
    } else {
        valloc_at(get_current_pcb()->vmc, addr, map_len / PFRAME_SIZE, page_to_vmo_flags(page_flags), (void*)fb_phys);
    }

    return EOK;
}

int dev_fb_read(device_t *dev, void *buffer, size_t size, size_t offset) {
    UNUSED(dev);
    UNUSED(buffer);
    UNUSED(size);
    UNUSED(offset);
    return -ENOSYS;
}
int dev_fb_ioctl(device_t *dev, int request, void *argp) {
    switch (request) {
        case FB_IOCTL_GET_INFO: {
            if (!argp) {
                return -EINVAL;
            }

            uint64_t phys_page = pg_virtual_to_phys((uint64_t*)PHYS_TO_VIRTUAL(get_current_pcb()->vmc->pml4_table), (uintptr_t)argp);
            uint64_t kernel_addr = (uint64_t)PHYS_TO_VIRTUAL(phys_page) + ((uintptr_t)argp & 0xFFF);

            fb_info_t *info = (fb_info_t *)kernel_addr;
            struct limine_framebuffer *fb =
                (struct limine_framebuffer *)dev->data;

            info->width = fb->width;
            info->height = fb->height;
            info->pitch = fb->pitch;
            info->bpp = fb->bpp;

            return EOK;
        }
        default:
            return -EINVAL;
    }
}
