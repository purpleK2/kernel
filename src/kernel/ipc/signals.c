#include "signals.h"
#include "stdio.h"

#include <errors.h>
#include <memory/heap/kheap.h>
#include <scheduler/scheduler.h>
#include <system/spinlock.h>
#include <system/time.h>
#include <uaccess.h>

#include <string.h>

extern registers_t *get_syscall_context(void);

#define KSIG_UNMASKABLE_MASK                                                   \
    ((1ULL << (KSIGKILL - 1)) | (1ULL << (KSIGSTOP - 1)))

typedef struct rt_sigframe {
    uint64_t return_address;
    registers_t saved_regs;
    user_siginfo_t info;
    ksigset_t saved_mask;
} rt_sigframe_t;

static inline bool signal_valid(int sig) {
    return sig > 0 && sig <= KSIG_MAX;
}

static inline ksigset_t signal_bit(int sig) {
    return 1ULL << (sig - 1);
}

static inline bool signal_is_realtime(int sig) {
    return sig >= KSIGRTMIN && sig <= KSIGRTMAX;
}

static inline bool signal_is_default_ignored(int sig) {
    return sig == KSIGCHLD || sig == KSIGURG || sig == KSIGWINCH ||
           sig == KSIGCONT;
}

static inline ksigset_t user_sigset_to_mask(const user_sigset_t *set) {
    if (!set)
        return 0;
    return (ksigset_t)set->__sig[0];
}

static inline void mask_to_user_sigset(ksigset_t mask, user_sigset_t *set) {
    memset(set, 0, sizeof(*set));
    set->__sig[0] = (unsigned long)mask;
}

static void user_siginfo_from_kernel(user_siginfo_t *out,
                                     const ksiginfo_t *in) {
    memset(out, 0, sizeof(*out));
    out->si_signo                                = in->si_signo;
    out->si_errno                                = in->si_errno;
    out->si_code                                 = in->si_code;
    out->__si_fields.__si_common.__piduid.si_pid = in->si_pid;
    out->__si_fields.__si_common.__piduid.si_uid = in->si_uid;
    out->__si_fields.__si_common.__second.__sigval.sival_int =
        in->si_value.sival_int;
}

static void kernel_siginfo_from_user(ksiginfo_t *out, int sig,
                                     const user_siginfo_t *in) {
    memset(out, 0, sizeof(*out));
    out->si_signo = sig;
    out->si_errno = in->si_errno;
    out->si_code  = in->si_code;
    out->si_pid   = in->__si_fields.__si_common.__piduid.si_pid;
    out->si_uid   = in->__si_fields.__si_common.__piduid.si_uid;
    out->si_value.sival_int =
        in->__si_fields.__si_common.__second.__sigval.sival_int;
}

static int queue_pending_signal(ksigset_t *pending_mask,
                                uint16_t *pending_count,
                                ksiginfo_t *pending_info, int sig,
                                const ksiginfo_t *info) {
    if (!signal_valid(sig))
        return -EINVAL;

    if (!signal_is_realtime(sig) && pending_count[sig] > 0)
        return 0;

    if (pending_count[sig] == UINT16_MAX)
        return -EAGAIN;

    pending_count[sig]++;
    *pending_mask |= signal_bit(sig);
    if (info)
        pending_info[sig] = *info;
    else
        memset(&pending_info[sig], 0, sizeof(pending_info[sig]));

    return 0;
}

static bool dequeue_pending_signal(ksigset_t *pending_mask,
                                   uint16_t *pending_count,
                                   ksiginfo_t *pending_info, ksigset_t allowed,
                                   int *sig, ksiginfo_t *info, bool consume) {
    for (int s = 1; s <= KSIG_MAX; s++) {
        ksigset_t bit = signal_bit(s);
        if (!(*pending_mask & bit))
            continue;
        if (!(allowed & bit))
            continue;

        *sig  = s;
        *info = pending_info[s];

        if (consume) {
            if (pending_count[s] > 0)
                pending_count[s]--;
            if (pending_count[s] == 0)
                *pending_mask &= ~bit;
        }

        return true;
    }

    return false;
}

static bool thread_has_unblocked_pending_locked(pcb_t *proc, tcb_t *thread) {
    int sig = 0;
    ksiginfo_t info;
    ksigset_t allowed  = ~thread->signal_mask;
    allowed           |= KSIG_UNMASKABLE_MASK;

    if (dequeue_pending_signal(
            &thread->signal_pending, thread->signal_pending_count,
            thread->signal_pending_info, allowed, &sig, &info, false))
        return true;

    if (dequeue_pending_signal(
            &proc->signal_pending, proc->signal_pending_count,
            proc->signal_pending_info, allowed, &sig, &info, false))
        return true;

    return false;
}

static void signal_try_wake_thread(tcb_t *thread, int sig) {
    if (!thread)
        return;

    if (!thread->signal_waiting)
        return;

    if (thread->signal_wait_mask != 0 &&
        !(thread->signal_wait_mask & signal_bit(sig)))
        return;

    if (thread->state == THREAD_WAITING) {
        thread->state       = THREAD_READY;
        thread->wakeup_tick = 0;
        scheduler_enqueue(thread);
    }
}

static int signal_enqueue_thread(pcb_t *proc, tcb_t *thread, int sig,
                                 const ksiginfo_t *info) {
    int r = queue_pending_signal(&thread->signal_pending,
                                 thread->signal_pending_count,
                                 thread->signal_pending_info, sig, info);
    if (r)
        return r;

    signal_try_wake_thread(thread, sig);
    (void)proc;
    return 0;
}

static int signal_enqueue_process(pcb_t *proc, int sig,
                                  const ksiginfo_t *info) {
    int r =
        queue_pending_signal(&proc->signal_pending, proc->signal_pending_count,
                             proc->signal_pending_info, sig, info);
    if (r)
        return r;

    for (int i = 0; i < proc->thread_count; i++) {
        signal_try_wake_thread(proc->threads[i], sig);
    }

    return 0;
}

void signal_process_init(pcb_t *proc) {
    proc->signal_lock    = (atomic_flag)ATOMIC_FLAG_INIT;
    proc->signal_pending = 0;
    memset(proc->signal_pending_count, 0, sizeof(proc->signal_pending_count));
    memset(proc->signal_pending_info, 0, sizeof(proc->signal_pending_info));

    for (int s = 1; s <= KSIG_MAX; s++) {
        proc->sigactions[s].handler   = KSIG_DFL;
        proc->sigactions[s].sigaction = NULL;
        proc->sigactions[s].flags     = 0;
        proc->sigactions[s].restorer  = NULL;
        proc->sigactions[s].mask      = 0;
    }
}

void signal_thread_init(tcb_t *thread) {
    thread->signal_mask    = 0;
    thread->signal_pending = 0;
    memset(thread->signal_pending_count, 0,
           sizeof(thread->signal_pending_count));
    memset(thread->signal_pending_info, 0, sizeof(thread->signal_pending_info));
    thread->signal_waiting          = 0;
    thread->signal_wait_mask        = 0;
    thread->signal_suspend_active   = 0;
    thread->signal_suspend_old_mask = 0;
}

void signal_process_fork(pcb_t *child, const pcb_t *parent) {
    child->signal_lock    = (atomic_flag)ATOMIC_FLAG_INIT;
    child->signal_pending = 0;
    memset(child->signal_pending_count, 0, sizeof(child->signal_pending_count));
    memset(child->signal_pending_info, 0, sizeof(child->signal_pending_info));
    memcpy(child->sigactions, parent->sigactions, sizeof(child->sigactions));
}

void signal_thread_fork(tcb_t *child, const tcb_t *parent) {
    child->signal_mask    = parent->signal_mask;
    child->signal_pending = 0;
    memset(child->signal_pending_count, 0, sizeof(child->signal_pending_count));
    memset(child->signal_pending_info, 0, sizeof(child->signal_pending_info));
    child->signal_waiting          = 0;
    child->signal_wait_mask        = 0;
    child->signal_suspend_active   = 0;
    child->signal_suspend_old_mask = 0;
}

void signal_process_cleanup(pcb_t *proc) {
    (void)proc;
}

void signal_thread_cleanup(tcb_t *thread) {
    (void)thread;
}

int signal_prepare_delivery(tcb_t *thread, registers_t *regs) {
    if (!thread || !(thread->flags & TF_MODE_USER) || !thread->parent)
        return 0;

    pcb_t *proc = thread->parent;

    while (1) {
        ksigaction_t action;
        ksiginfo_t info;
        int sig          = 0;
        bool from_thread = false;

        spinlock_acquire(&proc->signal_lock);

        ksigset_t allowed  = ~thread->signal_mask;
        allowed           |= KSIG_UNMASKABLE_MASK;

        if (dequeue_pending_signal(
                &thread->signal_pending, thread->signal_pending_count,
                thread->signal_pending_info, allowed, &sig, &info, true)) {
            from_thread = true;
        } else if (!dequeue_pending_signal(
                       &proc->signal_pending, proc->signal_pending_count,
                       proc->signal_pending_info, allowed, &sig, &info, true)) {
            spinlock_release(&proc->signal_lock);
            return 0;
        }

        action = proc->sigactions[sig];

        if (action.handler == KSIG_IGN ||
            (action.handler == KSIG_DFL && signal_is_default_ignored(sig))) {
            spinlock_release(&proc->signal_lock);
            (void)from_thread;
            continue;
        }

        if (action.handler == KSIG_DFL) {
            spinlock_release(&proc->signal_lock);
            proc_exit(128 + sig);
            return -EINTR;
        }

        if (action.flags & KSIG_SA_RESETHAND) {
            proc->sigactions[sig].handler   = KSIG_DFL;
            proc->sigactions[sig].sigaction = NULL;
            proc->sigactions[sig].flags     = 0;
            proc->sigactions[sig].restorer  = NULL;
            proc->sigactions[sig].mask      = 0;
        }

        spinlock_release(&proc->signal_lock);

        rt_sigframe_t frame;
        memset(&frame, 0, sizeof(frame));

        frame.return_address = (uint64_t)(uintptr_t)action.restorer;
        frame.saved_regs     = *regs;
        frame.saved_mask     = thread->signal_mask;
        user_siginfo_from_kernel(&frame.info, &info);

        uint64_t frame_sp = ((regs->rsp - sizeof(frame) - 8) & ~0xFULL) + 8;
        if (!user_range_ok((void *)(uintptr_t)frame_sp, sizeof(frame))) {
            proc_exit(128 + KSIGSEGV);
            return -EFAULT;
        }

        if (copy_to_user((void *)(uintptr_t)frame_sp, &frame, sizeof(frame)) !=
            0) {
            proc_exit(128 + KSIGSEGV);
            return -EFAULT;
        }

        thread->signal_mask |= action.mask;
        if (!(action.flags & KSIG_SA_NODEFER))
            thread->signal_mask |= signal_bit(sig);
        thread->signal_mask &= ~KSIG_UNMASKABLE_MASK;

        regs->rip = (uint64_t)(uintptr_t)((action.flags & KSIG_SA_SIGINFO)
                                              ? (void *)action.sigaction
                                              : (void *)action.handler);
        regs->rsp = frame_sp;
        regs->rdi = (uint64_t)sig;
        if (action.flags & KSIG_SA_SIGINFO) {
            regs->rsi = frame_sp + offsetof(rt_sigframe_t, info);
            regs->rdx = 0;
        }

        return 1;
    }
}

int sys_rt_sigaction(int sig, const user_sigaction_t *act,
                     user_sigaction_t *oact, size_t sigsetsize) {
    debugf_debug("sys_rt_sigaction: sig=%d act=%p oact=%p sigsetsize=%zu\n",
                 sig, act, oact, sigsetsize);
    if (!signal_valid(sig)) {
        debugf_warn("sys_rt_sigaction: invalid signal %d\n", sig);
        return -EINVAL;
    }

    if (sigsetsize < sizeof(unsigned long))
        return -EINVAL;

    if (sig == KSIGKILL || sig == KSIGSTOP) {
        if (act)
            return -EINVAL;
    }

    pcb_t *proc = get_current_pcb();
    if (!proc)
        return -EFAULT;

    spinlock_acquire(&proc->signal_lock);

    if (oact) {
        user_sigaction_t old_user;
        memset(&old_user, 0, sizeof(old_user));
        old_user.__sa_handler.sa_handler = proc->sigactions[sig].handler;
        old_user.sa_flags                = proc->sigactions[sig].flags;
        old_user.sa_restorer             = proc->sigactions[sig].restorer;
        mask_to_user_sigset(proc->sigactions[sig].mask, &old_user.sa_mask);

        spinlock_release(&proc->signal_lock);
        if (copy_to_user(oact, &old_user, sizeof(old_user)) != 0)
            return -EFAULT;
        spinlock_acquire(&proc->signal_lock);
    }

    if (act) {
        user_sigaction_t new_user;
        if (copy_from_user(&new_user, act, sizeof(new_user)) != 0) {
            spinlock_release(&proc->signal_lock);
            return -EFAULT;
        }

        proc->sigactions[sig].handler = new_user.__sa_handler.sa_handler;
        proc->sigactions[sig].sigaction =
            (void (*)(int, void *, void *))new_user.__sa_handler.sa_sigaction;
        proc->sigactions[sig].flags    = new_user.sa_flags;
        proc->sigactions[sig].restorer = new_user.sa_restorer;
        proc->sigactions[sig].mask =
            user_sigset_to_mask(&new_user.sa_mask) & ~KSIG_UNMASKABLE_MASK;
    }

    spinlock_release(&proc->signal_lock);
    return 0;
}

int sys_rt_sigprocmask(int how, const user_sigset_t *set, user_sigset_t *oldset,
                       size_t sigsetsize) {
    if (sigsetsize < sizeof(unsigned long))
        return -EINVAL;

    tcb_t *thread = get_current_tcb();
    pcb_t *proc   = get_current_pcb();
    if (!thread || !proc)
        return -EFAULT;

    spinlock_acquire(&proc->signal_lock);

    if (oldset) {
        user_sigset_t old_user;
        mask_to_user_sigset(thread->signal_mask, &old_user);
        spinlock_release(&proc->signal_lock);
        if (copy_to_user(oldset, &old_user, sizeof(old_user)) != 0)
            return -EFAULT;
        spinlock_acquire(&proc->signal_lock);
    }

    if (set) {
        user_sigset_t new_user;
        if (copy_from_user(&new_user, set, sizeof(new_user)) != 0) {
            spinlock_release(&proc->signal_lock);
            return -EFAULT;
        }

        ksigset_t new_mask =
            user_sigset_to_mask(&new_user) & ~KSIG_UNMASKABLE_MASK;

        switch (how) {
        case KSIG_BLOCK:
            thread->signal_mask |= new_mask;
            break;
        case KSIG_UNBLOCK:
            thread->signal_mask &= ~new_mask;
            break;
        case KSIG_SETMASK:
            thread->signal_mask = new_mask;
            break;
        default:
            spinlock_release(&proc->signal_lock);
            return -EINVAL;
        }

        thread->signal_mask &= ~KSIG_UNMASKABLE_MASK;
    }

    spinlock_release(&proc->signal_lock);
    return 0;
}

int sys_rt_sigpending(user_sigset_t *set, size_t sigsetsize) {
    if (!set || sigsetsize < sizeof(unsigned long))
        return -EINVAL;

    tcb_t *thread = get_current_tcb();
    pcb_t *proc   = get_current_pcb();
    if (!thread || !proc)
        return -EFAULT;

    spinlock_acquire(&proc->signal_lock);
    ksigset_t pending = thread->signal_pending | proc->signal_pending;
    spinlock_release(&proc->signal_lock);

    user_sigset_t out;
    mask_to_user_sigset(pending, &out);
    if (copy_to_user(set, &out, sizeof(out)) != 0)
        return -EFAULT;

    return 0;
}

int sys_rt_sigsuspend(const user_sigset_t *set, size_t sigsetsize) {
    if (!set || sigsetsize < sizeof(unsigned long))
        return -EINVAL;

    tcb_t *thread    = get_current_tcb();
    pcb_t *proc      = get_current_pcb();
    registers_t *ctx = get_syscall_context();
    if (!thread || !proc || !ctx)
        return -EFAULT;

    user_sigset_t tmp;
    if (copy_from_user(&tmp, set, sizeof(tmp)) != 0)
        return -EFAULT;

    ksigset_t new_mask = user_sigset_to_mask(&tmp) & ~KSIG_UNMASKABLE_MASK;

    spinlock_acquire(&proc->signal_lock);

    thread->signal_suspend_active   = 1;
    thread->signal_suspend_old_mask = thread->signal_mask;
    thread->signal_mask             = new_mask;

    ctx->rax = (uint64_t)(int64_t)-EINTR;

    if (thread_has_unblocked_pending_locked(proc, thread)) {
        spinlock_release(&proc->signal_lock);
        signal_prepare_delivery(thread, ctx);
        return -EINTR;
    }

    thread->signal_waiting   = 1;
    thread->signal_wait_mask = 0;
    thread->state            = THREAD_WAITING;
    thread->regs             = ctx;

    spinlock_release(&proc->signal_lock);

    yield(ctx);
    __builtin_unreachable();
}

int sys_rt_sigtimedwait(const user_sigset_t *set, user_siginfo_t *info,
                        const timespec_t *timeout, size_t sigsetsize) {
    if (!set || sigsetsize < sizeof(unsigned long))
        return -EINVAL;

    tcb_t *thread    = get_current_tcb();
    pcb_t *proc      = get_current_pcb();
    registers_t *ctx = get_syscall_context();
    if (!thread || !proc || !ctx)
        return -EFAULT;

    user_sigset_t user_set;
    if (copy_from_user(&user_set, set, sizeof(user_set)) != 0)
        return -EFAULT;

    ksigset_t wait_set = user_sigset_to_mask(&user_set);
    if (!wait_set)
        return -EINVAL;

    bool infinite          = true;
    bool immediate_timeout = false;
    uint64_t target_tick   = 0;

    if (timeout) {
        timespec_t kts;
        if (copy_from_user(&kts, timeout, sizeof(kts)) != 0)
            return -EFAULT;
        if (kts.tv_sec < 0 || kts.tv_nsec < 0 || kts.tv_nsec >= 1000000000LL)
            return -EINVAL;

        if (kts.tv_sec == 0 && kts.tv_nsec == 0)
            immediate_timeout = true;

        infinite        = false;
        uint64_t ticks  = (uint64_t)kts.tv_sec * TICKS_PER_SEC;
        ticks          += (uint64_t)kts.tv_nsec / NS_PER_TICK;
        if (((uint64_t)kts.tv_nsec % NS_PER_TICK) != 0)
            ticks++;
        target_tick = get_ticks() + ticks;
    }

    while (1) {
        int sig = 0;
        ksiginfo_t kinfo;

        spinlock_acquire(&proc->signal_lock);

        if (dequeue_pending_signal(
                &thread->signal_pending, thread->signal_pending_count,
                thread->signal_pending_info, wait_set, &sig, &kinfo, true) ||
            dequeue_pending_signal(
                &proc->signal_pending, proc->signal_pending_count,
                proc->signal_pending_info, wait_set, &sig, &kinfo, true)) {
            spinlock_release(&proc->signal_lock);

            if (info) {
                user_siginfo_t uinfo;
                user_siginfo_from_kernel(&uinfo, &kinfo);
                if (copy_to_user(info, &uinfo, sizeof(uinfo)) != 0)
                    return -EFAULT;
            }

            return sig;
        }

        if (immediate_timeout) {
            spinlock_release(&proc->signal_lock);
            return -EAGAIN;
        }

        if (!infinite && get_ticks() >= target_tick) {
            spinlock_release(&proc->signal_lock);
            return -EAGAIN;
        }

        spinlock_release(&proc->signal_lock);

        thread->signal_waiting   = 1;
        thread->signal_wait_mask = wait_set;
        thread->regs             = ctx;
        thread->state            = THREAD_WAITING;
        if (!infinite)
            thread->wakeup_tick = target_tick;

        spinlock_acquire(&proc->signal_lock);
        int peek_sig = 0;
        ksiginfo_t peek_info;
        bool has_pending =
            dequeue_pending_signal(&thread->signal_pending,
                                   thread->signal_pending_count,
                                   thread->signal_pending_info, wait_set,
                                   &peek_sig, &peek_info, false) ||
            dequeue_pending_signal(&proc->signal_pending,
                                   proc->signal_pending_count,
                                   proc->signal_pending_info, wait_set,
                                   &peek_sig, &peek_info, false);
        spinlock_release(&proc->signal_lock);

        if (has_pending) {
            thread->signal_waiting   = 0;
            thread->signal_wait_mask = 0;
            thread->state            = THREAD_RUNNING;
            continue;
        }

        yield(ctx);
        thread->signal_waiting   = 0;
        thread->signal_wait_mask = 0;

        if (!infinite && get_ticks() >= target_tick)
            return -EAGAIN;
    }
}

int sys_rt_sigreturn(void) {
    tcb_t *thread    = get_current_tcb();
    pcb_t *proc      = get_current_pcb();
    registers_t *ctx = get_syscall_context();
    if (!thread || !proc || !ctx)
        return -EFAULT;

    uint64_t frame_addr = (uintptr_t)ctx->rsp - sizeof(uint64_t);

    rt_sigframe_t frame;
    if (!user_range_ok((void *)(uintptr_t)frame_addr, sizeof(frame)))
        return -EFAULT;
    if (copy_from_user(&frame, (void *)(uintptr_t)frame_addr, sizeof(frame)) !=
        0)
        return -EFAULT;

    spinlock_acquire(&proc->signal_lock);
    if (thread->signal_suspend_active) {
        thread->signal_mask =
            thread->signal_suspend_old_mask & ~KSIG_UNMASKABLE_MASK;
        thread->signal_suspend_active   = 0;
        thread->signal_suspend_old_mask = 0;
    } else {
        thread->signal_mask = frame.saved_mask & ~KSIG_UNMASKABLE_MASK;
    }
    spinlock_release(&proc->signal_lock);

    uint64_t keep_cr3       = ctx->cr3;
    uint64_t keep_ds        = ctx->ds;
    uint64_t keep_interrupt = ctx->interrupt;
    uint64_t keep_error     = ctx->error;

    *ctx = frame.saved_regs;

    ctx->cr3       = keep_cr3;
    ctx->ds        = keep_ds;
    ctx->interrupt = keep_interrupt;
    ctx->error     = keep_error;

    ctx->cs      = 0x1B;
    ctx->ss      = 0x23;
    ctx->ds      = 0x23;
    ctx->rflags |= 0x200;

    return (int)ctx->rax;
}

static int signal_send_to_process_locked(pcb_t *target, tcb_t *target_thread,
                                         int sig, const ksiginfo_t *info) {
    if (!target)
        return -ESRCH;

    if (target_thread)
        return signal_enqueue_thread(target, target_thread, sig, info);

    return signal_enqueue_process(target, sig, info);
}

static pcb_t *signal_find_process_fallback(int pid) {
    pcb_t *current = get_current_pcb();
    if (!current)
        return NULL;

    if (current->pid == pid)
        return current;

    if (current->parent && current->parent->pid == pid)
        return current->parent;

    for (int i = 0; i < current->children_count; i++) {
        pcb_t *child = current->children[i];
        if (child && child->pid == pid)
            return child;
    }

    if (current->parent) {
        pcb_t *parent = current->parent;
        for (int i = 0; i < parent->children_count; i++) {
            pcb_t *sib = parent->children[i];
            if (sib && sib->pid == pid)
                return sib;
        }
    }

    return NULL;
}

static pcb_t *signal_find_process(int pid) {
    pcb_t *target = pcb_lookup(pid);
    if (target)
        return target;
    return signal_find_process_fallback(pid);
}

static tcb_t *signal_find_thread_in_process(pcb_t *proc, int tid) {
    if (!proc)
        return NULL;

    for (int i = 0; i < proc->thread_count; i++) {
        tcb_t *t = proc->threads[i];
        if (t && t->tid == tid)
            return t;
    }

    return NULL;
}

int sys_tgkill(int tgid, int tid, int sig) {
    if (!signal_valid(sig) && sig != 0)
        return -EINVAL;

    pcb_t *target = signal_find_process(tgid);
    if (!target)
        return -ESRCH;

    tcb_t *thread = tcb_lookup(tgid, tid);
    if (!thread)
        thread = signal_find_thread_in_process(target, tid);
    if (!thread)
        return -ESRCH;

    if (sig == 0)
        return 0;

    ksiginfo_t info;
    memset(&info, 0, sizeof(info));
    info.si_signo = sig;
    info.si_code  = KSIG_SI_TKILL;
    info.si_pid   = get_current_pcb() ? get_current_pcb()->pid : 0;
    info.si_uid   = get_current_cred() ? get_current_cred()->uid : 0;

    spinlock_acquire(&target->signal_lock);
    int r = signal_send_to_process_locked(target, thread, sig, &info);
    spinlock_release(&target->signal_lock);
    return r;
}

int sys_kill(int pid, int sig) {
    if (!signal_valid(sig) && sig != 0)
        return -EINVAL;

    pcb_t *target = NULL;

    if (pid > 0) {
        target = signal_find_process(pid);
    } else if (pid == 0) {
        pcb_t *self = get_current_pcb();
        target      = self;
    } else if (pid == -1) {
        pcb_t *self = get_current_pcb();
        target      = self;
    } else {
        target = pcb_lookup(-pid);
    }

    if (!target)
        return -ESRCH;

    if (sig == 0)
        return 0;

    ksiginfo_t info;
    memset(&info, 0, sizeof(info));
    info.si_signo = sig;
    info.si_code  = KSIG_SI_USER;
    info.si_pid   = get_current_pcb() ? get_current_pcb()->pid : 0;
    info.si_uid   = get_current_cred() ? get_current_cred()->uid : 0;

    spinlock_acquire(&target->signal_lock);
    int r = signal_send_to_process_locked(target, NULL, sig, &info);
    spinlock_release(&target->signal_lock);

    return r;
}

int sys_rt_sigqueueinfo(int pid, int sig, const user_siginfo_t *info) {
    if (!signal_valid(sig))
        return -EINVAL;

    pcb_t *target = signal_find_process(pid);
    if (!target)
        return -ESRCH;

    ksiginfo_t kinfo;
    user_siginfo_t uinfo;
    if (copy_from_user(&uinfo, info, sizeof(uinfo)) != 0)
        return -EFAULT;

    kernel_siginfo_from_user(&kinfo, sig, &uinfo);
    kinfo.si_signo = sig;

    spinlock_acquire(&target->signal_lock);
    int r = signal_send_to_process_locked(target, NULL, sig, &kinfo);
    spinlock_release(&target->signal_lock);

    return r;
}
