#include "execve.h"
#include "caps.h"
#include "dev/tty/tty.h"
#include "gdt/gdt.h"
#include "uaccess.h"
#include "user/access.h"

#include <errors.h>
#include <kernel.h>
#include <cpu.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include <elf/elf.h>
#include <fs/file_io.h>
#include <fs/vfs/vfs.h>
#include <loader/elf/elfloader.h>
#include <memory/heap/kheap.h>
#include <memory/pmm/pmm.h>
#include <memory/vmm/vflags.h>
#include <memory/vmm/vmm.h>
#include <paging/paging.h>
#include <scheduler/scheduler.h>
#include <auxv.h>

typedef struct {
    uint64_t a_type;
    union { uint64_t a_val; } a_un;
} execve_auxv_t;

typedef struct {
    uint64_t vaddr;
    uint64_t memsz;
    uint64_t filesz;
    uint32_t pflags;
    int      is_dyn;
    uint8_t *data;
} exec_seg_t;

static uint64_t exec_pf_to_page_flags(uint32_t pf) {
    uint64_t f = PMLE_PRESENT | PMLE_USER;
    if (pf & PF_W)    f |= PMLE_WRITE;
    if (!(pf & PF_X)) f |= PMLE_NOT_EXECUTABLE;
    return f;
}

static char **copy_strarray(const char __user *const __user *usrc, int *out_n) {
    *out_n = 0;

    if (!usrc)  {
        return NULL;
    }

    int n = 0;
    
    const char __user *p;
    while (copy_from_user(&p, usrc + n, sizeof(p)) == 0 && p) n++;
    
    char **dst = kmalloc(sizeof(char *) * (n + 1));

    if (!dst) {
        return NULL;
    }

    dst[n] = NULL;

    for (int i = 0; i < n; i++) {
        const char __user *ustr;

        if (copy_from_user(&ustr, usrc + i, sizeof(ustr)) != 0) {
            goto fail;
        }

        char tmp[4096];
        ssize_t len = strncpy_from_user(tmp, ustr, sizeof(tmp));

        if (len < 0) {
            goto fail;
        }

        dst[i] = kmalloc(len + 1);

        if (!dst[i]) {
            goto fail;
        }

        memcpy(dst[i], tmp, len + 1);

        continue;
fail:
        for (int j = 0; j < i; j++) {
            kfree(dst[j]); 
        }

        kfree(dst);
        return NULL;
    }
    *out_n = n;
    return dst;
}

static void free_strarray(char **arr, int n) {
    if (!arr) return;
    for (int i = 0; i < n; i++) kfree(arr[i]);
    kfree(arr);
}

static void free_segs(exec_seg_t *s, int n) {
    if (!s) return;
    for (int i = 0; i < n; i++) if (s[i].data) kfree(s[i].data);
    kfree(s);
}

static inline void *kva_of_uva(tcb_t *t, uint64_t uva) {
    uint64_t bot = USER_STACK_TOP - SCHEDULER_STACKSZ;
    if (uva < bot || uva >= USER_STACK_TOP) return NULL;
    return (char *)t->user_stack + (uva - bot);
}

static int build_stack(tcb_t *thread, const char **argv, int argc, const char **envp, int envc, execve_auxv_t *auxv, int auxc) {
    const char platform[] = "x86_64";
    size_t plat_len = sizeof(platform);

    size_t argv_b = 0; for (int i=0;i<argc;i++) if(argv[i]) argv_b+=strlen(argv[i])+1;
    size_t env_b  = 0; for (int i=0;i<envc;i++) if(envp[i]) env_b +=strlen(envp[i])+1;

    size_t str_pad = ((plat_len + argv_b + env_b) + 15) & ~(size_t)15;
    size_t total   = sizeof(uint64_t)
                   + (argc+1)*sizeof(uint64_t)
                   + (envc+1)*sizeof(uint64_t)
                   + (auxc+1)*sizeof(execve_auxv_t)
                   + str_pad;
    total = (total + 15) & ~(size_t)15;

    uint64_t rsp = (USER_STACK_TOP & ~(uint64_t)15) - total;
    if (rsp < USER_STACK_TOP - SCHEDULER_STACKSZ) return -ENOMEM;

    uint64_t pos = rsp;

    uint64_t *k_argc = kva_of_uva(thread, pos); if(!k_argc) return -ENOMEM;
    *k_argc = (uint64_t)argc;
    pos += sizeof(uint64_t);

    uint64_t *k_argv = kva_of_uva(thread, pos); if(!k_argv) return -ENOMEM;
    pos += (argc+1)*sizeof(uint64_t);

    uint64_t *k_envp = kva_of_uva(thread, pos); if(!k_envp) return -ENOMEM;
    pos += (envc+1)*sizeof(uint64_t);

    execve_auxv_t *k_auxv = kva_of_uva(thread, pos); if(!k_auxv) return -ENOMEM;
    pos += (auxc+1)*sizeof(execve_auxv_t);

    char    *k_str   = kva_of_uva(thread, pos); if(!k_str) return -ENOMEM;
    uint64_t str_uva = pos;
    uint64_t plat_uva = str_uva;

    memcpy(k_str, platform, plat_len);
    k_str += plat_len; str_uva += plat_len;

    for (int i=0;i<argc;i++) {
        if (argv[i]) { size_t l=strlen(argv[i])+1; memcpy(k_str,argv[i],l); k_argv[i]=str_uva; k_str+=l; str_uva+=l; }
        else k_argv[i]=0;
    }
    k_argv[argc]=0;

    for (int i=0;i<envc;i++) {
        if (envp[i]) { size_t l=strlen(envp[i])+1; memcpy(k_str,envp[i],l); k_envp[i]=str_uva; k_str+=l; str_uva+=l; }
        else k_envp[i]=0;
    }
    k_envp[envc]=0;

    memcpy(k_auxv, auxv, auxc*sizeof(execve_auxv_t));
    for (int i=0;i<auxc;i++) {
        if (k_auxv[i].a_type==AT_PLATFORM || k_auxv[i].a_type==AT_BASE_PLATFORM)
            k_auxv[i].a_un.a_val = plat_uva;
        if (k_auxv[i].a_type==AT_EXECFN)
            k_auxv[i].a_un.a_val = k_argv[0];
    }
    k_auxv[auxc].a_type=AT_NULL;
    k_auxv[auxc].a_un.a_val=0;

    thread->regs->rsp = rsp;
    return EOK;
}

int sys_execve(const char __user *upath, const char __user *const __user *uargv, const char __user *const __user *uenvp) {
    char kpath[4096];
    if (strncpy_from_user(kpath, upath, sizeof(kpath)) < 0) return -EFAULT;
    kpath[sizeof(kpath)-1] = '\0';

    int argc=0, envc=0;
    char **argv = copy_strarray(uargv, &argc);
    char **envp = copy_strarray(uenvp, &envc);

    const char *def_argv0 = kpath;
    const char *def_argv[] = { def_argv0, NULL };
    const char *def_envp[] = { "PATH=/bin:/usr/bin", NULL };

    const char** real_argv = (argv && argc>0) ? (const char**)argv : def_argv;
    int real_argc = (argv && argc>0) ? argc : 1;
    const char** real_envp = (envp && envc>0) ? (const char**)envp : def_envp;
    int real_envc = (envp && envc>0) ? envc : 1;

    fileio_t *elf_file = open(kpath, 0, 0);
    if (!elf_file || (int64_t)(intptr_t)elf_file < 0) {
        free_strarray(argv,argc); free_strarray(envp,envc);
        return -ENOENT;
    }

    vnode_t *vnode = elf_file->private;
    if (vnode_permission(get_current_cred(), vnode, X_OK) < 0) {
        close(elf_file);
        free_strarray(argv,argc); free_strarray(envp,envc);
        return -EACCES;
    }

    Elf64_Ehdr eh;
    seek(elf_file, 0, SEEK_SET);
    if (read(elf_file, sizeof(eh), (char*)&eh) != sizeof(eh) || elf_validate(&eh) != 0) {
        close(elf_file);
        free_strarray(argv,argc); free_strarray(envp,envc);
        return -ENOEXEC;
    }

    Elf64_Phdr *phdrs = kmalloc(sizeof(Elf64_Phdr) * eh.e_phnum);
    if (!phdrs) {
        close(elf_file);
        free_strarray(argv,argc);
        free_strarray(envp,envc);
        return -ENOMEM;
    }

    seek(elf_file, eh.e_phoff, SEEK_SET);
    if (read(elf_file, sizeof(Elf64_Phdr)*eh.e_phnum, (char*)phdrs) !=
        (ssize_t)(sizeof(Elf64_Phdr)*eh.e_phnum)) {
        kfree(phdrs); close(elf_file);
        free_strarray(argv,argc); free_strarray(envp,envc);
        return -EIO;
    }

    pcb_t *proc   = get_current_pcb();
    tcb_t *thread = get_current_tcb();
    if (!proc || !thread) {
        kfree(phdrs); close(elf_file);
        free_strarray(argv,argc); free_strarray(envp,envc);
        return -EINVAL;
    }

    if (vnode->mode & S_ISUID) proc->cred->euid = vnode->uid;
    if (vnode->mode & S_ISGID) proc->cred->egid = vnode->gid;

    uint64_t load_bias = 0;
    if (eh.e_type == ET_DYN) load_bias = choose_et_dyn_base();

    int nseg = 0;
    for (int i=0;i<eh.e_phnum;i++) {
        uint32_t t = phdrs[i].p_type;
        if (t==PT_LOAD || t==PT_DYNAMIC) nseg++;
    }

    exec_seg_t *segs = kmalloc(sizeof(exec_seg_t)*(nseg+1));
    if (!segs) {
        kfree(phdrs); close(elf_file);
        free_strarray(argv,argc); free_strarray(envp,envc);
        return -ENOMEM;
    }
    memset(segs, 0, sizeof(exec_seg_t)*(nseg+1));

    int si = 0;

    for (int i=0; i<eh.e_phnum; i++) {
        Elf64_Phdr *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD && ph->p_type != PT_DYNAMIC) continue;
        exec_seg_t *s = &segs[si++];
        s->vaddr   = ph->p_vaddr + load_bias;
        s->memsz   = ph->p_memsz;
        s->filesz  = ph->p_filesz;
        s->pflags  = ph->p_flags;
        s->is_dyn  = (ph->p_type == PT_DYNAMIC) ? 1 : 0;
        if (ph->p_filesz > 0) {
            s->data = kmalloc(ph->p_filesz);
            if (!s->data) { 
                free_segs(segs,si); kfree(phdrs); close(elf_file);
                free_strarray(argv,argc); free_strarray(envp,envc);
                return -ENOMEM;
            }
            seek(elf_file, ph->p_offset, SEEK_SET);
            if (read(elf_file, ph->p_filesz, (char*)s->data) != (ssize_t)ph->p_filesz) {
                free_segs(segs,si); kfree(phdrs); close(elf_file);
                free_strarray(argv,argc);
                free_strarray(envp,envc);
                return -EIO;
            }
        }
    }

    uint64_t tls_filesz=0, tls_memsz=0;
    uint8_t *tls_init = NULL;

    for (int i=0;i<eh.e_phnum;i++) {
        if (phdrs[i].p_type != PT_TLS) continue;
        tls_filesz = phdrs[i].p_filesz;
        tls_memsz  = phdrs[i].p_memsz;
        if (tls_filesz > 0) {
            tls_init = kmalloc(tls_filesz);
            if (!tls_init) {
                free_segs(segs,nseg);
                kfree(phdrs); close(elf_file);
                free_strarray(argv,argc);
                free_strarray(envp,envc); 
                return -ENOMEM; 
            }
            seek(elf_file, phdrs[i].p_offset, SEEK_SET);
            if (read(elf_file, tls_filesz, (char*)tls_init) != (ssize_t)tls_filesz) {
                kfree(tls_init);
                free_segs(segs,nseg);
                kfree(phdrs); close(elf_file);
                free_strarray(argv,argc);
                free_strarray(envp,envc);
                return -EIO;
            }
        }
        break;
    }

    close(elf_file);
    elf_file = NULL;

    asm volatile("cli");

    for (int i=0;i<proc->thread_count;i++) {
        tcb_t *t = proc->threads[i];
        if (t && t != thread && t->state != THREAD_DEAD) t->state = THREAD_DEAD;
    }

    free_tls(thread);

    vmc_t *old_vmc = proc->vmc;
    proc->vmc = NULL;
    process_vmm_init(&proc->vmc, VMO_USER_RW);


    uint64_t ubase = USER_STACK_TOP - SCHEDULER_STACKSZ;
    void *uvirt = valloc_at(proc->vmc, (void*)(uintptr_t)ubase,
                            SCHEDULER_STACK_PAGES, VMO_USER_RW, NULL);
    if (!uvirt) goto fatal;
    uint64_t *npml4 = (uint64_t*)PHYS_TO_VIRTUAL(proc->vmc->pml4_table);
    uint64_t  uphys = pg_virtual_to_phys(npml4, (uint64_t)(uintptr_t)uvirt);
    thread->user_stack = (void*)(uintptr_t)PHYS_TO_VIRTUAL(uphys);
    memset(thread->user_stack, 0, SCHEDULER_STACKSZ);


    void *nks = valloc(proc->vmc, SCHEDULER_STACK_PAGES, VMO_KERNEL_RW, NULL);
    if (nks) {
        uint64_t *npml4 = (uint64_t*)PHYS_TO_VIRTUAL(proc->vmc->pml4_table);
        uint64_t  kphys = pg_virtual_to_phys(npml4, (uint64_t)(uintptr_t)nks);
        thread->deferred_free_kstack = thread->kernel_stack;
        thread->kernel_stack = (void*)(uintptr_t)PHYS_TO_VIRTUAL(kphys);
    }

    vmc_destroy(old_vmc);
    
    uint64_t pml4_phys = (uint64_t)proc->vmc->pml4_table;
    vmc_switch(proc->vmc);
    _load_pml4((uint64_t*)pml4_phys);

    uint64_t *pml4 = (uint64_t*)PHYS_TO_VIRTUAL(proc->vmc->pml4_table);
    uint64_t  dyn_kva = 0;

    for (int i=0; i<nseg; i++) {
        exec_seg_t *s = &segs[i];
        uint64_t pg_start = ROUND_DOWN(s->vaddr,           PFRAME_SIZE);
        uint64_t pg_end   = ROUND_UP  (s->vaddr + s->memsz, PFRAME_SIZE);
        uint64_t pages    = (pg_end - pg_start) / PFRAME_SIZE;

        uint64_t phys = (uint64_t)pmm_alloc_pages(pages);
        if (!phys) goto fatal;

        memset((void*)PHYS_TO_VIRTUAL(phys), 0, pages*PFRAME_SIZE);

        uint64_t flags = s->is_dyn
            ? (PMLE_USER | PMLE_PRESENT | PMLE_WRITE)
            : exec_pf_to_page_flags(s->pflags);

        map_region(pml4, phys, pg_start, pages, flags);

        if (s->filesz > 0 && s->data) {
            uint64_t off = s->vaddr - pg_start;
            memcpy((void*)(PHYS_TO_VIRTUAL(phys)+off), s->data, s->filesz);
        }

        if (s->is_dyn)
            dyn_kva = PHYS_TO_VIRTUAL(phys) + (s->vaddr - pg_start);
    }

    if (dyn_kva) {
        Elf64_Dyn  *dyn     = (Elf64_Dyn*)dyn_kva;
        Elf64_Rela *rela    = NULL;
        uint64_t    rsz     = 0;
        uint64_t    rent    = sizeof(Elf64_Rela);
        for (; dyn->d_tag != DT_NULL; dyn++) {
            if      (dyn->d_tag == DT_RELA)    rela = (Elf64_Rela*)(dyn->d_un.d_ptr + load_bias);
            else if (dyn->d_tag == DT_RELASZ)   rsz  = dyn->d_un.d_val;
            else if (dyn->d_tag == DT_RELAENT)  rent = dyn->d_un.d_val;
        }
        if (rela && rsz) {
            size_t cnt = rsz / rent;
            for (size_t r=0;r<cnt;r++) {
                if (ELF64_R_TYPE(rela[r].r_info) != R_X86_64_RELATIVE) continue;
                uint64_t tphys = pg_virtual_to_phys(pml4, rela[r].r_offset + load_bias);
                *(uint64_t*)PHYS_TO_VIRTUAL(tphys) = load_bias + rela[r].r_addend;
            }
        }
    }

    if (!is_mapped(pml4, eh.e_entry + load_bias)) goto fatal;

    
    size_t req = (tls_memsz > 0)
        ? ROUND_UP(sizeof(user_tls_t) + tls_memsz, PFRAME_SIZE)
        : TLS_MIN_SIZE;
    if (find_new_tls_base(thread, req) != EOK) goto fatal;
    if (tls_memsz > 0) {
        void *dk = (void*)((uint64_t)thread->tls_ptr - tls_memsz);
        if (tls_init) memcpy(dk, tls_init, tls_filesz);
        if (tls_memsz > tls_filesz)
            memset((char*)dk + tls_filesz, 0, tls_memsz - tls_filesz);
    }
    thread->tls_ptr->self = (user_tls_t*)thread->tls.base_virt;
    _cpu_set_msr(0xC0000100, (uint64_t)thread->tls.base_virt);

    execve_auxv_t auxv[32]; int auxc=0;
    
    uint64_t phdr_va = load_bias + eh.e_phoff;
    for (int i=0;i<eh.e_phnum;i++)
        if (phdrs[i].p_type==PT_LOAD && phdrs[i].p_offset==0)
            { phdr_va = phdrs[i].p_vaddr + load_bias + eh.e_phoff; break; }

    uint64_t hwcaps[2]={0,0};
    get_athwcap_bitmap(hwcaps);

    auxv[auxc++]=(execve_auxv_t){AT_PHDR,          {phdr_va}};
    auxv[auxc++]=(execve_auxv_t){AT_PHENT,         {sizeof(Elf64_Phdr)}};
    auxv[auxc++]=(execve_auxv_t){AT_PHNUM,         {eh.e_phnum}};
    auxv[auxc++]=(execve_auxv_t){AT_PAGESZ,        {PFRAME_SIZE}};
    auxv[auxc++]=(execve_auxv_t){AT_BASE,          {eh.e_type==ET_DYN?load_bias:0}};
    auxv[auxc++]=(execve_auxv_t){AT_ENTRY,         {eh.e_entry+load_bias}};
    auxv[auxc++]=(execve_auxv_t){AT_UID,           {proc->cred->uid}};
    auxv[auxc++]=(execve_auxv_t){AT_EUID,          {proc->cred->euid}};
    auxv[auxc++]=(execve_auxv_t){AT_GID,           {proc->cred->gid}};
    auxv[auxc++]=(execve_auxv_t){AT_EGID,          {proc->cred->egid}};
    auxv[auxc++]=(execve_auxv_t){AT_SECURE,        {0}};
    auxv[auxc++]=(execve_auxv_t){AT_RANDOM,        {0}};
    auxv[auxc++]=(execve_auxv_t){AT_HWCAP,         {hwcaps[0]}};
    auxv[auxc++]=(execve_auxv_t){AT_HWCAP2,        {hwcaps[1]}};
    auxv[auxc++]=(execve_auxv_t){AT_PLATFORM,      {0}};
    auxv[auxc++]=(execve_auxv_t){AT_BASE_PLATFORM, {0}};
    auxv[auxc++]=(execve_auxv_t){AT_CLKTCK,        {100}};
    auxv[auxc++]=(execve_auxv_t){AT_EXECFN,        {0}};
    

    if (build_stack(thread, real_argv, real_argc, real_envp, real_envc, auxv, auxc) != EOK)
        goto fatal;

    free_segs(segs, nseg); segs = NULL;
    if (tls_init) { kfree(tls_init); tls_init = NULL; }
    kfree(phdrs); phdrs = NULL;
    free_strarray(argv, argc); argv = NULL;
    free_strarray(envp, envc); envp = NULL;

    uint64_t new_rsp = thread->regs->rsp;
    memset(thread->regs, 0, sizeof(registers_t));
    thread->regs->rip    = eh.e_entry + load_bias;
    thread->regs->rsp    = new_rsp;
    thread->regs->cs     = 0x1B | 3;
    thread->regs->ss     = 0x23 | 3;
    thread->regs->ds     = 0x23 | 3;
    thread->regs->rflags = 0x202;

    tss_set_kernel_stack((uint64_t)thread->kernel_stack + SCHEDULER_STACKSZ);
    thread->state = THREAD_RUNNING;

    asm volatile("sti");
    context_load(thread->regs);
    __builtin_unreachable();

fatal:
    if (segs)     free_segs(segs, nseg);
    if (tls_init) kfree(tls_init);
    if (phdrs)    kfree(phdrs);
    free_strarray(argv, argc);
    free_strarray(envp, envc);

    proc->state   = PROC_DEAD;
    thread->state = THREAD_DEAD;
    asm volatile("sti");
    yield(thread->regs);
    __builtin_unreachable();
}