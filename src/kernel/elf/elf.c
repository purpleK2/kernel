#include <elf/elf.h>

#include <limine.h>
#include <stddef.h>
#include <string.h>
#include <kprintf.h>

#include <mm/vmo.h>

/* Convert program header flags to VMO flags.
 * @param pflags program header flags
 * @returns VMO flags
 */
size_t vflags_from_pflags(size_t pflags) {
    size_t f = VMO_PRESENT | VMO_NX;
    if (pflags & PF_W) f |= VMO_WRITE;
    if (pflags & PF_X) f &= ~VMO_NX;
    return f;
}

int elf_read(void* buf) {
    if (!buf) return -1;
    struct eh_ident* ei = buf;
    if (strncmp((char*)&ei->ei_mag, EI_MAGIC, strlen(EI_MAGIC)) != 0) {
        kprintf_warn("Invalid ELF header!\n");
        return -1;
    }

    switch (ei->ei_class) {
        case ECLASS_64BIT:
            break;
        default:
            kprintf_warn("Unknown/unsupported ELF bit class! Expected %hhu, got %hhu\n", ECLASS_64BIT, ei->ei_class);
            return -3;
    }

    return 0;
}

int elf_compute_eh(struct eh_ident* ei, void* buf, struct eh_trailer** out) {
    if (!ei || !buf || !out || elf_read(buf) != 0) return -1;

    switch (ei->ei_class) {
        case ECLASS_64BIT:
            *out = buf + (sizeof(struct eh_ident) + sizeof(struct eh_offs64));
            break;
        default:
            kprintf_warn("Unknown/unsupported ELF bit class! Expected %hhu, got %hhu\n", ECLASS_64BIT, ei->ei_class);
            return -2;
    }

    return 0;
}
