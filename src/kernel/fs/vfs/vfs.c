#include "vfs.h"
#include "fs/file_io.h"
#include "stdio.h"
#include "time.h"
#include "user/access.h"
#include "user/user.h"

#include <util/assert.h>

#include <memory/heap/kheap.h>

#include <stdint.h>
#include <string.h>

#include <errors.h>

vfs_t *vfs_list                  = NULL;
static atomic_flag vfs_list_lock = ATOMIC_FLAG_INIT;

static vfs_fstype_t *registered_fstypes = NULL;
static atomic_flag fstype_lock          = ATOMIC_FLAG_INIT;
static uint16_t next_fstype_id          = 1;

#define MAX_SYMLINK_DEPTH 8

int vfs_register_fstype(vfs_fstype_t *fstype) {
    if (!fstype || !fstype->name[0] || !fstype->mount) {
        return -EINVAL;
    }

    while (atomic_flag_test_and_set(&fstype_lock))
        ;

    vfs_fstype_t *current = registered_fstypes;
    while (current) {
        if (strcmp(current->name, fstype->name) == 0) {
            atomic_flag_clear(&fstype_lock);
            return -EEXIST;
        }
        current = current->next;
    }

    fstype->id         = next_fstype_id++;
    fstype->next       = registered_fstypes;
    registered_fstypes = fstype;

    atomic_flag_clear(&fstype_lock);
    return EOK;
}

int vfs_unregister_fstype(const char *name) {
    if (!name)
        return -EINVAL;

    while (atomic_flag_test_and_set(&fstype_lock))
        ;

    vfs_fstype_t **prev   = &registered_fstypes;
    vfs_fstype_t *current = registered_fstypes;

    while (current) {
        if (strcmp(current->name, name) == 0) {
            *prev = current->next;
            atomic_flag_clear(&fstype_lock);
            return EOK;
        }
        prev    = &current->next;
        current = current->next;
    }

    atomic_flag_clear(&fstype_lock);
    return -ENOENT;
}

vfs_fstype_t *vfs_find_fstype(const char *name) {
    if (!name)
        return NULL;

    while (atomic_flag_test_and_set(&fstype_lock))
        ;

    vfs_fstype_t *current = registered_fstypes;
    while (current) {
        if (strcmp(current->name, name) == 0) {
            atomic_flag_clear(&fstype_lock);
            return current;
        }
        current = current->next;
    }

    atomic_flag_clear(&fstype_lock);
    return NULL;
}

vfs_t *vfs_create_fs(vfs_fstype_t *fs_type, void *fs_data) {
    if (!fs_type)
        return NULL;

    vfs_t *vfs = kmalloc(sizeof(vfs_t));
    if (!vfs)
        return NULL;

    memset(vfs, 0, sizeof(vfs_t));
    vfs->fs_type  = *fs_type;
    vfs->vfs_data = fs_data;
    vfs->ops      = kmalloc(sizeof(vfsops_t));

    if (!vfs->ops) {
        kfree(vfs);
        return NULL;
    }

    memset(vfs->ops, 0, sizeof(vfsops_t));
    atomic_flag_clear(&vfs->vfs_lock);

    return vfs;
}

vfs_t *vfs_mount(void *device, const char *fstype_name, char *path,
                 void *mount_data) {
    if (!fstype_name || !path) {
        return NULL;
    }

    vfs_fstype_t *fstype = vfs_find_fstype(fstype_name);
    if (!fstype) {
        return NULL;
    }

    vfs_t *vfs = NULL;
    int ret    = fstype->mount(device, path, mount_data, &vfs);

    if (ret != EOK || !vfs) {
        return NULL;
    }

    vfs->fs_type = *fstype;

    vnode_t *mount_point = NULL;
    if (strcmp(path, "/") != 0) {
        ret = vfs_lookup(path, &mount_point);
        if (ret != EOK) {
            if (!vfs_list) {
                vfs_append(vfs);
                return vfs;
            }

            if (vfs->ops && vfs->ops->unmount) {
                vfs->ops->unmount(vfs);
            }
            if (vfs->root_vnode) {
                vnode_unref(vfs->root_vnode);
            }
            if (vfs->ops)
                kfree(vfs->ops);
            kfree(vfs);
            return NULL;
        }

        if (mount_point->vtype != VNODE_DIR) {
            vnode_unref(mount_point);
            if (vfs->ops && vfs->ops->unmount) {
                vfs->ops->unmount(vfs);
            }
            if (vfs->root_vnode) {
                vnode_unref(vfs->root_vnode);
            }
            if (vfs->ops)
                kfree(vfs->ops);
            kfree(vfs);
            return NULL;
        }

        mount_point->vfs_here = vfs;
        vnode_unref(mount_point);
    }

    vfs_append(vfs);

    return vfs;
}

int vfs_append(vfs_t *vfs) {
    if (!vfs_list) {
        vfs_list = vfs;
        return EOK;
    }

    vfs_t *v;
    for (v = vfs_list; v->next != NULL; v = v->next)
        ;

    v->next = vfs;

    return EOK;
}

int vfs_unmount(const char *path) {
    if (!path)
        return -EINVAL;

    while (atomic_flag_test_and_set(&vfs_list_lock))
        ;

    vfs_t **prev   = &vfs_list;
    vfs_t *current = vfs_list;

    while (current) {
        if (current->root_vnode &&
            strcmp(current->root_vnode->path, path) == 0) {

            if (strcmp(path, "/") != 0) {
                vnode_t *mount_point;
                if (vfs_lookup(path, &mount_point) == EOK) {
                    mount_point->vfs_here = NULL;
                    vnode_unref(mount_point);
                }
            }

            if (current->ops && current->ops->unmount) {
                current->ops->unmount(current);
            }

            *prev = current->next;

            if (current->root_vnode) {
                vnode_unref(current->root_vnode);
            }

            if (current->ops)
                kfree(current->ops);
            kfree(current);

            atomic_flag_clear(&vfs_list_lock);
            return EOK;
        }
        prev    = &current->next;
        current = current->next;
    }

    atomic_flag_clear(&vfs_list_lock);
    return -ENOENT;
}

vnode_t *vnode_create(vfs_t *root_vfs, char *path, vnode_type_t type,
                      void *data) {
    vnode_t *vnode = kmalloc(sizeof(vnode_t));
    if (!vnode)
        return NULL;

    memset(vnode, 0, sizeof(vnode_t));
    vnode->path      = strdup(path);
    vnode->vtype     = type;
    vnode->root_vfs  = root_vfs;
    vnode->node_data = data;
    vnode->refcount  = 1;
    vnode->vfs_here  = NULL;
    atomic_flag_clear(&vnode->vnode_lock);

    vnode->ops = kmalloc(sizeof(vnops_t));
    if (!vnode->ops) {
        kfree(vnode->path);
        kfree(vnode);
        return NULL;
    }
    memset(vnode->ops, 0, sizeof(vnops_t));

    return vnode;
}

void vnode_ref(vnode_t *vnode) {
    if (!vnode)
        return;

    spinlock_acquire(&vnode->vnode_lock);
    vnode->refcount++;
    spinlock_release(&vnode->vnode_lock);
}

void vnode_unref(vnode_t *vnode) {
    if (!vnode)
        return;

    while (atomic_flag_test_and_set(&vnode->vnode_lock))
        ;
    vnode->refcount--;

    if (vnode->refcount == 0) {
        atomic_flag_clear(&vnode->vnode_lock);
        if (vnode->path)
            kfree(vnode->path);
        if (vnode->ops)
            kfree(vnode->ops);
        kfree(vnode);
    } else {
        atomic_flag_clear(&vnode->vnode_lock);
    }
}

int vfs_resolve_mount(const char *path, vfs_t **out, char **remaining_path) {
    vfs_t *best_match = NULL;
    size_t best_len   = 0;

    spinlock_acquire(&vfs_list_lock);

    for (vfs_t *v = vfs_list; v != NULL; v = v->next) {
        if (!v->root_vnode || !v->root_vnode->path) {
            continue;
        }

        const char *prefix = v->root_vnode->path;
        size_t prefix_len  = strlen(prefix);

        if (strlen(path) < prefix_len)
            continue;

        if (prefix_len == 1 && prefix[0] == '/') {
            if (path[0] == '/') {
                if (prefix_len > best_len) {
                    best_match = v;
                    best_len   = prefix_len;
                }
            }
        } else if (strncmp(path, prefix, prefix_len) == 0) {
            if (path[prefix_len] == '\0' || path[prefix_len] == '/') {
                if (prefix_len > best_len) {
                    best_match = v;
                    best_len   = prefix_len;
                }
            }
        }
    }

    spinlock_release(&vfs_list_lock);

    if (!best_match) {
        return -ENOENT;
    }

    *out = best_match;

    if (remaining_path) {
        const char *rem = path + best_len;
        while (*rem == '/')
            rem++;
        *remaining_path = strdup(rem);
    }

    return EOK;
}

static int vfs_lookup_internal(const char *path, vnode_t **out, int depth,
                               bool follow_symlinks);

static int vfs_follow_symlink(vnode_t *vnode, vnode_t **out, int depth) {
    if (depth >= MAX_SYMLINK_DEPTH) {
        return -ELOOP;
    }

    if (vnode->vtype != VNODE_LINK) {
        *out = vnode;
        return EOK;
    }

    if (!vnode->ops || !vnode->ops->readlink) {
        debugf_debug("The vnode doesnt have the readlink(...) operation\n");
        return -EINVAL;
    }

    char target[512];
    int ret = vnode->ops->readlink(vnode, target, sizeof(target));
    if (ret != EOK) {
        return ret;
    }

    // Resolve the symlink target
    vnode_t *target_vnode;
    ret = vfs_lookup_internal(target, &target_vnode, depth + 1, false);
    if (ret != EOK) {
        return ret;
    }

    *out = target_vnode;
    return EOK;
}

static int vfs_lookup_internal(const char *path, vnode_t **out, int depth,
                               bool follow_symlinks) {
    if (depth >= MAX_SYMLINK_DEPTH) {
        return -ELOOP;
    }

    vfs_t *vfs;
    char *rel_path;
    int ret = vfs_resolve_mount(path, &vfs, &rel_path);
    if (ret != EOK) {
        return ret;
    }

    if (!rel_path || rel_path[0] == '\0') {
        if (rel_path)
            kfree(rel_path);

        if (vfs->root_vnode && vfs->root_vnode->vfs_here) {
            *out = vfs->root_vnode->vfs_here->root_vnode;
        } else {
            *out = vfs->root_vnode;
        }
        vnode_ref(*out);
        return EOK;
    }

    vnode_t *current = vfs->root_vnode;
    if (!current) {
        kfree(rel_path);
        return -ENOENT;
    }

    vnode_ref(current);

    if (current->vfs_here) {
        vfs = current->vfs_here;
        vnode_unref(current);
        current = vfs->root_vnode;
        vnode_ref(current);
    }

    char *path_copy = rel_path;
    char *component = strtok(path_copy, "/");

    while (component) {
        if (current->vtype != VNODE_DIR) {
            vnode_unref(current);
            kfree(rel_path);
            return -ENOTDIR;
        }

        if (get_current_cred() && vnode_permission(get_current_cred(), current, X_OK) != 0) {
            vnode_unref(current);
            kfree(rel_path);
            return -EACCES;
        }

        if (!current->ops || !current->ops->lookup) {
            vnode_unref(current);
            kfree(rel_path);
            return -ENOSYS;
        }

        vnode_t *next;
        ret = current->ops->lookup(current, component, &next);
        if (ret != EOK) {
            vnode_unref(current);
            kfree(rel_path);
            return ret;
        }

        if (next->vfs_here) {
            vnode_unref(next);
            next = next->vfs_here->root_vnode;
            vnode_ref(next);
        }

        vnode_t *resolved = next;
        if (follow_symlinks) {
            ret = vfs_follow_symlink(next, &resolved, depth + 1);
            if (ret != EOK) {
                vnode_unref(next);
                vnode_unref(current);
                kfree(rel_path);
                return ret;
            }
        }

        if (resolved != next) {
            vnode_unref(next);
            vnode_ref(resolved);
        }

        vnode_unref(current);
        current = resolved;

        component = strtok(NULL, "/");
    }

    kfree(rel_path);
    *out = current;
    return EOK;
}

int vfs_lookup(const char *path, vnode_t **out) {
    return vfs_lookup_internal(path, out, 0, true);
}

int vfs_lookup_parent(const char *path, vnode_t **parent, char **filename) {
    if (!path || !parent || !filename)
        return -EINVAL;

    const char *last_slash = strrchr(path, '/');
    if (!last_slash) {
        return -EINVAL;
    }

    size_t parent_len = last_slash - path;

    char *parent_path;
    if (parent_len == 0) {
        parent_path = strdup("/");
    } else {
        parent_path = kmalloc(parent_len + 1);
        if (!parent_path)
            return -ENOMEM;
        memcpy(parent_path, path, parent_len);
        parent_path[parent_len] = '\0';
    }

    *filename = strdup(last_slash + 1);
    if (!*filename) {
        kfree(parent_path);
        return -ENOMEM;
    }

    int ret = vfs_lookup(parent_path, parent);
    kfree(parent_path);

    if (ret != EOK) {
        kfree(*filename);
        *filename = NULL;
        return ret;
    }

    return EOK;
}

int vfs_create(const char *path, mode_t mode) {
    vnode_t *vnode = NULL;

    vnode_t *parent;
    char *fname;

    int ret = vfs_lookup_parent(path, &parent, &fname);
    if (ret != EOK)
        return ret;

    if (!parent->ops || !parent->ops->create) {
        vnode_unref(parent);
        kfree(fname);
        return -ENOSYS;
    }

    ret = parent->ops->create(parent, fname, mode, &vnode);
    kfree(fname);
    vnode_unref(parent);

    if (ret != EOK)
        return ret;

    return 0;
}

int vfs_open(const char *path, int flags, fileio_t **out) {
    if (!path || !out)
        return -EINVAL;

    int required_perms = 0;
    if (flags & V_READ)
        required_perms |= R_OK;
    if (flags & V_WRITE)
        required_perms |= W_OK;

    vnode_t *vnode = NULL;
    int ret        = vfs_lookup(path, &vnode);

    if (get_current_cred() && vnode_permission(get_current_cred(), vnode, required_perms) != 0) {
        return -EACCES;
    }

    if (ret == ENOENT && (flags & V_CREATE)) {
        vnode_t *parent;
        char *fname;

        ret = vfs_lookup_parent(path, &parent, &fname);
        if (ret != EOK)
            return ret;

        if (!parent->ops || !parent->ops->create) {
            vnode_unref(parent);
            kfree(fname);
            return -ENOSYS;
        }

        ret = parent->ops->create(parent, fname, flags, &vnode);
        kfree(fname);
        vnode_unref(parent);

        if (ret != EOK)
            return ret;
    } else if (ret != EOK) {
        return ret;
    }

    if (!vnode) {
        return -ENOENT;
    }

    if (!vnode->ops || !vnode->ops->open) {
        vnode_unref(vnode);
        return -ENOSYS;
    }

    fileio_t *fio = fio_create();
    if (!fio) {
        vnode_unref(vnode);
        return -ENOMEM;
    }

    ret = vnode->ops->open(&vnode, flags, false, &fio);
    if (ret != EOK) {
        kfree(fio);
        vnode_unref(vnode);
        return ret;
    }

    fio->private = vnode;
    *out         = fio;

    return EOK;
}

int vfs_read(vnode_t *vnode, size_t size, size_t offset, void *out) {
    if (!vnode) {
        return -EFAULT;
    }

    int ret = vnode->ops->read(vnode, &size, &offset, out);

    return ret;
}

int vfs_write(vnode_t *vnode, void *buf, size_t size, size_t offset) {
    if (!vnode) {
        return EFAULT;
    }

    int ret = vnode->ops->write(vnode, buf, &size, &offset);

    if (ret != EOK) {
        return ret;
    } else {
        return size;
    }
}

int vfs_ioctl(vnode_t *vnode, int request, void *arg) {
    if (!vnode) {
        return EFAULT;
    }

    int ret = vnode->ops->ioctl(vnode, request, arg);

    return ret;
}

int vfs_close(vnode_t *vnode) {
    if (!vnode) {
        return -EFAULT;
    }

    if (!vnode->ops || !vnode->ops->close) {
        vnode_unref(vnode);
        return -ENOSYS;
    }

    int r = vnode->ops->close(vnode, 0, false);

    if (r != EOK) {
        vnode_unref(vnode);
        return r;
    }

    vnode_unref(vnode);

    return EOK;
}

int vfs_readdir(vnode_t *vnode, dirent_t *entries, size_t *count) {
    if (!vnode || !entries || !count)
        return -EINVAL;

    if (vnode->vtype != VNODE_DIR) {
        return -ENOTDIR;
    }

    if (!vnode->ops || !vnode->ops->readdir) {
        return -ENOSYS;
    }

    return vnode->ops->readdir(vnode, entries, count);
}

int vfs_mkdir(const char *path, int mode) {
    if (!path)
        return -EINVAL;

    vnode_t *parent;
    char *dirname;

    int ret = vfs_lookup_parent(path, &parent, &dirname);
    if (ret != EOK)
        return ret;

    if (!parent->ops || !parent->ops->mkdir) {
        vnode_unref(parent);
        kfree(dirname);
        return -ENOSYS;
    }

    if (get_current_cred() && vnode_permission(get_current_cred(), parent, W_OK) != 0) {
        vnode_unref(parent);
        kfree(dirname);
        return -EACCES;
    }

    ret = parent->ops->mkdir(parent, dirname, mode);
    kfree(dirname);
    vnode_unref(parent);

    return ret;
}

int vfs_rmdir(const char *path) {
    if (!path)
        return -EINVAL;

    vnode_t *parent;
    char *dirname;

    int ret = vfs_lookup_parent(path, &parent, &dirname);
    if (ret != EOK)
        return ret;

    if (!parent->ops || !parent->ops->rmdir) {
        vnode_unref(parent);
        kfree(dirname);
        return -ENOSYS;
    }

    if (get_current_cred() && vnode_permission(get_current_cred(), parent, W_OK | X_OK) != 0) {
        vnode_unref(parent);
        kfree(dirname);
        return -EACCES;
    }

    ret = parent->ops->rmdir(parent, dirname);
    kfree(dirname);
    vnode_unref(parent);

    return ret;
}

int vfs_remove(const char *path) {
    if (!path)
        return -EINVAL;

    vnode_t *parent;
    char *filename;

    int ret = vfs_lookup_parent(path, &parent, &filename);
    if (ret != EOK)
        return ret;

    if (!parent->ops || !parent->ops->remove) {
        vnode_unref(parent);
        kfree(filename);
        return -ENOSYS;
    }

    if (get_current_cred() && vnode_permission(get_current_cred(), parent, W_OK | X_OK) != 0) {
        vnode_unref(parent);
        kfree(filename);
        return -EACCES;
    }

    ret = parent->ops->remove(parent, filename);
    kfree(filename);
    vnode_unref(parent);

    return ret;
}

int vfs_readlink(const char *path, char *buf, size_t size) {
    if (!path || !buf)
        return -EINVAL;

    vnode_t *vnode;
    int ret = vfs_lookup_internal(path, &vnode, 0, false);
    if (ret != EOK)
        return ret;

    if (vnode->vtype != VNODE_LINK) {
        vnode_unref(vnode);
        debugf("vfs_readlink: vnode->vtype=%d @ vnode->path=%s (VNODE_LINK "
               "should be %d)\n",
               vnode->vtype, vnode->path, VNODE_LINK);
        return -EINVAL;
    }

    if (!vnode->ops || !vnode->ops->readlink) {
        vnode_unref(vnode);
        return -ENOSYS;
    }

    ret = vnode->ops->readlink(vnode, buf, size);
    vnode_unref(vnode);

    return ret;
}

int vfs_symlink(const char *target, const char *linkpath) {
    if (!target || !linkpath)
        return -EINVAL;

    vnode_t *parent;
    char *linkname;

    int ret = vfs_lookup_parent(linkpath, &parent, &linkname);
    if (ret != EOK)
        return ret;

    if (!parent->ops || !parent->ops->symlink) {
        vnode_unref(parent);
        kfree(linkname);
        return -ENOSYS;
    }

    ret = parent->ops->symlink(parent, linkname, target);
    kfree(linkname);
    vnode_unref(parent);

    return ret;
}

int vfs_stat(const char *path, stat_t *st) {
    if (!path || !st)
        return -EINVAL;

    vnode_t *vnode;
    int ret = vfs_lookup(path, &vnode);
    if (ret != EOK)
        return ret;

    if (!vnode->ops || !vnode->ops->getattr) {
        vnode_unref(vnode);
        return -ENOSYS;
    }

    ret = vnode->ops->getattr(vnode, st);
    vnode_unref(vnode);

    return ret;
}

int vfs_setstat(const char *path, stat_t *st) {
    if (!path || !st)
        return -EINVAL;

    vnode_t *vnode;
    int ret = vfs_lookup(path, &vnode);
    if (ret != EOK)
        return ret;

    if (!vnode->ops || !vnode->ops->setattr) {
        vnode_unref(vnode);
        return -ENOSYS;
    }

    ret = vnode->ops->setattr(vnode, st);
    vnode_unref(vnode);

    return ret;
}