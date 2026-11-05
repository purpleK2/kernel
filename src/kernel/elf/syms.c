#include <elf/elf.h>

#include <stddef.h>
#include <stdint.h>

#include <kprintf.h>
#include <string.h>

uintptr_t elf_resolve_sym(void* elf, const char* sym) {
    if (!elf) return 0;

    if (elf_read(elf) != 0) {
        kprintf_warn("File is not a valid ELF!\n");
        return 0;
    }

    struct eh_ident* ei = elf;
    struct eh_trailer* eh;
    if (elf_compute_eh(ei, elf, &eh) != 0) {
        kprintf_warn("Couldn't get ELF header trailer!\n");
        return 0;
    }

    uintptr_t sym_addr = 0;

    switch (ei->ei_class) {
        case ECLASS_64BIT:
            struct eh_offs64* offs = elf + sizeof(struct eh_ident);
            struct elf_shdr64* shdrtab = elf + offs->e_shoff;
            char* strtab = NULL;
            char* shstrtab = NULL;
            struct elf_sym64* symtab = NULL;
            size_t symtab_entries = 0;
            if (eh->e_shstrndx != SH_NULL) shstrtab = elf + shdrtab[eh->e_shstrndx].sh_offset;

            for (int s = 0; s < eh->e_shnum; s++) {
                struct elf_shdr64* shdr = &shdrtab[s];
                switch (shdr->sh_type) {
                    case SH_STRTAB:
                        if (shstrtab && strncmp(&shstrtab[shdr->sh_name], ELF_SH_STRTAB, strlen(ELF_SH_STRTAB)) != 0) continue;
                        strtab = elf + shdr->sh_offset;
                        break;
                    case SH_SYMTAB:
                        if (shstrtab && strncmp(&shstrtab[shdr->sh_name], ELF_SH_SYMTAB, strlen(ELF_SH_SYMTAB)) != 0) continue;
                        symtab = elf + shdr->sh_offset;
                        symtab_entries = shdr->sh_size / shdr->sh_esize;
                        break;
                    default:
                        break;
                }
            }

            if (!strtab || !symtab) {
                kprintf_warn("No symbol/string table!\n");
                return 0;
            }

            for (size_t sym_idx = 0; sym_idx < symtab_entries; sym_idx++) {
                struct elf_sym64* s = &symtab[sym_idx];
                char* symstr = &strtab[s->st_name];
                if (strcmp(symstr, sym) != 0) continue;

                switch (ELF_ST_TYPE64(s->st_info)) {
                    case STB_WEAK:
                        if (sym_addr) break;
                    case STB_GLOBAL:
                    case STB_LOCAL:
                        sym_addr = s->st_value;
                        break;
                    default:
                        break;
                }

            }
            break;
        default:
            kprintf_warn("Unknown/unsupported ELF bit class! Expected %hhu, got %hhu\n", ECLASS_64BIT, ei->ei_class);
            return 0;
    }

    if (!sym_addr) kprintf_warn("Couldn't find address for symbol %s\n", sym);

    return sym_addr;
}
