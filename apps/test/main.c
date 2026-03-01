#include <stdint.h>

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

static inline uint64_t syscall3p(uint64_t num, void *a1, void *a2, void *a3) {
    return syscall3(num, (uint64_t)a1, (uint64_t)a2, (uint64_t)a3);
}

static inline uint64_t syscall2p(uint64_t num, void *a1, void *a2) {
    return syscall2(num, (uint64_t)a1, (uint64_t)a2);
}

/* syscall6: 6-argument syscall (arg4 goes in r10 per kernel ABI) */
static inline uint64_t syscall6(uint64_t num, uint64_t a1, uint64_t a2,
                                uint64_t a3, uint64_t a4, uint64_t a5,
                                uint64_t a6) {
    uint64_t ret;
    register uint64_t r10 __asm__("r10") = a4;
    register uint64_t r8  __asm__("r8")  = a5;
    register uint64_t r9  __asm__("r9")  = a6;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3),
          "r"(r10), "r"(r8), "r"(r9)
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
    syscall3(3, (uint64_t)fd, (uint64_t)s, strlen(s));
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
    // reverse
    for (int j = 0; j < i / 2; j++) {
        char tmp = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = tmp;
    }
    buf[i] = '\0';
    print(fd, buf);
}

void main(uintptr_t *stack_ptr) {
    uint64_t *stack = (uint64_t *)stack_ptr;
    
    // Parse argc, argv, envp from stack (System V ABI style)
    uint64_t argc = stack[0];
    char **argv = (char **)&stack[1];
    char **envp = (char **)&stack[argc + 2];  // +2 to skip argc and NULL terminator
    
    int fd = syscall3(1, (uint64_t)"/dev/ttyS0", 0, 0);
    if (fd < 0) {
        syscall1(0, 1);
        return;
    }

    print(fd, "\r\n=== Test App Started ===\r\n");
    
    // Print argc
    print(fd, "argc = ");
    print_dec(fd, argc);
    print(fd, "\r\n");
    
    // Print all arguments
    print(fd, "Arguments:\r\n");
    for (uint64_t i = 0; i < argc; i++) {
        print(fd, "  argv[");
        print_dec(fd, i);
        print(fd, "] = \"");
        if (argv[i]) {
            print(fd, argv[i]);
        } else {
            print(fd, "(null)");
        }
        print(fd, "\"\r\n");
    }
    
    // Print environment variables
    print(fd, "Environment:\r\n");
    for (int i = 0; envp[i] != 0; i++) {
        print(fd, "  envp[");
        print_dec(fd, i);
        print(fd, "] = \"");
        print(fd, envp[i]);
        print(fd, "\"\r\n");
    }
    
    print(fd, "=== Test App Done ===\r\n");

    syscall1(0, 0);  // exit(0)
}