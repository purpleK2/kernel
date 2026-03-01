#include "elfloader.h"
#include "auxv.h"
#include "caps.h"
#include "dev/tty/tty.h"
#include "elf/elf.h"
#include "fs/devfs/devfs.h"
#include "fs/fd.h"
#include "fs/vfs/vfs.h"
#include "loader/binfmt.h"
#include "user/access.h"
#include "user/user.h"
#include "util/macro.h"

#include <autoconf.h>
#include <errors.h>
#include <kernel.h>
#include <cpu.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <fs/file_io.h>
#include <memory/heap/kheap.h>
#include <memory/pmm/pmm.h>
#include <memory/vmm/vflags.h>
#include <memory/vmm/vmm.h>
#include <paging/paging.h>
#include <scheduler/scheduler.h>

#include <util/assert.h>

binfmt_loader_t elf_binfmt_loader = {
    .name       = "ELF64",
    .magic      = (const uint8_t[4]){ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3},
    .magic_size = 4,
    .load       = load_elf,
};

int elf_validate(const Elf64_Ehdr *eh) {
    if (eh->e_ident[EI_MAG0] != ELFMAG0) return -1;
    if (eh->e_ident[EI_MAG1] != ELFMAG1) return -1;
    if (eh->e_ident[EI_MAG2] != ELFMAG2) return -1;
    if (eh->e_ident[EI_MAG3] != ELFMAG3) return -1;
    if (eh->e_ident[EI_CLASS] != ELFCLASS64) return -1;
    if (eh->e_ident[EI_DATA] != ELFDATA2LSB) return -1;
    if (eh->e_machine != EM_X86_64) return -1;
    if (eh->e_type != ET_EXEC && eh->e_type != ET_DYN) return -1;
    if (eh->e_phentsize != sizeof(Elf64_Phdr)) return -1;
    return 0;
}

static uint64_t elf_pf_to_page_flags(uint32_t pf_flags) {
    uint64_t flags = PMLE_PRESENT | PMLE_USER;
    
    if (pf_flags & PF_W) {
        flags |= PMLE_WRITE;
    }
    
    if (!(pf_flags & PF_X)) {
        flags |= PMLE_NOT_EXECUTABLE;
    }

    if ((pf_flags & PF_W) && (pf_flags & PF_X)) {
        debugf_warn("Warning W+X segment detected!\n");
    }
    
    return flags;
}

static int count_strings(const char **arr) {
    if (!arr) return 0;
    int count = 0;
    while (arr[count] != NULL) {
        count++;
    }
    return count;
}

static inline void *user_va_to_kernel_va(const tcb_t *thread,
                                        uint64_t user_va,
                                        uint64_t user_stack_bottom_va,
                                        uint64_t user_stack_top_va)
{
    if (user_va < user_stack_bottom_va || user_va >= user_stack_top_va) {
        debugf_warn("Invalid user VA 0x%llx for stack [0x%llx - 0x%llx]\n",
                    user_va, user_stack_bottom_va, user_stack_top_va);
        return NULL;
    }

    uint64_t offset = user_va - user_stack_bottom_va;
    return (char *)thread->user_stack + offset;
}

static int setup_initial_stack(pcb_t *proc,
                               const char **argv, int argc,
                               const char **envp, int envc,
                               Elf64_auxv_t *auxv, int auxc,
                               uint64_t load_bias, uint64_t phdr_vaddr)
{
    tcb_t *thread = proc->main_thread;
    if (!thread || !thread->user_stack)
        return -EINVAL;
    uint64_t user_stack_bottom_va = USER_STACK_TOP - SCHEDULER_STACKSZ;


    size_t argv_strings_bytes = 0;
    for (int i = 0; i < argc; i++) {
        if (argv[i]) argv_strings_bytes += strlen(argv[i]) + 1;
    }

    size_t env_strings_bytes = 0;
    for (int i = 0; i < envc; i++) {
        if (envp[i]) env_strings_bytes += strlen(envp[i]) + 1;
    }

    size_t platform_len = sizeof("x86_64");

    size_t strings_total = platform_len + argv_strings_bytes + env_strings_bytes;
    size_t strings_aligned = (strings_total + 15) & ~0xF;

    size_t argv_array_bytes = (argc + 1) * sizeof(uint64_t);
    size_t envp_array_bytes = (envc + 1) * sizeof(uint64_t);

    size_t auxv_bytes = (auxc + 1) * sizeof(Elf64_auxv_t);

    size_t argc_bytes = sizeof(uint64_t);

    size_t total_needed = strings_aligned +
                          argv_array_bytes +
                          envp_array_bytes +
                          auxv_bytes +
                          argc_bytes;

    total_needed = (total_needed + 15) & ~0xF;

    uint64_t rsp = USER_STACK_TOP & ~0xF;
    rsp -= total_needed;

    if (rsp < user_stack_bottom_va) {
        debugf_warn("User stack overflow: needed %zu bytes, only %llu bytes available "
                    "(top=0x%llx, bottom=0x%llx, rsp=0x%llx)\n",
                    total_needed,
                    USER_STACK_TOP - user_stack_bottom_va,
                    USER_STACK_TOP,
                    user_stack_bottom_va,
                    rsp);
        return -ENOMEM;
    }

    uint64_t pos = rsp;

    uint64_t *k_argc = (uint64_t *) user_va_to_kernel_va(thread, pos,
                                                     user_stack_bottom_va,
                                                     USER_STACK_TOP);
    if (!k_argc) return -ENOMEM;
    *k_argc = (uint64_t) argc;
    pos += sizeof(uint64_t);

    uint64_t *k_argv = (uint64_t *) user_va_to_kernel_va(thread, pos,
                                                     user_stack_bottom_va,
                                                     USER_STACK_TOP);
    if (!k_argv) return -ENOMEM;
    pos += argv_array_bytes;

    uint64_t *k_envp = (uint64_t *) user_va_to_kernel_va(thread, pos,
                                                     user_stack_bottom_va,
                                                     USER_STACK_TOP);
    if (!k_envp) return -ENOMEM;
    pos += envp_array_bytes;

    Elf64_auxv_t *k_auxv = (Elf64_auxv_t *) user_va_to_kernel_va(thread, pos,
                                                     user_stack_bottom_va,
                                                     USER_STACK_TOP);
    if (!k_auxv) return -ENOMEM;
    pos += auxv_bytes;

    char *k_str_area = (char *) user_va_to_kernel_va(thread, pos,
                                                     user_stack_bottom_va,
                                                     USER_STACK_TOP);
    if (!k_str_area) return -ENOMEM;
    char *k_str_write = k_str_area;

    uint64_t str_area_user_va = pos;
    char *str_write_user_va   = (char *) str_area_user_va;

    uint64_t platform_user_va = (uint64_t)str_write_user_va;

    memcpy(k_str_write, "x86_64", platform_len);
    k_str_write += platform_len;
    str_write_user_va += platform_len;

    for (int i = 0; i < argc; i++) {
        if (argv[i]) {
            size_t len = strlen(argv[i]) + 1;
            memcpy(k_str_write, argv[i], len);
            k_argv[i] = (uint64_t) str_write_user_va;
            k_str_write      += len;
            str_write_user_va += len;
        } else {
            k_argv[i] = 0;
        }
    }
    k_argv[argc] = 0;

    for (int i = 0; i < envc; i++) {
        if (envp[i]) {
            size_t len = strlen(envp[i]) + 1;
            memcpy(k_str_write, envp[i], len);
            k_envp[i] = (uint64_t) str_write_user_va;
            k_str_write      += len;
            str_write_user_va += len;
        } else {
            k_envp[i] = 0;
        }
    }
    k_envp[envc] = 0;

    memcpy(k_auxv, auxv, auxc * sizeof(Elf64_auxv_t));

    for (int i = 0; i < auxc; i++) {
        if (k_auxv[i].a_type == AT_PLATFORM ||
            k_auxv[i].a_type == AT_BASE_PLATFORM) {
            k_auxv[i].a_un.a_val = platform_user_va;
        }
        if (k_auxv[i].a_type == AT_EXECFN) {
            k_auxv[i].a_un.a_val = k_argv[0];
        }
    }

    k_auxv[auxc].a_type = AT_NULL;
    k_auxv[auxc].a_un.a_val = 0;

    thread->regs->rsp = rsp;

    debugf_debug("User stack prepared:\n"
                 "  rsp        = 0x%016llx (0x%016llx)  <- argc lives here\n"
                 "  argv[]     @ 0x%016llx\n"
                 "  envp[]     @ 0x%016llx\n"
                 "  auxv[]     @ 0x%016llx\n"
                 "  strings    @ 0x%016llx - 0x%016llx\n"
                 "  argc       = %llu\n",
                 rsp, thread->regs->rsp,
                 rsp + argc_bytes,
                 rsp + argc_bytes + argv_array_bytes,
                 rsp + argc_bytes + argv_array_bytes + envp_array_bytes,
                 str_area_user_va,
                 (uint64_t)str_write_user_va,
                 (unsigned long long)argc);

    return EOK;
}


int load_elf(const char *path, const char **argv, const char **envp, binfmt_program_t *out) {
    asm volatile("cli");
    if (!path || !out) {
        return -EFAULT;
    }

    debugf_debug("Loading ELF binary from %s\n", path);

    uint64_t dynamic_vaddr = 0;
    uint64_t tls_vaddr = 0;
    uint64_t tls_offset = 0;
    uint64_t tls_filesz = 0;
    uint64_t tls_memsz = 0;
    uint64_t tls_align = 8;

    fileio_t *elf_file = open(path, 0, 0);
    if (!elf_file || (int64_t)elf_file < 0) {
        debugf_warn("Failed to open ELF file %s: %d\n", path, (int64_t)elf_file);
        return -ENOENT;
    }

    Elf64_Ehdr eh;
    if (read(elf_file, sizeof(Elf64_Ehdr), (char *)&eh) != sizeof(Elf64_Ehdr)) {
        debugf_warn("Failed to read ELF header from %s\n", path);
        close(elf_file);
        return -EIO;
    }

    if (elf_validate(&eh) != 0) {
        debugf_warn("ELF file %s is not valid\n", path);
        close(elf_file);
        return -EINVAL;
    }

    debugf_debug("ELF: entry=0x%llx phnum=%d\n", eh.e_entry, eh.e_phnum);

    uint64_t load_bias = 0;

    if (eh.e_type == ET_DYN) {
        load_bias = choose_et_dyn_base();
        debugf_debug("ET_DYN detected, load bias = 0x%llx\n", load_bias);
    }


    Elf64_Phdr *phdrs = kmalloc(sizeof(Elf64_Phdr) * eh.e_phnum);
    if (!phdrs) {
        close(elf_file);
        return -ENOMEM;
    }

    seek(elf_file, eh.e_phoff, SEEK_SET);
    if (read(elf_file, sizeof(Elf64_Phdr) * eh.e_phnum, (char *)phdrs) !=
        sizeof(Elf64_Phdr) * eh.e_phnum) {
        debugf_warn("Failed to read ELF program headers from %s\n", path);
        kfree(phdrs);
        close(elf_file);
        return -EIO;
    }

    for (int i = 0; i < eh.e_phnum; i++) {
        Elf64_Phdr *phdr = &phdrs[i];
        
        if (phdr->p_type == PT_TLS) {
            tls_filesz = phdr->p_filesz;
            tls_memsz = phdr->p_memsz;
            tls_vaddr = phdr->p_vaddr;
            tls_offset = phdr->p_offset;
            tls_align = phdr->p_align > 0 ? phdr->p_align : 8;
            
            debugf_debug("Found PT_TLS: vaddr=0x%llx offset=0x%llx filesz=%llu memsz=%llu align=%llu\n",
                         tls_vaddr, tls_offset, tls_filesz, tls_memsz, tls_align);
            break;
        }
    }

    char *proc_name = strdup(path);
    int pid = proc_create((void (*)(void))eh.e_entry + load_bias, TF_MODE_USER, proc_name);
    if (pid < 0) {
        debugf_warn("Failed to create process for ELF %s\n", path);
        kfree(phdrs);
        close(elf_file);
        return pid;
    }

    pcb_t *proc = pcb_lookup(pid);
    if (!proc || !proc->vmc) {
        debugf_warn("Failed to lookup process PID=%d\n", pid);
        kfree(phdrs);
        close(elf_file);
        return -EINVAL;
    }

    vnode_t *vnode = elf_file->private;
    if (vnode->mode & S_ISUID) {
        proc->cred->euid = vnode->uid;
        debugf_debug("Setting SETUID bit for PID=%d due to SUID file\n", pid);
    }

    if (vnode->mode & S_ISGID) {
        proc->cred->egid = vnode->gid;
        debugf_debug("Setting SETGID bit for PID=%d due to SGID file\n", pid);
    }

    if (pid == 1) {
        fileio_t *console = open("/dev/console", 0, 0);
        if (console && (int64_t)console > 0) {
            fd_alloc(&proc->fd_table, FD_FILE, console);
            fd_alloc(&proc->fd_table, FD_FILE, console);
            fd_alloc(&proc->fd_table, FD_FILE, console);
            
            vnode_t *console_vnode = (vnode_t *)console->private;
            if (console_vnode && console_vnode->node_data) {
                devfs_node_t *devfs_node = (devfs_node_t *)console_vnode->node_data;
                if (devfs_node->device && devfs_node->device->data) {
                    tty_t *tty = (tty_t *)devfs_node->device->data;
                    proc->ctty = tty;
                    tty->fg_pgrp = proc->pgid;
                }
            }
            
            debugf_debug("Set up FDs 0/1/2 -> /dev/console for PID 1 (init)\n");
        } else {
            debugf_warn("Failed to open /dev/console for init process\n");
        }
    }

    uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRTUAL(proc->vmc->pml4_table);

    for (int i = 0; i < eh.e_phnum; i++) {
        Elf64_Phdr *phdr = &phdrs[i];

        if (phdr->p_type == PT_DYNAMIC) {
            uint64_t dyn_start = ROUND_DOWN(phdr->p_vaddr + load_bias, PFRAME_SIZE);
            uint64_t dyn_end   = ROUND_UP(phdr->p_vaddr + load_bias + phdr->p_memsz, PFRAME_SIZE);
            uint64_t pages     = (dyn_end - dyn_start) / PFRAME_SIZE;

            uint64_t phys = (uint64_t)(uintptr_t)pmm_alloc_pages(pages);
            map_region(pml4, phys, dyn_start, pages, PMLE_USER | PMLE_PRESENT | PMLE_WRITE);
        
            if (phdr->p_filesz > 0) {
                seek(elf_file, phdr->p_offset, SEEK_SET);
                uint64_t offset_in_page = (phdr->p_vaddr + load_bias) - dyn_start;
                void *dest = (void *)(PHYS_TO_VIRTUAL(phys) + offset_in_page);
        
                if (read(elf_file, phdr->p_filesz, dest) != phdr->p_filesz) {
                    debugf_warn("Failed to read PT_DYNAMIC data\n");
                    kfree(phdrs);
                    close(elf_file);
                    return -EIO;
                }
            }
    
            dynamic_vaddr = PHYS_TO_VIRTUAL(phys) + ((phdr->p_vaddr + load_bias) - dyn_start);
        }

        if (phdr->p_type == PT_TLS) {
            continue;
        }

        if (phdr->p_type != PT_LOAD) {
            continue;
        }

        debugf_debug("Loading segment %d: vaddr=0x%llx memsz=0x%llx filesz=0x%llx flags=0x%x\n",
                     i, phdr->p_vaddr, phdr->p_memsz, phdr->p_filesz, phdr->p_flags);

        uint64_t seg_vaddr = phdr->p_vaddr + load_bias;
        uint64_t seg_page_start = ROUND_DOWN(seg_vaddr, PFRAME_SIZE);
        uint64_t seg_page_end   = ROUND_UP(seg_vaddr + phdr->p_memsz, PFRAME_SIZE);

        uint64_t pages = (seg_page_end - seg_page_start) / PFRAME_SIZE;

        uint64_t phys_base = (uint64_t)pmm_alloc_pages(pages);
        if (!phys_base) {
            debugf_warn("Failed to allocate %llu pages for segment %d\n", pages, i);
            kfree(phdrs);
            close(elf_file);
            return -ENOMEM;
        }

        memset((void *)PHYS_TO_VIRTUAL(phys_base), 0, pages * PFRAME_SIZE);

        uint64_t page_flags = elf_pf_to_page_flags(phdr->p_flags);
        map_region(pml4, phys_base, seg_page_start, pages, page_flags);

        if (phdr->p_filesz > 0) {
            seek(elf_file, phdr->p_offset, SEEK_SET);

            uint64_t offset_in_page = seg_vaddr - seg_page_start;
            void *dest = (void *)(PHYS_TO_VIRTUAL(phys_base) + offset_in_page);

            if (read(elf_file, phdr->p_filesz, dest) != phdr->p_filesz) {
                debugf_warn("Failed to read segment %d data\n", i);
                kfree(phdrs);
                close(elf_file);
                return -EIO;
            }

        }

    }

    Elf64_Rela *rela      = NULL;
    uint64_t    rela_sz   = 0;
    uint64_t    rela_ent  = sizeof(Elf64_Rela);

    if (dynamic_vaddr) {
        Elf64_Dyn *dyn = (Elf64_Dyn *)dynamic_vaddr;

        for (; dyn->d_tag != DT_NULL; dyn++) {
            switch (dyn->d_tag) {
            case DT_RELA:
                rela = (Elf64_Rela *)(dyn->d_un.d_ptr + load_bias);
                break;
            case DT_RELASZ:
                rela_sz = dyn->d_un.d_val;
                break;
            case DT_RELAENT:
                rela_ent = dyn->d_un.d_val;
                break;
            default:
                break;
            }
        }
    }

    if (rela && rela_sz) {
        size_t count = rela_sz / rela_ent;

        debugf_debug("Applying %llu RELA relocations\n", count);

        for (size_t i = 0; i < count; i++) {
            Elf64_Rela *r = &rela[i];
            uint64_t *reloc_vaddr = (uint64_t *)(r->r_offset + load_bias);
            uint64_t phys_addr = pg_virtual_to_phys(pml4, (uint64_t)(uintptr_t)reloc_vaddr);
            uint64_t *reloc_addr = (uint64_t *)PHYS_TO_VIRTUAL(phys_addr);
            *reloc_addr = load_bias + r->r_addend;

            switch (ELF64_R_TYPE(r->r_info)) {
            case R_X86_64_RELATIVE:
                *reloc_addr = load_bias + r->r_addend;
                break;
            default:
                debugf_warn("Unsupported relocation type %llu at index %llu\n",
                            ELF64_R_TYPE(r->r_info), i);
                kfree(phdrs);
                close(elf_file);
                return -EINVAL;
            }
        }
    }

    if (!is_mapped(pml4, eh.e_entry + load_bias)) {
        debugf_warn("Entry point 0x%llx is not mapped!\n", eh.e_entry + load_bias);
        kfree(phdrs);
        close(elf_file);
        return -EINVAL;
    }
    
    if (tls_memsz > 0) {
        size_t tcb_size = sizeof(user_tls_t);
        size_t total_size = tcb_size + tls_memsz;
        total_size = ROUND_UP(total_size, PFRAME_SIZE);
        
        debugf_debug("Allocating TLS: TCB=%zu + data=%llu = %zu bytes\n",
                 tcb_size, tls_memsz, total_size);
        
        if (find_new_tls_base(proc->main_thread, total_size) != EOK) {
            debugf_warn("Failed to allocate TLS\n");
            kfree(phdrs);
            close(elf_file);
            return -ENOMEM;
        }
        
        user_tls_t *tcb = proc->main_thread->tls_ptr;
        uint64_t    fs_base = (uint64_t)proc->main_thread->tls.base_virt;

        uint64_t tls_data_virt = fs_base - tls_memsz;
        (void)tls_data_virt;

        void *tls_data_kern = (void *)((uint64_t)tcb - tls_memsz);

        if (tls_filesz > 0) {
            seek(elf_file, tls_offset, SEEK_SET);
            if (read(elf_file, tls_filesz, tls_data_kern) != tls_filesz) {
                debugf_warn("Failed to read TLS init data\n");
                kfree(phdrs);
                close(elf_file);
                return -EIO;
            }
            debugf_debug("Copied %llu bytes of TLS init data to %p\n", 
                         tls_filesz, tls_data_kern);
        }

        if (tls_memsz > tls_filesz) {
            memset((void *)((uint64_t)tls_data_kern + tls_filesz), 0,
                   tls_memsz - tls_filesz);
        }

        tcb->self = (user_tls_t *)fs_base;
        
    } else {
        if (find_new_tls_base(proc->main_thread, TLS_MIN_SIZE) != EOK) {
            debugf_warn("Failed to allocate basic TLS\n");
            kfree(phdrs);
            close(elf_file);
            return -ENOMEM;
        }
        
        user_tls_t *tcb = proc->main_thread->tls_ptr;
        uint64_t    fs_base = (uint64_t)proc->main_thread->tls.base_virt;
        tcb->self = (user_tls_t *)fs_base;
    }

    if (proc->main_thread && proc->main_thread->tls.base_virt) {
        _cpu_set_msr(0xC0000100, (uint64_t)proc->main_thread->tls.base_virt);
    }

    int argc = 0;
    int envc = 0;

    if (argv) {
        argc = count_strings(argv);
    }
    if (envp) {
        envc = count_strings(envp);
    }

    const char *default_argv[] = {path, NULL};
    if (!argv || argc == 0) {
        argv = default_argv;
        argc = 1;
    }

    const char *default_envp[] = {
        "PATH=/bin:/usr/bin",
        NULL
    };
    if (!envp || envc == 0) {
        envp = default_envp;
        envc = 1;
    }

    uint64_t phdr_vaddr = 0;
    for (int i = 0; i < eh.e_phnum; i++) {
        if (phdrs[i].p_type == PT_LOAD && phdrs[i].p_offset == 0) {
            phdr_vaddr = phdrs[i].p_vaddr + load_bias + eh.e_phoff;
            break;
        }
    }
    if (phdr_vaddr == 0) {
        phdr_vaddr = load_bias + eh.e_phoff;
    }

    Elf64_auxv_t auxv[32];
    int auxc = 0;

    uint64_t hwcaps[2] = {0, 0};
    get_athwcap_bitmap(hwcaps);

    auxv[auxc++] = (Elf64_auxv_t){AT_PHDR, {phdr_vaddr}};
    auxv[auxc++] = (Elf64_auxv_t){AT_PHENT, {sizeof(Elf64_Phdr)}};
    auxv[auxc++] = (Elf64_auxv_t){AT_PHNUM, {eh.e_phnum}};
    auxv[auxc++] = (Elf64_auxv_t){AT_PAGESZ, {PFRAME_SIZE}};
    auxv[auxc++] = (Elf64_auxv_t){AT_BASE, {eh.e_type == ET_DYN ? load_bias : 0}};
    auxv[auxc++] = (Elf64_auxv_t){AT_ENTRY, {eh.e_entry + load_bias}};
    auxv[auxc++] = (Elf64_auxv_t){AT_UID, {get_current_cred()->uid}};
    auxv[auxc++] = (Elf64_auxv_t){AT_EUID, {get_current_cred()->euid}};
    auxv[auxc++] = (Elf64_auxv_t){AT_GID, {get_current_cred()->gid}};
    auxv[auxc++] = (Elf64_auxv_t){AT_EGID, {get_current_cred()->egid}};
    auxv[auxc++] = (Elf64_auxv_t){AT_SECURE, {0}};
    auxv[auxc++] = (Elf64_auxv_t){AT_RANDOM, {0}};
    auxv[auxc++] = (Elf64_auxv_t){AT_HWCAP, {hwcaps[0]}};
    auxv[auxc++] = (Elf64_auxv_t){AT_HWCAP2, {hwcaps[1]}};
    auxv[auxc++] = (Elf64_auxv_t){AT_PLATFORM, {0}};
    auxv[auxc++] = (Elf64_auxv_t){AT_BASE_PLATFORM, {0}};
    auxv[auxc++] = (Elf64_auxv_t){AT_CLKTCK, {100}};
    auxv[auxc++] = (Elf64_auxv_t){AT_EXECFN, {(uint64_t)(uintptr_t)path}};

    if (setup_initial_stack(proc, argv, argc, envp, envc, auxv, auxc, 
                            load_bias, phdr_vaddr) != EOK) {
        debugf_warn("Failed to setup initial stack\n");
        kfree(phdrs);
        close(elf_file);
        return -ENOMEM;
    }

    close(elf_file);
    kfree(phdrs);

    if (out) {
        out->pid = pid;
        out->pcb = proc;
        out->path = path;
        out->vmc = proc->vmc;
        out->entry = eh.e_entry + load_bias;
        out->rsp = proc->main_thread->regs->rsp;
    }

    debugf_debug("ELF loaded successfully: PID=%d entry=0x%llx\n", pid, eh.e_entry + load_bias);

    asm volatile("sti");
    return EOK;
}