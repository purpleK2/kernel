/*
 * psABI-specific ELF components (eg. relocation).
 */

#ifndef ELF_PSABI
#define ELF_PSABI

#include <stddef.h>

#include <elf/elf.h>

/*
 * Apply relocations with addends (64-bits ELFs).
 * @param load_base base address of ELF load
 * @param relatab RELA table structure
 * @param strtab address of string table
 * @param symtab address of symbol table
 * @param rela_entries entries in RELA table
 * @param symsz size of symbol table
 * @param strtabsz size of string table
 */
void elf_do_rela64(void* load_base, struct elf_rela64* relatab, char* strtab, struct elf_sym64* symtab,
    size_t rela_entries, size_t symsz, size_t strtabsz);

#endif
