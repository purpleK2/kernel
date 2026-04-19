#include "scheduler.h"
#include "karg.h"
#include "loader/binfmt.h"
#include "user/group.h"
#include "user/user.h"

#include <gdt/gdt.h>

#include <autoconf.h>

#include <cpu.h>
#include <errors.h>
#include <kernel.h>
#include <string.h>

#include <memory/heap/kheap.h>
#include <memory/pmm/pmm.h>
#include <memory/vmm/vflags.h>
#include <memory/vmm/vmm.h>
#include <paging/paging.h>

#include <smp/smp.h>

#include <stdio.h>

#include <fs/procfs/procfs.h>
#include <fs/vfs/vfs.h>

#include <uaccess.h>
#include <util/assert.h>

#define WNOHANG    1
#define WUNTRACED  2
#define WCONTINUED 8

#define __W_EXITCODE(ret, sig) ((ret) << 8 | (sig))
#define __WIFEXITED(status)    (((status) & 0x7f) == 0)
#define __WEXITSTATUS(status)  (((status) >> 8) & 0xff)

extern registers_t *get_syscall_context(void);

cpu_local_t *cpu_locals;

typedef struct mlfq_queue {
    tcb_t *head;
    tcb_t *tail;
    size_t count;
    int time_quantum;
} mlfq_queue_t;

typedef struct cpu_mlfq {
    mlfq_queue_t queues[CONFIG_SCHED_NUM_MLFQ_QUEUES];
    uint64_t ticks_since_boost;
} cpu_mlfq_t;

static cpu_mlfq_t *thread_queues;
static tcb_t **current_threads;
static int cpu_count;

atomic_flag SCHEDULER_LOCK = ATOMIC_FLAG_INIT;

static void cleanup_dead_thread(tcb_t *thread) {
    if (!thread || !thread->parent)
        return;

    void *kernel_stack = thread->kernel_stack;
    void *fpu          = thread->fpu;
    registers_t *regs  = thread->regs;
    pcb_t *parent_proc = thread->parent;
    int is_user_mode   = thread->flags & TF_MODE_USER;

    if (is_user_mode) {
        free_tls(thread);
    }

    signal_thread_cleanup(thread);

    if (kernel_stack)
        pmm_free((void *)VIRT_TO_PHYSICAL(kernel_stack), SCHEDULER_STACK_PAGES);
    if (fpu)
        kfree(fpu);
    if (regs)
        kfree(regs);

    for (int i = 0; i < parent_proc->thread_count; i++) {
        if (parent_proc->threads[i] == thread) {
            for (int j = i; j < parent_proc->thread_count - 1; j++) {
                parent_proc->threads[j] = parent_proc->threads[j + 1];
            }
            parent_proc->thread_count--;
            break;
        }
    }

    kfree(thread);

    if (parent_proc->state == PROC_DEAD && parent_proc->thread_count == 0 &&
        parent_proc->exited == 0) {

        if (parent_proc->fd_table.entries) {
            for (size_t i = 0; i < parent_proc->fd_table.size; i++) {
                fd_entry_t *fe = &parent_proc->fd_table.entries[i];
                if (fe->type == FD_FILE && fe->ptr) {
                    close((fileio_t *)fe->ptr);
                    fe->ptr = NULL;
                }
            }
            kfree(parent_proc->fd_table.entries);
        }

        if (parent_proc->name)
            kfree(parent_proc->name);
        if (parent_proc->cred)
            kfree(parent_proc->cred);
        if (parent_proc->children)
            kfree(parent_proc->children);
        if (parent_proc->threads)
            kfree(parent_proc->threads);
        signal_process_cleanup(parent_proc);
        kfree(parent_proc);
    }
}

// SHOULD BE CALLED **ONLY ONCE** IN KSTART. NOWHERE ELSE.
int init_scheduler() {
    cpu_count = get_bootloader_data()->cpu_count;

    thread_queues   = kmalloc(sizeof(cpu_mlfq_t) * cpu_count);
    current_threads = kmalloc(sizeof(tcb_t *) * cpu_count);
    cpu_locals      = kmalloc(sizeof(cpu_local_t) * cpu_count);
    memset(thread_queues, 0, sizeof(cpu_mlfq_t) * cpu_count);
    memset(current_threads, 0, sizeof(tcb_t *) * cpu_count);
    memset(cpu_locals, 0, sizeof(cpu_local_t) * cpu_count);

    assert(thread_queues);
    assert(current_threads);
    assert(cpu_locals);

    for (int cpu = 0; cpu < cpu_count; cpu++) {
        for (int queue = 0; queue < CONFIG_SCHED_NUM_MLFQ_QUEUES; queue++) {
            thread_queues[cpu].queues[queue].head  = NULL;
            thread_queues[cpu].queues[queue].tail  = NULL;
            thread_queues[cpu].queues[queue].count = 0;
            thread_queues[cpu].queues[queue].time_quantum =
                (1 << queue) * SCHEDULER_THREAD_TS;
        }
        thread_queues[cpu].ticks_since_boost = 0;
    }

    for (int i = 0; i < cpu_count; i++) {
        cpu_locals[i].cpu_id  = i;
        cpu_locals[i].current = NULL;
    }

    procfs_init();
    vfs_mkdir("/proc", 0755);
    vfs_mount(NULL, "procfs", "/proc", NULL);

    return 0;
}

static void mlfq_enqueue(int cpu, tcb_t *thread, int priority) {
    if (priority < 0)
        priority = 0;
    if (priority >= CONFIG_SCHED_NUM_MLFQ_QUEUES)
        priority = CONFIG_SCHED_NUM_MLFQ_QUEUES - 1;

    thread->priority = priority;
    thread->next     = NULL;

    mlfq_queue_t *queue = &thread_queues[cpu].queues[priority];

    if (queue->tail) {
        queue->tail->next = thread;
        queue->tail       = thread;
    } else {
        queue->head = thread;
        queue->tail = thread;
    }
    queue->count++;
}

static tcb_t *mlfq_dequeue(int cpu, int priority) {
    mlfq_queue_t *queue = &thread_queues[cpu].queues[priority];

    if (!queue->head) {
        return NULL;
    }

    tcb_t *thread = queue->head;
    queue->head   = thread->next;

    if (!queue->head) {
        queue->tail = NULL;
    }

    queue->count--;
    thread->next = NULL;

    return thread;
}

static tcb_t *pick_next_thread(int cpu) {
    for (int priority = 0; priority < CONFIG_SCHED_NUM_MLFQ_QUEUES;
         priority++) {
        mlfq_queue_t *queue = &thread_queues[cpu].queues[priority];
        tcb_t *thread       = queue->head;
        tcb_t *prev         = NULL;

        while (thread != NULL) {
            if (thread->state == THREAD_READY) {
                if (prev) {
                    prev->next = thread->next;
                } else {
                    queue->head = thread->next;
                }

                if (thread == queue->tail) {
                    queue->tail = prev;
                }

                queue->count--;
                thread->next = NULL;

                thread->time_slice = queue->time_quantum;

                return thread;
            }

            if (thread->state == THREAD_DEAD) {
                tcb_t *dead = thread;

                if (prev)
                    prev->next = dead->next;
                else
                    queue->head = dead->next;

                if (dead == queue->tail)
                    queue->tail = prev;

                queue->count--;
                thread     = dead->next;
                dead->next = NULL;
                cleanup_dead_thread(dead);
                continue;
            }

            prev   = thread;
            thread = thread->next;
        }
    }

    return NULL;
}

static void mlfq_boost_all(int cpu) {
    for (int priority = 1; priority < CONFIG_SCHED_NUM_MLFQ_QUEUES;
         priority++) {
        mlfq_queue_t *src_queue = &thread_queues[cpu].queues[priority];
        mlfq_queue_t *dst_queue = &thread_queues[cpu].queues[0];

        if (src_queue->head) {
            if (dst_queue->tail) {
                dst_queue->tail->next = src_queue->head;
                dst_queue->tail       = src_queue->tail;
            } else {
                dst_queue->head = src_queue->head;
                dst_queue->tail = src_queue->tail;
            }

            dst_queue->count += src_queue->count;

            tcb_t *thread = src_queue->head;
            while (thread) {
                thread->priority = 0;
                thread           = thread->next;
            }

            src_queue->head  = NULL;
            src_queue->tail  = NULL;
            src_queue->count = 0;
        }
    }

    thread_queues[cpu].ticks_since_boost = 0;

#ifdef CONFIG_SCHED_DEBUG
    debugf_debug("MLFQ: Boosted all threads on CPU %d\n", cpu);
#endif
}

// I WANT ALL OF THE BITS :speaking_head: :fire: :fire:
static uint64_t global_pid = 0;

// @param name name of the process (it's optional)
int proc_create(void (*entry)(), int flags, char *name) {
    pcb_t *proc = kmalloc(sizeof(pcb_t));
    memset(proc, 0, sizeof(pcb_t));
    proc->pid   = __sync_add_and_fetch(&global_pid, 1);
    proc->state = PROC_READY;

    if (name) {
        proc->name = strdup(name);
    }

    proc->fd_table.entries = NULL;
    proc->fd_table.size    = 0;

    signal_process_init(proc);

    int vflags = (flags & TF_MODE_USER ? VMO_USER_RW : VMO_KERNEL_RW);
    if (flags & TF_MODE_USER) {
        process_vmm_init(&proc->vmc, vflags);
    } else {
        proc->vmc = get_kernel_vmc();
    }

    proc->cwd = NULL;

    proc->cred = kmalloc(sizeof(user_cred_t));
    assert(proc->cred != NULL);
    memset(proc->cred, 0, sizeof(user_cred_t));

    user_cred_t *current_cred = get_current_cred();
    if (current_cred && proc->pid != 1) {
        memcpy(proc->cred, current_cred, sizeof(user_cred_t));
    } else {
        if (flags & TF_MODE_USER) {
            proc->cred->uid       = UID_ROOT;
            proc->cred->euid      = UID_ROOT;
            proc->cred->suid      = UID_ROOT;
            proc->cred->gid       = GID_ROOT;
            proc->cred->egid      = GID_ROOT;
            proc->cred->sgid      = GID_ROOT;
            proc->cred->ngroups   = 1;
            proc->cred->groups[0] = GID_ROOT;
            debugf_debug("Created process PID=%d in user mode with root "
                         "credentials, since it doesnt have parent\n",
                         proc->pid);
        } else {
            proc->cred->uid     = UID_INVALID;
            proc->cred->euid    = UID_INVALID;
            proc->cred->suid    = UID_INVALID;
            proc->cred->gid     = GID_INVALID;
            proc->cred->egid    = GID_INVALID;
            proc->cred->sgid    = GID_INVALID;
            proc->cred->ngroups = 0;
        }
    }

    pcb_t *parent_pcb = get_current_pcb();

    if (proc->pid == 1 || !parent_pcb) {
        proc->pgid              = proc->pid;
        proc->sid               = proc->pid;
        proc->is_session_leader = 1;
        proc->ctty              = NULL;
    } else {
        proc->pgid              = parent_pcb->pgid;
        proc->sid               = parent_pcb->sid;
        proc->is_session_leader = 0;
        proc->ctty              = parent_pcb->ctty;
    }

#ifdef CONFIG_SCHED_DEBUG
    debugf_debug("Created process PID=%d flags=0x%x mode=%s\n", proc->pid,
                 flags, (flags & TF_MODE_USER) ? "USER" : "KERNEL");
#endif

    thread_create(proc, entry, flags);

    procfs_add_process(proc);

    return proc->pid;
}

int thread_create(pcb_t *parent, void (*entry)(), int flags) {
    tcb_t *thread = kmalloc(sizeof(tcb_t));
    memset(thread, 0, sizeof(tcb_t));
    thread->tid        = __sync_fetch_and_add(&parent->thread_count, 1);
    thread->flags      = flags;
    thread->state      = THREAD_WAITING;
    thread->parent     = parent;
    thread->priority   = 0;
    thread->time_slice = SCHEDULER_THREAD_TS;

    thread->fpu = (void *)PHYS_TO_VIRTUAL(pmm_alloc_page());
    memset(thread->fpu, 0, PFRAME_SIZE);

    signal_thread_init(thread);

    registers_t *ctx = kmalloc(sizeof(registers_t));
    memset(ctx, 0, sizeof(registers_t));

    if (flags & TF_MODE_USER) {
        uint64_t user_stack_top  = USER_STACK_TOP;
        uint64_t user_stack_base = user_stack_top - SCHEDULER_STACKSZ;

        thread->user_stack =
            valloc_at(parent->vmc, (void *)(uintptr_t)user_stack_base,
                      SCHEDULER_STACK_PAGES, VMO_USER_RW, NULL);

        thread->user_stack =
            (void *)(uintptr_t)PHYS_TO_VIRTUAL(pg_virtual_to_phys(
                (uint64_t *)(uintptr_t)PHYS_TO_VIRTUAL(parent->vmc->pml4_table),
                (uint64_t)(uintptr_t)thread->user_stack));

        thread->kernel_stack =
            valloc(parent->vmc, SCHEDULER_STACK_PAGES, VMO_KERNEL_RW, NULL);

        thread->kernel_stack =
            (void *)(uintptr_t)PHYS_TO_VIRTUAL(pg_virtual_to_phys(
                (uint64_t *)(uintptr_t)PHYS_TO_VIRTUAL(parent->vmc->pml4_table),
                (uint64_t)(uintptr_t)thread->kernel_stack));

        ctx->rip    = (uint64_t)entry;
        ctx->cs     = 0x1B | 3;
        ctx->ss     = 0x23 | 3;
        ctx->ds     = 0x23 | 3;
        ctx->rflags = 0x202;
        ctx->rbp    = 0;
        ctx->rsp    = user_stack_top;

        if (allocate_tls(thread, TLS_MIN_SIZE) != EOK) {
            debugf_warn("Failed to allocate TLS for thread TID=%d\n",
                        thread->tid);
            pmm_free((void *)VIRT_TO_PHYSICAL(thread->kernel_stack),
                     SCHEDULER_STACK_PAGES);
            pmm_free((void *)VIRT_TO_PHYSICAL(thread->user_stack),
                     SCHEDULER_STACK_PAGES);
            kfree(thread->fpu);
            kfree(ctx);
            kfree(thread);
            return -1;
        }

        if (!is_address_canonical(ctx->rsp)) {
            debugf_warn("Cannot create usermode thread TID=%d: stack address "
                        "%p is not canonical\n",
                        thread->tid, (void *)ctx->rsp);
            free_tls(thread);
            pmm_free((void *)VIRT_TO_PHYSICAL(thread->kernel_stack),
                     SCHEDULER_STACK_PAGES);
            pmm_free((void *)VIRT_TO_PHYSICAL(thread->user_stack),
                     SCHEDULER_STACK_PAGES);
            kfree(thread->fpu);
            kfree(ctx);
            kfree(thread);
            return -1;
        }

#ifdef CONFIG_SCHED_DEBUG
        debugf_debug("Created usermode thread TID=%d entry=%p ustack=%p "
                     "kstack=%p tls=%p priority=%d\n",
                     thread->tid, entry, (void *)ctx->rsp, thread->kernel_stack,
                     thread->tls.base_virt, thread->priority);
#endif
    } else {
        thread->kernel_stack =
            valloc(parent->vmc, SCHEDULER_STACK_PAGES, VMO_KERNEL_RW, NULL);

        thread->kernel_stack =
            (void *)(uintptr_t)PHYS_TO_VIRTUAL(pg_virtual_to_phys(
                (uint64_t *)(uintptr_t)PHYS_TO_VIRTUAL(parent->vmc->pml4_table),
                (uint64_t)(uintptr_t)thread->kernel_stack));

        thread->user_stack = NULL;

        ctx->rip    = (uint64_t)entry;
        ctx->cs     = 0x08;
        ctx->ss     = 0x10;
        ctx->ds     = 0x10;
        ctx->rflags = 0x202;
        ctx->rsp    = (uint64_t)(thread->kernel_stack + SCHEDULER_STACKSZ - 8);

#ifdef CONFIG_SCHED_DEBUG
        debugf_debug(
            "Created kernel thread TID=%d entry=%p kstack=%p priority=%d\n",
            thread->tid, entry, (void *)ctx->rsp, thread->priority);
#endif
    }

    thread->regs = ctx;

    parent->threads =
        krealloc(parent->threads, sizeof(tcb_t *) * parent->thread_count);
    parent->threads[thread->tid] = thread;

    spinlock_acquire(&SCHEDULER_LOCK);

    int cpu = get_cpu();
    assert(thread_queues);

    if (thread->tid == 0) {
        parent->main_thread = thread;
    }

    mlfq_enqueue(cpu, thread, 0);

    spinlock_release(&SCHEDULER_LOCK);

    return thread->tid;
}

int proc_fork(registers_t *regs) {
    if (!regs) {
        return -EINVAL;
    }

    pcb_t *parent  = get_current_pcb();
    tcb_t *current = get_current_tcb();

    if (!parent || !current) {
        return -EINVAL;
    }

    if (!(current->flags & TF_MODE_USER)) {
        debugf_warn("proc_fork: refusing to fork from non-user thread\n");
        return -EINVAL;
    }

    if (!parent->vmc) {
        debugf_warn("proc_fork: parent process has no VMC\n");
        return -EINVAL;
    }

    vmc_t *child_vmc = vmc_fork(parent->vmc);
    if (!child_vmc) {
        debugf_warn("proc_fork: vmc_fork failed\n");
        return -ENOMEM;
    }

    pcb_t *child = kmalloc(sizeof(pcb_t));
    if (!child) {
        return -ENOMEM;
    }
    memset(child, 0, sizeof(pcb_t));

    child->pid   = __sync_add_and_fetch(&global_pid, 1);
    child->state = PROC_READY;

    child->name = NULL;

    child->parent         = parent;
    child->children       = NULL;
    child->children_count = 0;

    child->wakeup_tick = 0;

    child->fd_table.entries = NULL;
    child->fd_table.size    = 0;

    if (parent->fd_table.size > 0) {
        for (size_t i = 0; i < parent->fd_table.size; i++) {
            fd_entry_t *pe = &parent->fd_table.entries[i];
            if (pe->type == FD_NONE)
                continue;

            if (pe->type == FD_FILE) {
                fileio_t *pf = (fileio_t *)pe->ptr;
                if (pf) {
                    fileio_t *nf = kmalloc(sizeof(fileio_t));
                    if (!nf) {
                        continue;
                    }
                    memcpy(nf, pf, sizeof(fileio_t));
                    if (nf->private && !(nf->flags & PIPE_READ_END) &&
                        !(nf->flags & PIPE_WRITE_END)) {
                        vnode_ref((vnode_t *)nf->private);
                    }
                    fd_alloc(&child->fd_table, FD_FILE, nf);
                }
            } else if (pe->type == FD_DIR) {
                fd_alloc(&child->fd_table, FD_DIR, pe->ptr);
            }
        }
    }

    child->cwd   = parent->cwd;
    child->cpu   = parent->cpu;
    child->flags = parent->flags;

    child->vmc = child_vmc;

    child->cred = kmalloc(sizeof(user_cred_t));
    if (!child->cred) {
        kfree(child->fd_table.entries);
        kfree(child);
        return -ENOMEM;
    }
    memcpy(child->cred, parent->cred, sizeof(user_cred_t));

    signal_process_fork(child, parent);

    child->pgid              = parent->pgid;
    child->sid               = parent->sid;
    child->is_session_leader = 0;
    child->ctty              = parent->ctty;

    child->thread_count = 1;
    child->threads      = kmalloc(sizeof(tcb_t *));
    if (!child->threads) {
        kfree(child->cred);
        kfree(child->fd_table.entries);
        kfree(child);
        return -ENOMEM;
    }

    tcb_t *child_thread = kmalloc(sizeof(tcb_t));
    if (!child_thread) {
        kfree(child->threads);
        kfree(child->cred);
        kfree(child->fd_table.entries);
        kfree(child);
        return -ENOMEM;
    }
    memset(child_thread, 0, sizeof(tcb_t));

    child_thread->tid        = 0;
    child_thread->parent     = child;
    child_thread->flags      = current->flags;
    child_thread->state      = THREAD_READY;
    child_thread->priority   = current->priority;
    child_thread->time_slice = SCHEDULER_THREAD_TS;

    child_thread->fpu = (void *)PHYS_TO_VIRTUAL(pmm_alloc_page());
    if (!child_thread->fpu) {
        kfree(child_thread);
        kfree(child->threads);
        kfree(child->cred);
        kfree(child->fd_table.entries);
        kfree(child);
        return -ENOMEM;
    }
    memcpy(child_thread->fpu, current->fpu, PFRAME_SIZE);

    signal_thread_fork(child_thread, current);

    registers_t *child_regs = kmalloc(sizeof(registers_t));
    if (!child_regs) {
        kfree(child_thread->fpu);
        kfree(child_thread);
        kfree(child->threads);
        kfree(child->cred);
        kfree(child->fd_table.entries);
        kfree(child);
        return -ENOMEM;
    }
    memcpy(child_regs, regs, sizeof(registers_t));
    child_regs->rax = 0;

    child_thread->regs = child_regs;

    child_thread->kernel_stack =
        valloc(child->vmc, SCHEDULER_STACK_PAGES, VMO_KERNEL_RW, NULL);

    uint64_t kstack_virt = (uint64_t)(uintptr_t)child_thread->kernel_stack;
    uint64_t kstack_phys = pg_virtual_to_phys(
        (uint64_t *)(uintptr_t)PHYS_TO_VIRTUAL(child->vmc->pml4_table),
        kstack_virt);

    child_thread->kernel_stack =
        (void *)(uintptr_t)PHYS_TO_VIRTUAL(kstack_phys);

    uint64_t user_stack_top  = USER_STACK_TOP;
    uint64_t user_stack_base = user_stack_top - SCHEDULER_STACKSZ;

    vfree(child->vmc, (void *)(uintptr_t)user_stack_base, false);

    child_thread->user_stack =
        valloc_at(child->vmc, (void *)(uintptr_t)user_stack_base,
                  SCHEDULER_STACK_PAGES, VMO_USER_RW, NULL);

    if (!child_thread->user_stack) {
        debugf_warn("proc_fork: valloc_at failed for child user stack!\n");
        kfree(child_regs);
        kfree(child_thread->fpu);
        kfree(child_thread);
        kfree(child->threads);
        kfree(child->cred);
        kfree(child->fd_table.entries);
        kfree(child);
        return -ENOMEM;
    }

    uint64_t child_stack_phys =
        pg_virtual_to_phys((uint64_t *)PHYS_TO_VIRTUAL(child->vmc->pml4_table),
                           (uint64_t)(uintptr_t)child_thread->user_stack);

    child_thread->user_stack =
        (void *)(uintptr_t)PHYS_TO_VIRTUAL(child_stack_phys);

    memcpy(child_thread->user_stack, current->user_stack, SCHEDULER_STACKSZ);

    memcpy(&child_thread->tls, &current->tls, sizeof(tls_region_t));
    child_thread->tls_ptr       = current->tls_ptr;
    child_thread->tls.base_phys = NULL;

    child->main_thread = child_thread;
    child->threads[0]  = child_thread;

    child->exit_code  = 0;
    child->exited     = 0;
    child->wait_lock  = (atomic_flag)ATOMIC_FLAG_INIT;
    child->wait_queue = NULL;

    proc_add_child(parent, child);

    procfs_add_process(child);

    spinlock_acquire(&SCHEDULER_LOCK);
    int cpu = get_cpu();
    mlfq_enqueue(cpu, child_thread, child_thread->priority);
    spinlock_release(&SCHEDULER_LOCK);

    return child->pid;
}

// marks all threads as READY
int proc_engage(pcb_t *proc) {
    if (!proc) {
        return -EINVAL;
    }

    spinlock_acquire(&SCHEDULER_LOCK);

    for (int i = 0; i < proc->thread_count; i++) {
        tcb_t *thread = proc->threads[i];
        if (!thread) {
            continue;
        }

        if (thread->state == THREAD_WAITING) {
            thread->state = THREAD_READY;
        }
    }

    spinlock_release(&SCHEDULER_LOCK);

    return EOK;
}

void proc_add_child(pcb_t *parent, pcb_t *child) {
    if (!parent || !child)
        return;

    parent->children = krealloc(parent->children,
                                sizeof(pcb_t *) * (parent->children_count + 1));
    parent->children[parent->children_count] = child;
    parent->children_count++;
    child->parent = parent;
}

void proc_remove_child(pcb_t *parent, pcb_t *child) {
    if (!parent || !child)
        return;

    for (int i = 0; i < parent->children_count; i++) {
        if (parent->children[i] == child) {
            for (int j = i; j < parent->children_count - 1; j++) {
                parent->children[j] = parent->children[j + 1];
            }
            parent->children_count--;
            if (parent->children_count == 0) {
                kfree(parent->children);
                parent->children = NULL;
            } else {
                parent->children = krealloc(
                    parent->children, sizeof(pcb_t *) * parent->children_count);
            }
            return;
        }
    }
}

static int wake_waiter_for_child(pcb_t *parent, pcb_t *child) {
    if (!parent || !child)
        return 0;

    spinlock_acquire(&parent->wait_lock);

    tcb_t **prev_ptr = &parent->wait_queue;
    tcb_t *waiter    = parent->wait_queue;

    while (waiter) {
        int match = 0;

        if (waiter->wait_pid == -1) {
            match = 1;
        } else if (waiter->wait_pid == 0) {
            match = (child->pgid == parent->pgid);
        } else if (waiter->wait_pid > 0) {
            match = (child->pid == waiter->wait_pid);
        } else {
            match = (child->pgid == -waiter->wait_pid);
        }

        if (match) {
            *prev_ptr            = waiter->wq_next;
            waiter->wq_next      = NULL;
            waiter->on_waitqueue = 0;

            int child_pid   = child->pid;
            int exit_status = __W_EXITCODE(child->exit_code, 0);

            waiter->wait_result = child_pid;
            waiter->wait_status = exit_status;

            if (waiter->regs) {
                waiter->regs->rax = (uint64_t)(int64_t)child_pid;
            }

            spinlock_release(&parent->wait_lock);

            waiter->state = THREAD_READY;
            spinlock_acquire(&SCHEDULER_LOCK);
            int cpu = get_cpu();
            mlfq_enqueue(cpu, waiter, waiter->priority);
            spinlock_release(&SCHEDULER_LOCK);

            return 1;
        }

        prev_ptr = &waiter->wq_next;
        waiter   = waiter->wq_next;
    }

    spinlock_release(&parent->wait_lock);
    return 0;
}

static void add_to_wait_queue(pcb_t *parent, tcb_t *thread) {
    if (!parent || !thread)
        return;

    spinlock_acquire(&parent->wait_lock);
    thread->wq_next      = parent->wait_queue;
    thread->on_waitqueue = 1;
    thread->state        = THREAD_WAITING;
    parent->wait_queue   = thread;
    spinlock_release(&parent->wait_lock);
}

int do_waitpid(int pid, int *status_ptr, int options) {
    pcb_t *current = get_current_pcb();
    if (!current)
        return -EFAULT;

    if (current->children_count == 0) {
        return -ECHILD;
    }

    for (int i = 0; i < current->children_count; i++) {
        pcb_t *child = current->children[i];
        if (!child)
            continue;

        if (pid > 0 && child->pid != pid)
            continue;
        if (pid == 0 && child->pgid != current->pgid)
            continue;
        if (pid < -1 && child->pgid != -pid)
            continue;

        if (child->exited) {
            int child_pid   = child->pid;
            int exit_status = __W_EXITCODE(child->exit_code, 0);

            if (status_ptr) {
                copy_to_user(status_ptr, &exit_status, sizeof(int));
            }

            child->exited = 0;
            proc_remove_child(current, child);

            if (child->state == PROC_DEAD && child->thread_count == 0) {
                if (child->fd_table.entries) {
                    for (size_t k = 0; k < child->fd_table.size; k++) {
                        fd_entry_t *fe = &child->fd_table.entries[k];
                        if (fe->type == FD_FILE && fe->ptr) {
                            close((fileio_t *)fe->ptr);
                            fe->ptr = NULL;
                        }
                    }
                    kfree(child->fd_table.entries);
                }
                if (child->name)
                    kfree(child->name);
                if (child->cred)
                    kfree(child->cred);
                if (child->children)
                    kfree(child->children);
                if (child->threads)
                    kfree(child->threads);
                kfree(child);
            }

            return child_pid;
        }
    }

    if (options & WNOHANG) {
        return 0;
    }

    if (pid > 0) {
        int found = 0;
        for (int i = 0; i < current->children_count; i++) {
            if (current->children[i] && current->children[i]->pid == pid) {
                found = 1;
                break;
            }
        }
        if (!found) {
            return -ECHILD;
        }
    }

    tcb_t *me = get_current_tcb();
    if (!me)
        return -EFAULT;

    registers_t *ctx = get_syscall_context();
    if (!ctx)
        return -EFAULT;

    me->wait_pid        = pid;
    me->wait_status_ptr = status_ptr;
    me->wait_result     = 0;
    me->wait_status     = 0;
    me->regs            = ctx;

    add_to_wait_queue(current, me);

    yield(ctx);
    __builtin_unreachable();
}

int allocate_tls(tcb_t *thread, size_t requested_size) {
    if (!thread || !(thread->flags & TF_MODE_USER) || !thread->parent) {
        debugf_warn("allocate_tls: invalid thread or not usermode\n");
        return -EINVAL;
    }

    if (requested_size < TLS_MIN_SIZE) {
        requested_size = TLS_MIN_SIZE;
    }
    if (requested_size > TLS_MAX_SIZE) {
        debugf_warn("allocate_tls: requested size %zu exceeds max %zu\n",
                    requested_size, (size_t)TLS_MAX_SIZE);
        return -EINVAL;
    }

    size_t tcb_size   = sizeof(user_tls_t);
    size_t total_size = ROUND_UP(requested_size, PFRAME_SIZE);
    if (total_size < tcb_size) {
        total_size = ROUND_UP(tcb_size, PFRAME_SIZE);
    }
    size_t pages = total_size / PFRAME_SIZE;

    uint64_t *pml4 =
        (uint64_t *)PHYS_TO_VIRTUAL(thread->parent->vmc->pml4_table);

    uint64_t region_virt  = choose_random_tls_base();
    uint64_t fs_base_virt = region_virt + (total_size - tcb_size);

    while (is_region_mapped_in((void *)pml4, region_virt, pages)) {
        region_virt  = choose_random_tls_base();
        fs_base_virt = region_virt + (total_size - tcb_size);
    }

    uint64_t region_phys = (uint64_t)(uintptr_t)pmm_alloc_pages(pages);
    if (!region_phys) {
        debugf_warn("allocate_tls: failed to allocate %zu pages\n", pages);
        return -ENOMEM;
    }

    uint64_t region_kernel_virt = PHYS_TO_VIRTUAL(region_phys);
    memset((void *)region_kernel_virt, 0, total_size);

    uint64_t tcb_offset = fs_base_virt - region_virt;
    user_tls_t *tcb     = (user_tls_t *)(region_kernel_virt + tcb_offset);
    tcb->self           = (user_tls_t *)fs_base_virt;
    tcb->dtv            = NULL;
    tcb->private_data   = NULL;
    tcb->errno          = 0;
    tcb->stack_guard    = (void *)0xDEADBEEFCAFEBABEULL;

    map_region(pml4, region_phys, region_virt, pages, PMLE_USER_READ_WRITE);

    thread->tls.base_virt = (void *)fs_base_virt;
    thread->tls.base_phys = (void *)region_phys;
    thread->tls.size      = total_size;
    thread->tls.pages     = pages;
    thread->tls_ptr       = tcb;

    return EOK;
}

void free_tls(tcb_t *thread) {
    if (!thread || !thread->tls.base_phys) {
        return;
    }

    if (thread->parent && thread->parent->vmc) {
        uint64_t region_virt = (uint64_t)(uintptr_t)thread->tls.base_virt -
                               (thread->tls.size - sizeof(user_tls_t));

        unmap_region(
            (uint64_t *)PHYS_TO_VIRTUAL(thread->parent->vmc->pml4_table),
            region_virt, thread->tls.pages);
    }

    pmm_free(thread->tls.base_phys, thread->tls.pages);

    debugf_debug("Freed TLS for TID=%d: %zu pages\n", thread->tid,
                 thread->tls.pages);

    memset(&thread->tls, 0, sizeof(tls_region_t));
    thread->tls_ptr = NULL;
}

int find_new_tls_base(tcb_t *tcb, size_t size) {
    if (!tcb || !(tcb->flags & TF_MODE_USER) || !tcb->parent) {
        debugf_warn("find_new_tls_base: invalid tcb or not usermode\n");
        return -EINVAL;
    }

    if (tcb->tls.base_phys) {
        free_tls(tcb);
    }

    return allocate_tls(tcb, size);
}

tcb_t *get_current_tcb() {
    int cpu = get_cpu();
    return current_threads[cpu];
}

pcb_t *pcb_lookup(int pid) {
    for (int c = 0; c < cpu_count; c++) {
        tcb_t *cur = current_threads[c];
        if (cur && cur->parent && cur->parent->pid == pid)
            return cur->parent;

        for (int p = 0; p < CONFIG_SCHED_NUM_MLFQ_QUEUES; p++) {
            for (tcb_t *t = thread_queues[c].queues[p].head; t; t = t->next) {
                if (t->parent && t->parent->pid == pid)
                    return t->parent;
            }
        }
    }

    return NULL;
}

tcb_t *tcb_lookup(int pid, int tid) {
    for (int c = 0; c < cpu_count; c++) {
        tcb_t *cur = current_threads[c];
        if (cur && cur->parent && cur->parent->pid == pid && cur->tid == tid)
            return cur;

        for (int p = 0; p < CONFIG_SCHED_NUM_MLFQ_QUEUES; p++) {
            for (tcb_t *t = thread_queues[c].queues[p].head; t; t = t->next) {
                if (t->parent && t->parent->pid == pid && t->tid == tid)
                    return t;
            }
        }
    }

    return NULL;
}

static void def_idle_proc() {
    while (1) {
        __asm__ volatile("hlt");
    }
}

// new array that has init path at the beginning and then the cmdline speicified
// arguments
const char **get_init_argv() {
    const char *init_path = get_bootloader_data()->init_exec;

    static char *argv_buffer[CONFIG_KERNEL_INIT_PROC_MAX_ARGS];
    memset(argv_buffer, 0, sizeof(argv_buffer));

    argv_buffer[0] = init_path;

    for (int i = 0; i < cmdline_init_argc; i++) {
        argv_buffer[i + 1] = cmdline_init_argv[i];
    }

    return (const char **)argv_buffer;
}

const char **get_init_argv_no_init_exec_arg(const char *init_path) {
    static char *argv_buffer[CONFIG_KERNEL_INIT_PROC_MAX_ARGS];
    memset(argv_buffer, 0, sizeof(argv_buffer));

    argv_buffer[0] = init_path;

    for (int i = 0; i < cmdline_init_argc; i++) {
        argv_buffer[i + 1] = cmdline_init_argv[i];
    }

    return (const char **)argv_buffer;
}

int init_cpu_scheduler() {
    int cpu = get_cpu();

    if (cpu == get_bootloader_data()->bootstrap_cpu_id) {
        if (get_bootloader_data()->init_exec == NULL) {
            debugf_warn("No init executable specified, starting idle process "
                        "instead.\n");
            binfmt_exec("/bin/init.elf",
                        get_init_argv_no_init_exec_arg("/initrd/bin/init.elf"),
                        NULL);
        } else {
            binfmt_exec(get_bootloader_data()->init_exec, get_init_argv(),
                        NULL);
        }
    }

    {
        int idle_pid = proc_create(def_idle_proc, TF_MODE_KERNEL, "idle");
        proc_engage(pcb_lookup(idle_pid));
    }

    _cpu_set_msr(0xC0000102,
                 (uint64_t)&cpu_locals[get_cpu()]); // IA32_KERNEL_GS_BASE

    return 0;
}

int pcb_destroy(int pid) {
    pcb_t *p = pcb_lookup(pid);
    if (!p) {
        debugf_warn("pcb_destroy: PID=%d not found\n", pid);
        return EFAULT;
    }

    p->state = PROC_DEAD;
    for (int i = 0; i < p->thread_count; i++) {
        if (!p->threads[i]) {
            continue;
        }

        tcb_t *t = p->threads[i];
        t->state = THREAD_DEAD;
    }

    procfs_remove_process(p);

    return EOK;
}

void thread_push_to_queue(tcb_t *to_push) {
    int cpu = get_cpu();

    mlfq_enqueue(cpu, to_push, to_push->priority);
}

void thread_remove_from_queue(tcb_t *to_remove) {
    int cpu = get_cpu();

    for (int priority = 0; priority < CONFIG_SCHED_NUM_MLFQ_QUEUES;
         priority++) {
        mlfq_queue_t *queue = &thread_queues[cpu].queues[priority];
        tcb_t **p           = &queue->head;
        tcb_t *prev         = NULL;

        while (*p && *p != to_remove) {
            prev = *p;
            p    = &(*p)->next;
        }

        if (*p == NULL) {
            continue;
        }

        *p = to_remove->next;

        if (to_remove == queue->tail) {
            queue->tail = prev;
        }

        queue->count--;
        to_remove->next = NULL;
        return;
    }
}

int thread_destroy(int pid, int tid) {
    tcb_t *t = tcb_lookup(pid, tid);
    if (!t) {
        return EFAULT;
    }

    t->state = THREAD_DEAD;

    return EOK;
}

pcb_t *get_current_pcb() {
    int cpu        = get_cpu();
    tcb_t *current = current_threads[cpu];
    return current ? current->parent : NULL;
}

// destroys current process
int proc_exit(int exit_code) {
    int cpu        = get_cpu();
    tcb_t *current = current_threads[cpu];

    if (!current || !current->parent) {
        return EFAULT;
    }

    pcb_t *proc = current->parent;
    int pid     = proc->pid;
    char *name  = proc->name;

    if (pid == 1) {
        kpanic("Init process was killed!");
        scheduler_idle();
    }

    debugf_debug("Process %d (%s) exited with code %d\n", pid,
                 name ? name : "no-name", exit_code);

    proc->exit_code = exit_code;
    proc->exited    = 1;

    proc->state = PROC_DEAD;
    for (int i = 0; i < proc->thread_count; i++) {
        if (proc->threads[i]) {
            proc->threads[i]->state = THREAD_DEAD;
        }
    }
    procfs_remove_process(proc);

    pcb_t *init_proc = pcb_lookup(1);
    if (init_proc && proc->children_count > 0) {
        for (int i = 0; i < proc->children_count; i++) {
            pcb_t *child = proc->children[i];
            if (child) {
                child->parent = init_proc;
                proc_add_child(init_proc, child);
            }
        }
        proc->children_count = 0;
        kfree(proc->children);
        proc->children = NULL;
    }

    if (proc->parent) {
        wake_waiter_for_child(proc->parent, proc);
    }

    registers_t *ctx = get_syscall_context();
    if (!ctx)
        ctx = current->regs;

    yield(ctx);
    __builtin_unreachable();
}

int scheduler_enqueue(tcb_t *thread) {
    if (!thread) {
        return -EINVAL;
    }

    spinlock_acquire(&SCHEDULER_LOCK);
    thread_push_to_queue(thread);
    spinlock_release(&SCHEDULER_LOCK);
    return EOK;
}

void yield(registers_t *ctx) {
    __asm__ volatile("cli");

    int cpu        = get_cpu();
    tcb_t *current = current_threads[cpu];
    tcb_t *next    = NULL;

    _load_pml4(get_kernel_pml4());

    if (current && current->deferred_free_kstack) {
        pmm_free((void *)VIRT_TO_PHYSICAL(current->deferred_free_kstack),
                 SCHEDULER_STACK_PAGES);
        current->deferred_free_kstack = NULL;
    }

    thread_queues[cpu].ticks_since_boost++;
    if (thread_queues[cpu].ticks_since_boost >=
        CONFIG_SCHED_MLFQ_BOOST_INTERVAL) {
        mlfq_boost_all(cpu);
    }

    spinlock_acquire(&SCHEDULER_LOCK);

    if (current) {
        switch (current->state) {
        case THREAD_RUNNING:
            if (--current->time_slice > 0) {
                if (current->flags & TF_MODE_USER) {
                    signal_prepare_delivery(current, ctx);
                }

                spinlock_release(&SCHEDULER_LOCK);

                if (current->parent && current->parent->vmc) {
                    uint64_t pml4_phys = VIRT_TO_PHYSICAL(
                        (uint64_t)current->parent->vmc->pml4_table);
                    _load_pml4((uint64_t *)pml4_phys);
                }
                __asm__ volatile("sti");
                return;
            }

            fpu_save(current->fpu);
            current->regs = ctx;

            current->state = THREAD_READY;

            int new_priority = current->priority + 1;
            if (new_priority >= CONFIG_SCHED_NUM_MLFQ_QUEUES) {
                new_priority = CONFIG_SCHED_NUM_MLFQ_QUEUES - 1;
            }

#ifdef CONFIG_SCHED_DEBUG
            if (new_priority != current->priority) {
                debugf_debug("Thread TID=%d demoted from priority %d to %d\n",
                             current->tid, current->priority, new_priority);
            }
#endif

            mlfq_enqueue(cpu, current, new_priority);
            next = pick_next_thread(cpu);
            break;

        case THREAD_READY:
            fpu_save(current->fpu);
            current->regs = ctx;
            next          = pick_next_thread(cpu);
            break;

        case THREAD_WAITING:
            fpu_save(current->fpu);
            current->regs = ctx;

            next = pick_next_thread(cpu);
            break;

        case THREAD_DEAD:
            thread_remove_from_queue(current);
            next = pick_next_thread(cpu);
            cleanup_dead_thread(current);
            current = NULL;
            break;

        default:
            debugf_warn(
                "yield: CPU %d current thread TID=%d has unknown state %d\n",
                cpu, current->tid, (int)current->state);
            current->state = THREAD_DEAD;
            next           = pick_next_thread(cpu);
            break;
        }
    } else {
        next = pick_next_thread(cpu);
    }

    if (!next) {
        current_threads[cpu] = NULL;
        spinlock_release(&SCHEDULER_LOCK);
        __asm__ volatile("sti");
        while (1) {
            __asm__ volatile("hlt");

            __asm__ volatile("cli");
            spinlock_acquire(&SCHEDULER_LOCK);
            next = pick_next_thread(cpu);
            if (next) {
                break;
            }
            spinlock_release(&SCHEDULER_LOCK);
            __asm__ volatile("sti");
        }
    }

    current_threads[cpu] = next;
    next->state          = THREAD_RUNNING;

    spinlock_release(&SCHEDULER_LOCK);

#ifdef CONFIG_SCHED_DEBUG
    if (next && next->parent) {
        debugf_debug("yield: switching to PID=%d TID=%d RIP=%p RSP=%p\n",
                     next->parent->pid, next->tid, (void *)next->regs->rip,
                     (void *)next->regs->rsp);
    }
#endif

    registers_t run_ctx = *next->regs;
    void *next_fpu      = next->fpu;
    vmc_t *next_vmc     = next->parent ? next->parent->vmc : NULL;

    if (next && (next->flags & TF_MODE_USER)) {
        uint64_t kernel_stack_top =
            (uint64_t)next->kernel_stack + SCHEDULER_STACKSZ;
        tss_set_kernel_stack(kernel_stack_top);

        if (next->tls.base_virt) {
            _cpu_set_msr(0xC0000100, (uint64_t)next->tls.base_virt);
        }

        signal_prepare_delivery(next, &run_ctx);
    }

    if (next->wait_status_ptr && next->wait_result > 0) {
        copy_to_user(next->wait_status_ptr, &next->wait_status, sizeof(int));
        next->wait_status_ptr = NULL;

        pcb_t *woken_parent = next->parent;
        int waited_pid      = next->wait_result;
        next->wait_result   = 0;

        if (woken_parent) {
            for (int i = 0; i < woken_parent->children_count; i++) {
                pcb_t *ch = woken_parent->children[i];
                if (ch && ch->pid == waited_pid) {
                    ch->exited = 0;
                    proc_remove_child(woken_parent, ch);
                    if (ch->state == PROC_DEAD && ch->thread_count == 0) {
                        if (ch->fd_table.entries) {
                            for (size_t k = 0; k < ch->fd_table.size; k++) {
                                fd_entry_t *fe = &ch->fd_table.entries[k];
                                if (fe->type == FD_FILE && fe->ptr) {
                                    close((fileio_t *)fe->ptr);
                                    fe->ptr = NULL;
                                }
                            }
                            kfree(ch->fd_table.entries);
                        }
                        if (ch->name)
                            kfree(ch->name);
                        if (ch->cred)
                            kfree(ch->cred);
                        if (ch->children)
                            kfree(ch->children);
                        if (ch->threads)
                            kfree(ch->threads);
                        kfree(ch);
                    }
                    break;
                }
            }
        }
    }

    fpu_restore(next_fpu);

    if (next_vmc) {
        uint64_t pml4_phys = VIRT_TO_PHYSICAL((uint64_t)next_vmc->pml4_table);
        vmc_switch(next_vmc);
        _load_pml4((uint64_t *)pml4_phys);
    }

    context_load(&run_ctx);
    __builtin_unreachable();
}