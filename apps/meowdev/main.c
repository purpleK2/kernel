#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/meowdev.h>
#include <unistd.h>

static mdev_event_t events[16];

static size_t cstr_len(const char *s) {
    size_t n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

static void put_str(const char *s) {
    write(1, s, cstr_len(s));
}

static void put_u64(uint64_t v) {
    char buf[32];
    size_t i = 0;

    if (v == 0) {
        put_str("0");
        return;
    }

    while (v > 0 && i < sizeof(buf)) {
        buf[i++]  = (char)('0' + (v % 10));
        v        /= 10;
    }

    while (i > 0) {
        i--;
        write(1, &buf[i], 1);
    }
}

static void put_i64(int64_t v) {
    if (v < 0) {
        put_str("-");
        put_u64((uint64_t)(-v));
        return;
    }

    put_u64((uint64_t)v);
}

static void put_i32(int32_t v) {
    put_i64((int64_t)v);
}

static void print_event(const mdev_event_t *ev) {
    const char *type = "UNKNOWN";
    if (ev->type == MDEV_KEY) {
        type = "MDEV_KEY";
    } else if (ev->type == MDEV_REL) {
        type = "MDEV_REL";
    }

    put_str("[");
    put_i64(ev->timestamp.tv_sec);
    put_str(".");
    put_u64((uint64_t)ev->timestamp.tv_usec);
    put_str("] ");
    put_str(type);
    put_str(" code=");
    put_u64((uint64_t)ev->code);
    put_str(" value=");
    put_i32((int32_t)ev->value);
    put_str("\n");
}

int main(int argc, char **argv) {
    const char *path = "/dev/input/kbd0";
    if (argc > 1 && argv[1] && argv[1][0] != '\0') {
        path = argv[1];
    }

    put_str("meowdev test: opening ");
    put_str(path);
    put_str("\n");

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        put_str("open failed, errno=");
        put_i32(errno);
        put_str("\n");
        return 1;
    }

    int grab_ret = ioctl(fd, MDEVGRABDEV, 1);
    if (grab_ret != 0) {
        put_str("MDEVGRABDEV(1) failed ret=");
        put_i32(grab_ret);
        put_str(" errno=");
        put_i32(errno);
        put_str("\ncontinuing without exclusive grab\n");
    } else {
        put_str("grab acquired\n");
    }

    put_str("waiting for events (Ctrl+C to stop)...\n");

    for (;;) {
        ssize_t n = read(fd, events, sizeof(events));
        if (n < 0) {
            if (errno == EAGAIN) {
                usleep(10 * 1000);
                continue;
            }

            put_str("read failed errno=");
            put_i32(errno);
            put_str("\n");
            break;
        }

        if (n == 0) {
            usleep(10 * 1000);
            continue;
        }

        if ((n % (ssize_t)sizeof(mdev_event_t)) != 0) {
            put_str("partial event payload bytes=");
            put_i64((int64_t)n);
            put_str("\n");
            continue;
        }

        size_t count = (size_t)n / sizeof(mdev_event_t);
        for (size_t i = 0; i < count; i++) {
            print_event(&events[i]);
        }
    }

    int ungrab_ret = ioctl(fd, MDEVGRABDEV, 0);
    if (ungrab_ret != 0) {
        put_str("MDEVGRABDEV(0) failed ret=");
        put_i32(ungrab_ret);
        put_str(" errno=");
        put_i32(errno);
        put_str("\n");
    }

    close(fd);
    return 0;
}