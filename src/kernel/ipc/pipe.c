#include "pipe.h"
#include "poll.h"
#include "errors.h"
#include "fs/file_io.h"
#include "memory/heap/kheap.h"
#include "spinlock.h"
#include <string.h>

static inline size_t pipe_space(pipe_t *p) {
    return PIPE_BUFFER_SIZE - p->used;
}

static inline size_t pipe_contig_read(pipe_t *p) {
    size_t end = PIPE_BUFFER_SIZE - p->read_pos;
    return (p->used < end) ? p->used : end;
}

static inline size_t pipe_contig_write(pipe_t *p) {
    size_t end = PIPE_BUFFER_SIZE - p->write_pos;
    return (pipe_space(p) < end) ? pipe_space(p) : end;
}

int pipe(fileio_t *fds[2]) {
    pipe_t *p = kmalloc(sizeof(pipe_t));
    if (!p)
        return -ENOMEM;
    memset(p, 0, sizeof(pipe_t));
    spinlock_acquire(&p->lock);
    p->readers = 1;
    p->writers = 1;

    fileio_t *rd = fio_create();
    fileio_t *wr = fio_create();
    if (!rd || !wr) {
        if (rd)
            kfree(rd);
        if (wr)
            kfree(wr);
        kfree(p);
        return -ENOMEM;
    }

    rd->flags   = PIPE_READ_END;
    rd->offset  = 0;
    rd->size    = 0;
    rd->private = p;

    wr->flags   = PIPE_WRITE_END;
    wr->offset  = 0;
    wr->size    = 0;
    wr->private = p;

    fds[0] = rd;
    fds[1] = wr;
    spinlock_release(&p->lock);
    return 0;
}

int pipe_read(fileio_t *fio, void *buf, size_t *size) {
    pipe_t *p         = (pipe_t *)fio->private;
    size_t requested  = *size;
    size_t read_bytes = 0;

    spinlock_acquire(&p->lock);
    while (read_bytes == 0) {
        if (p->used > 0) {
            size_t to_read = pipe_contig_read(p);
            if (to_read > requested)
                to_read = requested;

            memcpy(buf, &p->buffer[p->read_pos], to_read);
            p->read_pos  = (p->read_pos + to_read) % PIPE_BUFFER_SIZE;
            p->used     -= to_read;
            read_bytes   = to_read;
        } else {
            if (p->writers == 0) {
                spinlock_release(&p->lock);
                *size = 0; /* EOF */
                return 0;
            }
            /* No blocking support yet: return 0 if empty */
            spinlock_release(&p->lock);
            *size = 0;
            return 0;
        }
    }
    spinlock_release(&p->lock);

    *size = read_bytes;
    poll_wake_all();
    return 0;
}

int pipe_write(fileio_t *fio, const void *buf, size_t *size) {
    pipe_t *p        = (pipe_t *)fio->private;
    size_t requested = *size;
    size_t written   = 0;

    spinlock_acquire(&p->lock);
    while (written < requested) {
        if (pipe_space(p) > 0) {
            size_t to_write = pipe_contig_write(p);
            if (to_write > requested - written)
                to_write = requested - written;

            memcpy(&p->buffer[p->write_pos], (const uint8_t *)buf + written,
                   to_write);
            p->write_pos  = (p->write_pos + to_write) % PIPE_BUFFER_SIZE;
            p->used      += to_write;
            written      += to_write;
        } else {
            if (p->readers == 0) {
                spinlock_release(&p->lock);
                return -EPIPE;
            }
            /* No blocking: break if full */
            break;
        }
    }
    spinlock_release(&p->lock);

    *size = written;
    poll_wake_all();
    return 0;
}

int pipe_close(fileio_t *fio) {
    pipe_t *p = (pipe_t *)fio->private;
    spinlock_acquire(&p->lock);

    if (fio->flags & PIPE_READ_END)
        p->readers--;
    if (fio->flags & PIPE_WRITE_END)
        p->writers--;

    bool destroy = (p->readers == 0 && p->writers == 0);
    spinlock_release(&p->lock);

    poll_wake_all();

    kfree(fio);
    if (destroy)
        kfree(p);

    return 0;
}
