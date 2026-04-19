#ifndef SIGNALS_H
#define SIGNALS_H

#include <cpu.h>
#include <types.h>
#include <system/sleep.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KSIG_MAX 64
#define KSIG_NSIG (KSIG_MAX + 1)

#define KSIG_BLOCK   0
#define KSIG_UNBLOCK 1
#define KSIG_SETMASK 2

#define KSIG_SI_USER  0
#define KSIG_SI_QUEUE (-1)
#define KSIG_SI_TKILL (-6)

#define KSIG_SA_NOCLDSTOP 1
#define KSIG_SA_NOCLDWAIT 2
#define KSIG_SA_SIGINFO 4
#define KSIG_SA_ONSTACK 0x08000000
#define KSIG_SA_RESTART 0x10000000
#define KSIG_SA_NODEFER 0x40000000
#define KSIG_SA_RESETHAND 0x80000000
#define KSIG_SA_RESTORER 0x04000000

#define KSIGHUP 1
#define KSIGINT 2
#define KSIGQUIT 3
#define KSIGILL 4
#define KSIGTRAP 5
#define KSIGABRT 6
#define KSIGBUS 7
#define KSIGFPE 8
#define KSIGKILL 9
#define KSIGUSR1 10
#define KSIGSEGV 11
#define KSIGUSR2 12
#define KSIGPIPE 13
#define KSIGALRM 14
#define KSIGTERM 15
#define KSIGCHLD 17
#define KSIGCONT 18
#define KSIGSTOP 19
#define KSIGTSTP 20
#define KSIGTTIN 21
#define KSIGTTOU 22
#define KSIGURG 23
#define KSIGXCPU 24
#define KSIGXFSZ 25
#define KSIGVTALRM 26
#define KSIGPROF 27
#define KSIGWINCH 28
#define KSIGIO 29
#define KSIGPWR 30
#define KSIGSYS 31
#define KSIGRTMIN 35
#define KSIGRTMAX 64

#define KSIG_DFL ((void (*)(int))0)
#define KSIG_IGN ((void (*)(int))1)

typedef struct process pcb_t;
typedef struct thread tcb_t;

typedef uint64_t ksigset_t;

typedef struct {
    int si_signo;
    int si_errno;
    int si_code;
    pid_t si_pid;
    uid_t si_uid;
    union {
        int sival_int;
        void *sival_ptr;
    } si_value;
} ksiginfo_t;

typedef struct {
    void (*handler)(int);
    void (*sigaction)(int, void *, void *);
    unsigned long flags;
    void (*restorer)(void);
    ksigset_t mask;
} ksigaction_t;

typedef struct {
    unsigned long __sig[1024 / (8 * sizeof(unsigned long))];
} user_sigset_t;

typedef struct {
    int si_signo;
    int si_errno;
    int si_code;
    union {
        char __pad[128 - 2 * sizeof(int) - sizeof(long)];
        struct {
            struct {
                pid_t si_pid;
                uid_t si_uid;
            } __piduid;
            union {
                struct {
                    int sival_int;
                } __sigval;
            } __second;
        } __si_common;
    } __si_fields;
} user_siginfo_t;

typedef struct {
    union {
        void (*sa_handler)(int);
        void (*sa_sigaction)(int, user_siginfo_t *, void *);
    } __sa_handler;
    unsigned long sa_flags;
    void (*sa_restorer)(void);
    user_sigset_t sa_mask;
} user_sigaction_t;

void signal_process_init(pcb_t *proc);
void signal_thread_init(tcb_t *thread);
void signal_process_fork(pcb_t *child, const pcb_t *parent);
void signal_thread_fork(tcb_t *child, const tcb_t *parent);
void signal_process_cleanup(pcb_t *proc);
void signal_thread_cleanup(tcb_t *thread);

int signal_prepare_delivery(tcb_t *thread, registers_t *regs);

int sys_rt_sigaction(int sig, const user_sigaction_t *act,
    user_sigaction_t *oact, size_t sigsetsize);
int sys_rt_sigprocmask(int how, const user_sigset_t *set,
    user_sigset_t *oldset, size_t sigsetsize);
int sys_rt_sigpending(user_sigset_t *set, size_t sigsetsize);
int sys_rt_sigsuspend(const user_sigset_t *set, size_t sigsetsize);
int sys_rt_sigtimedwait(const user_sigset_t *set,
    user_siginfo_t *info, const timespec_t *timeout,
        size_t sigsetsize);
int sys_rt_sigreturn(void);
int sys_kill(int pid, int sig);
int sys_tgkill(int tgid, int tid, int sig);
int sys_rt_sigqueueinfo(int pid, int sig, const user_siginfo_t *info);

#endif // SIGNALS_H
