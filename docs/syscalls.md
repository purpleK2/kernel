# PurpleK2 Syscalls

Some docs for systemcalls in the PurpleK2 kernel.

## General information

System calls can be triggered using either the `syscall` instruction or with interrupt `0x80` (which can be triggered using `int 0x80` in assembly). Arguments are passed to the kernel using the ABI below. There isnt a need for any stack alignment.

## Argument Passing / ABI

- The **Syscall Number** is passed using the **`RAX`** register
- The **Syscall Return** value is also passed using the **`RAX`** register
- The **1. Syscall Argument** is passed using the **`RDI`** register
- The **2. Syscall Argument** is passed using the **`RSI`** register
- The **3. Syscall Argument** is passed using the **`RDX`** register
- The **4. Syscall Argument** is passed using the **`R8`** register
- The **5. Syscall Argument** is passed using the **`R9`** register
- The **5. Syscall Argument** is passed using the **`R10`** register

## Syscall Table

```c
#define SYS_exit 0
#define SYS_open 1
#define SYS_read 2
#define SYS_write 3
#define SYS_close 4
#define SYS_ioctl 5
#define SYS_seek 6
#define SYS_fcntl 7
#define SYS_dup 8
#define SYS_getpid 9
#define SYS_getuid 10
#define SYS_geteuid 11
#define SYS_getgid 12
#define SYS_getegid 13
#define SYS_setuid 14
#define SYS_seteuid 15
#define SYS_setreuid 16
#define SYS_setresuid 17
#define SYS_getresuid 18
#define SYS_setgid 19
#define SYS_setegid 20
#define SYS_setregid 21
#define SYS_setresgid 22
#define SYS_getresgid 23
#define SYS_fork 24
#define SYS_mmap 36
#define SYS_munmap 37
#define SYS_mprotect 38
#define SYS_msync 39
#define SYS_pipe 40
#define SYS_nanosleep 41
#define SYS_execve 42
#define SYS_waitpid 43
#define SYS_wait4 44
#define SYS_getppid 45
#define SYS_getpgrp 46
#define SYS_setpgid 47
#define SYS_getpgid 48
#define SYS_setsid 49
#define SYS_getsid 50
```

A complete table of all syscalls

| Syscall Name | Syscall Description                                  | Syscall Number (%rax) | Return Value (%rax)              | Arg 1 (%rdi)   | Arg 2 (%rsi)   | Arg 3 (%rdx)  | Arg 4 (%r8)   | Arg 5 (%r9) | Arg 6 (%r10)  |
| ------------ | ---------------------------------------------------- | --------------------- | -------------------------------- | -------------- | -------------- | ------------- | ------------- | ----------- | ------------- |
| exit         | Exits the current process                            | 0                     | /                                | int exit_code  | /              | /             | /             | /           | /             |
| open         | Opens a file from the VFS                            | 1                     | int fd or -1                     | char\* path    | int flags      | int mode      | /             | /           | /             |
| read         | Reads from a file descriptor                         | 2                     | int bytes_read or -1             | int fd         | void\* buffer  | int size      | /             | /           | /             |
| write        | Writes to a file descriptor                          | 3                     | int bytes_written or -1          | int fd         | void\* buffer  | int size      | /             | /           | /             |
| close        | Closes a file descriptor                             | 4                     | int (0 on success, -1 on error)  | int fd         | /              | /             | /             | /           | /             |
| ioctl        | Performs device-specific control                     | 5                     | int result                       | int fd         | int request    | void\* arg    | /             | /           | /             |
| seek         | Changes file offset                                  | 6                     | int new_offset or -1             | int fd         | int whence     | int offset    | /             | /           | /             |
| fcntl        | Manipulates file descriptor                          | 7                     | int result                       | int fd         | int op         | void\* arg    | /             | /           | /             |
| dup          | Duplicates a file descriptor                         | 8                     | int new_fd or -1                 | int old_fd     | /              | /             | /             | /           | /             |
| getpid       | Returns the calling process ID                       | 9                     | pid_t pid                        | /              | /              | /             | /             | /           | /             |
| getuid       | Returns real user ID                                 | 10                    | uid_t uid                        | /              | /              | /             | /             | /           | /             |
| geteuid      | Returns effective user ID                            | 11                    | uid_t euid                       | /              | /              | /             | /             | /           | /             |
| getgid       | Returns real group ID                                | 12                    | gid_t gid                        | /              | /              | /             | /             | /           | /             |
| getegid      | Returns effective group ID                           | 13                    | gid_t egid                       | /              | /              | /             | /             | /           | /             |
| setuid       | Sets real/effective/saved UID per POSIX rules        | 14                    | int (0 or -1)                    | uid_t uid      | /              | /             | /             | /           | /             |
| seteuid      | Sets effective UID                                   | 15                    | int (0 or -1)                    | uid_t euid     | /              | /             | /             | /           | /             |
| setreuid     | Sets real and/or effective UID                       | 16                    | int (0 or -1)                    | uid_t ruid     | uid_t euid     | /             | /             | /           | /             |
| setresuid    | Sets real, effective, and saved UID                  | 17                    | int (0 or -1)                    | uid_t ruid     | uid_t euid     | uid_t suid    | /             | /           | /             |
| getresuid    | Gets real, effective, and saved UID                  | 18                    | int (0 or -1)                    | uid_t\* ruid   | uid_t\* euid   | uid_t\* suid  | /             | /           | /             |
| setgid       | Sets real/effective/saved GID per POSIX rules        | 19                    | int (0 or -1)                    | gid_t gid      | /              | /             | /             | /           | /             |
| setegid      | Sets effective GID                                   | 20                    | int (0 or -1)                    | gid_t egid     | /              | /             | /             | /           | /             |
| setregid     | Sets real and/or effective GID                       | 21                    | int (0 or -1)                    | gid_t rgid     | gid_t egid     | /             | /             | /           | /             |
| setresgid    | Sets real, effective, and saved GID                  | 22                    | int (0 or -1)                    | gid_t rgid     | gid_t egid     | gid_t sgid    | /             | /           | /             |
| getresgid    | Gets real, effective, and saved GID                  | 23                    | int (0 or -1)                    | gid_t\* rgid   | gid_t\* egid   | gid_t\* sgid  | /             | /           | /             |
| fork         | Creates a new process by duplicating caller          | 24                    | pid_t (0 in child, >0 in parent) | /              | /              | /             | /             | /           | /             |
| mmap         | Maps memory into the process address space           | 36                    | void\* addr or MAP_FAILED        | void\* addr    | size_t length  | int prot      | int flags     | int fd      | size_t offset |
| munmap       | Unmaps a memory mapping                              | 37                    | int (0 or -1)                    | void\* addr    | size_t length  | /             | /             | /           | /             |
| mprotect     | Changes protection on a memory mapping               | 38                    | int (0 or -1)                    | void\* addr    | size_t length  | int prot      | /             | /           | /             |
| msync        | Syncs memory-mapped file region to disk              | 39                    | int (0 or -1)                    | void\* addr    | size_t length  | int flags     | /             | /           | /             |
| pipe         | Creates a unidirectional data channel                | 40                    | int (0 or -1)                    | int\* pipefd   | /              | /             | /             | /           | /             |
| nanosleep    | Suspends execution for a specified time              | 41                    | int (0 or -1)                    | timespec\* req | timespec\* rem | /             | /             | /           | /             |
| execve       | Replaces process image with new program              | 42                    | int (-1 on error, no return)     | char\* path    | char\*\* argv  | char\*\* envp | /             | /           | /             |
| waitpid      | Waits for a child process to change state            | 43                    | pid_t or -1                      | pid_t pid      | int\* status   | int options   | /             | /           | /             |
| wait4        | Waits for child with resource usage (rusage ignored) | 44                    | pid_t or -1                      | pid_t pid      | int\* status   | int options   | void\* rusage | /           | /             |
| getppid      | Returns the parent process ID                        | 45                    | pid_t ppid                       | /              | /              | /             | /             | /           | /             |
| getpgrp      | Returns process group ID of calling process          | 46                    | pid_t pgid                       | /              | /              | /             | /             | /           | /             |
| setpgid      | Sets process group ID of a process                   | 47                    | int (0 or -1)                    | pid_t pid      | pid_t pgid     | /             | /             | /           | /             |
| getpgid      | Returns process group ID of a process                | 48                    | pid_t pgid or -1                 | pid_t pid      | /              | /             | /             | /           | /             |
| setsid       | Creates a new session                                | 49                    | pid_t sid or -1                  | /              | /              | /             | /             | /           | /             |
| getsid       | Returns session ID of a process                      | 50                    | pid_t sid or -1                  | pid_t pid      | /              | /             | /             | /           | /             |
| settls       | Sets the address of the thread local storage         | 51                    | int (0)                          | void\* tlsaddr | /              | /             | /             | /           | /             |
