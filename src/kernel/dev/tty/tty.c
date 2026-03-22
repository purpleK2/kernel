#include "tty.h"
#include "dev/tty/termios.h"
#include "dev/tty/winsize.h"
#include "errors.h"
#include "memory/heap/kheap.h"
#include "scheduler/scheduler.h"
#include "stdio.h"
#include "structures/ringbuffer.h"
#include "uaccess.h"
#include <util/assert.h>
#include <string.h>

static int tty_write(struct device *dev, const void *buffer, size_t size, size_t offset) {
    (void)offset;
    tty_t *tty = (tty_t *)dev->data;
    size_t original_size = size;
    const char *buf = (const char *)buffer;

    while(size > 0){
        tty_output(tty, *buf);
        buf++;
        size--;
    }

    return original_size;
}

static int tty_read(struct device *dev, void *buffer, size_t size, size_t offset) {
    tty_t *tty = (tty_t *)dev->data;

    if (size == 0) {
        return 0;
    }

    if (tty->termios.c_lflag & ICANON) {
        for (;;) {
            /*
             * Prepare to wait BEFORE checking the buffer. This ensures that
             * if tty_input fires between our check and waitqueue_sleep, the
             * wake will still be seen (waitqueue_prepare_wait sets state to
             * THREAD_WAITING while we still hold the logical lock).
             */
            waitqueue_prepare_wait(&tty->read_queue);

            spinlock_acquire(&tty->input_buffer_lock);
            size_t buf_size = rb_size(&tty->input_buffer);

            if (buf_size == 0) {
                spinlock_release(&tty->input_buffer_lock);
                /* Actually sleep — waitqueue_prepare_wait already set state */
                while (get_current_tcb()->state == THREAD_WAITING) {
                    __asm__ volatile("sti; hlt" ::: "memory");
                }
                continue;
            }

            size_t to_read = 0;
            for (; to_read < buf_size && to_read < size; to_read++) {
                char c;
                rb_peek(&tty->input_buffer, to_read, &c);
                if (c == '\n' || c == tty->termios.c_cc[VEOL] || c == tty->termios.c_cc[VEOF]) {
                    to_read++;
                    break;
                }
            }

            if (to_read > 0) {
                size_t result = rb_read(&tty->input_buffer, buffer, to_read, 0);
                spinlock_release(&tty->input_buffer_lock);
                return result;
            }

            spinlock_release(&tty->input_buffer_lock);
            while (get_current_tcb()->state == THREAD_WAITING) {
                __asm__ volatile("sti; hlt" ::: "memory");
            }
        }
    }

    /* Raw mode: wait for VMIN characters */
    size_t vmin = tty->termios.c_cc[VMIN];
    if (vmin == 0) vmin = 1;

    for (;;) {
        waitqueue_prepare_wait(&tty->read_queue);

        spinlock_acquire(&tty->input_buffer_lock);
        size_t avail = rb_size(&tty->input_buffer);

        if (avail >= vmin) {
            size_t result = rb_read(&tty->input_buffer, buffer, size, 0);
            spinlock_release(&tty->input_buffer_lock);
            return result;
        }

        spinlock_release(&tty->input_buffer_lock);
        while (get_current_tcb()->state == THREAD_WAITING) {
            __asm__ volatile("sti; hlt" ::: "memory");
        }
    }
}

static int tty_ioctl(struct device *dev, int request, void *arg) {
    tty_t *tty = (tty_t *)dev->data;
    int ret;
    
    switch (request) {
    case IOCTLTTYIS: {
        int is_tty = 1;
        ret = copy_to_user(arg, &is_tty, sizeof(int));
        if (ret != 0) {
            return -EFAULT;
        }
        return 0;
    }

    //case TCGETS:
    case TIOCGETA: {
        struct termios tmp;
        tmp = tty->termios;
        ret = copy_to_user(arg, &tmp, sizeof(struct termios));
        if (ret != 0) {
            return -EFAULT;
        }
        return 0;
    }

    //case TCSETS:
    //case TCSETSW:
    //case TCSETSF:
    case TIOCSETA:
    case TIOCSETAF:
    case TIOCSETAW: {
        struct termios tmp;
        ret = copy_from_user(&tmp, arg, sizeof(struct termios));
        if (ret != 0) {
            return -EFAULT;
        }
        tty->termios = tmp;
        return 0;
    }
    
    case TIOCGPGRP: {
        pid_t tmp = tty->fg_pgrp;
        ret = copy_to_user(arg, &tmp, sizeof(pid_t));
        if (ret != 0) {
            return -EFAULT;
        }
        return 0;
    }
    
    case TIOCSPGRP: {
        pid_t tmp;
        ret = copy_from_user(&tmp, arg, sizeof(pid_t));
        if (ret != 0) {
            return -EFAULT;
        }
        tty->fg_pgrp = tmp;
        return 0;
    }
    
    case TIOCSWINSZ: {
        winsize_t tmp;
        ret = copy_from_user(&tmp, arg, sizeof(winsize_t));
        if (ret != 0) {
            return -EFAULT;
        }
        tty->winsize = tmp;
        return 0;
    }
    
    case TIOCGWINSZ: {
        winsize_t tmp;
        tmp = tty->winsize;
        ret = copy_to_user(arg, &tmp, sizeof(winsize_t));
        if (ret != 0) {
            return -EFAULT;
        }
        return 0;
    }
    
    case TIOCSCTTY: {
        pcb_t *proc = get_current_pcb();
        if (!proc) {
            return -ESRCH;
        }
        
        if (!proc->is_session_leader) {
            return -EPERM;
        }
        
        if (proc->ctty != NULL && (uintptr_t)arg != 1) {
            return -EPERM;
        }
        
        proc->ctty = tty;
        tty->fg_pgrp = proc->pgid;
        return 0;
    }
    
    case TIOCNOTTY: {
        pcb_t *proc = get_current_pcb();
        if (!proc) {
            return -ESRCH;
        }
        
        if (proc->ctty != tty) {
            return -ENOTTY;
        }
        
        proc->ctty = NULL;
        
        if (proc->is_session_leader) {
            tty->fg_pgrp = 0;
        }
        return 0;
    }
    
    default:
        if (tty->ops->ioctl) {
            return tty->ops->ioctl(tty, request, arg);
        }
        return -EINVAL;
    }
}

int tty_input(tty_t *tty, char c) {
    debugf_debug("tty_input: %d (0x%x)\n", c, c);
    if (tty->termios.c_iflag & INLCR) {
        if (c == '\n') {
            c = '\r';
        }
    }

    if (tty->termios.c_iflag & IGNCR) {
        if (c == '\r') {
            return 0;
        }
    }

    if (tty->termios.c_iflag & ICRNL) {
        if (c == '\r') {
            c = '\n';
        }
    }

    if (tty->termios.c_iflag & IUCLC) {
        if (c >= 'A' && c <= 'Z') {
            c += 'a' - 'A';
        }
    }

    if (tty->termios.c_iflag & ISTRIP) {
        c &= 0x7F;
    }

    if (tty->termios.c_lflag & ISIG || 1) {
        if (c == tty->termios.c_cc[VINTR]) {
            if (tty->fg_pgrp) {
                debugf_debug("SIGINT\n");
            }
        }

        if (c == tty->termios.c_cc[VQUIT]) {
            if (tty->fg_pgrp) {
                debugf_debug("SIGQUIT\n");
            }
        }

        if (c == tty->termios.c_cc[VSUSP]) {
            if (tty->fg_pgrp)  {
                debugf_debug("SIGTSTP\n");
            }
        }
    }

    if (tty->termios.c_lflag & ICANON) {
        if (tty->termios.c_lflag & ECHO) {
            if (c == tty->termios.c_cc[VERASE] && tty->termios.c_lflag & ECHOE) {
                if (tty->canon_idx > 0){
                    if (tty->canon_buf[tty->canon_idx -1] && tty->canon_buf[tty->canon_idx -1] <= 31 && tty->canon_buf[tty->canon_idx -1] != '\n'){
                        tty_output(tty, '\b');
                        tty_output(tty, ' ');
                        tty_output(tty, '\b');
                    }
                    tty_output(tty, '\b');
                    tty_output(tty, ' ');
                    tty_output(tty, '\b');
                }
            } else if (c && c <= 31 && c != '\n') {
                tty_output(tty, '^');
                tty_output(tty, c + 'A' - 1);
            } else {
                tty_output(tty, c);
            }
        } else if (c == '\n' && (tty->termios.c_lflag & ECHONL)) {
            tty_output(tty, '\n');
        }

        if ((tty->termios.c_lflag & IEXTEN)) {
            if (tty->termios.c_cc[VERASE] == c) {
                if(tty->canon_idx > 0){
                    tty->canon_idx--;
                }
                return 0;
            }
            if(tty->termios.c_cc[VKILL] == c){
                tty->canon_idx = 0;
                return 0;
            }
        }

        tty->canon_buf[tty->canon_idx] = c;
        tty->canon_idx++;
        if (c == '\n' || c == tty->termios.c_cc[VEOL] || c == tty->termios.c_cc[VEOF]) {
            spinlock_acquire(&tty->input_buffer_lock);
            size_t written = rb_write(&tty->input_buffer, tty->canon_buf, tty->canon_idx, 0);
            spinlock_release(&tty->input_buffer_lock);

            if (written < tty->canon_idx && (tty->termios.c_iflag & IMAXBEL)) {
                tty_output(tty, '\a');
            }

            waitqueue_wake_all(&tty->read_queue);

            tty->canon_idx = 0;
        }
        return 0;
    }

    if (tty->termios.c_lflag & ECHO) {
        tty_output(tty, c);
    }

    spinlock_acquire(&tty->input_buffer_lock);
    if (rb_write(&tty->input_buffer, &c, 1, 0) == 1) {
        spinlock_release(&tty->input_buffer_lock);
        waitqueue_wake_all(&tty->read_queue);
    } else {
        spinlock_release(&tty->input_buffer_lock);
        if (tty->termios.c_iflag & IMAXBEL)
            tty_output(tty, '\a');
    }

    return 0;
}

int tty_output(tty_t *tty, char c) {
    if (tty->termios.c_oflag & OPOST) {
        if (tty->termios.c_oflag & OLCUC) {
            if (c >= 'a' && c <= 'z') {
                c += 'A' - 'a';
            }
        }

        if (tty->termios.c_oflag & ONLCR) {
            if (c == '\n') {
                tty_output(tty, '\r');
            }
        }

        if (tty->termios.c_oflag & OCRNL) {
            if (c == '\r') {
                c = '\n';
            }
        }

        if (tty->termios.c_oflag & ONOCR) {
            if (c == '\r' && tty->column == 0) {
                return 0;
            }
        }
    }

    if (c == '\r' || (c == '\n' && tty->termios.c_oflag & ONLRET)) {
        tty->column = 0;
    } else {
        tty->column++;
    }

    tty->ops->out(tty, &c, 1);
    return 0;
}

tty_t *tty_create(tty_t *tty) {
    if (!tty) {
        tty = kmalloc(sizeof(tty_t));
        assert(tty);
        memset(tty, 0, sizeof(tty_t));
    }

    rb_init(&tty->input_buffer, 4096);
    tty->input_buffer_lock = (atomic_flag)ATOMIC_FLAG_INIT;

    memset(&tty->termios, 0, sizeof(termios_t));

    tty->termios.c_cc[VEOF]   = 0x04;
    tty->termios.c_cc[VERASE] = 127;
    tty->termios.c_cc[VINTR]  = 0x03;
    tty->termios.c_cc[VQUIT]  = 0x22;
    tty->termios.c_cc[VSUSP]  = 0x1A;
    tty->termios.c_cc[VMIN]   = 1;
    tty->termios.c_iflag = ICRNL | IMAXBEL;
    tty->termios.c_oflag = OPOST | ONLCR | ONLRET;
    tty->termios.c_lflag = ECHONL | ECHOK | ECHOE | ECHO | ICANON | IEXTEN | ISIG;
    tty->termios.c_cflag = CS8;

    tty->canon_buf = kmalloc(512);
    assert(tty->canon_buf);
    tty->canon_idx = 0;

    waitqueue_init(&tty->read_queue);
    waitqueue_init(&tty->write_queue);

    tty->device.major = 4;
    tty->device.minor = 0;
    tty->device.type = DEVICE_TYPE_CHAR;
    tty->device.data = tty;

    tty->device.read  = tty_read;
    tty->device.write = tty_write;
    tty->device.ioctl = tty_ioctl;

    return tty;
}