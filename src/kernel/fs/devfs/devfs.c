#include "devfs.h"
#include "user/access.h"
#include <errors.h>
#include <memory/heap/kheap.h>
#include <stdio.h>
#include <string.h>
#include <util/macro.h>

devfs_t *devfs_create_fs() {
    devfs_t *devfs = kmalloc(sizeof(devfs_t));
    if (!devfs) {
        return NULL;
    }

    memset(devfs, 0, sizeof(devfs_t));
    return devfs;
}

devfs_node_t *devfs_create_fs_node(devfs_ftype_t ftype) {
    devfs_node_t *node = kmalloc(sizeof(devfs_node_t));
    if (!node) {
        return NULL;
    }

    memset(node, 0, sizeof(devfs_node_t));
    node->type = ftype;

    switch (ftype) {
    case DEVFS_TYPE_DIR:
        node->mode = S_IFDIR | 0755;
        break;
    case DEVFS_TYPE_CHAR:
        node->mode = S_IFCHR | 0666;
        break;
    case DEVFS_TYPE_BLOCK:
        node->mode = S_IFBLK | 0666;
        break;
    case DEVFS_TYPE_FILE:
        node->mode = S_IFREG | 0644;
        break;
    }

    return node;
}

int devfs_find_node(devfs_t *devfs, char *path, devfs_node_t **out) {
    if (!devfs || !devfs->root_node || !path || !out) {
        return EFAULT;
    }

    *out = NULL;

    if (path[0] == '/') path++;

    if (*path == '\0') {
        *out = devfs->root_node;
        return EOK;
    }

    char *dup = strdup(path);
    if (!dup) return ENOMEM;

    char *save = NULL;
    char *token = strtok_r(dup, "/", &save);

    devfs_node_t *cur = devfs->root_node;

    while (token && cur) {
        devfs_node_t *child;
        for (child = cur->child; child; child = child->sibling) {
            if (strcmp(child->name, token) == 0) {
                break;
            }
        }

        if (!child) {
            kfree(dup);
            return ENOENT;
        }

        cur = child;
        token = strtok_r(NULL, "/", &save);
    }

    kfree(dup);
    *out = cur;
    return EOK;
}


int devfs_node_add(devfs_t *devfs, char *path, devfs_node_t **out) {
    if (!devfs || !devfs->root_node || !path || !out) {
        return EFAULT;
    }

    if (path[0] == '/') path++;

    char *dup = strdup(path);
    if (!dup) return ENOMEM;

    char *save = NULL;
    char *token = strtok_r(dup, "/", &save);

    devfs_node_t *cur = devfs->root_node;

    while (token) {
        devfs_node_t *child;
        for (child = cur->child; child; child = child->sibling) {
            if (strcmp(child->name, token) == 0) {
                break;
            }
        }

        if (!child) {
            devfs_ftype_t type = strtok_r(NULL, "/", &save)
                ? DEVFS_TYPE_DIR
                : DEVFS_TYPE_FILE;

            devfs_node_t *n = devfs_create_fs_node(type);
            n->name = strdup(token);

            for (int i = 0; i < device_count; i++) {
                if (device_table[i] &&
                    strcmp(device_table[i]->dev_node_path, token) == 0) {
                    n->device = device_table[i];
                    break;
                }
            }

            devfs_append_child(cur, n);
            child = n;
        }

        cur = child;
        token = strtok_r(NULL, "/", &save);
    }

    kfree(dup);
    *out = cur;
    return EOK;
}

int devfs_append_child(devfs_node_t *parent, devfs_node_t *child) {
    if (!parent->child) {
        parent->child = child;
        return 0;
    }

    devfs_node_t *last_child;
    for (last_child = parent->child; last_child->sibling != NULL;
         last_child = last_child->sibling)
        ;
    last_child->sibling = child;

    return 0;
}

int devfs_find_or_create_node(devfs_t *devfs, char *path,
                              devfs_ftype_t devfs_ftype, devfs_node_t **out) {

    devfs_node_t *found = NULL;
    devfs_find_node(devfs, path, &found);

    if (!found) {
        *out = devfs_create_fs_node(devfs_ftype);
        return 1;
    }

    *out = found;
    return 0;
}

int devfs_print(devfs_node_t *devfs, int lvl) {
    if (!devfs) {
        return -1;
    }

    int a = lvl;

    while (a--) {
        kprintf("  ");
    }

    switch (devfs->type) {
    case DEVFS_TYPE_DIR:
        kprintf("+ %-20s", devfs->name);
        break;

    case DEVFS_TYPE_FILE:
        kprintf("  %-20s", devfs->name);
        kprintf(" | % 10zuB", devfs->device ? 0 : 0);
        break;

    case DEVFS_TYPE_CHAR:
    case DEVFS_TYPE_BLOCK:
        kprintf("  %-20s", devfs->name);
        break;
    }

    kprintf("\n");

    if (devfs->child) {
        devfs_print(devfs->child, lvl + 1);
    }

    if (devfs->sibling) {
        devfs_print(devfs->sibling, lvl);
    }

    return 0;
}

int devfs_refresh(void) {
    for (vfs_t *vfs = vfs_list; vfs != NULL; vfs = vfs->next) {
        
        if (strcmp(vfs->fs_type.name, "devfs") != 0) {
            continue;
        }
        
        devfs_t *devfs = (devfs_t *)vfs->vfs_data;
        if (!devfs || !devfs->root_node) {
            continue;
        }
        
        
        devfs_node_t *root = devfs->root_node;
        
        // Remove devices that no longer exist
        devfs_node_t *prev = NULL;
        devfs_node_t *cur  = root->child;
        
        while (cur) {
            devfs_node_t *next = cur->sibling;
            bool still_exists = false;
            
            for (int i = 0; i < device_count; i++) {
                if (!device_table[i]) {
                    continue;
                }
                
                if (strcmp(device_table[i]->dev_node_path, cur->name) == 0) {
                    still_exists = true;
                    break;
                }
            }
            
            if (!still_exists) {
                if (prev) {
                    prev->sibling = next;
                } else {
                    root->child = next;
                }
                
                if (cur->name) {
                    kfree(cur->name);
                }
                kfree(cur);
                
                cur = next;
                continue;
            }
            
            prev = cur;
            cur  = next;
        }
        
        for (int i = 0; i < device_count; i++) {
            device_t *dev = device_table[i];
            if (!dev) {
                continue;
            }
            
            
            bool found = false;
            
            devfs_node_t *node = root->child;
            while (node) {
                if (strcmp(node->name, dev->dev_node_path) == 0) {
                    found = true;
                    break;
                }
                node = node->sibling;
            }
            
            if (!found) {
                devfs_node_t *new_node = devfs_create_fs_node(
                    dev->type == DEVICE_TYPE_BLOCK ? DEVFS_TYPE_BLOCK
                                                   : DEVFS_TYPE_CHAR);
                if (!new_node) {
                    return ENOMEM;
                }
                
                new_node->name   = strdup(dev->dev_node_path);
                new_node->device = dev;
                
                if (!new_node->name) {
                    kfree(new_node);
                    return ENOMEM;
                }
                
                devfs_append_child(root, new_node);
            }
		}
    }
    
    return EOK;
}


int devfs_open(vnode_t **vnode_r, int flags, bool clone, fileio_t **fio_out) {
    UNUSED(flags);
    UNUSED(clone);

    if (!vnode_r || !*vnode_r || !fio_out || !*fio_out)
        return EFAULT;

    vnode_t *vnode = *vnode_r;
    devfs_t *devfs = vnode->root_vfs->vfs_data;
    if (!devfs)
        return EFAULT;

    devfs_node_t *devfs_node = NULL;

    char *rel_path = vnode->path + strlen(vnode->root_vfs->root_vnode->path);
    if (rel_path[0] == '/')
        rel_path++;

    int find_res = devfs_find_node(devfs, rel_path, &devfs_node);
    if (find_res != 0) {
        if (!(flags & V_CREATE))
            return ENOENT;

        if (devfs_node_add(devfs, rel_path, &devfs_node) != EOK)
            return ENOTRECOVERABLE;
    }

    vnode->node_data = devfs_node;

    fileio_t *fio_file = *fio_out;
    fio_file->buf_start = NULL;
    fio_file->flags |= SPECIAL_FILE_TYPE_DEVICE;
    fio_file->size = 0;

    return EOK;
}

int devfs_close(vnode_t *vnode, int flags, bool clone) {
    UNUSED(flags);
    UNUSED(clone);
    if (!vnode) {
        return EFAULT;
    }

    return EOK;
}

int devfs_ioctl(vnode_t *vnode, int request, void *arg) {
    if (!vnode) {
        return EFAULT;
    }

    device_t *dev =
        vnode->node_data ? ((devfs_node_t *)vnode->node_data)->device : NULL;

    if (!dev || !dev->ioctl) {
        return ENOSYS;
    }

    return dev->ioctl(dev, request, arg);
}

int devfs_read(vnode_t *vn, size_t *bytes, size_t *offset, void *out) {
    if (!vn || !bytes || !offset || !out) {
        return EFAULT;
    }

    devfs_node_t *node = vn->node_data;
    if (!node || !node->device || !node->device->read) {
        return ENOSYS;
    }

    int ret = node->device->read(
        node->device,
        out,
        *bytes,
        *offset
    );

    if (ret < 0) {
        return ret;
	}

    *bytes = (size_t)ret;
    *offset += (size_t)ret;
    return EOK;
}


int devfs_write(vnode_t *vn, void *buf, size_t *bytes, size_t *offset) {
    if (!vn || !buf || !bytes || !offset) {
        return EFAULT;
    }

    devfs_node_t *node = vn->node_data;
    if (!node || !node->device || !node->device->write) {
        return ENOSYS;
    }

    int ret = node->device->write(
        node->device,
        buf,
        *bytes,
        *offset
    );

    if (ret < 0) {
        return ret;
    }

    *bytes = (size_t)ret;
    *offset += (size_t)ret;
    return EOK;
}

int devfs_lookup(vnode_t *parent, const char *name, vnode_t **out) {
    if (!parent || !name || !out) return EFAULT;

    devfs_node_t *pnode = parent->node_data;
    if (!pnode) return ENOENT;

    if (pnode->type != DEVFS_TYPE_DIR) return ENOTDIR;

    for (devfs_node_t *c = pnode->child; c; c = c->sibling) {
        if (strcmp(c->name, name) == 0) {

            size_t plen = strlen(parent->path);
            size_t nlen = strlen(name);

            char *path = kmalloc(plen + nlen + 2);
            if (!path) return ENOMEM;

            strcpy(path, parent->path);
            if (path[plen - 1] != '/') strcat(path, "/");
            strcat(path, name);

            vnode_type_t t =
                c->type == DEVFS_TYPE_DIR   ? VNODE_DIR :
                c->type == DEVFS_TYPE_BLOCK ? VNODE_BLOCK :
                c->type == DEVFS_TYPE_CHAR  ? VNODE_CHAR :
                                              VNODE_REGULAR;

            vnode_t *vn = vnode_create(parent->root_vfs, path, t, c);
            memcpy(vn->ops, parent->ops, sizeof(vnops_t));
            vn->mode = c->mode;

            *out = vn;
            return EOK;
        }
    }

    return ENOENT;
}

int devfs_readdir(vnode_t *vnode, dirent_t *entries, size_t *count) {
    if (!vnode || !entries || !count) return EFAULT;
    if (vnode->vtype != VNODE_DIR) return ENOTDIR;

    devfs_node_t *dir = vnode->node_data;
    if (!dir || dir->type != DEVFS_TYPE_DIR) return ENOTDIR;

    size_t i = 0;
    for (devfs_node_t *c = dir->child; c && i < *count; c = c->sibling) {
        entries[i].d_ino = (uint64_t)c;
        entries[i].d_off = i + 1;
        entries[i].d_reclen = sizeof(dirent_t);
        entries[i].d_type =
            c->type == DEVFS_TYPE_DIR ? DT_DIR :
            c->type == DEVFS_TYPE_BLOCK ? DT_BLK :
            c->type == DEVFS_TYPE_CHAR ? DT_CHR :
            DT_REG;
        strncpy(entries[i].d_name, c->name, sizeof(entries[i].d_name) - 1);
        entries[i].d_name[sizeof(entries[i].d_name) - 1] = 0;
        i++;
    }

    *count = i;
    return EOK;
}

static int devfs_mmap(vnode_t *vnode, void *addr, size_t length, int prot, int flags, size_t offset) {
    // call mmap on the device
    if (!vnode || !addr) {
        return EFAULT;
    }

    devfs_node_t *node = vnode->node_data;
    if (!node || !node->device || !node->device->mmap) {
        return ENOSYS;
    }

    return node->device->mmap(node->device, addr, length, prot, flags, offset);
}

static int devfs_getattr(vnode_t *vnode, struct stat *st) {
    if (!vnode || !st) {
        return EFAULT;
    }

    devfs_node_t *node = vnode->node_data;
    if (!node) {
        return ENOENT;
    }

    st->st_dev = 0;
    st->st_ino = (uint64_t)node;
    st->st_nlink = 1;
    st->st_mode = node->mode;
    st->st_uid = 0;
    st->st_gid = 0;

    if (node->device) {
        st->st_size = 0;
        st->st_blksize = 512;

        if (node->type == DEVFS_TYPE_BLOCK) {
            st->st_mode |= S_IFBLK;
        } else if (node->type == DEVFS_TYPE_CHAR) {
            st->st_mode |= S_IFCHR;
        } else {
            st->st_mode |= S_IFREG;
        }
    } else {
        st->st_size = 0;
        st->st_blksize = 512;
        st->st_mode |= S_IFDIR;
    }

    return EOK;
}

static int devfs_setattr(vnode_t *vnode, struct stat *st) {
    if (!vnode || !st) {
        return EFAULT;
    }

    devfs_node_t *node = vnode->node_data;
    if (!node) {
        return ENOENT;
    }

    node->mode = st->st_mode;
    vnode->uid = st->st_uid;
    vnode->gid = st->st_gid;

    return EOK;
}

vnops_t devfs_vnops = {
    .open    = devfs_open,
    .close   = devfs_close,
    .read    = devfs_read,
    .write   = devfs_write,
    .ioctl   = devfs_ioctl,
    .lookup  = devfs_lookup,
    .readdir = devfs_readdir,
    .mmap    = devfs_mmap,
    .getattr = devfs_getattr,
    .setattr = devfs_setattr
};

static int devfs_vfs_mount(vfs_t *vfs, char *path, void *data) {
    UNUSED(path);
    UNUSED(data);
    
    if (!vfs) {
        return EFAULT;
    }
    
    return EOK;
}

static int devfs_vfs_unmount(vfs_t *vfs) {
    if (!vfs) {
        return EFAULT;
    }
    
    devfs_t *devfs = vfs->vfs_data;
    if (devfs && devfs->root_node) {
        // TODO: free devfs tree
    }
    
    return EOK;
}

static int devfs_vfs_root(vfs_t *vfs, vnode_t **out) {
    if (!vfs || !out) {
        return EFAULT;
    }
    
    *out = vfs->root_vnode;
    vnode_ref(*out);
    
    return EOK;
}

static int devfs_vfs_statfs(vfs_t *vfs, statfs_t *stat) {
    if (!vfs || !stat) {
        return EFAULT;
    }
    
    devfs_t *devfs = vfs->vfs_data;
    if (!devfs) {
        return EFAULT;
    }
    
    stat->block_size = 1;
    stat->total_blocks = devfs->devfs_size;
    stat->free_blocks = 0;
    stat->total_nodes = device_count;
    stat->free_nodes = 0;
    
    return EOK;
}

static int devfs_vfs_sync(vfs_t *vfs) {
    UNUSED(vfs);
    return EOK;
}

vfsops_t devfs_vfsops = {
    .mount   = devfs_vfs_mount,
    .unmount = devfs_vfs_unmount,
    .root    = devfs_vfs_root,
    .statfs  = devfs_vfs_statfs,
    .sync    = devfs_vfs_sync,
};

static int devfs_fstype_mount(void *device, char *mount_point, void *mount_data, vfs_t **out) {
    UNUSED(mount_data);
    
    devfs_t *devfs = (devfs_t *)device;
    if (!devfs) {
        devfs = devfs_create_fs();
        if (!devfs) {
            return ENOMEM;
        }
    }
    
    vfs_fstype_t fstype;
    memset(&fstype, 0, sizeof(vfs_fstype_t));
    strncpy(fstype.name, "devfs", sizeof(fstype.name) - 1);
    
    vfs_t *vfs = vfs_create_fs(&fstype, devfs);
    if (!vfs) {
        return ENOMEM;
    }
    
    memcpy(vfs->ops, &devfs_vfsops, sizeof(vfsops_t));
    
    devfs_node_t *root_node = devfs_create_fs_node(DEVFS_TYPE_DIR);
    if (!root_node) {
        kfree(vfs->ops);
        kfree(vfs);
        return ENOMEM;
    }
    
    root_node->name = strdup("/");
    if (!root_node->name) {
        kfree(root_node);
        kfree(vfs->ops);
        kfree(vfs);
        return ENOMEM;
    }
    
    devfs->root_node = root_node;
    devfs->devfs_size = 0;
    root_node->device = NULL;
    root_node->sibling = NULL;
    root_node->child = NULL;
    root_node->type = DEVFS_TYPE_DIR;
    
    vfs->root_vnode = vnode_create(vfs, mount_point, VNODE_DIR, root_node);
    if (!vfs->root_vnode) {
        kfree(root_node->name);
        kfree(root_node);
        kfree(vfs->ops);
        kfree(vfs);
        return ENOMEM;
    }
	vfs->root_vnode->node_data = root_node;
    vfs->root_vnode->mode = root_node->mode;
    
    memcpy(vfs->root_vnode->ops, &devfs_vnops, sizeof(vnops_t));
    
    for (int i = 0; i < device_count; i++) {
        device_t *dev = device_table[i];
        if (!dev) {
            continue;
        }

        devfs_node_t *devfs_node =
            devfs_create_fs_node(dev->type == DEVICE_TYPE_BLOCK ? DEVFS_TYPE_BLOCK
                                                             : DEVFS_TYPE_CHAR);
        devfs_node->name   = strdup(dev->dev_node_path);
        devfs_node->device = dev;

        devfs_append_child(devfs->root_node, devfs_node);
    }
    
    *out = vfs;
    return EOK;
}

static vfs_fstype_t devfs_fstype = {
    .id = 0,
    .name = "devfs",
    .mount = devfs_fstype_mount,
    .next = NULL
};

void devfs_init(void) {
    vfs_register_fstype(&devfs_fstype);
}

int devfs_vfs_init(devfs_t *devfs, char *mount_path) {
    if (!mount_path) {
        return EFAULT;
    }

    vfs_t *vfs = vfs_mount(devfs, "devfs", mount_path, NULL);
    if (!vfs) {
        return ENOTRECOVERABLE;
    }

    return EOK;
}
