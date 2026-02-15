#ifndef TTY_H
#define TTY_H 1

#include <dev/device.h>
#include <structures/ringbuffer.h>
#include <dev/tty/termios.h>
#include <dev/tty/winsize.h>
#include <structures/waitqueue.h>
#include <system/types.h>

typedef long int ssize_t;

struct tty;

typedef struct tty_ops {
	int (*ioctl)(struct tty *, long , void *);
	ssize_t (*out)(struct tty *, const char *, size_t);
	void (*cleanup)(struct tty *);
} tty_ops_t;

typedef struct tty {
    device_t device;

    void *priv_data;

    ringbuffer_t input_buffer;
    atomic_flag input_buffer_lock;

    tty_ops_t *ops;

    termios_t termios;
    winsize_t winsize;

    waitqueue_t read_queue;
    waitqueue_t write_queue;

    size_t column;

    char *canon_buf;
    size_t canon_idx;

    pid_t fg_pgrp; // todo do something w/ this

    int unconnected; // no idea what this is
} tty_t;

int tty_input(tty_t *tty, char c);
int tty_output(tty_t *tty, char c);

tty_t *tty_create(tty_t *tty);
 
#endif // TTY_H