#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "fs/fd.h"
#include "tsc/tsc.h"
#include "user/user.h"
#include <types.h>

#include <ipc/signals.h>
#include <memory/vmm/vmm.h>

#include <cpu.h>
#include <interrupts/isr.h>

#include <fs/file_io.h>

// **T**ime **S**lice, not that "ts"
#define SCHEDULER_THREAD_TS 10

#define SCHEDULER_STACK_PAGES 10
// keeping this just in case
#define SCHEDULER_STACKSZ (PFRAME_SIZE * SCHEDULER_STACK_PAGES)

#define TF_MODE_USER   (1 << 0)
#define TF_MODE_KERNEL (1 << 1)
#define TF_BUSY        (1 << 2)
#define TF_DETACHED    (1 << 3)
#define TF_HAS_FPU     (1 << 4)

#define PF_SETUID_BIT  (1 << 0)

#define TIME_SLICE_TICKS 10

#define TLS_MIN_SIZE PFRAME_SIZE
#define TLS_MAX_SIZE (PFRAME_SIZE * CONFIG_SCHED_TLS_MAX_SIZE_PAGES)
#define TLS_ALIGNMENT PFRAME_SIZE

#define USER_STACK_TOP 0x7FFFFFFFF000ULL

typedef enum pcb_state {
    PROC_READY,
    PROC_RUNNING,
    PROC_WAIT_FOR_THREAD,
    PROC_DEAD,
} pcb_state_t;

typedef enum tcb_state {
    THREAD_READY,   // good to go
    THREAD_RUNNING, // should be only one per CPU
    THREAD_WAITING, // I/O, syscall, ...
    THREAD_DEAD,
} tcb_state_t;

typedef struct process pcb_t;
typedef struct thread tcb_t;

typedef struct user_tls {
    struct user_tls *self;
    void *dtv;
    void *private_data;
    int errno;
    void *stack_guard;
} user_tls_t;

typedef struct tls_region {
    void *base_virt;
    void *base_phys;
    size_t size;
    size_t pages;
} tls_region_t;

typedef struct thread {
    int tid;
    struct process *parent;

    size_t time_slice;

    tcb_state_t state;
    int flags;

    registers_t *regs;
    void *fpu; // 512 bytes memory region

    int priority; // for MLFQ

    // for usermode
    void *kernel_stack;
    void *user_stack;

    // Deferred kernel stack to free (set by execve, freed by yield)
    void *deferred_free_kstack;

    atomic_flag lock;

    struct thread *next;

    uint64_t wakeup_tick;

    struct thread *wq_next;
    int            on_waitqueue;

    int wait_pid;
    int *wait_status_ptr;
    int wait_result;
    int wait_status;

    tls_region_t tls;
    user_tls_t *tls_ptr;
} tcb_t;

typedef struct process {
    int pid;
    char *name;

    struct process *parent;
    int children_count; // diddy really likes this value >:)
    struct process **children;

    pcb_state_t state;
    uint64_t wakeup_tick;

    int thread_count;
    tcb_t **threads;
    tcb_t *main_thread;

    fd_table_t fd_table;
    fileio_t *cwd; // Current Working Directory (yes it's a file :3c)

    int cpu; // the cpu we're running on

    uint8_t flags;

    vmc_t *vmc;

    user_cred_t *cred;

    void (*signal_handler)(int);

    int exit_code;
    int exited;
    atomic_flag wait_lock;
    tcb_t *wait_queue;

    int pgid;
    int sid;
    int is_session_leader;
    
    void *ctty;
} pcb_t;

typedef struct cpu_thread_queue {
    size_t count;
    tcb_t *head;
} cpu_queue_t;

typedef struct cpu_local {
    int cpu_id;
    tcb_t *current;
} cpu_local_t;

extern cpu_local_t *cpu_locals;

static inline uint64_t choose_random_tls_base(void) {
    uint64_t min = 0x0000700000000000ULL;  // Higher in user space
    uint64_t max = 0x00007F0000000000ULL;
    uint64_t r = (uint64_t)_get_tsc();
    uint64_t base = min + (r % (max - min));
    return ROUND_DOWN(base, TLS_ALIGNMENT);
}

/* from arch/[TARGET]/scheduler.asm */
extern void context_load(registers_t *ctx);
extern void fpu_save(void *ctx);
extern void fpu_restore(void *ctx);
extern __attribute__((noreturn)) void scheduler_idle();

int pcb_destroy(int pid);
int thread_destroy(int pid, int tid);
int proc_exit(int exit_code);
int proc_engage(pcb_t *proc);

// inits the scheduler on every CPU
// with an "init" process
int init_scheduler();
/*
    the "init" function is a custom function
    that the developer can assign it to:
    a simple idle function, an initialization function (that will be called for
    every CPU), whatever.
*/

int init_cpu_scheduler(); // inits the scheduler for a specific CPU
                                     // with a custom process and a thread queue

int proc_create(void (*entry)(), int flags, char *name);
int thread_create(pcb_t *parent, void (*entry)(), int flags);
int pcb_destroy(int pid);
pcb_t *get_current_pcb();
tcb_t *get_current_tcb();
int thread_destroy(int pid, int tid);
int proc_fork(registers_t *regs);

int allocate_tls(tcb_t *thread, size_t size);
void free_tls(tcb_t *thread);
int find_new_tls_base(tcb_t *tcb, size_t size);

void yield(registers_t *ctx);

void enter_usermode(void (*entry)(), void *user_stack);
void syscall_return(registers_t *regs);

void scheduler_procfs_print();

tcb_t *tcb_lookup(int pid, int tid);
pcb_t *pcb_lookup(int pid);

int do_waitpid(int pid, int *status_ptr, int options);
void proc_add_child(pcb_t *parent, pcb_t *child);
void proc_remove_child(pcb_t *parent, pcb_t *child);

int scheduler_enqueue(tcb_t *thread);

#endif // SCHEDULER_H
