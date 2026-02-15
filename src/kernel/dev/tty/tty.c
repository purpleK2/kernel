#include "tty.h"
#include "dev/tty/termios.h"
#include "errors.h"
#include "memory/heap/kheap.h"
#include "structures/ringbuffer.h"
#include <util/assert.h>
#include <string.h>

static int tty_write(struct device *dev, const void *buffer, size_t size, size_t offset) {
    (void)offset;
    tty_t *tty = (tty_t *)dev->data;

    while(size > 0){
		tty_output(tty,*(char *)buffer);
		(char *)buffer++;
		size--;
	}

	return size;
}

static int tty_read(struct device *dev, void *buffer, size_t size, size_t offset) {
    (void)offset;
	tty_t *tty = (tty_t *)dev->data;

	if(tty->termios.c_lflag & ICANON){
		ssize_t rsize = rb_read(&tty->input_buffer, buffer, size, 0);
		if(rsize < 0){
			return rsize;
		}
		if(((char *)buffer)[rsize - 1] == tty->termios.c_cc[VEOF]){
			rsize--;
		}
		return rsize;
	}

	return rb_read(&tty->input_buffer, buffer, size, 0);
}

static int tty_ioctl(struct device *dev, int request, void *arg) {
	tty_t *tty = (tty_t *)dev->data;
	switch (request) {
	case TIOCGETA:
		*(struct termios *)arg = tty->termios;
		return 0;
	case TIOCSETA:
	case TIOCSETAF:
	case TIOCSETAW:
		tty->termios = *(struct termios *)arg;
		return 0;
	case TIOCGPGRP:
		*(pid_t *)arg =  tty->fg_pgrp;
		return 0;
	case TIOCSPGRP:
		tty->fg_pgrp = *(pid_t *)arg;
		return 0;
	case TIOCSWINSZ:
		tty->winsize = *(winsize_t *)arg;
		return 0;
	case TIOCGWINSZ:
		*(struct winsize *)arg = tty->winsize;
		return 0;
	default:
		if (tty->ops->ioctl) {
			return tty->ops->ioctl(tty, request, arg);
		}
		return -EINVAL;
	}
}

int tty_input(tty_t *tty, char c) {
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

    if (tty->termios.c_lflag & ISIG) {
		if (c == tty->termios.c_cc[VINTR]) {
			if (tty->fg_pgrp) {
				// when signals, send sigint
			}
		}

		if (c == tty->termios.c_cc[VQUIT]) {
			if (tty->fg_pgrp) {
				// when signals, send sigquit
			}
        }

		if (c == tty->termios.c_cc[VSUSP]) {
			if (tty->fg_pgrp)  {
				// when signals, send sigtstp
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
			if ((size_t)rb_write(&tty->input_buffer, tty->canon_buf, tty->canon_idx, 0) < tty->canon_idx) {
				if (tty->termios.c_iflag & IMAXBEL){
					tty_output(tty, '\a');
				}
			}
			tty->canon_idx = 0;
		}
		return 0;
	}

	if (tty->termios.c_lflag & ECHO) {
		tty_output(tty, c);
	}

	if (rb_write(&tty->input_buffer, &c, 1, 0) == 0) {
		if (tty->termios.c_iflag & IMAXBEL) {
			tty_output(tty, '\a');
		}
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

    memset(&tty->termios, 0, sizeof(termios_t));

    tty->termios.c_cc[VEOF] = 0x04;
	tty->termios.c_cc[VERASE] = 127;
	tty->termios.c_cc[VINTR] = 0x03;
	tty->termios.c_cc[VQUIT] = 0x22;
	tty->termios.c_cc[VSUSP] = 0x1A;
	tty->termios.c_cc[VMIN] = 1;
	tty->termios.c_iflag = ICRNL | IMAXBEL;
	tty->termios.c_oflag = OPOST | ONLCR | ONLRET;
	tty->termios.c_lflag = ECHONL | ECHOK | ECHOE | ECHO | ICANON | IEXTEN | ISIG;
	tty->termios.c_oflag = CS8;

    tty->canon_buf = kmalloc(512);
    assert(tty->canon_buf);
    tty->canon_idx = 0;

    waitqueue_init(&tty->read_queue);
    waitqueue_init(&tty->write_queue);

    tty->device.major = 4;
    tty->device.minor = 0;
    tty->device.type = DEVICE_TYPE_CHAR;
    tty->device.data = tty;

    tty->device.read = tty_read;
    tty->device.write = tty_write;
    tty->device.ioctl = tty_ioctl;

    return tty;
}