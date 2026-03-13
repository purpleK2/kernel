// HUGE thanks to malwarepad
#ifndef PSF_H
#define PSF_H 1

#include <kernel.h>
#include <limine.h>

#include <stdbool.h>
#include <stdint.h>

#define PSF1_MAGIC 0x0436

typedef struct PSF1Header {
    uint16_t magic;
    uint8_t mode;
    uint8_t height; // width is always 8px
} PSF1Header;

typedef enum PSF1_MODES {
    PSF1_MODE512    = 0x01,
    PSF1_MODEHASTAB = 0x02,
    PSF1_MODESEQ    = 0x04,
} PSF1_MODES;

extern PSF1Header *psf;
extern struct limine_framebuffer *framebuffer_b;

bool psfLoadDefaults();
bool psfLoad(void *buffer);

void psfPutC(char c, uint32_t x, uint32_t y, uint32_t fg_r, uint32_t fg_g,
             uint32_t fg_b, uint32_t bg_r, uint32_t bg_g, uint32_t bg_b);

#endif // PSF_H