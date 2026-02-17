#include "access.h"
#include "errors.h"
#include "user/user.h"

bool is_in_group(const user_cred_t *cred, gid_t gid) {
    if (!cred) {
        return false;
    }

    for (int i = 0; i < cred->ngroups; i++) {
        if (cred->groups[i] == gid) {
            return true;
        }
    }
    return false;
}

int vnode_permission(const user_cred_t *cred, const vnode_t *vnode, int mask) {
    if (!cred || !vnode)
        return -EFAULT;

    mode_t mode = vnode->mode;

    if (cred->euid == UID_ROOT) {
        if ((mask & X_OK) &&
            !(mode & (S_IXUSR | S_IXGRP | S_IXOTH)))
            return -EACCES;
        return 0;
    }

    if (cred->euid == vnode->uid) {
        if ((mask & R_OK) && !(mode & S_IRUSR)) return -EACCES;
        if ((mask & W_OK) && !(mode & S_IWUSR)) return -EACCES;
        if ((mask & X_OK) && !(mode & S_IXUSR)) return -EACCES;
        return 0;
    }

    if (cred->egid == vnode->gid ||
        is_in_group(cred, vnode->gid)) {
        if ((mask & R_OK) && !(mode & S_IRGRP)) return -EACCES;
        if ((mask & W_OK) && !(mode & S_IWGRP)) return -EACCES;
        if ((mask & X_OK) && !(mode & S_IXGRP)) return -EACCES;
        return 0;
    }

    if ((mask & R_OK) && !(mode & S_IROTH)) return -EACCES;
    if ((mask & W_OK) && !(mode & S_IWOTH)) return -EACCES;
    if ((mask & X_OK) && !(mode & S_IXOTH)) return -EACCES;

    return 0;
}