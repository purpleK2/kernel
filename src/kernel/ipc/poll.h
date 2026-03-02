#ifndef POLL_H
#define POLL_H 1

#include <stddef.h>
#include <stdint.h>

#define POLLIN     0x0001
#define POLLPRI    0x0002
#define POLLOUT    0x0004
#define POLLERR    0x0008
#define POLLHUP    0x0010
#define POLLNVAL   0x0020
#define POLLRDNORM 0x0040
#define POLLRDBAND 0x0080
#define POLLWRNORM 0x0100
#define POLLWRBAND 0x0200

typedef struct pollfd {
    int   fd;
    short events;
    short revents;
} pollfd_t;

int do_poll(pollfd_t *fds, size_t nfds, int timeout_ms);
void poll_wake_all(void);

#endif
