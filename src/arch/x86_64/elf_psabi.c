#include <elf_psabi.h>
#include <elf/elf.h>

#include <kprintf.h>
#include <stdint.h>
#include <x86_64.h>

void elf_do_rela64(void* load_base, struct elf_rela64* relatab, char* strtab, struct elf_sym64* symtab,
    size_t rela_entries, size_t symsz, size_t strtabsz)
{
    for (size_t r = 0; r < rela_entries; r++) {
        struct elf_rela64* rela = &relatab[r];
        size_t s = ELF_REL_SYM64(rela->r_info);     // index in symbol table
        struct elf_sym64* sym = &symtab[s];
        char* symname = &strtab[sym->st_name];
        uintptr_t ksym;
        kprintf_trace("Relocate symbol: %s\n", symname);
        void* target = load_base + rela->r_offset;

        kprintf_trace("Relocation @ %p type=%#llx\n", target, ELF_REL_TYPE64(rela->r_info));
        switch (ELF_REL_TYPE64(rela->r_info)) {
            case R_X8664_64:
                // Symbol address + addend
                ksym = elf_resolve_ksym(symname);
                *(uint64_t*)target = (uint64_t)ksym + rela->r_addend;
                break;
            case R_X8664_GLOB_DAT:
            case R_X8664_JUMP_SLOT:
                // Symbol address
                ksym = elf_resolve_ksym(symname);
                *(uint64_t*)target = (uint64_t)ksym;
                break;
            case R_X8664_RELATIVE:
                // Program base + addend
                *(uint64_t*)target = (uint64_t)load_base + rela->r_addend;
                break;
            default:
                // Unknown
                kprintf_warn("Unsupported relocation type %#llx!\n", ELF_REL_TYPE64(rela->r_info));
                break;
        }
    }
}
