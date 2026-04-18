#include "meowdev.h"
#include "dev/device.h"
#include "fs/vfs/vfs.h"

#include <memory/heap/kheap.h>

#include <errors.h>
#include <stdio.h>
#include <string.h>

static int meowdev_testkbd_read(device_t *dev, void *buffer, size_t size,
                                size_t offset) {
    (void)dev;
    (void)buffer;
    (void)size;
    (void)offset;
    return 0;
}

static int meowdev_testkbd_write(device_t *dev, const void *buffer, size_t size,
                                 size_t offset) {
    (void)dev;
    (void)buffer;
    (void)offset;
    return (int)size;
}

static int meowdev_testkbd_ioctl(device_t *dev, int request, void *arg) {
    (void)dev;
    (void)request;
    (void)arg;
    return -ENOSYS;
}

static int mkdir_p(const char *path, mode_t mode) {
    char tmp[4096];
    char *p = NULL;
    size_t len;

    if (!path) {
        return -EINVAL;
    }

    len = strlen(path);

    if (len == 0 || len >= sizeof(tmp)) {
        return -ENAMETOOLONG;
    }

    strcpy(tmp, path);

    /* remove trailing slash */
    if (tmp[len - 1] == '/')
        tmp[len - 1] = '\0';

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';

            int ret = vfs_mkdir(tmp, mode);
            if (ret != 0) {
                if (ret != EEXIST)
                    return ret;
            }

            *p = '/';
        }
    }

    int ret = vfs_mkdir(tmp, mode);
    if (ret != 0) {
        if (ret != EEXIST)
            return ret;
    }

    return 0;
}

int meowdev_init(void) {
    int ret = mkdir_p(MEOWDEV_ROOT_PATH, 0755);
    if (ret != EOK) {
        return ret;
    }

    if (get_device("kbd0") != NULL) {
        return EOK;
    }

    device_t *dev = kmalloc(sizeof(device_t));
    if (!dev) {
        return ENOMEM;
    }
    memset(dev, 0, sizeof(device_t));

    snprintf(dev->name, DEVICE_NAME_MAX, "kbd0");
    dev->dev_node_path = strdup("input/kbd0");
    if (!dev->dev_node_path) {
        kfree(dev);
        return ENOMEM;
    }

    dev->major = 13;
    dev->minor = 0;
    dev->type  = DEVICE_TYPE_CHAR;
    dev->read  = meowdev_testkbd_read;
    dev->write = meowdev_testkbd_write;
    dev->ioctl = meowdev_testkbd_ioctl;

    ret = register_device(dev);
    if (ret != EOK) {
        kfree(dev->dev_node_path);
        kfree(dev);
        return ret;
    }

    return EOK;
}