#include "poll.h"
#include "pipe.h"
#include "errors.h"
#include "fs/fd.h"
#include "fs/file_io.h"
#include "scheduler/scheduler.h"
#include "structures/waitqueue.h"
#include "system/time.h"

#include <stdatomic.h>

extern registers_t *get_syscall_context(void);

static waitqueue_t poll_wq;
static atomic_flag poll_wq_init = ATOMIC_FLAG_INIT;

static void ensure_poll_wq_init(void) {
    if (!atomic_flag_test_and_set(&poll_wq_init)) {
        waitqueue_init(&poll_wq);
    }
}

static short poll_check_pipe(fileio_t *fio) {
    short revents = 0;
    pipe_t *p = (pipe_t *)fio->private;

    spinlock_acquire(&p->lock);

    if (fio->flags & PIPE_READ_END) {
        if (p->used > 0) {
            revents |= POLLIN | POLLRDNORM;
        }
        if (p->writers == 0) {
            revents |= POLLHUP;
        }
    }

    if (fio->flags & PIPE_WRITE_END) {
        if (p->used < PIPE_BUFFER_SIZE) {
            revents |= POLLOUT | POLLWRNORM;
        }
        if (p->readers == 0) {
            revents |= POLLERR;
        }
    }

    spinlock_release(&p->lock);
    return revents;
}

static short poll_check_file(fileio_t *fio) {
    short revents = 0;

    if (fio->offset < fio->size) {
        revents |= POLLIN | POLLRDNORM;
    }

    revents |= POLLOUT | POLLWRNORM;

    return revents;
}

static short poll_check_fd(fd_table_t *ft, int fd, short events) {
    if (fd < 0 || (size_t)fd >= ft->size)
        return POLLNVAL;

    fd_entry_t *e = &ft->entries[fd];
    if (e->type == FD_NONE)
        return POLLNVAL;

    if (e->type == FD_DIR)
        return POLLNVAL;

    fileio_t *fio = (fileio_t *)e->ptr;
    if (!fio)
        return POLLNVAL;

    short revents = 0;

    if (fio->flags & (PIPE_READ_END | PIPE_WRITE_END)) {
        revents = poll_check_pipe(fio);
    } else {
        revents = poll_check_file(fio);
    }

    return revents & (events | POLLERR | POLLHUP | POLLNVAL);
}

int do_poll(pollfd_t *fds, size_t nfds, int timeout_ms) {
    ensure_poll_wq_init();

    pcb_t *current = get_current_pcb();
    if (!current)
        return -EFAULT;

    fd_table_t *ft = &current->fd_table;
    int ready = 0;

    for (size_t i = 0; i < nfds; i++) {
        fds[i].revents = poll_check_fd(ft, fds[i].fd, fds[i].events);
        if (fds[i].revents != 0)
            ready++;
    }

    if (ready > 0 || timeout_ms == 0)
        return ready;

    tcb_t *me = get_current_tcb();
    if (!me)
        return -EFAULT;

    registers_t *ctx = get_syscall_context();
    if (!ctx)
        return -EFAULT;

    uint64_t now = get_ticks();
    uint64_t target = 0;
    if (timeout_ms > 0) {
        target = now + (uint64_t)timeout_ms;
    }

    while (ready == 0) {
        if (timeout_ms > 0) {
            uint64_t current_tick = get_ticks();
            if (current_tick >= target) {
                break;
            }
            me->wakeup_tick = target;
        } else {
            me->wakeup_tick = 0;
        }

        me->regs = ctx;
        waitqueue_prepare_wait(&poll_wq);
        yield(ctx);

        for (size_t i = 0; i < nfds; i++) {
            fds[i].revents = poll_check_fd(ft, fds[i].fd, fds[i].events);
            if (fds[i].revents != 0)
                ready++;
        }

        if (ready > 0)
            break;

        if (timeout_ms > 0 && get_ticks() >= target)
            break;
    }

    return ready;
}

void poll_wake_all(void) {
    if (atomic_flag_test_and_set(&poll_wq_init)) {
        atomic_flag_clear(&poll_wq_init);
        waitqueue_wake_all(&poll_wq);
        atomic_flag_test_and_set(&poll_wq_init);
    }
}
