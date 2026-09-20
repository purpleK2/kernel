#ifndef CPUID_H

#include <stdint.h>

struct cpuid_ctx {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
};

/*
 * Executes CPUID instruction.
 * @param eax CPUID's eax value
 * @param ctx a structure where eax, ebx, ecx, edx registers will be saved.
 * @returns 0 on success, -1 on failure.
 */
extern int _cpuid(uint32_t eax, struct cpuid_ctx* ctx);

#endif
