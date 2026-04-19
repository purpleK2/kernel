#include "syscall.h"
#include "cpu.h"
#include "errors.h"
#include "ipc/pipe.h"
#include "ipc/poll.h"
#include "paging/paging.h"
#include "scheduler/execve.h"
#include "structures/futex.h"
#include "uaccess.h"
#include "user/user.h"

#include <fs/fd.h>
#include <fs/file_io.h>
#include <fs/vfs/vfs.h>
#include <scheduler/scheduler.h>

#include <memory/heap/kheap.h>
#include <memory/mmap.h>

#include <system/sleep.h>

#include <stdint.h>
#include <string.h>
#include <system/stdio.h>
#include <util/macro.h>

static registers_t *current_syscall_ctx[256];

void set_syscall_context(registers_t *ctx) {
    current_syscall_ctx[get_current_cpu()] = ctx;
}

registers_t *get_syscall_context(void) {
    return current_syscall_ctx[get_current_cpu()];
}

void sys_exit(int status) {
    registers_t *ctx = get_syscall_context();

    debugf_debug("sys_exit called with status %d\n", status);

    proc_exit(status);
    yield(ctx);
}

int sys_open(const char __user *path, int flags, mode_t mode) {
    pcb_t *current = get_current_pcb();

    if (!path || !current) {
        return -EINVAL;
    }

    char kpath[4096];
    if (copy_from_user(kpath, path, sizeof(kpath)) != 0) {
        return -EFAULT;
    }
    kpath[sizeof(kpath) - 1] = '\0';

    fileio_t *file = open(kpath, flags, mode);
    if (!file) {
        return -1;
    }

    int fd = fd_alloc(&current->fd_table, FD_FILE, file);
    if (fd < 0) {
        close(file);
        return -fd;
    }

    return fd;
}

int sys_read(int fd, char __user *buf, int count) {
    pcb_t *current = get_current_pcb();

    if (fd < 0 || !buf || count <= 0) {
        return -EINVAL;
    }

    fileio_t *file = fd_get(&current->fd_table, fd, FD_FILE);
    if (!file) {
        return -EBADF;
    }

    char *kbuf = kmalloc(count);
    if (!kbuf) {
        return -EFAULT;
    }

    int bytes_read = read(file, count, kbuf);
    if (bytes_read < 0) {
        kfree(kbuf);
        return -EIO;
    }

    if (copy_to_user(buf, kbuf, bytes_read) != 0) {
        kfree(kbuf);
        return -EFAULT;
    }

    kfree(kbuf);
    return bytes_read;
}

int sys_write(int fd, const char __user *buf, int count) {
    pcb_t *current = get_current_pcb();

    if (fd < 0 || !buf || count <= 0) {
        return -1;
    }

    fileio_t *file = fd_get(&current->fd_table, fd, FD_FILE);
    if (!file) {
        return -EBADF;
    }

    char *kbuf = kmalloc(count);
    if (!kbuf) {
        return -EFAULT;
    }

    if (copy_from_user(kbuf, buf, count) != 0) {
        kfree(kbuf);
        return -EFAULT;
    }

    int bytes_written = write(file, kbuf, count);
    kfree(kbuf);
    return bytes_written;
}

int sys_close(int fd) {
    pcb_t *current = get_current_pcb();

    if (fd < 0) {
        return -EBADF;
    }

    if ((size_t)fd >= current->fd_table.size) {
        return -EBADF;
    }

    fd_entry_t *e = &current->fd_table.entries[fd];

    if (e->type == FD_NONE) {
        return -EBADF;
    }

    if (e->type == FD_DIR) {
        sys_closedir(fd);
    } else {
        close(e->ptr);
    }

    fd_free(&current->fd_table, fd);

    return 0;
}

int sys_ioctl(int fd, int request, void *arg) {
    pcb_t *current = get_current_pcb();

    if (fd < 0) {
        return -EBADF;
    }

    fileio_t *file = fd_get(&current->fd_table, fd, FD_FILE);
    if (!file) {
        return -EBADF;
    }

    return -vfs_ioctl(file->private, request, arg);
}

int sys_seek(int fd, int whence, int offset) {
    pcb_t *current = get_current_pcb();

    if (fd < 0) {
        return -EBADF;
    }

    fileio_t *file = fd_get(&current->fd_table, fd, FD_FILE);
    if (!file) {
        return -EBADF;
    }

    return -seek(file, whence, offset);
}

int sys_fcntl(int fd, int op, void *arg) {
    pcb_t *current = get_current_pcb();

    if (fd < 0) {
        return -1;
    }

    fileio_t *file = fd_get(&current->fd_table, fd, FD_FILE);
    if (!file) {
        return -EBADF;
    }

    return -fcntl(file, op, arg);
}

int sys_dup(int fd, int newfd) {
    pcb_t *current = get_current_pcb();
    if (fd < 0) {
        return -EFAULT;
    }

    fileio_t *file = fd_get(&current->fd_table, fd, FD_FILE);
    if (!file) {
        return -EBADF;
    }

    fileio_t *new_file = kmalloc(sizeof(fileio_t));
    if (!new_file) {
        return -ENOMEM;
    }
    memcpy(new_file, file, sizeof(fileio_t));

    int result_fd;
    if (newfd >= 0) {
        if ((size_t)newfd < current->fd_table.size &&
            current->fd_table.entries[newfd].type != FD_NONE) {
            sys_close(newfd);
        }

        while ((size_t)newfd >= current->fd_table.size) {
            size_t new_size =
                current->fd_table.size ? current->fd_table.size * 2 : 8;
            if (new_size <= (size_t)newfd) {
                new_size = (size_t)newfd + 1;
            }
            fd_entry_t *n = krealloc(current->fd_table.entries,
                                     new_size * sizeof(fd_entry_t));
            if (!n) {
                kfree(new_file);
                return -ENOMEM;
            }
            for (size_t i = current->fd_table.size; i < new_size; i++) {
                n[i].type = FD_NONE;
                n[i].ptr  = NULL;
            }
            current->fd_table.entries = n;
            current->fd_table.size    = new_size;
        }

        current->fd_table.entries[newfd].type = FD_FILE;
        current->fd_table.entries[newfd].ptr  = new_file;
        result_fd                             = newfd;
    } else {
        result_fd = fd_alloc(&current->fd_table, FD_FILE, new_file);
        if (result_fd < 0) {
            kfree(new_file);
            return -result_fd;
        }
    }

    return result_fd;
}

int sys_getpid(void) {
    pcb_t *current = get_current_pcb();
    if (!current) {
        return -1;
    }
    return current->pid;
}

int sys_fork(void) {
    registers_t *ctx = get_syscall_context();
    if (!ctx) {
        return -EFAULT;
    }

    int child_pid = proc_fork(ctx);

    if (child_pid < 0) {
        return -EFAULT;
    }

    ctx->rax = child_pid;
    return child_pid;
}

int sys_getuid(void) {
    return get_current_cred()->uid;
}
int sys_geteuid(void) {
    return get_current_cred()->euid;
}
int sys_getgid(void) {
    return get_current_cred()->gid;
}
int sys_getegid(void) {
    return get_current_cred()->egid;
}

int sys_getresuid(uid_t __user *ruid, uid_t __user *euid, uid_t __user *suid) {
    user_cred_t *c = get_current_cred();
    if (!ruid || !euid || !suid)
        return -1;

    if (copy_to_user(ruid, &c->uid, sizeof(uid_t)) != 0)
        return -EFAULT;
    if (copy_to_user(euid, &c->euid, sizeof(uid_t)) != 0)
        return -EFAULT;
    if (copy_to_user(suid, &c->suid, sizeof(uid_t)) != 0)
        return -EFAULT;

    return 0;
}

int sys_getresgid(gid_t __user *rgid, gid_t __user *egid, gid_t __user *sgid) {
    user_cred_t *c = get_current_cred();
    if (!rgid || !egid || !sgid)
        return -1;

    if (copy_to_user(rgid, &c->gid, sizeof(gid_t)) != 0)
        return -EFAULT;
    if (copy_to_user(egid, &c->egid, sizeof(gid_t)) != 0)
        return -EFAULT;
    if (copy_to_user(sgid, &c->sgid, sizeof(gid_t)) != 0)
        return -EFAULT;

    return 0;
}

int sys_setuid(uid_t uid) {
    user_cred_t *c = get_current_cred();

    if (is_privileged()) {
        c->uid  = uid;
        c->euid = uid;
        c->suid = uid;
        return 0;
    }

    if (uid == c->uid || uid == c->suid) {
        c->euid = uid;
        return 0;
    }

    return -EACCES;
}

int sys_seteuid(uid_t euid) {
    user_cred_t *c = get_current_cred();

    if (is_privileged() || euid == c->uid || euid == c->suid) {
        c->euid = euid;
        return 0;
    }

    return -EACCES;
}

int sys_setreuid(uid_t ruid, uid_t euid) {
    user_cred_t *c = get_current_cred();

    if (!is_privileged()) {
        if ((ruid != (uid_t)-1 && ruid != c->uid && ruid != c->euid) ||
            (euid != (uid_t)-1 && euid != c->uid && euid != c->euid &&
             euid != c->suid))
            return -EACCES;
    }

    if (ruid != (uid_t)-1)
        c->uid = ruid;
    if (euid != (uid_t)-1)
        c->euid = euid;

    if (ruid != (uid_t)-1 || euid != (uid_t)-1)
        c->suid = c->euid;

    return 0;
}

int sys_setresuid(uid_t ruid, uid_t euid, uid_t suid) {
    user_cred_t *c = get_current_cred();

    if (!is_privileged()) {
        if ((ruid != (uid_t)-1 && ruid != c->uid && ruid != c->euid &&
             ruid != c->suid) ||
            (euid != (uid_t)-1 && euid != c->uid && euid != c->euid &&
             euid != c->suid) ||
            (suid != (uid_t)-1 && suid != c->uid && suid != c->euid &&
             suid != c->suid))
            return -EACCES;
    }

    if (ruid != (uid_t)-1)
        c->uid = ruid;
    if (euid != (uid_t)-1)
        c->euid = euid;
    if (suid != (uid_t)-1)
        c->suid = suid;

    return 0;
}

int sys_setgid(gid_t gid) {
    user_cred_t *c = get_current_cred();

    if (is_privileged()) {
        c->gid  = gid;
        c->egid = gid;
        c->sgid = gid;
        return 0;
    }

    if (gid == c->gid || gid == c->sgid) {
        c->egid = gid;
        return 0;
    }

    return -EACCES;
}

int sys_setegid(gid_t egid) {
    user_cred_t *c = get_current_cred();

    if (is_privileged() || egid == c->gid || egid == c->sgid) {
        c->egid = egid;
        return 0;
    }

    return -EACCES;
}

int sys_setregid(gid_t rgid, gid_t egid) {
    user_cred_t *c = get_current_cred();

    if (!is_privileged()) {
        if ((rgid != (gid_t)-1 && rgid != c->gid && rgid != c->egid) ||
            (egid != (gid_t)-1 && egid != c->gid && egid != c->egid &&
             egid != c->sgid))
            return -EACCES;
    }

    if (rgid != (gid_t)-1)
        c->gid = rgid;
    if (egid != (gid_t)-1)
        c->egid = egid;

    if (rgid != (gid_t)-1 || egid != (gid_t)-1)
        c->sgid = c->egid;

    return 0;
}

int sys_setresgid(gid_t rgid, gid_t egid, gid_t sgid) {
    user_cred_t *c = get_current_cred();

    if (!is_privileged()) {
        if ((rgid != (gid_t)-1 && rgid != c->gid && rgid != c->egid &&
             rgid != c->sgid) ||
            (egid != (gid_t)-1 && egid != c->gid && egid != c->egid &&
             egid != c->sgid) ||
            (sgid != (gid_t)-1 && sgid != c->gid && sgid != c->egid &&
             sgid != c->sgid))
            return -EACCES;
    }

    if (rgid != (gid_t)-1)
        c->gid = rgid;
    if (egid != (gid_t)-1)
        c->egid = egid;
    if (sgid != (gid_t)-1)
        c->sgid = sgid;

    return 0;
}

int sys_mount(const char __user *device, const char __user *fstype,
              const char __user *path, int flags, void __user *data) {
    UNUSED(flags);

    char kernel_device[4096];
    char kernel_fstype[4096];
    char kernel_path[4096];
    char kernel_data[4096];

    size_t len =
        strncpy_from_user(kernel_device, device, sizeof(kernel_device));
    if (len == (size_t)-1) {
        return -EFAULT;
    }
    kernel_device[sizeof(kernel_device) - 1] = '\0';

    len = strncpy_from_user(kernel_fstype, fstype, sizeof(kernel_fstype));
    if (len == (size_t)-1) {
        return -EFAULT;
    }
    kernel_fstype[sizeof(kernel_fstype) - 1] = '\0';

    len = strncpy_from_user(kernel_path, path, sizeof(kernel_path));
    if (len == (size_t)-1) {
        return -EFAULT;
    }
    kernel_path[sizeof(kernel_path) - 1] = '\0';

    if (data) {
        if (strncpy_from_user(kernel_data, data, sizeof(kernel_data)) != 0) {
            return -EFAULT;
        }
    }

    vfs_t *ret_vfs =
        vfs_mount(kernel_device, kernel_fstype, kernel_path, kernel_data);
    if (!ret_vfs || !is_addr_mapped((uintptr_t)ret_vfs)) {
        return -EFAULT;
    }

    return 0;
}

int sys_umount(const char __user *path) {
    char kernel_path[4096];
    size_t len = strncpy_from_user(kernel_path, path, sizeof(kernel_path));
    if (len == (size_t)-1) {
        return -1;
    }
    kernel_path[sizeof(kernel_path) - 1] = '\0';

    return -vfs_unmount(kernel_path);
}

int sys_opendir(const char __user *path) {
    char kernel_path[4096];
    if (strncpy_from_user(kernel_path, path, sizeof(kernel_path)) < 0) {
        return -EFAULT;
    }
    kernel_path[sizeof(kernel_path) - 1] = '\0';

    vnode_t *vn;
    int ret = vfs_lookup(kernel_path, &vn);
    if (ret != EOK) {
        return -ret;
    }

    if (vn->vtype != VNODE_DIR) {
        vnode_unref(vn);
        return -ENOTDIR;
    }

    size_t cap     = 256;
    dirent_t *ents = kmalloc(sizeof(dirent_t) * cap);

    size_t count = cap;

    ret = vfs_readdir(vn, ents, &count);
    if (ret != EOK) {
        vnode_unref(vn);
        kfree(ents);
        return -ret;
    }

    dir_handle_t *dh    = kmalloc(sizeof(dir_handle_t));
    dh->vnode           = vn;
    dh->entries         = ents;
    dh->count           = count;
    dh->index           = 0;
    dh->syscall_ret_num = 1;

    int fd = fd_alloc(&get_current_pcb()->fd_table, FD_DIR, dh);
    return fd;
}

int sys_readdir(int fd, void __user *buf, size_t max_size) {
    dir_handle_t *dh = fd_get(&get_current_pcb()->fd_table, fd, FD_DIR);
    if (!dh) {
        return -EBADF;
    }

    if (dh->index >= dh->count) {
        return 0;
    }

    size_t bytes_written = 0;
    char *out            = (char *)buf;

    while (dh->index < dh->count) {
        dirent_t *entry = &dh->entries[dh->index];

        size_t name_len   = strlen(entry->d_name);
        size_t entry_size = offsetof(dirent_t, d_name) + name_len + 1;

        entry_size = (entry_size + 7) & ~7UL;

        entry->d_reclen = entry_size;

        if (bytes_written + entry_size > max_size) {
            break;
        }

        if (copy_to_user(out + bytes_written, entry, entry_size) != 0) {
            return bytes_written > 0 ? (int)bytes_written : -EFAULT;
        }

        bytes_written += entry_size;
        dh->index++;
    }

    return (int)bytes_written;
}

int sys_closedir(int fd) {
    dir_handle_t *dh = fd_get(&get_current_pcb()->fd_table, fd, FD_DIR);
    if (!dh)
        return -EBADF;

    vnode_unref(dh->vnode);
    kfree(dh->entries);
    kfree(dh);
    fd_free(&get_current_pcb()->fd_table, fd);
    return 0;
}

int sys_mkdir(const char __user *path, int mode) {
    char kernel_path[4096];
    if (strncpy_from_user(kernel_path, path, sizeof(kernel_path)) < 0) {
        return -EFAULT;
    }
    kernel_path[sizeof(kernel_path) - 1] = '\0';

    return -vfs_mkdir(kernel_path, mode);
}

int sys_create(const char __user *path, mode_t mode) {
    char kernel_path[4096];
    if (strncpy_from_user(kernel_path, path, sizeof(kernel_path)) < 0) {
        return -EFAULT;
    }
    kernel_path[sizeof(kernel_path) - 1] = '\0';

    return -vfs_create(kernel_path, mode);
}

int sys_rmdir(const char __user *path) {
    char kernel_path[4096];
    if (strncpy_from_user(kernel_path, path, sizeof(kernel_path)) < 0) {
        return -EFAULT;
    }
    kernel_path[sizeof(kernel_path) - 1] = '\0';

    return -vfs_rmdir(kernel_path);
}

int sys_remove(const char __user *path) {
    char kernel_path[4096];
    if (strncpy_from_user(kernel_path, path, sizeof(kernel_path)) < 0) {
        return -EFAULT;
    }
    kernel_path[sizeof(kernel_path) - 1] = '\0';

    return -vfs_remove(kernel_path);
}

int sys_symlink(const char __user *target, const char __user *linkpath) {
    char kernel_target[4096];
    char kernel_linkpath[4096];

    if (strncpy_from_user(kernel_target, target, sizeof(kernel_target)) < 0) {
        return -EFAULT;
    }
    kernel_target[sizeof(kernel_target) - 1] = '\0';

    if (strncpy_from_user(kernel_linkpath, linkpath, sizeof(kernel_linkpath)) <
        0) {
        return -EFAULT;
    }
    kernel_linkpath[sizeof(kernel_linkpath) - 1] = '\0';

    return -vfs_symlink(kernel_target, kernel_linkpath);
}

int sys_readlink(const char __user *path, char __user *buf, size_t size) {
    char kernel_path[4096];
    if (strncpy_from_user(kernel_path, path, sizeof(kernel_path)) < 0) {
        return -EFAULT;
    }
    kernel_path[sizeof(kernel_path) - 1] = '\0';

    char kbuf[size];
    int ret = vfs_readlink(kernel_path, kbuf, sizeof(kbuf));
    if (ret < 0) {
        return -ret;
    }

    if (copy_to_user(buf, kbuf, ret) != 0) {
        return -EFAULT;
    }

    return ret;
}

long sys_mmap(void __user *addr, size_t length, int prot, int flags, int fd,
              off_t offset) {
    pcb_t *current = get_current_pcb();
    if (!current || !current->vmc) {
        return (long)(uintptr_t)MAP_FAILED;
    }

    vnode_t *vnode = NULL;

    if (!(flags & MAP_ANONYMOUS)) {
        if (fd < 0) {
            return (long)(uintptr_t)MAP_FAILED;
        }
        fileio_t *file = fd_get(&current->fd_table, fd, FD_FILE);
        if (!file || !file->private) {
            return (long)(uintptr_t)MAP_FAILED;
        }
        vnode = (vnode_t *)file->private;
    }

    void *result =
        do_mmap(current->vmc, addr, length, prot, flags, vnode, offset);
    return (long)(uintptr_t)result;
}

int sys_munmap(void __user *addr, size_t length) {
    pcb_t *current = get_current_pcb();
    if (!current || !current->vmc) {
        return -EFAULT;
    }

    return do_munmap(current->vmc, addr, length);
}

int sys_mprotect(void __user *addr, size_t length, int prot) {
    pcb_t *current = get_current_pcb();
    if (!current || !current->vmc) {
        return -EFAULT;
    }

    return do_mprotect(current->vmc, addr, length, prot);
}

int sys_msync(void __user *addr, size_t length, int flags) {
    pcb_t *current = get_current_pcb();
    if (!current || !current->vmc) {
        return -EFAULT;
    }

    return do_msync(current->vmc, addr, length, flags);
}

int sys_pipe(int *user_fds) {
    if (!user_fds) {
        return -EINVAL;
    }

    fileio_t *ends[2];
    int ret = pipe(ends);
    if (ret < 0) {
        return ret;
    }

    pcb_t *pcb = get_current_pcb();

    int fd_read  = -1;
    int fd_write = -1;

    fd_read = fd_alloc(&pcb->fd_table, FD_FILE, ends[0]);
    if (fd_read < 0) {
        goto fail;
    }
    fd_write = fd_alloc(&pcb->fd_table, FD_FILE, ends[1]);
    if (fd_write < 0) {
        goto fail;
    }

    int kfds[2] = {fd_read, fd_write};

    if (copy_to_user(user_fds, kfds, sizeof(kfds)) < 0)
        goto fail;

    return 0;
fail:
    if (fd_read >= 0)
        fd_free(&pcb->fd_table, fd_read);
    if (fd_write >= 0)
        fd_free(&pcb->fd_table, fd_write);

    pipe_close(ends[0]);
    pipe_close(ends[1]);
    return -EMFILE;
}

int sys_nanosleep(const void __user *req, void __user *rem) {
    if (!req)
        return -EINVAL;

    timespec_t kreq;
    if (copy_from_user(&kreq, req, sizeof(timespec_t)) != 0)
        return -EFAULT;

    timespec_t krem = {0, 0};
    int ret         = do_nanosleep(&kreq, &krem);

    if (rem && ret != 0) {
        copy_to_user(rem, &krem, sizeof(timespec_t));
    }

    return ret;
}

int64_t sys_waitpid(int pid, int __user *status, int options) {
    int64_t ret = do_waitpid(pid, status, options);
    return ret;
}

int64_t sys_wait4(int pid, int __user *status, int options,
                  void __user *rusage) {
    (void)rusage;
    return sys_waitpid(pid, status, options);
}

int64_t sys_getppid(void) {
    pcb_t *current = get_current_pcb();
    if (!current) {
        return -1;
    }
    if (!current->parent) {
        return 0;
    }
    return current->parent->pid;
}

int64_t sys_getpgrp(void) {
    pcb_t *current = get_current_pcb();
    if (!current) {
        return -EFAULT;
    }
    return current->pgid;
}

int64_t sys_setpgid(int pid, int pgid) {
    pcb_t *current = get_current_pcb();
    if (!current) {
        return -EFAULT;
    }

    pcb_t *target;

    if (pid == 0) {
        target = current;
    } else {
        target = pcb_lookup(pid);
        if (!target) {
            return -ESRCH;
        }

        if (target != current && target->parent != current) {
            return -ESRCH;
        }

        if (target->sid != current->sid) {
            return -EPERM;
        }
    }

    if (target->is_session_leader) {
        return -EPERM;
    }

    if (pgid == 0) {
        pgid = target->pid;
    }
    target->pgid = pgid;
    return 0;
}

int64_t sys_getpgid(int pid) {
    pcb_t *target;

    if (pid == 0) {
        target = get_current_pcb();
    } else {
        target = pcb_lookup(pid);
    }

    if (!target) {
        return -ESRCH;
    }

    return target->pgid;
}

int64_t sys_setsid(void) {
    pcb_t *current = get_current_pcb();
    if (!current) {
        return -EFAULT;
    }

    if (current->is_session_leader) {
        return -EPERM;
    }

    if (current->pgid == current->pid) {
        return -EPERM;
    }

    current->sid               = current->pid;
    current->pgid              = current->pid;
    current->is_session_leader = 1;

    return current->sid;
}

int64_t sys_getsid(int pid) {
    pcb_t *target;

    if (pid == 0) {
        target = get_current_pcb();
    } else {
        target = pcb_lookup(pid);
    }

    if (!target) {
        return -ESRCH;
    }

    return target->sid;
}

int sys_settls(void __user *tlsptr) {
    tcb_t *thread = get_current_tcb();
    if (!thread) {
        return -ESRCH;
    }

    thread->tls.base_virt = tlsptr;
    thread->tls_ptr       = (user_tls_t *)tlsptr;

    _cpu_set_msr(0xC0000100, (uint64_t)tlsptr);

    return 0;
}

int sys_poll(void __user *user_fds, size_t nfds, int timeout_ms) {
    if (nfds == 0)
        return 0;

    if (nfds > 1024)
        return -EINVAL;

    size_t fds_size = nfds * sizeof(pollfd_t);
    pollfd_t *kfds  = kmalloc(fds_size);
    if (!kfds)
        return -ENOMEM;

    if (copy_from_user(kfds, user_fds, fds_size) != 0) {
        kfree(kfds);
        return -EFAULT;
    }

    int ret = do_poll(kfds, nfds, timeout_ms);

    if (ret >= 0) {
        if (copy_to_user(user_fds, kfds, fds_size) != 0) {
            kfree(kfds);
            return -EFAULT;
        }
    }

    kfree(kfds);
    return ret;
}

int sys_getcwd(void __user *buf, size_t size) {
    if (!buf || size == 0) {
        return -EINVAL;
    }

    pcb_t *current = get_current_pcb();
    if (!current) {
        return -EFAULT;
    }

    // No cwd set yet — process is at root
    if (!current->cwd) {
        if (size < 2)
            return -ERANGE;
        char root[] = "/";
        if (copy_to_user(buf, root, 2) != 0)
            return -EFAULT;
        return 2;
    }

    vnode_t *vn = (vnode_t *)current->cwd->private;
    if (!vn) {
        return -EFAULT;
    }

    char kbuf[4096];
    memcpy(kbuf, vn->path, sizeof(kbuf));

    size_t path_len = strlen(kbuf) + 1;
    if (path_len > size) {
        return -ERANGE;
    }

    if (copy_to_user(buf, kbuf, path_len) != 0) {
        return -EFAULT;
    }

    return (int)path_len;
}

int sys_chdir(const char __user *path) {
    if (!path) {
        return -EINVAL;
    }

    char kpath[4096];
    if (strncpy_from_user(kpath, path, sizeof(kpath)) < 0) {
        return -EFAULT;
    }
    kpath[sizeof(kpath) - 1] = '\0';

    vnode_t *vn;
    int ret = vfs_lookup(kpath, &vn);
    if (ret != EOK) {
        return -ret;
    }

    if (vn->vtype != VNODE_DIR) {
        vnode_unref(vn);
        return -ENOTDIR;
    }

    pcb_t *current = get_current_pcb();
    if (!current) {
        vnode_unref(vn);
        return -EFAULT;
    }

    if (current->cwd) {
        close(current->cwd);
        current->cwd = NULL;
    }

    fileio_t *new_cwd = open(kpath, 0, 0);
    if (!new_cwd) {
        vnode_unref(vn);
        return -ENOENT;
    }

    vnode_unref(vn);
    current->cwd = new_cwd;
    return 0;
}

int sys_futex_wait(int __user *uaddr, int expected,
                   const struct timespec __user *timeout) {
    int val;

    if (copy_from_user(&val, uaddr, sizeof(int)) != 0) {
        return -EFAULT;
    }

    if (val != expected) {
        return -EAGAIN;
    }

    futex_t *f = futex_get((uintptr_t)uaddr);
    if (!f) {
        return -EFAULT;
    }

    if (timeout) {
        timespec_t kts;
        if (copy_from_user(&kts, timeout, sizeof(timespec_t)) != 0) {
            return -EFAULT;
        }

        uint64_t ticks  = kts.tv_sec * TICKS_PER_SEC;
        ticks          += (uint64_t)kts.tv_nsec / NS_PER_TICK;
        if (((uint64_t)kts.tv_nsec % NS_PER_TICK) != 0)
            ticks++;

        if (ticks == 0 && (kts.tv_sec > 0 || kts.tv_nsec > 0))
            ticks = 1;

        tcb_t *t       = get_current_tcb();
        t->wakeup_tick = get_ticks() + ticks;
    }

    waitqueue_sleep(&f->wq);

    return 0;
}

int sys_futex_wake(int __user *uaddr, bool wake_all) {
    futex_t *f = futex_get((uintptr_t)uaddr);

    int woken = 0;

    if (wake_all) {
        woken = waitqueue_wake_all(&f->wq);
    } else {
        woken = waitqueue_wake_one(&f->wq);
    }

    return woken;
}

int sys_stat(const char __user *path, struct stat __user *buf) {
    char kernel_path[4096];
    if (strncpy_from_user(kernel_path, path, sizeof(kernel_path)) < 0) {
        return -EFAULT;
    }
    kernel_path[sizeof(kernel_path) - 1] = '\0';

    struct stat kstat;
    int ret = vfs_stat(kernel_path, &kstat);
    if (ret != EOK) {
        return -ret;
    }

    if (copy_to_user(buf, &kstat, sizeof(struct stat)) != 0) {
        return -EFAULT;
    }

    return 0;
}

int sys_setstat(const char __user *path, const struct stat __user *buf) {
    char kernel_path[4096];
    if (strncpy_from_user(kernel_path, path, sizeof(kernel_path)) < 0) {
        return -EFAULT;
    }
    kernel_path[sizeof(kernel_path) - 1] = '\0';

    struct stat kstat;
    if (copy_from_user(&kstat, buf, sizeof(struct stat)) != 0) {
        return -EFAULT;
    }

    int ret = vfs_setstat(kernel_path, &kstat);
    if (ret != EOK) {
        return -ret;
    }

    return 0;
}

int sys_getfdpath(int fd, char __user *buf, size_t size) {
    pcb_t *current = get_current_pcb();
    if (fd < 0) {
        return -EBADF;
    }

    fd_entry_t *e = fd_get(&current->fd_table, fd, FD_FILE);
    if (!e) {
        return -EBADF;
    }

    fileio_t *file = e->ptr;
    if (!file || !file->private) {
        return -EBADF;
    }

    vnode_t *vn = (vnode_t *)file->private;
    if (!vn) {
        return -EBADF;
    }

    char kbuf[4096];
    memcpy(kbuf, vn->path, sizeof(kbuf));

    size_t path_len = strlen(kbuf) + 1;
    if (path_len > size) {
        return -ERANGE;
    }

    if (copy_to_user(buf, kbuf, path_len) != 0) {
        return -EFAULT;
    }

    return (int)path_len;
}

void *syscall_table[] = {
    (void *)sys_exit,
    (void *)sys_open,
    (void *)sys_read,
    (void *)sys_write,
    (void *)sys_close,
    (void *)sys_ioctl,
    (void *)sys_seek,
    (void *)sys_fcntl,
    (void *)sys_dup,
    (void *)sys_getpid,
    (void *)sys_getuid,
    (void *)sys_geteuid,
    (void *)sys_getgid,
    (void *)sys_getegid,
    (void *)sys_setuid,
    (void *)sys_seteuid,
    (void *)sys_setreuid,
    (void *)sys_setresuid,
    (void *)sys_getresuid,
    (void *)sys_setgid,
    (void *)sys_setegid,
    (void *)sys_setregid,
    (void *)sys_setresgid,
    (void *)sys_getresgid,
    (void *)sys_fork,
    (void *)sys_mount,
    (void *)sys_umount,
    (void *)sys_opendir,
    (void *)sys_readdir,
    (void *)sys_closedir,
    (void *)sys_mkdir,
    (void *)sys_create,
    (void *)sys_rmdir,
    (void *)sys_remove,
    (void *)sys_symlink,
    (void *)sys_readlink,
    (void *)sys_mmap,
    (void *)sys_munmap,
    (void *)sys_mprotect,
    (void *)sys_msync,
    (void *)sys_pipe,
    (void *)sys_nanosleep,
    (void *)sys_execve,
    (void *)sys_waitpid,
    (void *)sys_wait4,
    (void *)sys_getppid,
    (void *)sys_getpgrp,
    (void *)sys_setpgid,
    (void *)sys_getpgid,
    (void *)sys_setsid,
    (void *)sys_getsid,
    (void *)sys_settls,
    (void *)sys_poll,
    (void *)sys_getcwd,
    (void *)sys_futex_wait,
    (void *)sys_futex_wake,
    (void *)sys_chdir,
    (void *)sys_stat,
    (void *)sys_setstat,
    (void *)sys_getfdpath,
    (void *)sys_rt_sigaction,
    (void *)sys_rt_sigprocmask,
    (void *)sys_rt_sigpending,
    (void *)sys_rt_sigsuspend,
    (void *)sys_rt_sigtimedwait,
    (void *)sys_rt_sigreturn,
    (void *)sys_kill,
    (void *)sys_tgkill,
    (void *)sys_rt_sigqueueinfo,
};