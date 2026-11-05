#ifndef ELF_H
#define ELF_H

#include <stdint.h>
#include <stdbool.h>

#include <limine.h>

#define EI_MAGIC   "\x7f" "ELF"

#define ELF_REL_SYM64(i)    (i) >> 32
#define ELF_REL_TYPE64(i)   (i) & 0xffffffff

#define ELF_ST_BIND64(i)   ((i)>>4)
#define ELF_ST_TYPE64(i)   ((i)&0xf)

#define ELF_SH_STRTAB   ".strtab"
#define ELF_SH_SYMTAB   ".symtab"


/* EI_CLASS */
enum elf_class {
    ECLASS_NONE = 0x0,
    ECLASS_32BIT = 0x1,
    ECLASS_64BIT = 0x2
};

/* EI_DATA */
enum elf_data {
    EDATA_LITTLE = 0x1,
    EDATA_BIG = 0x2
};

/* EI_OSABI */
enum elf_abi {
    ABI_SYSV = 0x0,
    ABI_HPUX = 0x1,
    ABI_NBSD = 0x2,
    ABI_LINUX = 0x3,
    ABI_HURD = 0x4,
    ABI_SLRS = 0x6,     // Solaris
    ABI_AIX = 0x7,
    ABI_IRIX = 0x8,
    ABI_FBSD = 0x9,
    ABI_TRU64 = 0xA,
    ABI_NMOD = 0xB,     // Novell Modesto
    ABI_OBSD = 0xC,
    ABI_OVMS = 0xD,
    ABI_NSKR = 0xE,
    ABI_AROS = 0xF,
    ABI_FENX = 0x10,    // FenixOS
    ABI_NUXI = 0x11,
    ABI_OVOS = 0x12     // Stratus Technologies OpenVOS
};

/* EI_VERSION */
enum elf_version {
    EV_NONE = 0,
    EV_CURRENT = 1
};

/* e_type */
enum elf_etype {
    ET_NONE = 0x0,
    ET_REL = 0x1,
    ET_EXEC = 0x2,
    ET_DYN = 0x3,
    ET_CORE = 0x4,
    ET_LOOS = 0xFE00,
    ET_HIOS = 0xFEFF,
    ET_LOPROC = 0xFF00,
    ET_HIPROC = 0xFFFF
};

enum elf_shtype : uint32_t {
    SH_NULL           = 0,
    SH_PROGBITS       = 1,
    SH_SYMTAB         = 2,
    SH_STRTAB         = 3,
    SH_RELA           = 4,
    SH_HASH           = 5,
    SH_DYNAMIC        = 6,
    SH_NOTE           = 7,
    SH_NOBITS         = 8,
    SH_REL            = 9,
    SH_SHLIB          = 10,
    SH_DYNSYM         = 11,
    SH_INIT_ARRAY     = 14,
    SH_FINI_ARRAY     = 15,
    SH_PREINIT_ARRAY  = 16,
    SH_GROUP          = 17,
    SH_SYMTAB_SHNDX   = 18,
    SH_RELR           = 19,
    SH_LOOS           = 0x60000000,
    SH_HIOS           = 0x6fffffff,
    SH_LOPROC         = 0x70000000,
    SH_HIPROC         = 0x7fffffff,
    SH_LOUSER         = 0x80000000,
    SH_HIUSER         = 0xffffffff,
};

/* p_type */
enum elf_phtype {
    PT_NULL = 0,
    PT_LOAD = 1,
    PT_DYNAMIC = 2,
    PT_INTERP = 3,
    PT_NOTE = 4,
    PT_SHLIB = 5,
    PT_PHDR = 6,
    PT_TLS = 7,
    PT_LOOS = 0x60000000,
    PT_HIOS = 0x6fffffff,
    PT_LOPROC = 0x70000000,
    PT_HIPROC = 0x7fffffff
};

/* p_flags */
enum elf_pflags {
    PF_X = 1 << 0,
    PF_W = 1 << 1,
    PF_R = 1 << 2,
    PF_OSMASK = 0x0ff00000,
    PF_PROCMASK = 0xf0000000
};

/* d_tag */
enum elf_dtag {
    DT_NULL = 0,
    DT_HASH = 4,
    DT_GNU_HASH = 4,
    DT_STRTAB = 5,
    DT_SYMTAB = 6,
    DT_RELA = 7,
    DT_RELASZ = 8,
    DT_RELAENT = 9,
    DT_STRSZ = 10,
    DT_SYMENT = 11
};

/* st_info */
enum sym_type {
    STB_LOCAL = 0,
    STB_GLOBAL = 1,
    STB_WEAK = 2,
    STB_LOOS = 10,
    STB_HIOS = 12,
    STB_LOPROC = 13,
    STB_HIPROC = 15
};

struct eh_ident {
    uint32_t ei_mag;
    uint8_t ei_class;
    uint8_t ei_data;
    uint8_t ei_version;
    uint8_t ei_osabi;
    uint8_t ei_abiversion;
    uint8_t ei_pad[7];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
};

struct eh_offs64 {
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
};

struct eh_trailer {
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct elf_shdr64 {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_align;
    uint64_t sh_esize;  // table entry size
};

struct elf_phdr64 {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

struct elf_dyn64 {
    int64_t d_tag;
    uint64_t d_val;
};

struct elf_sym64 {
    uint32_t st_name;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
};

struct elf_rela64 {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t r_addend;
};

/*
 * Checks if an ELF file
 * @param buf buffer to read file data
 * @returns 0 on successful read, non-zero otherwise
 */
int elf_read(void* buf);

/*
 * Get the ELF header trailer.
 * @param ELF identification header
 * @param buf buffer where AT LEAST the whole ELF header can be accessed
 * @param out where the pointer to eh will be saved
 * @returns 0 on successful opperation, nonzero on error
 */
int elf_compute_eh(struct eh_ident* ei, void* buf, struct eh_trailer** out);

/*
 * Get the symbol address.
 * @param elf buffer where AT LEAST the header, section, string and symbol table can be accessed.
 * @param sym symbol name to find
 * @returns offset of symbol into elf if found, 0 otherwise.
 */
uintptr_t elf_resolve_sym(void* elf, const char* sym);

/*
 * Resolve a kernel symbol.
 * @param sym symbol name to resolve
 * @returns address of symbol if found, 0 otherwise.
 * @notes implementation is in kernel.c to avoid implementing a dumb set_kernel_file function in elf.c (this AIN'T java bro)
 */
uintptr_t elf_resolve_ksym(const char* sym);

#endif
