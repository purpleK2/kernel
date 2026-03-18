#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <time.h>
#include <poll.h>
#include <termios.h>

#define SYS_IOCTL 5

static inline long sys_ioctl(int fd, unsigned long request, void *arg) {
    long ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_IOCTL), "D"((long)fd), "S"(request), "d"((long)arg)
        : "memory"
    );
    return ret;
}

#define TIOCGETA    19
#define TIOCSETA    20
#define IOCTLTTYIS  0x5401

#define FB_IOCTL_GET_INFO 0x1001

#define VT_ACTIVATE   0x5606
#define VT_WAITACTIVE 0x5607

#define PAGE_SIZE 0x1000

typedef struct fb_info {
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint64_t bpp;
} fb_info_t;

static int tests_passed = 0;
static int tests_failed = 0;
enum { MAX_FAILED_TESTS = 256 };
static const char *failed_tests[MAX_FAILED_TESTS];
static int failed_tests_count = 0;

static void test_pass(const char *name) {
    printf("  [PASS] %s\n", name);
    tests_passed++;
}

static void test_fail(const char *name) {
    printf("  [FAIL] %s\n", name);
    tests_failed++;
    if (failed_tests_count < MAX_FAILED_TESTS) {
        failed_tests[failed_tests_count++] = name;
    }
}

static void test_check(int condition, const char *name) {
    if (condition) {
        test_pass(name);
    } else {
        test_fail(name);
    }
}

static void test_stdio(void) {
    printf("\n=== STDIO Tests ===\n");

    printf("Testing printf with various formats:\n");
    printf("  Integer: %d\n", 42);
    printf("  Unsigned: %u\n", 123456U);
    printf("  Hex: 0x%x\n", 0xDEADBEEF);
    printf("  String: %s\n", "Hello, World!");
    printf("  Pointer: %p\n", (void *)0x12345678);
    printf("  Character: %c\n", 'A');
    test_pass("printf basic formats");

    char buf[128];
    int n = sprintf(buf, "Value: %d, String: %s", 999, "test");
    test_check(n > 0 && strcmp(buf, "Value: 999, String: test") == 0, "sprintf");

    n = snprintf(buf, 10, "Hello, World!");
    test_check(n == 13 && strlen(buf) == 9, "snprintf truncation");

    printf("=== End STDIO Tests ===\n");
}

static void test_string(void) {
    printf("\n=== String Tests ===\n");

    test_check(strlen("hello") == 5, "strlen");
    test_check(strlen("") == 0, "strlen empty");

    test_check(strcmp("abc", "abc") == 0, "strcmp equal");
    test_check(strcmp("abc", "abd") < 0, "strcmp less");
    test_check(strcmp("abd", "abc") > 0, "strcmp greater");

    test_check(strncmp("hello", "help", 3) == 0, "strncmp partial equal");
    test_check(strncmp("hello", "help", 4) != 0, "strncmp partial differ");

    char buf[64];
    strcpy(buf, "test");
    test_check(strcmp(buf, "test") == 0, "strcpy");

    memset(buf, 'X', sizeof(buf));
    strncpy(buf, "short", 10);
    test_check(strcmp(buf, "short") == 0, "strncpy");

    strcpy(buf, "Hello, ");
    strcat(buf, "World!");
    test_check(strcmp(buf, "Hello, World!") == 0, "strcat");

    memset(buf, 0xAA, 16);
    int memset_ok = 1;
    for (int i = 0; i < 16; i++) {
        if ((unsigned char)buf[i] != 0xAA) {
            memset_ok = 0;
            break;
        }
    }
    test_check(memset_ok, "memset");

    const char src[] = "MEMCPY_TEST";
    char dst[32];
    memcpy(dst, src, sizeof(src));
    test_check(strcmp(dst, src) == 0, "memcpy");

    test_check(memcmp("abc", "abc", 3) == 0, "memcmp equal");
    test_check(memcmp("abc", "abd", 3) != 0, "memcmp differ");

    const char *p = strchr("hello", 'l');
    test_check(p != NULL && *p == 'l' && p == "hello" + 2, "strchr found");
    test_check(strchr("hello", 'z') == NULL, "strchr not found");

    p = strrchr("hello", 'l');
    test_check(p != NULL && p == "hello" + 3, "strrchr");

    printf("=== End String Tests ===\n");
}

static void test_memory(void) {
    printf("\n=== Memory Tests (malloc/free) ===\n");

    void *p = malloc(1024);
    test_check(p != NULL, "malloc(1024)");

    if (p) {
        memset(p, 0x42, 1024);
        test_check(((unsigned char *)p)[0] == 0x42 && ((unsigned char *)p)[1023] == 0x42,
                   "malloc memory usable");
        free(p);
        test_pass("free");
    }

    int *arr = calloc(100, sizeof(int));
    test_check(arr != NULL, "calloc(100, sizeof(int))");
    if (arr) {
        int zeroed = 1;
        for (int i = 0; i < 100; i++) {
            if (arr[i] != 0) {
                zeroed = 0;
                break;
            }
        }
        test_check(zeroed, "calloc memory is zeroed");
        free(arr);
    }

    p = malloc(64);
    if (p) {
        memset(p, 0xAA, 64);
        p = realloc(p, 256);
        test_check(p != NULL, "realloc grow");
        if (p) {
            int preserved = 1;
            for (int i = 0; i < 64; i++) {
                if (((unsigned char *)p)[i] != 0xAA) {
                    preserved = 0;
                    break;
                }
            }
            test_check(preserved, "realloc preserves data");
            free(p);
        }
    }

    void *ptrs[10];
    int alloc_ok = 1;
    for (int i = 0; i < 10; i++) {
        ptrs[i] = malloc(128);
        if (!ptrs[i]) alloc_ok = 0;
    }
    test_check(alloc_ok, "multiple allocations");

    int unique = 1;
    for (int i = 0; i < 10 && unique; i++) {
        for (int j = i + 1; j < 10; j++) {
            if (ptrs[i] == ptrs[j]) {
                unique = 0;
                break;
            }
        }
    }
    test_check(unique, "allocations are unique");

    for (int i = 0; i < 10; i++) {
        free(ptrs[i]);
    }

    printf("=== End Memory Tests ===\n");
}

static void test_mmap(void) {
    printf("\n=== mmap/munmap/mprotect Tests ===\n");

    void *p = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    test_check(p != MAP_FAILED, "mmap anonymous");

    if (p != MAP_FAILED) {
        int zeroed = 1;
        for (int i = 0; i < 64; i++) {
            if (((unsigned char *)p)[i] != 0) {
                zeroed = 0;
                break;
            }
        }
        test_check(zeroed, "anonymous mmap zeroed");

        ((unsigned char *)p)[0] = 0xDE;
        ((unsigned char *)p)[4095] = 0xAD;
        test_check(((unsigned char *)p)[0] == 0xDE && ((unsigned char *)p)[4095] == 0xAD,
                   "mmap read/write");

        int ret = mprotect(p, PAGE_SIZE, PROT_READ);
        test_check(ret == 0, "mprotect to read-only");

        ret = mprotect(p, PAGE_SIZE, PROT_READ | PROT_WRITE);
        test_check(ret == 0, "mprotect to read-write");

        ret = munmap(p, PAGE_SIZE);
        test_check(ret == 0, "munmap");
    }

    size_t multi_size = 4 * PAGE_SIZE;
    p = mmap(NULL, multi_size, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    test_check(p != MAP_FAILED, "mmap multi-page");
    if (p != MAP_FAILED) {
        for (int i = 0; i < 4; i++) {
            ((unsigned char *)p)[i * PAGE_SIZE] = (unsigned char)(i + 1);
        }
        int pages_ok = 1;
        for (int i = 0; i < 4; i++) {
            if (((unsigned char *)p)[i * PAGE_SIZE] != (unsigned char)(i + 1)) {
                pages_ok = 0;
                break;
            }
        }
        test_check(pages_ok, "multi-page access");
        munmap(p, multi_size);
    }

    void *fixed_addr = (void *)0x200000000ULL;
    p = mmap(fixed_addr, PAGE_SIZE, PROT_READ | PROT_WRITE,
             MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p != MAP_FAILED) {
        test_check(p == fixed_addr, "MAP_FIXED at requested address");
        munmap(p, PAGE_SIZE);
    } else {
        test_fail("MAP_FIXED");
    }

    printf("=== End mmap Tests ===\n");
}

static void test_file_io(void) {
    printf("\n=== File I/O Tests ===\n");

    int fd = open("/dev/null", O_RDWR);
    test_check(fd >= 0, "open /dev/null");

    if (fd >= 0) {
        ssize_t n = write(fd, "test", 4);
        test_check(n == 4, "write to /dev/null");

        char buf[16];
        n = read(fd, buf, sizeof(buf));
        test_check(n == 0, "read from /dev/null");

        close(fd);
    }

    fd = open("/etc/test.txt", O_RDONLY);
    if (fd >= 0) {
        char buf[256];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            printf("  Read from /etc/test.txt: \"%s\"\n", buf);
            test_pass("read from /etc/test.txt");
        }

        off_t pos = lseek(fd, 0, SEEK_SET);
        test_check(pos == 0, "lseek SEEK_SET");

        pos = lseek(fd, 0, SEEK_END);
        test_check(pos >= 0, "lseek SEEK_END");

        close(fd);
    } else {
        printf("  SKIP: /etc/test.txt not available\n");
    }

    fd = open("/dev/e9", O_WRONLY);
    if (fd >= 0) {
        int fd2 = dup(fd);
        test_check(fd2 >= 0 && fd2 != fd, "dup");
        if (fd2 >= 0) {
            write(fd2, "dup test\n", 9);
            close(fd2);
        }
        close(fd);
    }

    fd = open("/dev/e9", O_WRONLY);
    if (fd >= 0) {
        int target = 100;
        int ret = dup2(fd, target);
        test_check(ret == target, "dup2");
        if (ret == target) {
            write(target, "dup2 test\n", 10);
            close(target);
        }
        close(fd);
    }

    printf("=== End File I/O Tests ===\n");
}

enum { DIR_OUT_BUF_SZ = 4096 };
static char dir_out_buf[DIR_OUT_BUF_SZ];
static size_t dir_out_len = 0;

static void dir_emit_line(const char *line) {

    size_t line_len = strlen(line);
    if (line_len >= DIR_OUT_BUF_SZ) {
        if (dir_out_len) {
            write(1, dir_out_buf, dir_out_len);
            dir_out_len = 0;
        }
        write(1, line, line_len);
        return;
    }

    if (dir_out_len + line_len > DIR_OUT_BUF_SZ) {
        write(1, dir_out_buf, dir_out_len);
        dir_out_len = 0;
    }

    memcpy(dir_out_buf + dir_out_len, line, line_len);
    dir_out_len += line_len;
}

static void dir_flush_output(void) {
    if (dir_out_len) {
        write(1, dir_out_buf, dir_out_len);
        dir_out_len = 0;
    }
}

static void walk_directory(const char *path, int depth, int *printed, int max_print) {
    if (depth > 2 || *printed >= max_print) return;

    DIR *dir = opendir(path);
    if (!dir) {
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char line[640];
        int off = 0;
        for (int i = 0; i < depth && off < (int)sizeof(line) - 1; i++) {
            off += snprintf(line + off, sizeof(line) - (size_t)off, "  ");
        }

        const char *type_str;
        switch (entry->d_type) {
            case DT_DIR: type_str = "DIR"; break;
            case DT_REG: type_str = "FILE"; break;
            case DT_LNK: type_str = "LINK"; break;
            case DT_CHR: type_str = "CHAR"; break;
            case DT_BLK: type_str = "BLOCK"; break;
            default: type_str = "?"; break;
        }
        snprintf(line + off, sizeof(line) - (size_t)off, "[%s] %s\n", type_str, entry->d_name);
        dir_emit_line(line);

        if (entry->d_type == DT_DIR) {
            char child_path[512];
            snprintf(child_path, sizeof(child_path), "%s/%s", path, entry->d_name);
            walk_directory(child_path, depth + 1, printed, max_print);
            if (*printed >= max_print) {
                break;
            }
        }

        (*printed)++;
        if (*printed >= max_print) {
            break;
        }
    }

    closedir(dir);
}

static void test_directory(void) {
    printf("\n=== Directory Tests ===\n");

    DIR *dir = opendir("/");
    test_check(dir != NULL, "opendir /");

    if (dir) {
        struct dirent *entry = readdir(dir);
        test_check(entry != NULL, "readdir");
        closedir(dir);
    }

    int ret = mkdir("/libc_test_dir", 0755);
    test_check(ret == 0 || errno == EEXIST, "mkdir");

    int fd = open("/libc_test_dir/test_file.txt", O_CREAT | O_WRONLY, 0644);
    if (fd >= 0) {
        write(fd, "test data", 9);
        close(fd);
        test_pass("create file in directory");
    }

    ret = symlink("/libc_test_dir/test_file.txt", "/libc_test_dir/test_link");
    test_check(ret == 0 || errno == EEXIST, "symlink");

    char linkbuf[256];
    ssize_t linklen = readlink("/libc_test_dir/test_link", linkbuf, sizeof(linkbuf) - 1);
    if (linklen > 0) {
        linkbuf[linklen] = '\0';
        printf("  readlink: %s\n", linkbuf);
        test_pass("readlink");
    }

    ret = unlink("/libc_test_dir/test_link");
    test_check(ret == 0 || errno == ENOENT, "unlink symlink");

    ret = unlink("/libc_test_dir/test_file.txt");
    test_check(ret == 0 || errno == ENOENT, "unlink file");

    ret = rmdir("/libc_test_dir");
    test_check(ret == 0 || errno == ENOENT, "rmdir");

    printf("\nFilesystem tree (depth<=2, first 120 entries):\n");
    int printed = 0;
    walk_directory("/", 0, &printed, 120);
    dir_flush_output();
    if (printed >= 120) {
        printf("... output truncated ...\n");
    }

    printf("=== End Directory Tests ===\n");
}

static void test_process(void) {
    printf("\n=== Process Tests ===\n");

    pid_t pid = getpid();
    printf("  PID: %d\n", pid);
    test_check(pid > 0, "getpid");

    pid_t ppid = getppid();
    printf("  PPID: %d\n", ppid);
    test_check(ppid >= 0, "getppid");

    uid_t uid = getuid();
    uid_t euid = geteuid();
    printf("  UID: %d, EUID: %d\n", uid, euid);
    test_pass("getuid/geteuid");

    gid_t gid = getgid();
    gid_t egid = getegid();
    printf("  GID: %d, EGID: %d\n", gid, egid);
    test_pass("getgid/getegid");

    pid_t pgrp = getpgrp();
    printf("  PGRP: %d\n", pgrp);
    test_pass("getpgrp");

    pid_t sid = getsid(0);
    printf("  SID: %d\n", sid);
    test_check(sid > 0, "getsid");

    printf("  Testing fork...\n");
    pid_t child = fork();
    if (child < 0) {
        test_fail("fork");
    } else if (child == 0) {
        _exit(42);
    } else {
        int status = 0;
        pid_t waited = waitpid(child, &status, 0);
        if (waited == child) {
            write(1, "  [PASS] fork+waitpid\n", 22);
            tests_passed++;
        } else {
            write(1, "  [FAIL] fork+waitpid\n", 22);
            tests_failed++;
        }
    }

    printf("=== End Process Tests ===\n");
}

static void test_pipe(void) {
    printf("\n=== Pipe Tests ===\n");

    int pipefd[2];
    int ret = pipe(pipefd);
    test_check(ret == 0, "pipe");

    if (ret == 0) {
        const char *msg = "Hello through pipe!";
        ssize_t written = write(pipefd[1], msg, strlen(msg));
        test_check(written == (ssize_t)strlen(msg), "write to pipe");

        char buf[64];
        ssize_t nread = read(pipefd[0], buf, sizeof(buf) - 1);
        test_check(nread == (ssize_t)strlen(msg), "read from pipe");

        if (nread > 0) {
            buf[nread] = '\0';
            test_check(strcmp(buf, msg) == 0, "pipe data matches");
        }

        close(pipefd[0]);
        close(pipefd[1]);
    }

    ret = pipe(pipefd);
    if (ret == 0) {
        pid_t child = fork();
        if (child == 0) {
            close(pipefd[0]);
            const char *child_msg = "Message from child!";
            write(pipefd[1], child_msg, strlen(child_msg));
            close(pipefd[1]);
            exit(0);
        } else if (child > 0) {
            close(pipefd[1]);
            char buf[64];
            ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                printf("  Received from child: \"%s\"\n", buf);
                test_pass("pipe with fork");
            }
            close(pipefd[0]);
            waitpid(child, NULL, 0);
        }
    }

    printf("=== End Pipe Tests ===\n");
}

static void test_poll(void) {
    printf("\n=== Poll Tests ===\n");

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        test_fail("pipe for poll test");
        return;
    }

    write(pipefd[1], "test", 4);

    struct pollfd fds[1];
    fds[0].fd = pipefd[0];
    fds[0].events = POLLIN;
    fds[0].revents = 0;

    int ret = poll(fds, 1, 1000);
    test_check(ret > 0, "poll returns ready");
    test_check((fds[0].revents & POLLIN) != 0, "poll POLLIN set");

    char buf[16];
    read(pipefd[0], buf, sizeof(buf));

    close(pipefd[0]);
    close(pipefd[1]);

    printf("=== End Poll Tests ===\n");
}

static void test_sleep(void) {
    printf("\n=== Sleep Tests ===\n");

    printf("  Sleeping for 100ms...\n");
    struct timespec req = { .tv_sec = 0, .tv_nsec = 100000000 };
    struct timespec rem;
    int ret = nanosleep(&req, &rem);
    test_check(ret == 0, "nanosleep 100ms");

    printf("  Sleeping for 1 second...\n");
    req.tv_sec = 1;
    req.tv_nsec = 0;
    ret = nanosleep(&req, &rem);
    test_check(ret == 0, "nanosleep 1s");

    printf("=== End Sleep Tests ===\n");
}

static void test_terminal(void) {
    printf("\n=== Terminal Tests ===\n");

    int tty = open("/dev/tty0", O_RDWR);
    if (tty < 0) {
        printf("  SKIP: /dev/tty0 not available\n");
        return;
    }

    int ret = isatty(tty);
    test_check(ret == 1, "isatty on /dev/tty0");

    struct termios term;
    ret = tcgetattr(tty, &term);
    test_check(ret == 0, "tcgetattr");

    if (ret == 0) {
        printf("  Terminal flags - c_lflag: 0x%x, c_iflag: 0x%x\n",
               (unsigned)term.c_lflag, (unsigned)term.c_iflag);
    }

    const char *msg = "Hello from libc_test on /dev/tty0!\r\n";
    ssize_t n = write(tty, msg, strlen(msg));
    test_check(n == (ssize_t)strlen(msg), "write to terminal");

    close(tty);

    printf("=== End Terminal Tests ===\n");
}

static void test_framebuffer(void) {
    printf("\n=== Framebuffer Tests ===\n");

    int fb = open("/dev/fb0", O_RDWR);
    if (fb < 0) {
        printf("  SKIP: /dev/fb0 not available\n");
        return;
    }

    fb_info_t info;
    sys_ioctl(fb, FB_IOCTL_GET_INFO, &info);

    printf("  FB: %lux%lu, pitch=%lu, bpp=%lu\n",
           (unsigned long)info.width, (unsigned long)info.height,
           (unsigned long)info.pitch, (unsigned long)info.bpp);
    test_pass("FB ioctl get info");

    size_t fb_size = info.pitch * info.height;
    void *fb_ptr = mmap(NULL, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);

    if (fb_ptr != MAP_FAILED) {
        test_pass("mmap framebuffer");

        printf("  Drawing gradient...\n");
        uint8_t *pixels = (uint8_t *)fb_ptr;
        for (uint32_t y = 0; y < info.height; y++) {
            for (uint32_t x = 0; x < info.width; x++) {
                size_t off = y * info.pitch + x * 4;
                pixels[off + 0] = (uint8_t)(x & 0xFF);
                pixels[off + 1] = (uint8_t)(y & 0xFF);
                pixels[off + 2] = (uint8_t)((x + y) & 0xFF);
                pixels[off + 3] = 0x00;
            }
        }
        struct timespec ts = { .tv_sec = 2, .tv_nsec = 0 };
        nanosleep(&ts, NULL);
        test_pass("draw to framebuffer");

        munmap(fb_ptr, fb_size);
    } else {
        test_fail("mmap framebuffer");
    }

    close(fb);

    printf("=== End Framebuffer Tests ===\n");
}

int main(int argc, char *argv[]) {
    printf("\n");
    printf("============================================\n");
    printf("       purpleK2 libc Test Suite\n");
    printf("============================================\n");
    printf("\n");

    printf("argc = %d\n", argc);
    for (int i = 0; i < argc; i++) {
        printf("argv[%d] = \"%s\"\n", i, argv[i]);
    }
    printf("\n");

    test_stdio();
    test_string();
    test_memory();
    test_mmap();
    test_file_io();
    test_directory();
    test_process();
    test_pipe();
    test_poll();
    test_sleep();
    test_terminal();
    test_framebuffer();

    printf("\n");
    printf("============================================\n");
    printf("              Test Summary\n");
    printf("============================================\n");
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("  Total:  %d\n", tests_passed + tests_failed);
    printf("============================================\n");

    if (tests_failed == 0) {
        printf("  All tests PASSED!\n");
    } else {
        printf("  Some tests FAILED!\n");
        printf("  Failed tests:\n");
        for (int i = 0; i < failed_tests_count; i++) {
            printf("    - %s\n", failed_tests[i]);
        }
    }
    printf("============================================\n\n");

    printf("libc_test complete. Entering infinite loop.\n");
    for (;;) {
        struct timespec ts = { .tv_sec = 1, .tv_nsec = 0 };
        nanosleep(&ts, NULL);
    }

    return 0;
}