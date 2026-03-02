#include "ramfs.h"
#include "user/access.h"
#include "user/user.h"
#include <errors.h>
#include <fs/file_io.h>
#include <memory/heap/kheap.h>
#include <stdio.h>
#include <string.h>
#include <util/macro.h>

ramfs_t *ramfs_create_fs() {
    ramfs_t *ramfs = kmalloc(sizeof(ramfs_t));
    memset(ramfs, 0, sizeof(ramfs_t));
    return ramfs;
}

ramfs_node_t *ramfs_create_node(ramfs_ftype_t ftype) {
    ramfs_node_t *node = kmalloc(sizeof(ramfs_node_t));
    memset(node, 0, sizeof(ramfs_node_t));
    node->type = ftype;

    switch (ftype) {
    case RAMFS_DIRECTORY:
        node->mode = S_IFDIR | 0755;
        break;
    case RAMFS_FILE:
        node->mode = S_IFREG | 0644;
        break;
    case RAMFS_SYMLINK:
        node->mode = S_IFLNK | 0777;
        break;
    }


    return node;
}

int ramfs_find_node(ramfs_t *ramfs, char *path, ramfs_node_t **out) {
    ramfs_node_t *cur_node = ramfs->root_node->child; // root node is /
    *out                   = NULL;

    // use this to check for parents, and eventually create them
    if (path[0] == '/') {
        path++;
    }

    if (path[0] == '\0') {
        *out = ramfs->root_node;
        return 0;
    }

    char *name_dup = strdup(path);
    char *temp     = name_dup;
    char *dir;

    // j = level of current node
    for (int j = 0; *temp; j++) {
        UNUSED(j);
        dir = strtok_r(NULL, "/", &temp);

        for (; cur_node != NULL; cur_node = cur_node->sibling) {
            if (strcmp(cur_node->name, dir) != 0)
                continue;

            // if there's more to parse, go to the child
            if (*temp) {
                cur_node = cur_node->child;
                break;
            }

            // we should be fine and we can exit the loop
            break;
        }
    }

    kfree(name_dup);
    *out = cur_node;

    if (!cur_node) {
        return -1;
    }

    return 0;
}

/**
 * Create all RAMFS nodes for a given path, if necessary
 * @param ramfs the ramfs to work with
 * @param path the path of the node that needs to be created
 * @param out the node that represents the given path
 */
int ramfs_node_add(ramfs_t *ramfs, char *path, ramfs_node_t **out) {
    if (!ramfs || !ramfs->root_node || !path) {
        return EFAULT;
    }

    if (path[0] == '/') {
        path++;
    }

    char *name_dup = strdup(path);
    char *temp     = name_dup;
    char *dir;

    ramfs_node_t *cur_node = ramfs->root_node;
    ramfs_ftype_t rt;

    // j = level of current node
    for (int j = 0; *temp; j++) {
        UNUSED(j);
        dir = strtok_r(NULL, "/", &temp);

        for (; cur_node != NULL; cur_node = cur_node->sibling) {
            if (*temp) {
                rt = RAMFS_DIRECTORY;
            } else {
                rt = RAMFS_FILE;
            }

            if (strcmp(cur_node->name, dir) != 0) {
                if (cur_node->sibling) {
                    continue;
                }

                // finally, create the entry
                ramfs_node_t *n = ramfs_create_node(rt);
                n->name         = strdup(dir);
                continue;
            }

            if (cur_node->type != rt) {
                continue;
            }

            // we should be fine and we can exit the loop
            break;
        }

        // go to the child
        if (*temp) {
            if (strchr(temp, '/')) {
                rt = RAMFS_DIRECTORY;
            } else {
                rt = RAMFS_FILE;
            }

            if (!cur_node->child) {
                ramfs_node_t *n = ramfs_create_node(rt);
                n->name         = strdup(temp);
                ramfs_append_child(cur_node, n);
            }

            cur_node = cur_node->child;
        } else {
            *out = cur_node;
        }

        if (!cur_node) {
            // something went really wrong lol
            return ENOTRECOVERABLE;
        }
    }

    return EOK;
}

// @returns 0 if a node was found
// @returns 1 if a node wasn't found and it has been created
int ramfs_find_or_create_node(ramfs_t *ramfs, char *path,
                              ramfs_ftype_t ramfs_ftype, ramfs_node_t **out) {
    ramfs_node_t *found = NULL;
    ramfs_find_node(ramfs, path, &found);

    if (!found) {
        *out = ramfs_create_node(ramfs_ftype);
        return 1;
    }

    *out = found;
    return 0;
}

// appends a node to a list of a parent's children
int ramfs_append_child(ramfs_node_t *parent, ramfs_node_t *child) {
    if (!parent->child) {
        parent->child = child;
        return 0;
    }

    ramfs_node_t *last_child;
    for (last_child = parent->child; last_child->sibling != NULL;
         last_child = last_child->sibling)
        ;
    last_child->sibling = child;

    return 0;
}

int ramfs_print(ramfs_node_t *node, int lvl) {
    if (!node) {
        return -1;
    }

    int a = lvl;

    while (a--) {
        kprintf("  ");
    }

    switch (node->type) {
    case RAMFS_DIRECTORY:
        kprintf("+ %-20s", node->name);
        break;

    case RAMFS_FILE:
        kprintf("  %-20s", node->name);
        kprintf(" | % 10zuB", node->size, node->data);
        break;

    case RAMFS_SYMLINK:
        kprintf("  %-20s -> %s", node->name, (char *)node->data);
        break;
    }

    kprintf("\n");

    if (node->child) {
        ramfs_print(node->child, lvl + 1);
    }

    if (node->sibling) {
        ramfs_print(node->sibling, lvl);
    }
    return 0;
}

size_t ramfs_get_node_size(ramfs_node_t *node) {
    if (!node) {
        return EFAULT;
    }

    size_t s = 0;

    switch (node->type) {
    case RAMFS_FILE:
        s = node->size;
        break;

    case RAMFS_DIRECTORY:
        for (ramfs_node_t *n = node->child; n != NULL; n = n->sibling) {
            s += ramfs_get_node_size(n);
        }
        break;

    case RAMFS_SYMLINK:
        s = strlen((char *)node->data) + 1;
        break;
    }

    return s;
}

int ramfs_open(vnode_t **vnode_r, int flags, bool clone, fileio_t **fio_out) {
    UNUSED(clone);

    // TODO: flags
    // TODO: clone

    // 1. find the file in the RAMFS

    vnode_t *vnode = *vnode_r;
    if (!vnode || !vnode_r) {
        return EFAULT;
    }

    // vfs_data should have the root RAMFS struct
    ramfs_t *ramfs = vnode->root_vfs->vfs_data;

    if (!ramfs) {
        return EFAULT;
    }

    ramfs_node_t *ramfs_node;
    ramfs_find_node(ramfs, vnode->path, &ramfs_node);

    if (!ramfs_node) {

        ramfs_node = (ramfs_node_t *)vnode->node_data;

        if (!ramfs_node) {

            if (!(flags & V_CREATE)) {
                return ENOENT;
            }

            // create the node
            if (ramfs_node_add(ramfs, vnode->path, &ramfs_node) != EOK) {
                return ENOTRECOVERABLE;
            }

            if (flags & V_DIR) {
                ramfs_node->type = RAMFS_DIRECTORY;
                ramfs_node->size = 0;
                ramfs_node->data = NULL;
            }
        }
    }

    // this is the RAMFS node to be attached to the vnode
    ramfs_node_t *v_ramfs_node = kmalloc(sizeof(ramfs_node_t));
    memcpy(v_ramfs_node, ramfs_node, sizeof(ramfs_node_t));

    // 2. create a buffer for the file (do not use the original file buffer)

    v_ramfs_node->size = ramfs_get_node_size(ramfs_node);
    v_ramfs_node->data = kmalloc(v_ramfs_node->size);

    if (v_ramfs_node->type == RAMFS_DIRECTORY) {
        // TODO

        // 03/08/2025: there's nothing to do i think, but i'll keep it here just
        // in case
    } else if (v_ramfs_node->type == RAMFS_SYMLINK) {
        memcpy(v_ramfs_node->data, ramfs_node->data, v_ramfs_node->size);
    } else {
        memcpy(v_ramfs_node->data, ramfs_node->data, v_ramfs_node->size);
    }

    v_ramfs_node->child   = NULL;
    v_ramfs_node->sibling = NULL;

    vnode->node_data = v_ramfs_node;

    fileio_t *fio = *fio_out;
    if (!fio || !fio_out) {
        return EFAULT;
    }

    fio->buf_start = v_ramfs_node->data;
    fio->size      = v_ramfs_node->size;
    fio->private   = vnode;

    return EOK;
}

int ramfs_read(vnode_t *vn, size_t *bytes, size_t *offset, void *out) {
    if (!vn) {
        return -EFAULT;
    }

    memset(out, 0, (*bytes));

    ramfs_node_t *ramfs_node = (ramfs_node_t *)vn->node_data;
    if (!ramfs_node) {
        return -EFAULT;
    }

    if ((*bytes) > ramfs_node->size) {
        (*bytes) = ramfs_node->size;
    } else if ((*offset) >= ramfs_node->size) {
        return -EINVAL;
    }

    if ((*bytes) + (*offset) > ramfs_node->size) {
        // read whatever remains that can be copied
        (*bytes) = (ramfs_node->size - (*offset));
    }

    void *src = ramfs_node->data + (*offset);
    memcpy(out, src, (*bytes));

    return EOK;
}

int ramfs_write(vnode_t *vn, void *buf, size_t *bytes, size_t *offset) {
    if (!vn) {
        return EFAULT;
    }

    ramfs_node_t *ramfs_node = vn->node_data;
    if (!ramfs_node) {
        return EFAULT;
    }

    if ((*bytes) + (*offset) > ramfs_node->size) {
        // we need to do some relocation
        size_t more = ((*bytes) + (*offset)) - ramfs_node->size;

        void *new_data    = krealloc(ramfs_node->data, ramfs_node->size + more);
        ramfs_node->data  = new_data;
        ramfs_node->size += more;
    }

    void *dst = ramfs_node->data + (*offset);
    memcpy(dst, buf, (*bytes));

    return EOK;
}

int ramfs_close(vnode_t *vnode, int flags, bool clone) {
    UNUSED(flags);
    UNUSED(clone);

    // TODO: flags
    // TODO: clone

    if (!vnode) {
        return EFAULT;
    }

    ramfs_node_t *ramfs_node = vnode->node_data;
    if (!ramfs_node) {
        return EFAULT;
    }

    // we'll sync the new buffer

    ramfs_t *ramfs = vnode->root_vfs->vfs_data;
    if (!ramfs) {
        return EFAULT;
    }

    ramfs_node_t *ramfs_node_original;
    char *path = vnode->path + strlen(vnode->root_vfs->root_vnode->path);
    ramfs_find_node(ramfs, path, &ramfs_node_original);

    if (!ramfs_node_original) {
        return ENOENT; // this file is probably not ours
    }

    if (ramfs_node_original->data != ramfs_node->data) {
        kfree(ramfs_node_original->data);
        // the old node points to the original (now probably updated) data
        ramfs_node_original->data = ramfs_node->data;
        ramfs_node_original->size = ramfs_node->size;
    }

    // get rid of the RAMFS node on vnode
    kfree(vnode->node_data);

    return EOK;
}

int ramfs_ioctl(vnode_t *vnode, int request, void *arg) {
    if (!vnode) {
        return EFAULT;
    }

    UNUSED(request);
    UNUSED(arg);

    return ENOSYS;
}

int ramfs_lookup(vnode_t *parent, const char *name, vnode_t **out) {
    if (!parent || !name || !out) {
        return EFAULT;
    }

    ramfs_t *ramfs = parent->root_vfs->vfs_data;
    if (!ramfs) {
        return EFAULT;
    }

    ramfs_node_t *parent_node = parent->node_data;

    if (!parent_node) {
        char *rel_path =
            parent->path + strlen(parent->root_vfs->root_vnode->path);
        if (rel_path[0] == '/')
            rel_path++;

        if (rel_path[0] == '\0' ||
            strcmp(parent->path, parent->root_vfs->root_vnode->path) == 0) {
            parent_node = ramfs->root_node;
        } else {
            ramfs_find_node(ramfs, rel_path, &parent_node);
        }
    }

    if (!parent_node) {
        return ENOENT;
    }

    if (parent_node->type != RAMFS_DIRECTORY) {
        return ENOTDIR;
    }

    int child_count = 0;
    for (ramfs_node_t *child = parent_node->child; child != NULL;
         child               = child->sibling) {
        if (strcmp(child->name, name) == 0) {

            size_t parent_len = strlen(parent->path);
            size_t child_len  = strlen(name);
            char *child_path  = kmalloc(parent_len + child_len + 2);
            strcpy(child_path, parent->path);
            if (child_path[parent_len - 1] != '/') {
                strcat(child_path, "/");
            }
            strcat(child_path, name);

            vnode_type_t vtype = VNODE_REGULAR;
            if (child->type == RAMFS_DIRECTORY)
                vtype = VNODE_DIR;
            else if (child->type == RAMFS_SYMLINK)
                vtype = VNODE_LINK;

            vnode_t *child_vnode =
                vnode_create(parent->root_vfs, child_path, vtype, child);
            child_vnode->mode = child->mode;
#ifdef CONFIG_FILE_OWNER_GID_SETTING_SYSV
    child_vnode->uid = get_current_cred()->uid;
    child_vnode->gid = get_current_cred()->gid;
#else
    child_vnode->uid = get_current_cred()->uid;
    child_vnode->gid = parent->gid;
#endif // CONFIG_FILE_OWNER_GID_SETTING_SYSV
            memcpy(child_vnode->ops, parent->ops, sizeof(vnops_t));

            *out = child_vnode;
            return EOK;
        }
    }

    return ENOENT;
}

int ramfs_readdir(vnode_t *vnode, dirent_t *entries, size_t *count) {
    if (!vnode || !entries || !count) {
        return EFAULT;
    }

    if (vnode->vtype != VNODE_DIR) {
        return ENOTDIR;
    }

    ramfs_t *ramfs = vnode->root_vfs->vfs_data;
    if (!ramfs) {
        return EFAULT;
    }

    ramfs_node_t *dir_node = vnode->node_data;
    if (!dir_node) {
        char *rel_path =
            vnode->path + strlen(vnode->root_vfs->root_vnode->path);
        if (rel_path[0] == '/')
            rel_path++;
        ramfs_find_node(ramfs, rel_path, &dir_node);
    }

    if (!dir_node || dir_node->type != RAMFS_DIRECTORY) {
        return ENOTDIR;
    }

    size_t idx = 0;
    size_t max = *count;

    for (ramfs_node_t *child = dir_node->child; child != NULL && idx < max;
         child               = child->sibling) {
        entries[idx].d_ino    = (uint64_t)child;
        entries[idx].d_off    = idx + 1;
        entries[idx].d_reclen = sizeof(dirent_t);

        if (child->type == RAMFS_DIRECTORY) {
            entries[idx].d_type = DT_DIR;
        } else if (child->type == RAMFS_SYMLINK) {
            entries[idx].d_type = DT_LNK;
        } else {
            entries[idx].d_type = DT_REG;
        }

        strncpy(entries[idx].d_name, child->name,
                sizeof(entries[idx].d_name) - 1);
        entries[idx].d_name[sizeof(entries[idx].d_name) - 1] = '\0';

        idx++;
    }

    *count = idx;
    return EOK;
}

int ramfs_readlink(vnode_t *vnode, char *buf, size_t size) {
    if (!vnode || !buf) {
        return EFAULT;
    }

    if (vnode->vtype != VNODE_LINK) {
        return EINVAL;
    }

    ramfs_node_t *link_node = vnode->node_data;
    if (!link_node || link_node->type != RAMFS_SYMLINK) {
        return EINVAL;
    }

    if (!link_node->data) {
        return EINVAL;
    }

    size_t target_len = strlen((char *)link_node->data);
    size_t copy_len   = target_len < size - 1 ? target_len : size - 1;

    memcpy(buf, link_node->data, copy_len);
    buf[copy_len] = '\0';

    return EOK;
}

int ramfs_mkdir(vnode_t *parent, const char *name, int mode) {
    UNUSED(mode);

    if (!parent || !name) {
        return EFAULT;
    }

    ramfs_t *ramfs = parent->root_vfs->vfs_data;
    if (!ramfs) {
        return EFAULT;
    }

    ramfs_node_t *parent_node = parent->node_data;
    if (!parent_node) {
        char *rel_path =
            parent->path + strlen(parent->root_vfs->root_vnode->path);
        if (rel_path[0] == '/')
            rel_path++;
        ramfs_find_node(ramfs, rel_path, &parent_node);
    }

    if (!parent_node || parent_node->type != RAMFS_DIRECTORY) {
        return ENOTDIR;
    }

    for (ramfs_node_t *child = parent_node->child; child != NULL;
         child               = child->sibling) {
        if (strcmp(child->name, name) == 0) {
            return EEXIST;
        }
    }

    ramfs_node_t *new_dir = ramfs_create_node(RAMFS_DIRECTORY);
    new_dir->name         = strdup(name);
    new_dir->mode         = S_IFDIR | (mode & 0777);

    ramfs_append_child(parent_node, new_dir);
    return EOK;
}

int ramfs_rmdir(vnode_t *parent, const char *name) {
    if (!parent || !name) {
        return EFAULT;
    }

    ramfs_t *ramfs = parent->root_vfs->vfs_data;
    if (!ramfs) {
        return EFAULT;
    }

    ramfs_node_t *parent_node = parent->node_data;
    if (!parent_node) {
        char *rel_path =
            parent->path + strlen(parent->root_vfs->root_vnode->path);
        if (rel_path[0] == '/')
            rel_path++;
        ramfs_find_node(ramfs, rel_path, &parent_node);
    }

    if (!parent_node || parent_node->type != RAMFS_DIRECTORY) {
        return ENOTDIR;
    }

    ramfs_node_t **prev = &parent_node->child;
    for (ramfs_node_t *child = parent_node->child; child != NULL;
         prev = &child->sibling, child = child->sibling) {
        if (strcmp(child->name, name) == 0) {
            if (child->type != RAMFS_DIRECTORY) {
                return ENOTDIR;
            }

            if (child->child != NULL) {
                return ENOTEMPTY;
            }

            *prev = child->sibling;
            kfree(child->name);
            kfree(child);
            return EOK;
        }
    }

    return ENOENT;
}

int ramfs_create(vnode_t *parent, const char *name, mode_t mode, vnode_t **out) {
    if (!parent || !name || !out) {
        return EFAULT;
    }

    ramfs_t *ramfs = parent->root_vfs->vfs_data;
    if (!ramfs) {
        return EFAULT;
    }

    ramfs_node_t *parent_node = parent->node_data;
    if (!parent_node) {
        char *rel_path =
            parent->path + strlen(parent->root_vfs->root_vnode->path);
        if (rel_path[0] == '/')
            rel_path++;
        ramfs_find_node(ramfs, rel_path, &parent_node);
    }

    if (!parent_node || parent_node->type != RAMFS_DIRECTORY) {
        return ENOTDIR;
    }

    for (ramfs_node_t *child = parent_node->child; child != NULL;
         child               = child->sibling) {
        if (strcmp(child->name, name) == 0) {
            return EEXIST;
        }
    }

    ramfs_ftype_t rt = RAMFS_FILE;

    ramfs_node_t *new_file = ramfs_create_node(rt);
    new_file->name         = strdup(name);
    new_file->size         = 0;
    new_file->data         = NULL;
    new_file->mode         = S_IFREG | mode;

    ramfs_append_child(parent_node, new_file);

    size_t parent_len = strlen(parent->path);
    size_t name_len   = strlen(name);
    char *file_path   = kmalloc(parent_len + name_len + 2);
    strcpy(file_path, parent->path);
    if (file_path[parent_len - 1] != '/') {
        strcat(file_path, "/");
    }
    strcat(file_path, name);

    vnode_t *file_vnode =
        vnode_create(parent->root_vfs, file_path, VNODE_REGULAR, new_file);
    memcpy(file_vnode->ops, parent->ops, sizeof(vnops_t));
    file_vnode->mode = new_file->mode;

#ifdef CONFIG_FILE_OWNER_GID_SETTING_SYSV
    file_vnode->uid = get_current_cred()->uid;
    file_vnode->gid = get_current_cred()->gid;
#else
    file_vnode->uid = get_current_cred()->uid;
    file_vnode->gid = parent->gid;
#endif // CONFIG_FILE_OWNER_GID_SETTING_SYSV

    *out = file_vnode;
    return EOK;
}

int ramfs_remove(vnode_t *parent, const char *name) {
    if (!parent || !name) {
        return EFAULT;
    }

    ramfs_t *ramfs = parent->root_vfs->vfs_data;
    if (!ramfs) {
        return EFAULT;
    }

    ramfs_node_t *parent_node = parent->node_data;
    if (!parent_node) {
        char *rel_path =
            parent->path + strlen(parent->root_vfs->root_vnode->path);
        if (rel_path[0] == '/')
            rel_path++;
        ramfs_find_node(ramfs, rel_path, &parent_node);
    }

    if (!parent_node || parent_node->type != RAMFS_DIRECTORY) {
        return ENOTDIR;
    }

    ramfs_node_t **prev = &parent_node->child;
    for (ramfs_node_t *child = parent_node->child; child != NULL;
         prev = &child->sibling, child = child->sibling) {
        if (strcmp(child->name, name) == 0) {
            if (child->type == RAMFS_DIRECTORY) {
                return EISDIR;
            }

            *prev = child->sibling;
            kfree(child->name);
            if (child->data)
                kfree(child->data);
            kfree(child);
            return EOK;
        }
    }

    return ENOENT;
}

int ramfs_symlink(vnode_t *parent, const char *name, const char *target) {
    if (!parent || !name || !target) {
        return EFAULT;
    }

    ramfs_t *ramfs = parent->root_vfs->vfs_data;
    if (!ramfs) {
        return EFAULT;
    }

    ramfs_node_t *parent_node = parent->node_data;
    if (!parent_node) {
        char *rel_path =
            parent->path + strlen(parent->root_vfs->root_vnode->path);
        if (rel_path[0] == '/')
            rel_path++;
        ramfs_find_node(ramfs, rel_path, &parent_node);
    }

    if (!parent_node || parent_node->type != RAMFS_DIRECTORY) {
        return ENOTDIR;
    }

    for (ramfs_node_t *child = parent_node->child; child != NULL;
         child               = child->sibling) {
        if (strcmp(child->name, name) == 0) {
            return EEXIST;
        }
    }

    ramfs_node_t *new_link = ramfs_create_node(RAMFS_SYMLINK);
    new_link->name         = strdup(name);
    new_link->size         = strlen(target) + 1;
    new_link->data         = strdup(target);

    ramfs_append_child(parent_node, new_link);
    return EOK;
}

vnops_t ramfs_vnops = {
    .open     = ramfs_open,
    .close    = ramfs_close,
    .read     = ramfs_read,
    .write    = ramfs_write,
    .ioctl    = ramfs_ioctl,
    .lookup   = ramfs_lookup,
    .readdir  = ramfs_readdir,
    .readlink = ramfs_readlink,
    .mkdir    = ramfs_mkdir,
    .rmdir    = ramfs_rmdir,
    .create   = ramfs_create,
    .remove   = ramfs_remove,
    .symlink  = ramfs_symlink,
};

static int ramfs_vfs_mount(vfs_t *vfs, char *path, void *data) {
    UNUSED(path);
    UNUSED(data);

    if (!vfs) {
        return EFAULT;
    }

    return EOK;
}

static int ramfs_vfs_unmount(vfs_t *vfs) {
    if (!vfs) {
        return EFAULT;
    }

    ramfs_t *ramfs = vfs->vfs_data;
    if (ramfs && ramfs->root_node) {
        // TODO: free ramfs tree
    }

    return EOK;
}

static int ramfs_vfs_root(vfs_t *vfs, vnode_t **out) {
    if (!vfs || !out) {
        return EFAULT;
    }

    *out = vfs->root_vnode;
    vnode_ref(*out);

    return EOK;
}

static int ramfs_vfs_statfs(vfs_t *vfs, statfs_t *stat) {
    if (!vfs || !stat) {
        return EFAULT;
    }

    ramfs_t *ramfs = vfs->vfs_data;
    if (!ramfs) {
        return EFAULT;
    }

    stat->block_size   = 1;
    stat->total_blocks = ramfs->ramfs_size;
    stat->free_blocks  = 0;
    stat->total_nodes  = 0;
    stat->free_nodes   = 0;

    return EOK;
}

static int ramfs_vfs_sync(vfs_t *vfs) {
    UNUSED(vfs);
    return EOK;
}

vfsops_t ramfs_vfsops = {
    .mount   = ramfs_vfs_mount,
    .unmount = ramfs_vfs_unmount,
    .root    = ramfs_vfs_root,
    .statfs  = ramfs_vfs_statfs,
    .sync    = ramfs_vfs_sync,
};

static int ramfs_fstype_mount(void *device, char *mount_point, void *mount_data,
                              vfs_t **out) {
    UNUSED(mount_data);

    ramfs_t *ramfs = (ramfs_t *)device;
    if (!ramfs) {
        ramfs                  = ramfs_create_fs();
        ramfs->root_node       = ramfs_create_node(RAMFS_DIRECTORY);
        ramfs->root_node->name = strdup("/");
    }

    if (!ramfs->root_node) {
        ramfs->root_node       = ramfs_create_node(RAMFS_DIRECTORY);
        ramfs->root_node->name = strdup("/");
    }

    vfs_fstype_t fstype;
    memset(&fstype, 0, sizeof(vfs_fstype_t));
    strncpy(fstype.name, "ramfs", sizeof(fstype.name) - 1);

    vfs_t *vfs = vfs_create_fs(&fstype, ramfs);
    if (!vfs) {
        return ENOMEM;
    }

    memcpy(vfs->ops, &ramfs_vfsops, sizeof(vfsops_t));

    vfs->root_vnode =
        vnode_create(vfs, mount_point, VNODE_DIR, ramfs->root_node);
    if (!vfs->root_vnode) {
        kfree(vfs->ops);
        kfree(vfs);
        return ENOMEM;
    }

    memcpy(vfs->root_vnode->ops, &ramfs_vnops, sizeof(vnops_t));
    vfs->root_vnode->mode = ramfs->root_node->mode;
    vfs->root_vnode->uid  = get_current_cred()->uid;
    vfs->root_vnode->gid  = get_current_cred()->gid;
    *out = vfs;
    return EOK;
}

static vfs_fstype_t ramfs_fstype = {
    .id = 0, .name = "ramfs", .mount = ramfs_fstype_mount, .next = NULL};

void ramfs_init(void) {
    vfs_register_fstype(&ramfs_fstype);
}

int ramfs_vfs_init(ramfs_t *ramfs, char *path) {
    if (!path) {
        return EFAULT;
    }

    vfs_t *vfs = vfs_mount(ramfs, "ramfs", path, NULL);
    if (!vfs) {
        return ENOTRECOVERABLE;
    }

    return EOK;
}
