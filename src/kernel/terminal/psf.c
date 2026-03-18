#include "psf.h"

#include <graphical/framebuffer.h>
#include <kernel.h>
#include <terminal/font.h>
#include <terminal/terminal.h>

#include <stdio.h>

PSF1Header *psf;
struct limine_framebuffer *framebuffer_b;

bool psfLoad(void *buffer) {
    PSF1Header *header = (PSF1Header *)buffer;

    if (header->magic != PSF1_MAGIC) {
        debugf("Invalid PSF magic! Only PSF1 is supported{0x0436} "
               "supplied{%04X}\n",
               header->magic);
        return false;
    }

    if (!(header->mode & PSF1_MODE512) && !(header->mode & PSF1_MODEHASTAB)) {
        debugf("Invalid PSF mode!\n");
        return false;
    }

    psf           = buffer;
    framebuffer_b = get_bootloader_data()->framebuffer;

    return true;
}

bool psfLoadDefaults() {
    return psfLoad(&u_vga16_psf[0]);
}

void psfPutC(char c, uint32_t x, uint32_t y, uint32_t fg_r, uint32_t fg_g,
             uint32_t fg_b, uint32_t bg_r, uint32_t bg_g, uint32_t bg_b) {
    uint8_t *targ =
        (uint8_t *)((size_t)psf + sizeof(PSF1Header) + c * psf->height);
    for (int i = 0; i < psf->height; i++) {
        for (int j = 0; j < 8; j++) {
            if (targ[i] & (1 << (8 - j))) // NOT little endian
                drawPixel(x + j, y + i, fg_r, fg_g, fg_b);
            else
                drawPixel(x + j, y + i, bg_r, bg_g, bg_b);
        }
    }
}