#include <stdint.h>

typedef unsigned long size_t;

#define SYS_EXIT   0
#define SYS_OPEN   1
#define SYS_READ   2
#define SYS_WRITE  3
#define SYS_CLOSE  4
#define SYS_PIPE   40
#define SYS_FORK   24
#define SYS_POLL   52

#define POLLIN     0x0001
#define POLLOUT    0x0004
#define POLLERR    0x0008
#define POLLHUP    0x0010
#define POLLNVAL   0x0020
#define POLLRDNORM 0x0040
#define POLLWRNORM 0x0100

typedef struct pollfd {
    int   fd;
    short events;
    short revents;
} pollfd_t;

static inline uint64_t syscall0(uint64_t num) {
    uint64_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num)
        : "memory"
    );
    return ret;
}

static inline uint64_t syscall1(uint64_t num, uint64_t a1) {
    uint64_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a1)
        : "memory"
    );
    return ret;
}

static inline uint64_t syscall2(uint64_t num, uint64_t a1, uint64_t a2) {
    uint64_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2)
        : "memory"
    );
    return ret;
}

static inline uint64_t syscall3(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3) {
    uint64_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3)
        : "memory"
    );
    return ret;
}

static uint64_t strlen(const char *s) {
    uint64_t len = 0;
    while (s[len]) len++;
    return len;
}

static void print(int fd, const char *s) {
    syscall3(SYS_WRITE, (uint64_t)fd, (uint64_t)s, strlen(s));
}

static void print_dec(int fd, uint64_t n) {
    char buf[32];
    int i = 0;
    if (n == 0) {
        buf[i++] = '0';
    } else {
        while (n > 0) {
            buf[i++] = '0' + (n % 10);
            n /= 10;
        }
    }
    for (int j = 0; j < i / 2; j++) {
        char tmp = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = tmp;
    }
    buf[i] = '\0';
    print(fd, buf);
}

static void print_hex(int fd, uint64_t n) {
    char buf[32];
    const char *hex = "0123456789ABCDEF";
    int i = 0;
    if (n == 0) {
        buf[i++] = '0';
    } else {
        while (n > 0) {
            buf[i++] = hex[n & 0xF];
            n >>= 4;
        }
    }
    for (int j = 0; j < i / 2; j++) {
        char tmp = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = tmp;
    }
    buf[i] = '\0';
    print(fd, "0x");
    print(fd, buf);
}

void main(uintptr_t *stack_ptr) {
    (void)stack_ptr;

    int log_fd = syscall3(SYS_OPEN, (uint64_t)"/dev/ttyS0", 0, 0);
    if (log_fd < 0) {
        syscall1(SYS_EXIT, 1);
        return;
    }

    print(log_fd, "\r\n=== Poll Test Started ===\r\n");

    int pipe_fds[2];
    int ret = syscall1(SYS_PIPE, (uint64_t)pipe_fds);
    if (ret < 0) {
        print(log_fd, "pipe() failed\r\n");
        syscall1(SYS_EXIT, 1);
        return;
    }

    print(log_fd, "Created pipe: read_fd=");
    print_dec(log_fd, pipe_fds[0]);
    print(log_fd, ", write_fd=");
    print_dec(log_fd, pipe_fds[1]);
    print(log_fd, "\r\n");

    pollfd_t fds[2];
    fds[0].fd = pipe_fds[0];
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    fds[1].fd = pipe_fds[1];
    fds[1].events = POLLOUT;
    fds[1].revents = 0;

    print(log_fd, "Test 1: Poll empty pipe (timeout=0)\r\n");
    ret = syscall3(SYS_POLL, (uint64_t)fds, 2, 0);
    print(log_fd, "  poll returned: ");
    print_dec(log_fd, ret);
    print(log_fd, "\r\n");
    print(log_fd, "  read_fd revents: ");
    print_hex(log_fd, fds[0].revents);
    print(log_fd, "\r\n");
    print(log_fd, "  write_fd revents: ");
    print_hex(log_fd, fds[1].revents);
    print(log_fd, "\r\n");

    if (fds[1].revents & POLLOUT) {
        print(log_fd, "  [PASS] write_fd is writable\r\n");
    } else {
        print(log_fd, "  [FAIL] write_fd should be writable\r\n");
    }

    print(log_fd, "Test 2: Write to pipe, then poll for read\r\n");
    const char *msg = "Hello";
    ret = syscall3(SYS_WRITE, (uint64_t)pipe_fds[1], (uint64_t)msg, 5);
    print(log_fd, "  wrote ");
    print_dec(log_fd, ret);
    print(log_fd, " bytes\r\n");

    fds[0].revents = 0;
    fds[1].revents = 0;
    ret = syscall3(SYS_POLL, (uint64_t)fds, 2, 0);
    print(log_fd, "  poll returned: ");
    print_dec(log_fd, ret);
    print(log_fd, "\r\n");
    print(log_fd, "  read_fd revents: ");
    print_hex(log_fd, fds[0].revents);
    print(log_fd, "\r\n");

    if (fds[0].revents & POLLIN) {
        print(log_fd, "  [PASS] read_fd has data available\r\n");
    } else {
        print(log_fd, "  [FAIL] read_fd should have data\r\n");
    }

    print(log_fd, "Test 3: Read from pipe, verify empty\r\n");
    char buf[64];
    ret = syscall3(SYS_READ, (uint64_t)pipe_fds[0], (uint64_t)buf, 64);
    print(log_fd, "  read ");
    print_dec(log_fd, ret);
    print(log_fd, " bytes\r\n");

    fds[0].revents = 0;
    ret = syscall3(SYS_POLL, (uint64_t)fds, 1, 0);
    print(log_fd, "  poll returned: ");
    print_dec(log_fd, ret);
    print(log_fd, "\r\n");
    print(log_fd, "  read_fd revents: ");
    print_hex(log_fd, fds[0].revents);
    print(log_fd, "\r\n");

    if (!(fds[0].revents & POLLIN)) {
        print(log_fd, "  [PASS] read_fd is empty\r\n");
    } else {
        print(log_fd, "  [FAIL] read_fd should be empty\r\n");
    }

    print(log_fd, "Test 4: Close write end, poll for POLLHUP\r\n");
    syscall1(SYS_CLOSE, (uint64_t)pipe_fds[1]);

    fds[0].revents = 0;
    ret = syscall3(SYS_POLL, (uint64_t)fds, 1, 0);
    print(log_fd, "  poll returned: ");
    print_dec(log_fd, ret);
    print(log_fd, "\r\n");
    print(log_fd, "  read_fd revents: ");
    print_hex(log_fd, fds[0].revents);
    print(log_fd, "\r\n");

    if (fds[0].revents & POLLHUP) {
        print(log_fd, "  [PASS] got POLLHUP after write end closed\r\n");
    } else {
        print(log_fd, "  [FAIL] should get POLLHUP\r\n");
    }

    print(log_fd, "Test 5: Poll invalid fd\r\n");
    pollfd_t bad_fd;
    bad_fd.fd = 999;
    bad_fd.events = POLLIN;
    bad_fd.revents = 0;
    ret = syscall3(SYS_POLL, (uint64_t)&bad_fd, 1, 0);
    print(log_fd, "  poll returned: ");
    print_dec(log_fd, ret);
    print(log_fd, "\r\n");
    print(log_fd, "  bad_fd revents: ");
    print_hex(log_fd, bad_fd.revents);
    print(log_fd, "\r\n");

    if (bad_fd.revents & POLLNVAL) {
        print(log_fd, "  [PASS] got POLLNVAL for invalid fd\r\n");
    } else {
        print(log_fd, "  [FAIL] should get POLLNVAL\r\n");
    }

    syscall1(SYS_CLOSE, (uint64_t)pipe_fds[0]);

    print(log_fd, "=== Poll Test Done ===\r\n");
    syscall1(SYS_CLOSE, (uint64_t)log_fd);
    syscall1(SYS_EXIT, 0);
}
