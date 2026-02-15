#include "file_io.h"
#include "ipc/pipe.h"
#include "scheduler/scheduler.h"
#include "user/access.h"

#include <fs/vfs/vfs.h>

#include <memory/heap/kheap.h>

#include <stdio.h>
#include <string.h>

#include <errors.h>
#include <util/assert.h>
#include <util/macro.h>

int f2vflags(int fio_flags) {
    int vflags = 0;
    if (fio_flags & (O_CREATE)) {
        vflags |= V_CREATE;
    }
    if (fio_flags & (O_DIRECTORY)) {
        vflags |= V_DIR;
    }

    return vflags;
}

fileio_t *fio_create() {
    fileio_t *fio = kmalloc(sizeof(fileio_t));
    memset(fio, 0, sizeof(fileio_t));
    return fio;
}

fileio_t *open(const char *path, int flags, mode_t mode) {
    fileio_t *f = NULL;

    if (flags & O_CREATE) {
        int ret = vfs_create(path, mode);
        if (ret != 0) {
            return (fileio_t *)-ret; // most hackey thing ever ffs
        } else {
            open(path, flags & ~O_CREATE, mode); // technically mode is ignored but still :^)
        }
    }

    int ret = vfs_open(path, f2vflags(flags), &f);
    if (ret != 0) {
        return (fileio_t *)-ret; // most hackey thing ever ffs
    }

    f->offset = 0;
    f->flags = flags |= f->flags;

    return f;
}

size_t read(fileio_t *file, size_t size, void *out) {
    if (!file) {
        return 0;
    }

    if (file->flags & PIPE_READ_END) {
        pipe_read(file, out, &size);
        return size;
    } else if (file->flags & PIPE_WRITE_END) {
        return 0;
    }

    if (!(file->flags & SPECIAL_FILE_TYPE_DEVICE)) {
        if (file->offset >= file->size) {
            return 0;
        }
        if (size > file->size) {
            size = (file->size - file->offset);
        }
    }

    size_t bytes = size;
    size_t offset = file->offset;
    int ret = ((vnode_t *)file->private)->ops->read(
        ((vnode_t *)file->private), &bytes, &offset, out);
    
    if (ret != 0) {
        return 0;
    }

    file->offset = offset;
    return bytes;
}

int write(fileio_t *file, void *buf, size_t size) {
    vnode_t *vn = file->private;

    if (file->flags & PIPE_WRITE_END) {
        pipe_write(file, buf, &size);
        return size;
    } else if (file->flags & PIPE_READ_END) {
        return -EBADF;
    }

    size_t offset = file->offset;
    if (file->flags & O_APPEND) {
        offset += file->size;
    }

    if (vfs_write(vn, buf, size, offset) != 0) {
        return -EIO;
    }

    if (file->size < size) {
        file->size = size;
    }

    file->offset += size;
    return EOK;
}

int close(fileio_t *file) {
    vnode_t *vn = file->private;

    if (file->flags & PIPE_READ_END || file->flags & PIPE_WRITE_END) {
        pipe_close(file);
        return 0;
    }

    if (vfs_close(vn) != 0) {
        return -1;
    }

    kfree(file);
    return EOK;
}

size_t seek(fileio_t *file, size_t offset, fseek_t whence) {
    if (!file) {
        return -ENULLPTR;
    }

    switch (whence) {
    case SEEK_CUR:
        file->offset += offset;
        break;
    case SEEK_END:
        file->offset = (file->size + offset);
        break;
    case SEEK_SET:
        file->offset = offset;
        break;
    default:
        break;
    }

    return file->offset;
}

size_t fcntl(fileio_t *file, fcntl_cmd_t cmd, void *arg) {
    switch (cmd) {
    case F_GETFL:
        return file->flags;
    case F_SETFL:
        size_t flags  = *(size_t *)arg;
        file->flags  |= (flags & (O_APPEND));
        break;
    default:
        break;
    }

    return EOK;
}

static const char *vtype_to_str(vnode_type_t type) {
    switch (type) {
    case VNODE_DIR:
        return "DIR ";
    case VNODE_REGULAR:
        return "FILE";
    case VNODE_BLOCK:
        return "BLK ";
    case VNODE_CHAR:
        return "CHR ";
    case VNODE_LINK:
        return "LINK";
    case VNODE_PIPE:
        return "PIPE";
    case VNODE_SOCKET:
        return "SOCK";
    default:
        return "????";
    }
}

const char *file_type_char(mode_t mode) {
    mode_t type = mode & S_IFMT;  // S_IFMT masks the file type bits
    
    if (type == S_IFREG)  return "-";
    if (type == S_IFDIR)  return "d";
    if (type == S_IFLNK)  return "l";
    if (type == S_IFCHR)  return "c";
    if (type == S_IFBLK)  return "b";
    if (type == S_IFIFO)  return "p";
    if (type == S_IFSOCK) return "s";
    return "?";
}

void mode_to_string(mode_t mode, char *str) {
    str[0] = file_type_char(mode)[0];

    str[1] = (mode & S_IRUSR) ? 'r' : '-';
    str[2] = (mode & S_IWUSR) ? 'w' : '-';
    str[3] = (mode & S_IRUSR) ? 'x' : '-';

    str[4] = (mode & S_IRGRP) ? 'r' : '-';
    str[5] = (mode & S_IWGRP) ? 'w' : '-';
    str[6] = (mode & S_IRGRP) ? 'x' : '-';

    str[7] = (mode & S_IROTH) ? 'r' : '-';
    str[8] = (mode & S_IWOTH) ? 'w' : '-';
    str[9] = (mode & S_IROTH) ? 'x' : '-';

    str[10] = '\0';
}

static void fs_list_internal(vnode_t *dir, int depth, int max_depth,
                             int indent) {
    if (max_depth != -1 && depth > max_depth) {
        return;
    }

    if (!dir || dir->vtype != VNODE_DIR) {
        return;
    }

    dirent_t entries[256];
    size_t count = 256;

    int ret = vfs_readdir(dir, entries, &count);
    if (ret != EOK) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        for (int j = 0; j < indent; j++) {
            kprintf("  ");
        }

        if (entries[i].d_type == VNODE_DIR) {
            char mode_buf[11];
            char *path_buf = kmalloc(strlen(dir->path) + strlen(entries[i].d_name));
            snprintf(path_buf, strlen(dir->path) + strlen(entries[i].d_name) + 2, "%s/%s", dir->path, entries[i].d_name);
            fileio_t *f = open(path_buf, 0, 0);
            vnode_t *vnode = (vnode_t*)f->private;
            mode_to_string(vnode->mode, mode_buf);
            close(f);
            kfree(path_buf);
            kprintf("|- [%s] %s/\n", mode_buf, entries[i].d_name); // TODO: idk how to make it work on dirs rn
        } else if (entries[i].d_type == VNODE_LINK) {
            char mode_buf[11];
            char *path_buf = kmalloc(strlen(dir->path) + strlen(entries[i].d_name));
            snprintf(path_buf, strlen(dir->path) + strlen(entries[i].d_name) + 2, "%s/%s", dir->path, entries[i].d_name);
            fileio_t *f = open(path_buf, 0, 0);
            vnode_t *vnode = (vnode_t*)f->private;
            mode_to_string(vnode->mode, mode_buf);
            close(f);
            kfree(path_buf);

            size_t path_len = strlen(dir->path) + strlen(entries[i].d_name) + 2;
            char *full_path = kmalloc(path_len);
            snprintf(full_path, path_len, "%s/%s", dir->path,
                     entries[i].d_name);

            char target[256];
            int ret = vfs_readlink(full_path, target, sizeof(target));
            if (ret == EOK) {
                kprintf("|- [%s] %s -> %s\n", mode_buf, entries[i].d_name,
                        target);
            } else {
                kprintf("|- [%s] %s -> ??? (%d)\n", mode_buf, entries[i].d_name,
                        ret);
            }

            kfree(full_path);
        } else {
            char mode_buf[11];
            char *path_buf = kmalloc(strlen(dir->path) + strlen(entries[i].d_name));
            snprintf(path_buf, strlen(dir->path) + strlen(entries[i].d_name) + 2, "%s/%s", dir->path, entries[i].d_name);
            fileio_t *f = open(path_buf, 0, 0);
            vnode_t *vnode = (vnode_t*)f->private;
            mode_to_string(vnode->mode, mode_buf);
            close(f);
            kfree(path_buf);
            kprintf("|- [%s] %s\n", mode_buf, entries[i].d_name);
        }

        if (entries[i].d_type == VNODE_DIR) {
            if (strcmp(entries[i].d_name, ".") == 0 ||
                strcmp(entries[i].d_name, "..") == 0) {
                continue;
            }

            size_t path_len = strlen(dir->path) + strlen(entries[i].d_name) + 2;
            char *child_path = kmalloc(path_len);
            if (strcmp(dir->path, "/") == 0) {
                snprintf(child_path, path_len, "/%s", entries[i].d_name);
            } else {
                snprintf(child_path, path_len, "%s/%s", dir->path,
                         entries[i].d_name);
            }

            vnode_t *child_vnode;
            if (vfs_lookup(child_path, &child_vnode) == EOK) {
                fs_list_internal(child_vnode, depth + 1, max_depth, indent + 1);
                vnode_unref(child_vnode);
            }
            kfree(child_path);
        }
    }
}

int fs_list(const char *path, int max_depth) {
    if (!path) {
        return -EINVAL;
    }

    vnode_t *vnode;
    int ret = vfs_lookup(path, &vnode);
    if (ret != EOK) {
        kprintf("Error: Cannot access '%s'\n", path);
        return ret;
    }

    if (vnode->vtype != VNODE_DIR) {
        kprintf("Error: '%s' is not a directory\n", path);
        vnode_unref(vnode);
        return -ENOTDIR;
    }

    kprintf("%s\n", path);
    fs_list_internal(vnode, 0, max_depth, 0);

    vnode_unref(vnode);
    return EOK;
}
