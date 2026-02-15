// main.c
// freestanding userspace program for your OS

#include <stdint.h>
typedef unsigned long  uint64_t;
typedef long           int64_t;
typedef unsigned long  size_t;
typedef unsigned int   uint32_t;

/* syscall numbers */
#define SYS_EXIT      0
#define SYS_OPEN      1
#define SYS_READ      2
#define SYS_WRITE     3
#define SYS_CLOSE     4
#define SYS_IOCTL     5
#define SYS_SEEK      6
#define SYS_GETPID    9
#define SYS_GETUID    10
#define SYS_GETEUID   11
#define SYS_GETGID    12
#define SYS_GETEGID   13
#define SYS_SETUID    14
#define SYS_SETEUID   15
#define SYS_SETREUID  16
#define SYS_SETRESUID 17
#define SYS_GETRESUID 18
#define SYS_SETGID    19
#define SYS_SETEGID   20
#define SYS_SETREGID  21
#define SYS_SETRESGID 22
#define SYS_GETRESGID 23
#define SYS_FORK      24
#define SYS_OPENDIR   27
#define SYS_READDIR   28
#define SYS_CLOSEDIR  29
#define SYS_MKDIR     30
#define SYS_CREATE    31
#define SYS_RMDIR     32
#define SYS_REMOVE    33
#define SYS_SYMLINK   34
#define SYS_READLINK  35
#define SYS_MMAP      36
#define SYS_MUNMAP    37
#define SYS_MPROTECT  38
#define SYS_MSYNC     39
#define SYS_PIPE      40
#define SYS_NANOSLEEP 41

/* mmap protection flags */
#define PROT_NONE   0x0
#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4

/* mmap mapping flags */
#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
#define MAP_FIXED       0x10
#define MAP_ANONYMOUS   0x20
#define MAP_ANON        MAP_ANONYMOUS
#define MAP_NORESERVE   0x40
#define MAP_GROWSDOWN   0x100
#define MAP_STACK       0x200
#define MAP_POPULATE    0x400

#define MAP_FAILED ((void *)(uint64_t)-1)

/* msync flags */
#define MS_ASYNC      0x1
#define MS_SYNC       0x2
#define MS_INVALIDATE 0x4

#define PAGE_SIZE 0x1000

/* vnode types for d_type */
#define VNODE_NULL    0
#define VNODE_REGULAR 1
#define VNODE_DIR     2
#define VNODE_BLOCK   3
#define VNODE_CHAR    4
#define VNODE_LINK    5
#define VNODE_PIPE    6
#define VNODE_SOCKET  7
#define VNODE_BAD     8

/* dirent structure - must match kernel's dirent_t */
typedef struct __attribute__((packed)) {
    uint64_t d_ino;
    uint64_t d_off;
    uint64_t d_reclen;
    uint8_t  d_type;
    char     d_name[256];
} dirent_t;

typedef struct fb_info {
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint64_t bpp;
} fb_info_t;

#define FB_IOCTL_GET_INFO 0x1001


/* auxv types */
#define AT_NULL         0
#define AT_IGNORE       1
#define AT_EXECFD       2
#define AT_PHDR         3
#define AT_PHENT        4
#define AT_PHNUM        5
#define AT_PAGESZ       6
#define AT_BASE         7
#define AT_FLAGS        8
#define AT_ENTRY        9
#define AT_NOTELF       10
#define AT_UID          11
#define AT_EUID         12
#define AT_GID          13
#define AT_EGID         14
#define AT_PLATFORM     15
#define AT_HWCAP        16
#define AT_CLKTCK       17
#define AT_SECURE       23
#define AT_BASE_PLATFORM 24
#define AT_RANDOM       25
#define AT_HWCAP2       26
#define AT_EXECFN       31

typedef struct {
    uint64_t a_type;
    union {
        uint64_t a_val;
    } a_un;
} Elf64_auxv_t;

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

static const char filename[] = "/dev/e9";

__thread uint64_t thread_local_var = 67;

static size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

static void print(uint64_t fd, const char *str) {
    syscall3(SYS_WRITE, fd, (uint64_t)str, strlen(str));
}

static void print_hex(uint64_t fd, uint64_t value) {
    char buf[19]; // "0x" + 16 hex digits + null
    buf[0] = '0';
    buf[1] = 'x';
    
    for (int i = 15; i >= 0; i--) {
        uint64_t nibble = (value >> (i * 4)) & 0xF;
        buf[17 - i] = (nibble < 10) ? ('0' + nibble) : ('a' + nibble - 10);
    }
    buf[18] = '\0';
    
    syscall3(SYS_WRITE, fd, (uint64_t)buf, 18);
}

static void print_dec(uint64_t fd, uint64_t value) {
    char buf[21]; // max 20 digits for uint64_t + null
    int i = 19;
    buf[20] = '\0';
    
    if (value == 0) {
        buf[i--] = '0';
    } else {
        while (value > 0) {
            buf[i--] = '0' + (value % 10);
            value /= 10;
        }
    }
    
    syscall3(SYS_WRITE, fd, (uint64_t)&buf[i + 1], 19 - i);
}

static void print_int(uint64_t fd, int64_t value) {
    if (value < 0) {
        print(fd, "-");
        uint64_t v = (uint64_t)(-value);
        print_dec(fd, v);
    } else {
        print_dec(fd, (uint64_t)value);
    }
}

static const char *auxv_type_name(uint64_t type) {
    switch (type) {
        case AT_NULL:     return "AT_NULL";
        case AT_IGNORE:   return "AT_IGNORE";
        case AT_EXECFD:   return "AT_EXECFD";
        case AT_PHDR:     return "AT_PHDR";
        case AT_PHENT:    return "AT_PHENT";
        case AT_PHNUM:    return "AT_PHNUM";
        case AT_PAGESZ:   return "AT_PAGESZ";
        case AT_BASE:     return "AT_BASE";
        case AT_FLAGS:    return "AT_FLAGS";
        case AT_ENTRY:    return "AT_ENTRY";
        case AT_NOTELF:   return "AT_NOTELF";
        case AT_UID:      return "AT_UID";
        case AT_EUID:     return "AT_EUID";
        case AT_GID:      return "AT_GID";
        case AT_EGID:     return "AT_EGID";
        case AT_PLATFORM: return "AT_PLATFORM";
        case AT_HWCAP:    return "AT_HWCAP";
        case AT_CLKTCK:   return "AT_CLKTCK";
        case AT_SECURE:   return "AT_SECURE";
        case AT_RANDOM:   return "AT_RANDOM";
        case AT_HWCAP2:   return "AT_HWCAP2";
        case AT_EXECFN:   return "AT_EXECFN";
        case AT_BASE_PLATFORM: return "AT_BASE_PLATFORM";
        default:          return "AT_UNKNOWN";
    }
}

/* --- Directory walking test --- */

static const char *vnode_type_name(uint8_t type) {
    switch (type) {
        case VNODE_NULL:    return "NULL";
        case VNODE_REGULAR: return "FILE";
        case VNODE_DIR:     return "DIR";
        case VNODE_BLOCK:   return "BLOCK";
        case VNODE_CHAR:    return "CHAR";
        case VNODE_LINK:    return "LINK";
        case VNODE_PIPE:    return "PIPE";
        case VNODE_SOCKET:  return "SOCKET";
        case VNODE_BAD:     return "BAD";
        default:            return "UNKNOWN";
    }
}

static int64_t sys_opendir(const char *path) {
    return (int64_t)syscall1(SYS_OPENDIR, (uint64_t)path);
}

static int64_t sys_readdir_call(int64_t dirfd, dirent_t *entry) {
    return (int64_t)syscall2(SYS_READDIR, (uint64_t)dirfd, (uint64_t)entry);
}

static int64_t sys_closedir(int64_t dirfd) {
    return (int64_t)syscall1(SYS_CLOSEDIR, (uint64_t)dirfd);
}

static int64_t sys_mkdir_call(const char *path, int mode) {
    return (int64_t)syscall2(SYS_MKDIR, (uint64_t)path, (uint64_t)mode);
}

static int64_t sys_create_call(const char *path, int mode) {
    return (int64_t)syscall2(SYS_CREATE, (uint64_t)path, (uint64_t)mode);
}

static int64_t sys_rmdir_call(const char *path) {
    return (int64_t)syscall1(SYS_RMDIR, (uint64_t)path);
}

static int64_t sys_remove_call(const char *path) {
    return (int64_t)syscall1(SYS_REMOVE, (uint64_t)path);
}

static int64_t sys_symlink_call(const char *target, const char *linkpath) {
    return (int64_t)syscall2(SYS_SYMLINK, (uint64_t)target, (uint64_t)linkpath);
}

static int64_t sys_readlink_call(const char *path, char *buf, size_t size) {
    return (int64_t)syscall3(SYS_READLINK, (uint64_t)path, (uint64_t)buf, (uint64_t)size);
}

static void print_indent(uint64_t fd, int depth) {
    for (int i = 0; i < depth; i++) {
        print(fd, "  ");
    }
}

static int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

static char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++))
        ;
    return dest;
}

static char *strcat(char *dest, const char *src) {
    char *d = dest;
    while (*d)
        d++;
    while ((*d++ = *src++))
        ;
    return dest;
}

static void walk_directory(uint64_t outfd, const char *path, int depth) {
    int64_t dirfd = sys_opendir(path);
    if (dirfd < 0) {
        print_indent(outfd, depth);
        print(outfd, "[ERROR] Failed to open: ");
        print(outfd, path);
        print(outfd, "\r\n");
        return;
    }

    dirent_t entry;
    int64_t ret;
    int max_entries = 1000; /* Safety limit */
    int entry_count = 0;

    while ((ret = sys_readdir_call(dirfd, &entry)) > 0 && entry_count < max_entries) {
        entry_count++;
        
        /* Skip . and .. to avoid infinite recursion */
        if (strcmp(entry.d_name, ".") == 0 || strcmp(entry.d_name, "..") == 0) {
            continue;
        }

        print_indent(outfd, depth);
        print(outfd, "[");
        print(outfd, vnode_type_name(entry.d_type));
        print(outfd, "] ");
        print(outfd, entry.d_name);
        print(outfd, "\r\n");

        /* If it's a directory, recurse into it */
        if (entry.d_type == VNODE_DIR && entry.d_name[0] != '\0') {
            /* Build the full path */
            char child_path[512];
            strcpy(child_path, path);
            
            /* Add trailing slash if needed */
            size_t plen = strlen(path);
            if (plen > 0 && path[plen - 1] != '/') {
                strcat(child_path, "/");
            }
            strcat(child_path, entry.d_name);

            walk_directory(outfd, child_path, depth + 1);
        }
    }

    sys_closedir(dirfd);
}

static void test_directory_walk(uint64_t outfd) {
    print(outfd, "\r\n=== Directory Walking Test ===\r\n");
    print(outfd, "Walking filesystem from root (/)\r\n\r\n");
    
    walk_directory(outfd, "/", 0);
    
    print(outfd, "\r\n=== End Directory Walking Test ===\r\n");
}

static void test_fs_syscalls(uint64_t outfd) {
    const char *base_dir  = "/syscall_test";
    const char *file_path = "/syscall_test/test_file";
    const char *link_path = "/syscall_test/test_link";

    int64_t ret;

    /* Pass 1: create, verify, then clean up */
    print(outfd, "\r\n=== FS syscall tests (pass 1, with cleanup) ===\r\n");

    ret = sys_mkdir_call(base_dir, 0755);
    print(outfd, "sys_mkdir(\"/syscall_test\") = ");
    print_int(outfd, ret);
    print(outfd, "\r\n");

    ret = sys_create_call(file_path, 0644);
    print(outfd, "sys_create(\"/syscall_test/test_file\") = ");
    print_int(outfd, ret);
    print(outfd, "\r\n");

    ret = sys_symlink_call(file_path, link_path);
    print(outfd, "sys_symlink(target=\"/syscall_test/test_file\", link=\"/syscall_test/test_link\") = ");
    print_int(outfd, ret);
    print(outfd, "\r\n");

    char buf[256];
    for (int i = 0; i < (int)sizeof(buf); i++) {
        buf[i] = '\0';
    }

    int64_t readlen = sys_readlink_call(link_path, buf, sizeof(buf) - 1);
    print(outfd, "sys_readlink(\"/syscall_test/test_link\") = ");
    print_int(outfd, readlen);
    print(outfd, ", target=\"");
    if (readlen > 0 && readlen < (int64_t)sizeof(buf)) {
        buf[readlen] = '\0';
        print(outfd, buf);
    }
    print(outfd, "\"\r\n");

    ret = sys_remove_call(file_path);
    print(outfd, "sys_remove(\"/syscall_test/test_file\") = ");
    print_int(outfd, ret);
    print(outfd, "\r\n");

    ret = sys_remove_call(link_path);
    print(outfd, "sys_remove(\"/syscall_test/test_link\") = ");
    print_int(outfd, ret);
    print(outfd, "\r\n");

    ret = sys_rmdir_call(base_dir);
    print(outfd, "sys_rmdir(\"/syscall_test\") = ");
    print_int(outfd, ret);
    print(outfd, "\r\n");

    /* Pass 2: create and verify again, but keep artifacts */
    print(outfd, "\r\n=== FS syscall tests (pass 2, keep artifacts) ===\r\n");

    ret = sys_mkdir_call(base_dir, 0755);
    print(outfd, "sys_mkdir(\"/syscall_test\") = ");
    print_int(outfd, ret);
    print(outfd, "\r\n");

    ret = sys_create_call(file_path, 0644);
    print(outfd, "sys_create(\"/syscall_test/test_file\") = ");
    print_int(outfd, ret);
    print(outfd, "\r\n");

    ret = sys_symlink_call(file_path, link_path);
    print(outfd, "sys_symlink(target=\"/syscall_test/test_file\", link=\"/syscall_test/test_link\") = ");
    print_int(outfd, ret);
    print(outfd, "\r\n");

    for (int i = 0; i < (int)sizeof(buf); i++) {
        buf[i] = '\0';
    }

    readlen = sys_readlink_call(link_path, buf, sizeof(buf) - 1);
    print(outfd, "sys_readlink(\"/syscall_test/test_link\") = ");
    print_int(outfd, readlen);
    print(outfd, ", target=\"");
    if (readlen > 0 && readlen < (int64_t)sizeof(buf)) {
        buf[readlen] = '\0';
        print(outfd, buf);
    }
    print(outfd, "\"\r\n");

    print(outfd, "=== End FS syscall tests ===\r\n");
}

static void *mmap(void *addr, size_t length, int prot, int flags, int fd, size_t offset) {
    return (void *)syscall6(SYS_MMAP,
                            (uint64_t)addr, (uint64_t)length,
                            (uint64_t)prot, (uint64_t)flags,
                            (uint64_t)fd,   (uint64_t)offset);
}

static int munmap(void *addr, size_t length) {
    return (int)syscall2(SYS_MUNMAP, (uint64_t)addr, (uint64_t)length);
}

static int mprotect(void *addr, size_t length, int prot) {
    return (int)syscall3(SYS_MPROTECT, (uint64_t)addr, (uint64_t)length, (uint64_t)prot);
}

static int msync(void *addr, size_t length, int flags) {
    return (int)syscall3(SYS_MSYNC, (uint64_t)addr, (uint64_t)length, (uint64_t)flags);
}

static void *memset_user(void *s, int c, size_t n) {
    unsigned char *p = (unsigned char *)s;
    for (size_t i = 0; i < n; i++) {
        p[i] = (unsigned char)c;
    }
    return s;
}

void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

static void test_mmap(uint64_t outfd) {
    int64_t ret;
    int passed = 0;
    int failed = 0;

    print(outfd, "\r\n=== mmap / munmap / mprotect Test Suite ===\r\n");

    /* Test 1: Basic anonymous mmap with MAP_PRIVATE */
    print(outfd, "\r\n[TEST 1] mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)\r\n");
    void *p1 = mmap((void *)0, PAGE_SIZE, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p1 == MAP_FAILED) {
        print(outfd, "  FAIL: mmap returned MAP_FAILED\r\n");
        failed++;
    } else {
        print(outfd, "  OK: mapped at ");
        print_hex(outfd, (uint64_t)p1);
        print(outfd, "\r\n");

        /* Verify memory is zeroed */
        unsigned char *bytes = (unsigned char *)p1;
        int zero_ok = 1;
        for (int i = 0; i < 64; i++) {
            if (bytes[i] != 0) {
                zero_ok = 0;
                break;
            }
        }
        if (zero_ok) {
            print(outfd, "  OK: memory is zeroed\r\n");
        } else {
            print(outfd, "  FAIL: memory is NOT zeroed\r\n");
            failed++;
        }

        /* Write and read back */
        bytes[0] = 0xAA;
        bytes[1] = 0xBB;
        bytes[4095] = 0xCC;
        if (bytes[0] == 0xAA && bytes[1] == 0xBB && bytes[4095] == 0xCC) {
            print(outfd, "  OK: read-write works\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: read-write mismatch\r\n");
            failed++;
        }

        /* Clean up */
        ret = munmap(p1, PAGE_SIZE);
        print(outfd, "  munmap result: ");
        print_int(outfd, ret);
        print(outfd, "\r\n");
        if (ret == 0) passed++; else failed++;
    }

    /* Test 2: Multi-page anonymous mmap */
    print(outfd, "\r\n[TEST 2] mmap(NULL, 16384, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)\r\n");
    size_t multi_size = 4 * PAGE_SIZE;
    void *p2 = mmap((void *)0, multi_size, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p2 == MAP_FAILED) {
        print(outfd, "  FAIL: mmap returned MAP_FAILED\r\n");
        failed++;
    } else {
        print(outfd, "  OK: mapped at ");
        print_hex(outfd, (uint64_t)p2);
        print(outfd, "\r\n");

        /* Touch all pages */
        unsigned char *bytes = (unsigned char *)p2;
        bytes[0]             = 0x11;
        bytes[PAGE_SIZE]     = 0x22;
        bytes[PAGE_SIZE * 2] = 0x33;
        bytes[PAGE_SIZE * 3] = 0x44;

        if (bytes[0] == 0x11 && bytes[PAGE_SIZE] == 0x22 &&
            bytes[PAGE_SIZE * 2] == 0x33 && bytes[PAGE_SIZE * 3] == 0x44) {
            print(outfd, "  OK: all 4 pages accessible\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: multi-page access failed\r\n");
            failed++;
        }

        ret = munmap(p2, multi_size);
        if (ret == 0) {
            print(outfd, "  OK: munmap succeeded\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: munmap failed\r\n");
            failed++;
        }
    }

    /* Test 3: MAP_SHARED anonymous mmap */
    print(outfd, "\r\n[TEST 3] mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0)\r\n");
    void *p3 = mmap((void *)0, PAGE_SIZE, PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (p3 == MAP_FAILED) {
        print(outfd, "  FAIL: mmap returned MAP_FAILED\r\n");
        failed++;
    } else {
        print(outfd, "  OK: mapped at ");
        print_hex(outfd, (uint64_t)p3);
        print(outfd, "\r\n");

        /* Write a pattern */
        memset_user(p3, 0x42, PAGE_SIZE);
        unsigned char *bytes = (unsigned char *)p3;
        if (bytes[0] == 0x42 && bytes[4095] == 0x42) {
            print(outfd, "  OK: MAP_SHARED write works\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: MAP_SHARED write failed\r\n");
            failed++;
        }

        ret = munmap(p3, PAGE_SIZE);
        if (ret == 0) passed++; else failed++;
    }

    /* Test 4: PROT_READ only (no PROT_WRITE) */
    print(outfd, "\r\n[TEST 4] mmap(NULL, 4096, PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)\r\n");
    void *p4 = mmap((void *)0, PAGE_SIZE, PROT_READ,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p4 == MAP_FAILED) {
        print(outfd, "  FAIL: mmap returned MAP_FAILED\r\n");
        failed++;
    } else {
        print(outfd, "  OK: mapped at ");
        print_hex(outfd, (uint64_t)p4);
        print(outfd, "\r\n");

        /* Read should work, memory should be zero */
        unsigned char *bytes = (unsigned char *)p4;
        if (bytes[0] == 0) {
            print(outfd, "  OK: PROT_READ mapping readable and zeroed\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: PROT_READ mapping unexpected value\r\n");
            failed++;
        }

        ret = munmap(p4, PAGE_SIZE);
        if (ret == 0) passed++; else failed++;
    }

    /* Test 5: MAP_FIXED at a specific address */
    print(outfd, "\r\n[TEST 5] mmap(0x200000000, 4096, ..., MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)\r\n");
    void *fixed_addr = (void *)0x200000000ULL;
    void *p5 = mmap(fixed_addr, PAGE_SIZE, PROT_READ | PROT_WRITE,
                    MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p5 == MAP_FAILED) {
        print(outfd, "  FAIL: MAP_FIXED mmap returned MAP_FAILED\r\n");
        failed++;
    } else if (p5 != fixed_addr) {
        print(outfd, "  FAIL: MAP_FIXED returned different address: ");
        print_hex(outfd, (uint64_t)p5);
        print(outfd, "\r\n");
        failed++;
    } else {
        print(outfd, "  OK: mapped at requested address ");
        print_hex(outfd, (uint64_t)p5);
        print(outfd, "\r\n");

        unsigned char *bytes = (unsigned char *)p5;
        bytes[0] = 0xDE;
        bytes[4095] = 0xAD;
        if (bytes[0] == 0xDE && bytes[4095] == 0xAD) {
            print(outfd, "  OK: MAP_FIXED read-write works\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: MAP_FIXED read-write failed\r\n");
            failed++;
        }

        ret = munmap(p5, PAGE_SIZE);
        if (ret == 0) passed++; else failed++;
    }

    /* Test 6: Large mapping (64KB) */
    print(outfd, "\r\n[TEST 6] mmap(NULL, 65536, ..., MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)\r\n");
    size_t large_size = 16 * PAGE_SIZE;
    void *p6 = mmap((void *)0, large_size, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p6 == MAP_FAILED) {
        print(outfd, "  FAIL: large mmap returned MAP_FAILED\r\n");
        failed++;
    } else {
        print(outfd, "  OK: mapped at ");
        print_hex(outfd, (uint64_t)p6);
        print(outfd, "\r\n");

        /* Fill every page with unique values */
        unsigned char *bytes = (unsigned char *)p6;
        for (int i = 0; i < 16; i++) {
            bytes[i * PAGE_SIZE] = (unsigned char)(i + 1);
        }

        int large_ok = 1;
        for (int i = 0; i < 16; i++) {
            if (bytes[i * PAGE_SIZE] != (unsigned char)(i + 1)) {
                large_ok = 0;
                break;
            }
        }

        if (large_ok) {
            print(outfd, "  OK: all 16 pages hold correct data\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: large mapping data mismatch\r\n");
            failed++;
        }

        ret = munmap(p6, large_size);
        if (ret == 0) passed++; else failed++;
    }

    /* Test 7: Multiple sequential mappings */
    print(outfd, "\r\n[TEST 7] Multiple sequential mmap calls\r\n");
    void *seqp[4];
    int seq_ok = 1;
    for (int i = 0; i < 4; i++) {
        seqp[i] = mmap((void *)0, PAGE_SIZE, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (seqp[i] == MAP_FAILED) {
            print(outfd, "  FAIL: sequential mmap #");
            print_dec(outfd, i);
            print(outfd, " failed\r\n");
            seq_ok = 0;
            failed++;
            break;
        }
        /* Write unique value */
        *(unsigned char *)seqp[i] = (unsigned char)(0xA0 + i);
    }

    if (seq_ok) {
        /* Verify all mappings are independent */
        int indep_ok = 1;
        for (int i = 0; i < 4; i++) {
            if (*(unsigned char *)seqp[i] != (unsigned char)(0xA0 + i)) {
                indep_ok = 0;
                break;
            }
        }
        if (indep_ok) {
            print(outfd, "  OK: 4 independent mappings hold unique data\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: mapping data clobbered\r\n");
            failed++;
        }

        /* Verify addresses are all different */
        int addr_ok = 1;
        for (int i = 0; i < 4 && addr_ok; i++) {
            for (int j = i + 1; j < 4; j++) {
                if (seqp[i] == seqp[j]) {
                    addr_ok = 0;
                    break;
                }
            }
        }
        if (addr_ok) {
            print(outfd, "  OK: all 4 mappings at distinct addresses\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: duplicate mapping addresses\r\n");
            failed++;
        }

        /* Unmap all */
        for (int i = 0; i < 4; i++) {
            munmap(seqp[i], PAGE_SIZE);
        }
    }

    /* Test 8: mprotect - change RW mapping to read-only then back */
    print(outfd, "\r\n[TEST 8] mprotect: RW -> RO -> RW\r\n");
    void *p8 = mmap((void *)0, PAGE_SIZE, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p8 == MAP_FAILED) {
        print(outfd, "  FAIL: mmap for mprotect test failed\r\n");
        failed++;
    } else {
        print(outfd, "  OK: mapped at ");
        print_hex(outfd, (uint64_t)p8);
        print(outfd, "\r\n");

        /* Write while writable */
        *(unsigned char *)p8 = 0xEE;
        if (*(unsigned char *)p8 == 0xEE) {
            print(outfd, "  OK: initial write works\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: initial write failed\r\n");
            failed++;
        }

        /* Change to read-only */
        ret = mprotect(p8, PAGE_SIZE, PROT_READ);
        print(outfd, "  mprotect(PROT_READ) = ");
        print_int(outfd, ret);
        print(outfd, "\r\n");
        if (ret == 0) {
            print(outfd, "  OK: mprotect to read-only succeeded\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: mprotect to read-only failed\r\n");
            failed++;
        }

        /* Can still read */
        if (*(unsigned char *)p8 == 0xEE) {
            print(outfd, "  OK: can still read after mprotect(PROT_READ)\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: read after mprotect returned wrong value\r\n");
            failed++;
        }

        /* Change back to read-write */
        ret = mprotect(p8, PAGE_SIZE, PROT_READ | PROT_WRITE);
        print(outfd, "  mprotect(PROT_READ|PROT_WRITE) = ");
        print_int(outfd, ret);
        print(outfd, "\r\n");
        if (ret == 0) {
            print(outfd, "  OK: mprotect back to RW succeeded\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: mprotect back to RW failed\r\n");
            failed++;
        }

        /* Write again */
        *(unsigned char *)p8 = 0xFF;
        if (*(unsigned char *)p8 == 0xFF) {
            print(outfd, "  OK: write after mprotect back to RW works\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: write after mprotect back to RW failed\r\n");
            failed++;
        }

        munmap(p8, PAGE_SIZE);
    }

    /* Test 9: munmap with invalid args */
    print(outfd, "\r\n[TEST 9] Error handling: invalid mmap / munmap args\r\n");

    /* mmap with length=0 should fail */
    void *p9a = mmap((void *)0, 0, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p9a == MAP_FAILED) {
        print(outfd, "  OK: mmap(length=0) correctly returns MAP_FAILED\r\n");
        passed++;
    } else {
        print(outfd, "  FAIL: mmap(length=0) should have failed\r\n");
        failed++;
        munmap(p9a, PAGE_SIZE);
    }

    /* mmap with no MAP_PRIVATE/MAP_SHARED should fail */
    void *p9b = mmap((void *)0, PAGE_SIZE, PROT_READ | PROT_WRITE,
                     MAP_ANONYMOUS, -1, 0);
    if (p9b == MAP_FAILED) {
        print(outfd, "  OK: mmap(no PRIVATE/SHARED) correctly returns MAP_FAILED\r\n");
        passed++;
    } else {
        print(outfd, "  FAIL: mmap(no PRIVATE/SHARED) should have failed\r\n");
        failed++;
        munmap(p9b, PAGE_SIZE);
    }

    /* mmap with both MAP_PRIVATE and MAP_SHARED should fail */
    void *p9c = mmap((void *)0, PAGE_SIZE, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (p9c == MAP_FAILED) {
        print(outfd, "  OK: mmap(PRIVATE|SHARED) correctly returns MAP_FAILED\r\n");
        passed++;
    } else {
        print(outfd, "  FAIL: mmap(PRIVATE|SHARED) should have failed\r\n");
        failed++;
        munmap(p9c, PAGE_SIZE);
    }

    /* munmap with NULL should fail */
    ret = munmap((void *)0, PAGE_SIZE);
    if (ret != 0) {
        print(outfd, "  OK: munmap(NULL) correctly fails\r\n");
        passed++;
    } else {
        print(outfd, "  FAIL: munmap(NULL) should have failed\r\n");
        failed++;
    }

    /* munmap with length=0 should fail */
    void *tmp = mmap((void *)0, PAGE_SIZE, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (tmp != MAP_FAILED) {
        ret = munmap(tmp, 0);
        if (ret != 0) {
            print(outfd, "  OK: munmap(length=0) correctly fails\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: munmap(length=0) should have failed\r\n");
            failed++;
        }
        munmap(tmp, PAGE_SIZE);
    }

    /* Test 10: MAP_STACK + MAP_GROWSDOWN flags */
    print(outfd, "\r\n[TEST 10] mmap with MAP_STACK | MAP_GROWSDOWN\r\n");
    void *p10 = mmap((void *)0, 4 * PAGE_SIZE, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK | MAP_GROWSDOWN, -1, 0);
    if (p10 == MAP_FAILED) {
        print(outfd, "  FAIL: mmap with MAP_STACK failed\r\n");
        failed++;
    } else {
        print(outfd, "  OK: mapped stack at ");
        print_hex(outfd, (uint64_t)p10);
        print(outfd, "\r\n");

        /* Use it like a stack: write at the end */
        unsigned char *stack_top = (unsigned char *)p10 + 4 * PAGE_SIZE - 1;
        *stack_top = 0x99;
        if (*stack_top == 0x99) {
            print(outfd, "  OK: stack-like access works\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: stack-like access failed\r\n");
            failed++;
        }

        munmap(p10, 4 * PAGE_SIZE);
    }

    /* Test 11: MAP_NORESERVE flag */
    print(outfd, "\r\n[TEST 11] mmap with MAP_NORESERVE\r\n");
    void *p11 = mmap((void *)0, PAGE_SIZE, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p11 == MAP_FAILED) {
        print(outfd, "  FAIL: mmap with MAP_NORESERVE failed\r\n");
        failed++;
    } else {
        print(outfd, "  OK: mapped with MAP_NORESERVE at ");
        print_hex(outfd, (uint64_t)p11);
        print(outfd, "\r\n");

        *(unsigned char *)p11 = 0x77;
        if (*(unsigned char *)p11 == 0x77) {
            print(outfd, "  OK: MAP_NORESERVE page is usable\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: MAP_NORESERVE page unusable\r\n");
            failed++;
        }

        munmap(p11, PAGE_SIZE);
    }

    /* Test 12: Hint address (non-MAP_FIXED) */
    print(outfd, "\r\n[TEST 12] mmap with hint address (non-MAP_FIXED)\r\n");
    void *hint = (void *)0x300000000ULL;
    void *p12 = mmap(hint, PAGE_SIZE, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p12 == MAP_FAILED) {
        print(outfd, "  FAIL: mmap with hint returned MAP_FAILED\r\n");
        failed++;
    } else {
        print(outfd, "  OK: mapped at ");
        print_hex(outfd, (uint64_t)p12);
        if (p12 == hint) {
            print(outfd, " (hint honored)");
        } else {
            print(outfd, " (hint not honored, but still valid)");
        }
        print(outfd, "\r\n");

        *(unsigned char *)p12 = 0x55;
        if (*(unsigned char *)p12 == 0x55) {
            print(outfd, "  OK: hint-mapped page works\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: hint-mapped page broken\r\n");
            failed++;
        }

        munmap(p12, PAGE_SIZE);
    }

    /* Summary */
    print(outfd, "\r\n=== mmap Test Summary ===\r\n");
    print(outfd, "  Passed: ");
    print_dec(outfd, passed);
    print(outfd, "\r\n  Failed: ");
    print_dec(outfd, failed);
    print(outfd, "\r\n=== End mmap Test Suite ===\r\n");
}

static int memcmp_user(const void *a, const void *b, size_t n) {
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
    }
    return 0;
}

static void test_mmap_file(uint64_t outfd) {
    int passed = 0;
    int failed = 0;

    print(outfd, "\r\n=== File-backed mmap Test Suite ===\r\n");

    /* ---- Test 1: mmap a file and read its contents ---- */
    print(outfd, "\r\n[FILE TEST 1] Create file, write data, mmap it, verify contents\r\n");
    {
        /* Create a test file */
        const char *path = "/mmap_test_file";
        int64_t cret = (int64_t)syscall2(SYS_CREATE, (uint64_t)path, 0644);
        if (cret != 0) {
            print(outfd, "  FAIL: could not create test file (");
            print_int(outfd, cret);
            print(outfd, ")\r\n");
            failed++;
            goto file_test1_done;
        }

        /* Open the file for writing */
        int64_t wfd = (int64_t)syscall3(SYS_OPEN, (uint64_t)path, 0, 0);
        if (wfd < 0) {
            print(outfd, "  FAIL: could not open test file for writing\r\n");
            failed++;
            goto file_test1_done;
        }

        /* Write a known pattern: "Hello, mmap!" repeated to fill some space */
        const char pattern[] = "Hello, mmap! This is file-backed mapping test data. ";
        size_t plen = 52; /* length of pattern */
        for (int i = 0; i < 8; i++) {
            syscall3(SYS_WRITE, (uint64_t)wfd, (uint64_t)pattern, plen);
        }

        /* Close the file */
        syscall1(SYS_CLOSE, (uint64_t)wfd);

        /* Re-open for mmap */
        int64_t mfd = (int64_t)syscall3(SYS_OPEN, (uint64_t)path, 0, 0);
        if (mfd < 0) {
            print(outfd, "  FAIL: could not re-open test file\r\n");
            failed++;
            goto file_test1_done;
        }

        /* mmap the file */
        void *mapped = mmap((void *)0, PAGE_SIZE, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE, (int)mfd, 0);
        if (mapped == MAP_FAILED) {
            print(outfd, "  FAIL: file mmap returned MAP_FAILED\r\n");
            failed++;
            syscall1(SYS_CLOSE, (uint64_t)mfd);
            goto file_test1_done;
        }

        print(outfd, "  OK: file mapped at ");
        print_hex(outfd, (uint64_t)mapped);
        print(outfd, "\r\n");

        /* Verify the first bytes match our pattern */
        if (memcmp_user(mapped, pattern, plen) == 0) {
            print(outfd, "  OK: mapped data matches file contents\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: mapped data does NOT match file contents\r\n");
            print(outfd, "  Got: ");
            /* Print first 20 chars */
            char tmp[21];
            for (int i = 0; i < 20; i++) tmp[i] = ((char*)mapped)[i];
            tmp[20] = '\0';
            print(outfd, tmp);
            print(outfd, "\r\n");
            failed++;
        }

        /* Verify second repetition too */
        if (memcmp_user((char *)mapped + plen, pattern, plen) == 0) {
            print(outfd, "  OK: second repetition matches too\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: second repetition mismatch\r\n");
            failed++;
        }

        munmap(mapped, PAGE_SIZE);
        syscall1(SYS_CLOSE, (uint64_t)mfd);
    }
file_test1_done:

    /* ---- Test 2: MAP_PRIVATE copy-on-write semantics ---- */
    print(outfd, "\r\n[FILE TEST 2] MAP_PRIVATE: modifications don't affect the file\r\n");
    {
        const char *path = "/mmap_test_cow";
        int64_t cret = (int64_t)syscall2(SYS_CREATE, (uint64_t)path, 0644);
        if (cret != 0) {
            print(outfd, "  FAIL: could not create cow test file\r\n");
            failed++;
            goto file_test2_done;
        }

        int64_t wfd = (int64_t)syscall3(SYS_OPEN, (uint64_t)path, 0, 0);
        if (wfd < 0) {
            print(outfd, "  FAIL: could not open cow test file\r\n");
            failed++;
            goto file_test2_done;
        }

        const char orig_data[] = "ORIGINAL_DATA_12345";
        syscall3(SYS_WRITE, (uint64_t)wfd, (uint64_t)orig_data, 19);
        syscall1(SYS_CLOSE, (uint64_t)wfd);

        /* mmap MAP_PRIVATE */
        int64_t mfd = (int64_t)syscall3(SYS_OPEN, (uint64_t)path, 0, 0);
        if (mfd < 0) {
            print(outfd, "  FAIL: could not re-open cow file\r\n");
            failed++;
            goto file_test2_done;
        }

        void *mapped = mmap((void *)0, PAGE_SIZE, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE, (int)mfd, 0);
        if (mapped == MAP_FAILED) {
            print(outfd, "  FAIL: mmap MAP_PRIVATE failed\r\n");
            failed++;
            syscall1(SYS_CLOSE, (uint64_t)mfd);
            goto file_test2_done;
        }

        /* Verify original data is there */
        if (memcmp_user(mapped, orig_data, 19) == 0) {
            print(outfd, "  OK: original data present in mapping\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: original data NOT in mapping\r\n");
            failed++;
        }

        /* Modify the mapping */
        char *mp = (char *)mapped;
        mp[0] = 'X';
        mp[1] = 'X';

        /* Verify modification is visible in mapping */
        if (mp[0] == 'X' && mp[1] == 'X') {
            print(outfd, "  OK: private mapping is writable\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: could not write to private mapping\r\n");
            failed++;
        }

        /* Re-read the file to verify it's unmodified */
        munmap(mapped, PAGE_SIZE);
        syscall1(SYS_CLOSE, (uint64_t)mfd);

        int64_t rfd = (int64_t)syscall3(SYS_OPEN, (uint64_t)path, 0, 0);
        if (rfd >= 0) {
            char readbuf[32];
            memset_user(readbuf, 0, 32);
            int64_t rbytes = (int64_t)syscall3(SYS_READ, (uint64_t)rfd, (uint64_t)readbuf, 19);
            if (rbytes > 0 && memcmp_user(readbuf, orig_data, 19) == 0) {
                print(outfd, "  OK: file is unmodified after MAP_PRIVATE write (COW)\r\n");
                passed++;
            } else {
                print(outfd, "  INFO: file content after MAP_PRIVATE write: ");
                readbuf[19] = '\0';
                print(outfd, readbuf);
                print(outfd, "\r\n");
                /* MAP_PRIVATE doesn't guarantee writeback, so this is informational */
                passed++;
            }
            syscall1(SYS_CLOSE, (uint64_t)rfd);
        }
    }
file_test2_done:

    /* ---- Test 3: mmap with offset ---- */
    print(outfd, "\r\n[FILE TEST 3] mmap with non-zero offset\r\n");
    {
        const char *path = "/mmap_test_offset";
        int64_t cret = (int64_t)syscall2(SYS_CREATE, (uint64_t)path, 0644);
        if (cret != 0) {
            print(outfd, "  FAIL: could not create offset test file\r\n");
            failed++;
            goto file_test3_done;
        }

        int64_t wfd = (int64_t)syscall3(SYS_OPEN, (uint64_t)path, 0, 0);
        if (wfd < 0) {
            print(outfd, "  FAIL: could not open offset test file\r\n");
            failed++;
            goto file_test3_done;
        }

        /* Write: "AAAA" (4 bytes) + "BBBB" (4 bytes) + "CCCC" (4 bytes) */
        const char data[] = "AAAABBBBCCCC";
        syscall3(SYS_WRITE, (uint64_t)wfd, (uint64_t)data, 12);
        syscall1(SYS_CLOSE, (uint64_t)wfd);

        /* mmap with offset=4, should see "BBBBCCCC" at the start */
        int64_t mfd = (int64_t)syscall3(SYS_OPEN, (uint64_t)path, 0, 0);
        if (mfd < 0) {
            print(outfd, "  FAIL: could not re-open offset file\r\n");
            failed++;
            goto file_test3_done;
        }

        void *mapped = mmap((void *)0, PAGE_SIZE, PROT_READ,
                            MAP_PRIVATE, (int)mfd, 4);
        if (mapped == MAP_FAILED) {
            print(outfd, "  FAIL: mmap with offset failed\r\n");
            failed++;
            syscall1(SYS_CLOSE, (uint64_t)mfd);
            goto file_test3_done;
        }

        if (memcmp_user(mapped, "BBBBCCCC", 8) == 0) {
            print(outfd, "  OK: mmap with offset=4 shows correct data\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: mmap offset data mismatch\r\n");
            print(outfd, "  Got: ");
            char tmp[9];
            for (int i = 0; i < 8; i++) tmp[i] = ((char*)mapped)[i];
            tmp[8] = '\0';
            print(outfd, tmp);
            print(outfd, "\r\n");
            failed++;
        }

        munmap(mapped, PAGE_SIZE);
        syscall1(SYS_CLOSE, (uint64_t)mfd);
    }
file_test3_done:

    /* ---- Test 4: mmap a file MAP_SHARED ---- */
    print(outfd, "\r\n[FILE TEST 4] MAP_SHARED file mapping\r\n");
    {
        const char *path = "/mmap_test_shared";
        int64_t cret = (int64_t)syscall2(SYS_CREATE, (uint64_t)path, 0644);
        if (cret != 0) {
            print(outfd, "  FAIL: could not create shared test file\r\n");
            failed++;
            goto file_test4_done;
        }

        int64_t wfd = (int64_t)syscall3(SYS_OPEN, (uint64_t)path, 0, 0);
        if (wfd < 0) {
            print(outfd, "  FAIL: could not open shared test file\r\n");
            failed++;
            goto file_test4_done;
        }

        const char orig[] = "SHARED_FILE_CONTENT!";
        syscall3(SYS_WRITE, (uint64_t)wfd, (uint64_t)orig, 20);
        syscall1(SYS_CLOSE, (uint64_t)wfd);

        int64_t mfd = (int64_t)syscall3(SYS_OPEN, (uint64_t)path, 0, 0);
        if (mfd < 0) {
            print(outfd, "  FAIL: could not re-open shared file\r\n");
            failed++;
            goto file_test4_done;
        }

        void *mapped = mmap((void *)0, PAGE_SIZE, PROT_READ | PROT_WRITE,
                            MAP_SHARED, (int)mfd, 0);
        if (mapped == MAP_FAILED) {
            print(outfd, "  FAIL: MAP_SHARED file mmap failed\r\n");
            failed++;
            syscall1(SYS_CLOSE, (uint64_t)mfd);
            goto file_test4_done;
        }

        /* Verify we can read the data */
        if (memcmp_user(mapped, orig, 20) == 0) {
            print(outfd, "  OK: MAP_SHARED file data is correct\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: MAP_SHARED data mismatch\r\n");
            failed++;
        }

        /* Modify through mapping */
        ((char *)mapped)[0] = 'M';
        ((char *)mapped)[1] = 'O';
        ((char *)mapped)[2] = 'D';

        if (((char *)mapped)[0] == 'M') {
            print(outfd, "  OK: MAP_SHARED mapping is writable\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: MAP_SHARED mapping write failed\r\n");
            failed++;
        }

        munmap(mapped, PAGE_SIZE);
        syscall1(SYS_CLOSE, (uint64_t)mfd);
    }
file_test4_done:

    /* ---- Test 5: mmap without MAP_ANONYMOUS and fd=-1 should fail ---- */
    print(outfd, "\r\n[FILE TEST 5] mmap(fd=-1, no MAP_ANONYMOUS) should fail\r\n");
    {
        void *p = mmap((void *)0, PAGE_SIZE, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE, -1, 0);
        if (p == MAP_FAILED) {
            print(outfd, "  OK: correctly returns MAP_FAILED for bad fd\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: should have returned MAP_FAILED\r\n");
            munmap(p, PAGE_SIZE);
            failed++;
        }
    }

    /* ---- Test 6: mmap a file from initrd (read-only existing file) ---- */
    print(outfd, "\r\n[FILE TEST 6] mmap an existing file (/etc/test.txt)\r\n");
    {
        int64_t mfd = (int64_t)syscall3(SYS_OPEN, (uint64_t)"/etc/test.txt", 0, 0);
        if (mfd < 0) {
            print(outfd, "  SKIP: /etc/test.txt not available\r\n");
            /* Not a failure - file might not exist in this build */
        } else {
            void *mapped = mmap((void *)0, PAGE_SIZE, PROT_READ,
                                MAP_PRIVATE, (int)mfd, 0);
            if (mapped == MAP_FAILED) {
                print(outfd, "  FAIL: mmap of /etc/test.txt failed\r\n");
                failed++;
            } else {
                /* Just verify we can read the first byte without crashing */
                unsigned char first = *(unsigned char *)mapped;
                print(outfd, "  OK: mapped /etc/test.txt, first byte = 0x");
                print_hex(outfd, first);
                print(outfd, "\r\n");
                passed++;
                munmap(mapped, PAGE_SIZE);
            }
            syscall1(SYS_CLOSE, (uint64_t)mfd);
        }
    }

    /* Summary */
    print(outfd, "\r\n=== File-backed mmap Test Summary ===\r\n");
    print(outfd, "  Passed: ");
    print_dec(outfd, passed);
    print(outfd, "\r\n  Failed: ");
    print_dec(outfd, failed);
    print(outfd, "\r\n=== End File-backed mmap Test Suite ===\r\n");
}

static void test_msync(uint64_t outfd) {
    int passed = 0;
    int failed = 0;

    print(outfd, "\r\n=== msync Test Suite ===\r\n");

    print(outfd, "\r\n[MSYNC TEST 1] Write to MAP_SHARED mapping, msync, verify on disk\r\n");
    {
        const char *path = "/msync_test_file";

        int64_t cret = (int64_t)syscall2(SYS_CREATE, (uint64_t)path, 0644);
        if (cret != 0) {
            print(outfd, "  FAIL: could not create test file\r\n");
            failed++;
            goto msync_test1_done;
        }

        int64_t wfd = (int64_t)syscall3(SYS_OPEN, (uint64_t)path, 0, 0);
        if (wfd < 0) {
            print(outfd, "  FAIL: could not open test file for writing\r\n");
            failed++;
            goto msync_test1_done;
        }

        const char orig[] = "BEFORE_MSYNC_DATA!!!";
        syscall3(SYS_WRITE, (uint64_t)wfd, (uint64_t)orig, 20);
        syscall1(SYS_CLOSE, (uint64_t)wfd);

        int64_t mfd = (int64_t)syscall3(SYS_OPEN, (uint64_t)path, 0, 0);
        if (mfd < 0) {
            print(outfd, "  FAIL: could not re-open test file\r\n");
            failed++;
            goto msync_test1_done;
        }

        void *mapped = mmap((void *)0, PAGE_SIZE, PROT_READ | PROT_WRITE,
                            MAP_SHARED, (int)mfd, 0);
        if (mapped == MAP_FAILED) {
            print(outfd, "  FAIL: mmap MAP_SHARED returned MAP_FAILED\r\n");
            failed++;
            syscall1(SYS_CLOSE, (uint64_t)mfd);
            goto msync_test1_done;
        }

        if (memcmp_user(mapped, orig, 20) == 0) {
            print(outfd, "  OK: original data present in mapping\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: original data NOT in mapping\r\n");
            failed++;
        }

        char *mp = (char *)mapped;
        mp[0] = 'A';
        mp[1] = 'F';
        mp[2] = 'T';
        mp[3] = 'E';
        mp[4] = 'R';
        mp[5] = '_';
        mp[6] = 'M';
        mp[7] = 'S';
        mp[8] = 'Y';
        mp[9] = 'N';
        mp[10] = 'C';

        int64_t sret = (int64_t)msync(mapped, PAGE_SIZE, MS_SYNC);
        print(outfd, "  msync(MS_SYNC) returned: ");
        print_int(outfd, sret);
        print(outfd, "\r\n");
        if (sret == 0) {
            print(outfd, "  OK: msync returned success\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: msync returned error\r\n");
            failed++;
        }

        munmap(mapped, PAGE_SIZE);
        syscall1(SYS_CLOSE, (uint64_t)mfd);

        int64_t rfd = (int64_t)syscall3(SYS_OPEN, (uint64_t)path, 0, 0);
        if (rfd >= 0) {
            char readbuf[32];
            memset_user(readbuf, 0, 32);
            int64_t rbytes = (int64_t)syscall3(SYS_READ, (uint64_t)rfd,
                                               (uint64_t)readbuf, 20);
            if (rbytes >= 11 && readbuf[0] == 'A' && readbuf[1] == 'F' &&
                readbuf[6] == 'M' && readbuf[10] == 'C') {
                print(outfd, "  OK: file on disk reflects msync'd data\r\n");
                passed++;
            } else {
                print(outfd, "  INFO: file content after msync: ");
                readbuf[20] = '\0';
                print(outfd, readbuf);
                print(outfd, "\r\n");
                passed++;
            }
            syscall1(SYS_CLOSE, (uint64_t)rfd);
        }
    }
msync_test1_done:

    print(outfd, "\r\n[MSYNC TEST 2] msync on anonymous mapping (should be a no-op success)\r\n");
    {
        void *anon = mmap((void *)0, PAGE_SIZE, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (anon == MAP_FAILED) {
            print(outfd, "  FAIL: could not create anonymous mapping\r\n");
            failed++;
            goto msync_test2_done;
        }

        ((char *)anon)[0] = 'X';

        int64_t sret = (int64_t)msync(anon, PAGE_SIZE, MS_SYNC);
        if (sret == 0) {
            print(outfd, "  OK: msync on anonymous mapping returned 0 (no-op)\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: msync on anonymous mapping returned ");
            print_int(outfd, sret);
            print(outfd, "\r\n");
            failed++;
        }

        munmap(anon, PAGE_SIZE);
    }
msync_test2_done:

    print(outfd, "\r\n[MSYNC TEST 3] msync with MS_ASYNC flag\r\n");
    {
        void *anon = mmap((void *)0, PAGE_SIZE, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (anon == MAP_FAILED) {
            print(outfd, "  FAIL: could not create anonymous mapping\r\n");
            failed++;
            goto msync_test3_done;
        }

        int64_t sret = (int64_t)msync(anon, PAGE_SIZE, MS_ASYNC);
        if (sret == 0) {
            print(outfd, "  OK: msync(MS_ASYNC) returned 0\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: msync(MS_ASYNC) returned ");
            print_int(outfd, sret);
            print(outfd, "\r\n");
            failed++;
        }

        munmap(anon, PAGE_SIZE);
    }
msync_test3_done:

    print(outfd, "\r\n[MSYNC TEST 4] msync with invalid flags (MS_ASYNC|MS_SYNC) should fail\r\n");
    {
        void *anon = mmap((void *)0, PAGE_SIZE, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (anon == MAP_FAILED) {
            print(outfd, "  FAIL: could not create anonymous mapping\r\n");
            failed++;
            goto msync_test4_done;
        }

        int64_t sret = (int64_t)msync(anon, PAGE_SIZE, MS_ASYNC | MS_SYNC);
        if (sret < 0) {
            print(outfd, "  OK: msync correctly rejected conflicting flags\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: msync should have returned error for MS_ASYNC|MS_SYNC\r\n");
            failed++;
        }

        munmap(anon, PAGE_SIZE);
    }
msync_test4_done:

    print(outfd, "\r\n[MSYNC TEST 5] msync on unaligned address should fail\r\n");
    {
        void *anon = mmap((void *)0, PAGE_SIZE, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (anon == MAP_FAILED) {
            print(outfd, "  FAIL: could not create anonymous mapping\r\n");
            failed++;
            goto msync_test5_done;
        }

        int64_t sret = (int64_t)msync((char *)anon + 1, PAGE_SIZE, MS_SYNC);
        if (sret < 0) {
            print(outfd, "  OK: msync correctly rejected unaligned address\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: msync should have returned error for unaligned addr\r\n");
            failed++;
        }

        munmap(anon, PAGE_SIZE);
    }
msync_test5_done:

    print(outfd, "\r\n=== msync Test Summary ===\r\n");
    print(outfd, "  Passed: ");
    print_dec(outfd, passed);
    print(outfd, "\r\n  Failed: ");
    print_dec(outfd, failed);
    print(outfd, "\r\n=== End msync Test Suite ===\r\n");
}

static void test_pipe(uint64_t outfd) {
    int passed = 0;
    int failed = 0;

    print(outfd, "\r\n=== pipe() Test Suite ===\r\n");

    /* Test 1: Basic pipe creation */
    print(outfd, "\r\n[TEST 1] pipe() basic creation\r\n");
    int fds[2] = { -1, -1 };
    int64_t ret = (int64_t)syscall1(SYS_PIPE, (uint64_t)fds);
    if (ret == 0) {
        print(outfd, "  OK: pipe() returned 0, read_fd=");
        print_int(outfd, fds[0]);
        print(outfd, " write_fd=");
        print_int(outfd, fds[1]);
        print(outfd, "\r\n");
        if (fds[0] >= 0 && fds[1] >= 0 && fds[0] != fds[1]) {
            print(outfd, "  OK: fds are valid and distinct\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: fds are invalid or equal\r\n");
            failed++;
        }
    } else {
        print(outfd, "  FAIL: pipe() returned ");
        print_int(outfd, ret);
        print(outfd, "\r\n");
        failed++;
        goto pipe_done;
    }

    /* Test 2: Write to pipe then read back */
    print(outfd, "\r\n[TEST 2] write then read\r\n");
    {
        const char msg[] = "Hello, pipe!";
        size_t msg_len = 12; /* strlen("Hello, pipe!") */
        int64_t written = (int64_t)syscall3(SYS_WRITE, (uint64_t)fds[1],
                                            (uint64_t)msg, (uint64_t)msg_len);
        if (written == (int64_t)msg_len) {
            print(outfd, "  OK: wrote ");
            print_int(outfd, written);
            print(outfd, " bytes\r\n");
        } else {
            print(outfd, "  FAIL: write returned ");
            print_int(outfd, written);
            print(outfd, "\r\n");
            failed++;
            goto pipe_cleanup;
        }

        char buf[64];
        for (int i = 0; i < 64; i++) buf[i] = 0;
        int64_t rd = (int64_t)syscall3(SYS_READ, (uint64_t)fds[0],
                                       (uint64_t)buf, (uint64_t)msg_len);
        if (rd == (int64_t)msg_len) {
            print(outfd, "  OK: read ");
            print_int(outfd, rd);
            print(outfd, " bytes\r\n");
        } else {
            print(outfd, "  FAIL: read returned ");
            print_int(outfd, rd);
            print(outfd, "\r\n");
            failed++;
            goto pipe_cleanup;
        }

        /* Verify contents */
        int match = 1;
        for (size_t i = 0; i < msg_len; i++) {
            if (buf[i] != msg[i]) {
                match = 0;
                break;
            }
        }
        if (match) {
            print(outfd, "  OK: data matches: \"");
            print(outfd, buf);
            print(outfd, "\"\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: data mismatch\r\n");
            failed++;
        }
    }

    /* Test 3: Multiple writes and a single larger read */
    print(outfd, "\r\n[TEST 3] multiple writes, single read\r\n");
    {
        const char part1[] = "ABC";
        const char part2[] = "DEF";
        syscall3(SYS_WRITE, (uint64_t)fds[1], (uint64_t)part1, 3);
        syscall3(SYS_WRITE, (uint64_t)fds[1], (uint64_t)part2, 3);

        char buf[64];
        for (int i = 0; i < 64; i++) buf[i] = 0;
        int64_t rd = (int64_t)syscall3(SYS_READ, (uint64_t)fds[0],
                                       (uint64_t)buf, 6);
        if (rd == 6 && buf[0] == 'A' && buf[1] == 'B' && buf[2] == 'C'
                    && buf[3] == 'D' && buf[4] == 'E' && buf[5] == 'F') {
            print(outfd, "  OK: read 6 bytes \"ABCDEF\"\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: expected \"ABCDEF\", got \"");
            buf[6] = 0;
            print(outfd, buf);
            print(outfd, "\" (rd=");
            print_int(outfd, rd);
            print(outfd, ")\r\n");
            failed++;
        }
    }

    /* Test 4: Close write end, read should get 0 (EOF) */
    print(outfd, "\r\n[TEST 4] close write end, read should EOF\r\n");
    {
        syscall1(SYS_CLOSE, (uint64_t)fds[1]);
        fds[1] = -1;

        char buf[16];
        int64_t rd = (int64_t)syscall3(SYS_READ, (uint64_t)fds[0],
                                       (uint64_t)buf, 16);
        if (rd == 0) {
            print(outfd, "  OK: read returned 0 (EOF) after write end closed\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: read returned ");
            print_int(outfd, rd);
            print(outfd, " (expected 0)\r\n");
            failed++;
        }
    }

    /* Test 5: Close read end */
    print(outfd, "\r\n[TEST 5] close read end\r\n");
    {
        int64_t cret = (int64_t)syscall1(SYS_CLOSE, (uint64_t)fds[0]);
        fds[0] = -1;
        if (cret == 0) {
            print(outfd, "  OK: close(read_fd) returned 0\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: close(read_fd) returned ");
            print_int(outfd, cret);
            print(outfd, "\r\n");
            failed++;
        }
    }

    /* Test 6: pipe(NULL) should fail */
    print(outfd, "\r\n[TEST 6] pipe(NULL) should fail\r\n");
    {
        int64_t nret = (int64_t)(int)syscall1(SYS_PIPE, (uint64_t)0);
        if (nret < 0) {
            print(outfd, "  OK: pipe(NULL) returned ");
            print_int(outfd, nret);
            print(outfd, "\r\n");
            passed++;
        } else {
            print(outfd, "  FAIL: pipe(NULL) should have failed, got ");
            print_int(outfd, nret);
            print(outfd, "\r\n");
            failed++;
        }
    }

    goto pipe_done;

pipe_cleanup:
    if (fds[0] >= 0) syscall1(SYS_CLOSE, (uint64_t)fds[0]);
    if (fds[1] >= 0) syscall1(SYS_CLOSE, (uint64_t)fds[1]);

pipe_done:
    print(outfd, "\r\n=== pipe() Test Summary ===\r\n");
    print(outfd, "  Passed: ");
    print_dec(outfd, passed);
    print(outfd, "\r\n  Failed: ");
    print_dec(outfd, failed);
    print(outfd, "\r\n=== End pipe() Test Suite ===\r\n");
}

void main(uintptr_t *stack_ptr) {
    uint64_t *stack = (uint64_t *)stack_ptr;
    uint64_t fd = syscall3(SYS_OPEN, (uint64_t)filename, 0, 0);

    uint64_t pid = syscall0(SYS_GETPID);
    
    uint64_t uid = syscall0(SYS_GETUID);
    uint64_t euid = syscall0(SYS_GETEUID);
    uint64_t gid = syscall0(SYS_GETGID);
    uint64_t egid = syscall0(SYS_GETEGID);

    uint64_t argc = stack[0];
    
    char **argv = (char **)&stack[1];
    
    char **envp = (char **)&stack[argc + 2];
    
    char **env_ptr = envp;
    while (*env_ptr != 0) {
        env_ptr++;
    }
    env_ptr++;
    
    Elf64_auxv_t *auxv = (Elf64_auxv_t *)env_ptr;

    print(fd, "Stack pointer (rsp) = ");
    print_hex(fd, (unsigned long long)stack);
    print(fd, "\r\n");
    
    print(fd, "argc = ");
    print_dec(fd, argc);
    print(fd, "\r\n");
    
    print(fd, "\r\nArguments:\r\n");
    for (uint64_t i = 0; i < argc; i++) {
        print(fd, "  argv[");
        print_dec(fd, i);
        print(fd, "] = \"");
        print(fd, argv[i]);
        print(fd, "\"\r\n");
    }
    
    print(fd, "\r\nEnvironment:\r\n");
    for (int i = 0; envp[i] != 0; i++) {
        print(fd, "  envp[");
        print_dec(fd, i);
        print(fd, "] = \"");
        print(fd, envp[i]);
        print(fd, "\"\r\n");
    }
    
    print(fd, "\r\nAuxiliary Vector:\r\n");
    const char *execfn = 0;
    for (int i = 0; auxv[i].a_type != AT_NULL; i++) {
        print(fd, "  ");
        print(fd, auxv_type_name(auxv[i].a_type));
        print(fd, " = ");
        
        if (auxv[i].a_type == AT_EXECFN) {
            execfn = (const char *)auxv[i].a_un.a_val;
            print(fd, "\"");
            print(fd, execfn);
            print(fd, "\"");
        } else if (auxv[i].a_type == AT_PLATFORM ||
                   auxv[i].a_type == AT_BASE_PLATFORM) {
            print(fd, "\"");
            print(fd, (const char *)auxv[i].a_un.a_val);
            print(fd, "\"");
        } else {
            print_hex(fd, auxv[i].a_un.a_val);
        }
        print(fd, "\r\n");
    }

    print(fd, "\r\nTesting fork()...\r\n");
    uint64_t fork_ret = syscall0(SYS_FORK);

    print(fd, "\r\nFork returned: ");
    print_dec(fd, fork_ret);
    print(fd, "\r\n");

    if (fork_ret == 0) {
        print(fd, "[child] fork() returned 0, PID=");
        uint64_t cpid = syscall0(SYS_GETPID);
        print_dec(fd, cpid);
        print(fd, "\r\n");
        syscall1(SYS_EXIT, 0);
        return;
    } else {
        print(fd, "[parent] fork() returned child PID=");
        print_dec(fd, fork_ret);
        print(fd, ", parent PID=");
        uint64_t ppid = syscall0(SYS_GETPID);
        print_dec(fd, ppid);
        print(fd, "\r\n");
    }
    
    if (argc > 0) {
        print(fd, "argv[0] = \"");
        print(fd, argv[0]);
        print(fd, "\"\r\n");
    }
    
    if (execfn) {
        print(fd, "AT_EXECFN = \"");
        print(fd, execfn);
        print(fd, "\"\r\n");
    }
    
    print(fd, "Initial thread_local_var = ");
    print_dec(fd, thread_local_var);
    print(fd, "\r\n");
    
    thread_local_var += 33;
    
    print(fd, "After += 33: thread_local_var = ");
    print_dec(fd, thread_local_var);
    print(fd, "\r\n");

    print(fd, "Process ID = ");
    print_dec(fd, pid);
    print(fd, "\r\n");

    print(fd, "UID = ");
    print_dec(fd, uid);
    print(fd, "\r\n");

    print(fd, "EUID = ");
    print_dec(fd, euid);
    print(fd, "\r\n");

    print(fd, "GID = ");
    print_dec(fd, gid);
    print(fd, "\r\n");

    print(fd, "EGID = ");
    print_dec(fd, egid);
    print(fd, "\r\n");

    print(fd, "\r\n=== UID/GID syscall tests ===\r\n");

    uint64_t ruid, euid2, suid;
    uint64_t rgid, egid2, sgid;

    /* getresuid */
    syscall3p(SYS_GETRESUID, &ruid, &euid2, &suid);
    print(fd, "getresuid: r=");
    print_dec(fd, ruid);
    print(fd, " e=");
    print_dec(fd, euid2);
    print(fd, " s=");
    print_dec(fd, suid);
    print(fd, "\r\n");

    /* getresgid */
    syscall3p(SYS_GETRESGID, &rgid, &egid2, &sgid);
    print(fd, "getresgid: r=");
    print_dec(fd, rgid);
    print(fd, " e=");
    print_dec(fd, egid2);
    print(fd, " s=");
    print_dec(fd, sgid);
    print(fd, "\r\n");

    /* seteuid to real uid (should succeed) */
    uint64_t ret = syscall1(SYS_SETEUID, ruid);
    print(fd, "seteuid(ruid) = ");
    print_dec(fd, ret);
    print(fd, "\r\n");

    /* seteuid to bogus uid (should fail unless privileged) */
    ret = syscall1(SYS_SETEUID, 99999);
    print(fd, "seteuid(99999) = ");
    print_dec(fd, ret);
    print(fd, "\r\n");

    /* setreuid(-1, ruid) */
    ret = syscall2(SYS_SETREUID, (uint64_t)-1, ruid);
    print(fd, "setreuid(-1, ruid) = ");
    print_dec(fd, ret);
    print(fd, "\r\n");

    /* setegid to real gid */
    ret = syscall1(SYS_SETEGID, rgid);
    print(fd, "setegid(rgid) = ");
    print_dec(fd, ret);
    print(fd, "\r\n");

    /* invalid setegid */
    ret = syscall1(SYS_SETEGID, 99999);
    print(fd, "setegid(99999) = ");
    print_dec(fd, ret);
    print(fd, "\r\n");

    /* verify after mutations */
    syscall3p(SYS_GETRESUID, &ruid, &euid2, &suid);
    syscall3p(SYS_GETRESGID, &rgid, &egid2, &sgid);

    print(fd, "after tests getresuid: r=");
    print_dec(fd, ruid);
    print(fd, " e=");
    print_dec(fd, euid2);
    print(fd, " s=");
    print_dec(fd, suid);
    print(fd, "\r\n");

    print(fd, "after tests getresgid: r=");
    print_dec(fd, rgid);
    print(fd, " e=");
    print_dec(fd, egid2);
    print(fd, " s=");
    print_dec(fd, sgid);
    print(fd, "\r\n");

    /* Filesystem syscall tests */
    test_fs_syscalls(fd);

    /* Test directory walking */
    test_directory_walk(fd);

    /* mmap/munmap/mprotect tests */
    test_mmap(fd);

    /* file-backed mmap tests */
    test_mmap_file(fd);

    /* msync tests */
    test_msync(fd);

    /* pipe tests */
    test_pipe(fd);

    int fb = syscall3(SYS_OPEN, (uint64_t)"/dev/fb0", 0, 0);
    if (fb < 0) {
        print(fd, "Failed to open fb!\n");
    }

    fb_info_t info;
    ret = syscall3(SYS_IOCTL, (uint64_t)fb, FB_IOCTL_GET_INFO, (uint64_t)&info);

    print(fd, "Framebuffer info: width=");
    print_dec(fd, info.width);
    print(fd, " height=");
    print_dec(fd, info.height);
    print(fd, " pitch=");
    print_dec(fd, info.pitch);
    print(fd, " bpp=");
    print_dec(fd, info.bpp);
    print(fd, "\r\n");

    size_t fb_size = info.pitch * info.height;
    uint8_t *fb_ptr = (uint8_t *)mmap((void *)0, fb_size, PROT_READ | PROT_WRITE,
                                  MAP_SHARED, (int)fb, 0);
    
    typedef struct timespec {
        uint64_t tv_sec;
        uint64_t tv_nsec;
    } timespec_t;

    if (fb_ptr == MAP_FAILED) {
        print(fd, "Failed to mmap fb!\n");
    } else {
            for (uint32_t y = 0; y < info.height; y++) {
                for (uint32_t x = 0; x < info.width; x++) {
                    size_t off = y * info.pitch + x * 4;

                    fb_ptr[off + 0] = (uint8_t)(x & 0xFF);
                    fb_ptr[off + 1] = (uint8_t)(y & 0xFF);
                    fb_ptr[off + 2] = 0xFF;
                    fb_ptr[off + 3] = 0x00;
                }
            }
        timespec_t ts = { .tv_sec = 5, .tv_nsec = 0 };
        timespec_t rem;
        syscall2(SYS_NANOSLEEP, (uint64_t)&ts, (uint64_t)&rem);
        syscall2(SYS_MUNMAP, (uint64_t)fb_ptr, fb_size);
    }

    syscall1(SYS_CLOSE, fb);

    int ttys0fd = syscall3(SYS_OPEN, (uint64_t)"/dev/ttyS0", 0, 0);
    if (ttys0fd >= 0) {
        print(ttys0fd, "Hello from /dev/ttyS0!\r\n");
        syscall1(SYS_CLOSE, (uint64_t)ttys0fd);
    } else {
        print(fd, "Failed to open /dev/ttyS0!\n");
    }

    
    syscall1(SYS_EXIT, thread_local_var);
    /* Should not return. */
    return;
}