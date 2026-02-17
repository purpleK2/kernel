#include "binfmt.h"
#include "errors.h"
#include "fs/vfs/vfs.h"
#include "memory/heap/kheap.h"
#include "scheduler/scheduler.h"
#include "stdio.h"
#include "user/access.h"
#include "user/user.h"
#include <string.h>

binfmt_loader_t **binfmt_loaders = NULL;
int binfmt_loader_count = 0;
int binfmt_loader_capacity = 0;

int binfmt_register_loader(binfmt_loader_t *loader) {
    if (binfmt_loader_count == binfmt_loader_capacity) {
        size_t new_cap = binfmt_loader_capacity ? binfmt_loader_capacity * 2 : 4;
        binfmt_loader_t **new_arr = krealloc(binfmt_loaders, sizeof(*binfmt_loaders) * new_cap);
        if (!new_arr) return -ENOMEM;
        binfmt_loaders = new_arr;
        binfmt_loader_capacity = new_cap;
    }
    binfmt_loaders[binfmt_loader_count++] = loader;
    return 0;
}

int binfmt_load(const char *path, const char **argv, const char **envp, binfmt_program_t *out) {
    if (!path || !out) {
        return -EFAULT;
    }

    for (int i = 0; i < binfmt_loader_count; i++) {
        binfmt_loader_t *loader = binfmt_loaders[i];

        fileio_t *file = open(path, 0, 0);
        if (!file || (int64_t)file < 0) {
            continue;
        }

        vnode_t *vnode = file->private;
        if (vnode_permission(get_current_cred(), vnode, X_OK) < 0) {
            close(file);
            return -EACCES;
        }

        uint8_t magic[loader->magic_size];
        if (read(file, loader->magic_size, magic) != loader->magic_size) {
            close(file);
            continue;
        }

        close(file);

        if (memcmp(magic, loader->magic, loader->magic_size) == 0) {
            debugf_debug("BINFMT: using loader %s for %s\n", loader->name, path);
            return loader->load(path, argv, envp, out);
        }
    }

    return -ENOEXEC;
}

int binfmt_run(binfmt_program_t *prog) {
    if (!prog) {
        return -EFAULT;
    }

    if (!prog->pcb) {
        return -EINVAL;
    }

    return proc_engage(prog->pcb);
}

int binfmt_exec(const char *path, const char **argv, const char **envp) {
    binfmt_program_t prog;
    memset(&prog, 0, sizeof(prog));

    int ret = binfmt_load(path, argv, envp, &prog);
    if (ret < 0) {
        debugf_warn("BINFMT: failed to load %s: %d\n", path, ret);
        return ret;
    }

    ret = binfmt_run(&prog);
    if (ret < 0) {
        debugf_warn("BINFMT: failed to run %s: %d\n", path, ret);
        return ret;
    }

    return prog.pid;
}