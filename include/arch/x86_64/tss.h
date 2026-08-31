#ifndef TSS_H
#define TSS_H

#include <stdint.h>

#include <macro.h>

struct tss{
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved;
    uint16_t iobase;
} PACKED;

#endif
