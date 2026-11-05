#ifndef LOADER_H
#define LOADER_H

/*
 * Loads an ELF into memory.
 * @param elf ELF file buffer
 * @param base_out pointer to variable to write load base address onto
 */
int elf_load(void* elf, void** base_out);

#endif
