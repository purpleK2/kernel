#include "elf_psabi.h"
#include <elf/elf.h>

#include <mm/kvmm.h>
#include <mm/pmm.h>

#include <kprintf.h>
#include <macro.h>
#include <memory.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Load ELF into memory
// it's either DYN or EXEC
// REL: you have to change MOST address references (e.g. function, data)
// DYN: middle path, put the program headers at said offsets with a custom base, you have to write external data in the GOT
// EXEC: easiest thing, everything has its base address and you just have to put everything into memory (i'll probably never have to use it)

int elf_loaddyn(void* elf, void** base_out) {
    if (!elf) return -1;
    struct eh_ident* ei = elf;
    if (elf_read(ei) != 0) {
        kprintf_error("Attempted load of a non-ELF executable\n");
        return -2;
    }

    uintptr_t load_base = 0;
    size_t memsz = 0;           // total load "buffer" size

    switch (ei->ei_class) {
        case ECLASS_64BIT:
            struct eh_offs64* offs = elf + sizeof(struct eh_ident);
            struct eh_trailer* eh = (void*)offs + sizeof(struct eh_offs64);
            struct elf_phdr64* phdrs = elf + offs->e_phoff;
            // calculate memory size
            for (uint16_t p = 0; p < eh->e_phnum; p++) {
                struct elf_phdr64* phdr = &phdrs[p];
                if (phdr->p_type != PT_LOAD) continue;
                memsz += ROUND_UP(phdr->p_memsz, phdr->p_align);
            }
            break;
        default:
            kprintf_warn("Invalid ELF class %hhu!\n", ei->ei_class);
            return -3;
    }

    size_t elf_pages = ROUND_UP(memsz, PAGESZ) / PAGESZ;
    load_base = (uintptr_t)kvalloc(elf_pages, VMO_KERNEL_RW, 0);
    *base_out = (void*)load_base;
    // load into memory and apply relocation
    switch (ei->ei_class) {
        case ECLASS_64BIT:
            struct eh_offs64* offs = elf + sizeof(struct eh_ident);
            struct eh_trailer* eh = (void*)offs + sizeof(struct eh_offs64);
            struct elf_phdr64* phdrs = elf + offs->e_phoff;
            for (uint16_t p = 0; p < eh->e_phnum; p++) {
                struct elf_phdr64* phdr = &phdrs[p];
                if (phdr->p_type != PT_LOAD) continue;
                // copy phdr data
                void* data = elf + phdr->p_offset;
                memcpy((void*)load_base + phdr->p_vaddr, data, phdr->p_memsz);
                // TODO: vreflag(vmc, vbase, len, new_flags) to assign correct flags for each header
            }
            size_t relaoffs, relasz, relatabsz;
            size_t stroffs, strtabsz;
            size_t symoffs, symsz;

            // relocations
            for (uint16_t p = 0; p < eh->e_phnum; p++) {
                struct elf_phdr64* phdr = &phdrs[p];
                if (phdr->p_type != PT_DYNAMIC) continue;

                struct elf_dyn64* dyns = elf + phdr->p_offset;
                size_t dyn_entries = phdr->p_filesz / sizeof(struct elf_dyn64);
                for (size_t d = 0; d < dyn_entries; d++) {
                    struct elf_dyn64* dyn = &dyns[d];
                    if (dyn->d_tag == DT_NULL) break;
                    switch (dyn->d_tag) {
                        case DT_SYMTAB:
                            symoffs = dyn->d_val;
                            continue;
                        case DT_STRTAB:
                            stroffs = dyn->d_val;
                            continue;
                        case DT_RELA:
                            relaoffs = dyn->d_val;
                            continue;
                        case DT_RELASZ:
                            relatabsz = dyn->d_val;
                            continue;
                        case DT_RELAENT:
                            relasz = dyn->d_val;
                            continue;
                        case DT_STRSZ:
                            strtabsz = dyn->d_val;
                            continue;
                        case DT_SYMENT:
                            symsz = dyn->d_val;
                            continue;
                        default:
                            continue;
                    }
                }
                char* strtab = elf + stroffs;
                struct elf_rela64* relatab = elf + relaoffs;
                struct elf_sym64 *symtab = elf + symoffs;
                size_t rela_entries = relatabsz / relasz;

                elf_do_rela64((void*)load_base, relatab, strtab, symtab, rela_entries, symsz, strtabsz);
            }
            break;
        default:
            kprintf_warn("Invalid ELF class %hhu!\n", ei->ei_class);
            return -3;
    }

    return 0;
}

int elf_load(void* elf, void** base_out) {
    if (!elf) return -1;
    struct eh_ident* ei = elf;
    if (elf_read(ei) != 0) {
        kprintf_error("Attempted load of a non-ELF executable\n");
        return -2;
    }

    switch (ei->e_type) {
        case ET_DYN:
            if (elf_loaddyn(elf, base_out) != 0) {
                kprintf_error("Couldn't load dynamic ELF!\n");
                return -4;
            }
            break;
        default:
            kprintf_warn("Unknown ELF type %hu\n", ei->e_type);
            return -3;
    }

    return 0;
}
