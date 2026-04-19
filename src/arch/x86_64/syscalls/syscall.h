#ifndef SYSCALL_H
#define SYSCALL_H 1

#include "cpu.h"
#include "types.h"
#include "uaccess.h"
#include "system/sleep.h"
#include "fs/vfs/vfs.h"
#include <ipc/signals.h>

#define SYS_exit       0
#define SYS_open       1
#define SYS_read       2
#define SYS_write      3
#define SYS_close      4
#define SYS_ioctl      5
#define SYS_seek       6
#define SYS_fcntl      7
#define SYS_dup        8
#define SYS_getpid     9
#define SYS_getuid     10
#define SYS_geteuid    11
#define SYS_getgid     12
#define SYS_getegid    13
#define SYS_setuid     14
#define SYS_seteuid    15
#define SYS_setreuid   16
#define SYS_setresuid  17
#define SYS_getresuid  18
#define SYS_setgid     19
#define SYS_setegid    20
#define SYS_setregid   21
#define SYS_setresgid  22
#define SYS_getresgid  23
#define SYS_fork       24
#define SYS_mount      25
#define SYS_umount     26
#define SYS_opendir    27
#define SYS_readdir    28
#define SYS_closedir   29
#define SYS_mkdir      30
#define SYS_create     31
#define SYS_rmdir      32
#define SYS_remove     33
#define SYS_symlink    34
#define SYS_readlink   35
#define SYS_mmap       36
#define SYS_munmap     37
#define SYS_mprotect   38
#define SYS_msync      39
#define SYS_pipe       40
#define SYS_nanosleep  41
#define SYS_execve     42
#define SYS_waitpid    43
#define SYS_wait4      44
#define SYS_getppid    45
#define SYS_getpgrp    46
#define SYS_setpgid    47
#define SYS_getpgid    48
#define SYS_setsid     49
#define SYS_getsid     50
#define SYS_settls     51
#define SYS_poll       52
#define SYS_getcwd     53
#define SYS_futex_wait 54
#define SYS_futex_wake 55
#define SYS_chdir      56
#define SYS_stat       57
#define SYS_setstat    58
#define SYS_getfdpath  59
#define SYS_rt_sigaction 60
#define SYS_rt_sigprocmask 61
#define SYS_rt_sigpending 62
#define SYS_rt_sigsuspend 63
#define SYS_rt_sigtimedwait 64
#define SYS_rt_sigreturn 65
#define SYS_kill 66
#define SYS_tgkill 67
#define SYS_rt_sigqueueinfo 68

void set_syscall_context(registers_t *ctx);
registers_t *get_syscall_context(void);

void sys_exit(int status);
int sys_open(const char __user *path, int flags, mode_t mode);
int sys_read(int fd, char __user *buf, int count);
int sys_write(int fd, const char __user *buf, int count);
int sys_close(int fd);
int sys_ioctl(int fd, int request, void *arg);
int sys_seek(int fd, int whence, int offset);
int sys_fcntl(int fd, int op, void *arg);
int sys_dup(int fd, int newfd);
int sys_getpid(void);
int sys_getuid(void);
int sys_geteuid(void);
int sys_getgid(void);
int sys_getegid(void);
int sys_setuid(uid_t uid);
int sys_seteuid(uid_t euid);
int sys_setreuid(uid_t ruid, uid_t euid);
int sys_setresuid(uid_t ruid, uid_t euid, uid_t suid);
int sys_getresuid(uid_t __user *ruid, uid_t __user *euid, uid_t __user *suid);
int sys_setgid(gid_t gid);
int sys_setegid(gid_t egid);
int sys_setregid(gid_t rgid, gid_t egid);
int sys_setresgid(gid_t rgid, gid_t egid, gid_t sgid);
int sys_getresgid(gid_t __user *rgid, gid_t __user *egid, gid_t __user *sgid);
int sys_fork(void);
int sys_mount(const char __user *device, const char __user *fstype, const char __user *path, int flags, void __user *data);
int sys_umount(const char __user *path);
int sys_opendir(const char __user *path);
int sys_readdir(int fd, void __user *buf, size_t max_size);
int sys_closedir(int fd);
int sys_mkdir(const char __user *path, int mode);
int sys_create(const char __user *path, mode_t mode);
int sys_rmdir(const char __user *path);
int sys_remove(const char __user *path);
int sys_symlink(const char __user *target, const char __user *linkpath);
int sys_readlink(const char __user *path, char __user *buf, size_t size);
long sys_mmap(void __user *addr, size_t length, int prot, int flags, int fd, off_t offset);
int sys_munmap(void __user *addr, size_t length);
int sys_mprotect(void __user *addr, size_t length, int prot);
int sys_msync(void __user *addr, size_t length, int flags);
int sys_pipe(int *user_fds);
int sys_nanosleep(const void __user *req, void __user *rem);
int64_t sys_waitpid(int pid, int __user *status, int options);
int64_t sys_wait4(int pid, int __user *status, int options, void __user *rusage);
int64_t sys_getppid(void);
int64_t sys_getpgrp(void);
int64_t sys_setpgid(int pid, int pgid);
int64_t sys_getpgid(int pid);
int64_t sys_setsid(void);
int64_t sys_getsid(int pid);
int sys_settls(void __user *tlsptr);
int sys_poll(void __user *fds, size_t nfds, int timeout_ms);
int sys_getcwd(void __user *buf, size_t size);
int sys_futex_wait(int __user* uaddr, int expected, const struct timespec __user *timeout);
int sys_futex_wake(int __user *uaddr, bool wake_all);
int sys_chdir(const char __user *path);
int sys_stat(const char __user *path, struct stat __user *statbuf);
int sys_setstat(const char __user *path, const struct stat __user *statbuf);
int sys_getfdpath(int fd, char __user *buf, size_t size);
int sys_rt_sigaction(int sig, const user_sigaction_t *act, user_sigaction_t *oact, size_t sigsetsize);
int sys_rt_sigprocmask(int how, const user_sigset_t *set, user_sigset_t *oldset, size_t sigsetsize);
int sys_rt_sigpending(user_sigset_t *set, size_t sigsetsize);
int sys_rt_sigsuspend(const user_sigset_t *set, size_t sigsetsize);
int sys_rt_sigtimedwait(const user_sigset_t *set, user_siginfo_t *info,
	const timespec_t *timeout, size_t sigsetsize);
int sys_rt_sigreturn(void);
int sys_kill(int pid, int sig);
int sys_tgkill(int tgid, int tid, int sig);
int sys_rt_sigqueueinfo(int pid, int sig, const user_siginfo_t *info);

#endif // SYSCALL_H