#include "meowdev.h"
#include "dev/device.h"
#include "fs/vfs/vfs.h"

#include <autoconf.h>
#include <pit/pit.h>

#include <memory/heap/kheap.h>

#include <scheduler/scheduler.h>

#include <system/spinlock.h>

#include <errors.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define MDEV_EVENT_QUEUE_SIZE 256

struct mdev_device {
    device_t *dev;
    mdev_device_kind_t kind;
    int index;

    mdev_event_t queue[MDEV_EVENT_QUEUE_SIZE];
    size_t queue_head;
    size_t queue_tail;
    size_t queue_count;

    int grab_pid;

    atomic_flag lock;
    struct mdev_device *next;
};

static atomic_flag mdev_list_lock    = ATOMIC_FLAG_INIT;
static struct mdev_device *mdev_list = NULL;
static int mdev_kbd_count            = 0;
static int mdev_mouse_count          = 0;

static struct timeval mdev_now_monotonic(void) {
    uint64_t ticks = get_ticks();
    uint64_t ms    = ticks * CONFIG_SCHED_TIMER_INTERVAL_MS;

    struct timeval tv;
    tv.tv_sec  = (int64_t)(ms / 1000ULL);
    tv.tv_usec = (int64_t)((ms % 1000ULL) * 1000ULL);
    return tv;
}

static inline int mdev_current_pid(void) {
    pcb_t *proc = get_current_pcb();
    return proc ? proc->pid : -1;
}

static int mdev_device_read(device_t *dev, void *buffer, size_t size,
                            size_t offset) {
    (void)offset;

    if (!dev || !buffer) {
        return -EFAULT;
    }

    struct mdev_device *mdev = (struct mdev_device *)dev->data;
    if (!mdev) {
        return -EFAULT;
    }

    int pid = mdev_current_pid();

    spinlock_acquire(&mdev->lock);

    if (mdev->grab_pid < 0 || (pid >= 0 && mdev->grab_pid != pid)) {
        spinlock_release(&mdev->lock);
        return -EACCES;
    }

    size_t max_events = size / sizeof(mdev_event_t);
    if (max_events == 0) {
        spinlock_release(&mdev->lock);
        return -EINVAL;
    }

    size_t to_copy =
        (mdev->queue_count < max_events) ? mdev->queue_count : max_events;
    mdev_event_t *out = (mdev_event_t *)buffer;

    for (size_t i = 0; i < to_copy; i++) {
        out[i]           = mdev->queue[mdev->queue_tail];
        mdev->queue_tail = (mdev->queue_tail + 1) % MDEV_EVENT_QUEUE_SIZE;
        mdev->queue_count--;
    }

    spinlock_release(&mdev->lock);

    return (int)(to_copy * sizeof(mdev_event_t));
}

static int mdev_device_write(device_t *dev, const void *buffer, size_t size,
                             size_t offset) {
    (void)dev;
    (void)buffer;
    (void)size;
    (void)offset;
    return -ENOSYS;
}

static int mdev_device_ioctl(device_t *dev, int request, void *arg) {
    if (!dev) {
        return -EFAULT;
    }

    struct mdev_device *mdev = (struct mdev_device *)dev->data;
    if (!mdev) {
        return -EFAULT;
    }

    if (request != MDEVGRABDEV) {
        return -EINVAL;
    }

    int pid  = mdev_current_pid();
    int grab = (int)(uintptr_t)arg;

    spinlock_acquire(&mdev->lock);

    if (grab) {
        if (mdev->grab_pid >= 0 && mdev->grab_pid != pid) {
            spinlock_release(&mdev->lock);
            return -EBUSY;
        }

        mdev->grab_pid = pid;
        spinlock_release(&mdev->lock);
        return EOK;
    }

    if (mdev->grab_pid >= 0 && mdev->grab_pid != pid) {
        spinlock_release(&mdev->lock);
        return -EPERM;
    }

    mdev->grab_pid = -1;
    spinlock_release(&mdev->lock);
    return EOK;
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
    return mkdir_p(MEOWDEV_ROOT_PATH, 0755);
}

int meowdev_deinit(void) {
    return EOK;
}

mdev_device_t *mdev_register_device(mdev_device_kind_t kind) {
    int mkdir_res = meowdev_init();
    if (mkdir_res != EOK) {
        return NULL;
    }

    struct mdev_device *mdev = kmalloc(sizeof(struct mdev_device));
    if (!mdev) {
        return NULL;
    }
    memset(mdev, 0, sizeof(struct mdev_device));

    device_t *dev = kmalloc(sizeof(device_t));
    if (!dev) {
        kfree(mdev);
        return NULL;
    }
    memset(dev, 0, sizeof(device_t));

    int index;
    char dev_name[DEVICE_NAME_MAX];
    char dev_path[64];

    if (kind == MDEV_DEVICE_KEYBOARD) {
        spinlock_acquire(&mdev_list_lock);
        index = mdev_kbd_count++;
        spinlock_release(&mdev_list_lock);

        snprintf(dev_name, sizeof(dev_name), "kbd%d", index);
        snprintf(dev_path, sizeof(dev_path), "input/kbd%d", index);
    } else {
        spinlock_acquire(&mdev_list_lock);
        index = mdev_mouse_count++;
        spinlock_release(&mdev_list_lock);

        snprintf(dev_name, sizeof(dev_name), "mouse%d", index);
        snprintf(dev_path, sizeof(dev_path), "input/mouse%d", index);
    }

    dev->dev_node_path = strdup(dev_path);
    if (!dev->dev_node_path) {
        kfree(dev);
        kfree(mdev);
        return NULL;
    }

    snprintf(dev->name, DEVICE_NAME_MAX, "%s", dev_name);
    dev->major = 13;
    dev->minor = index;
    dev->type  = DEVICE_TYPE_CHAR;
    dev->read  = mdev_device_read;
    dev->write = mdev_device_write;
    dev->ioctl = mdev_device_ioctl;
    dev->data  = mdev;

    mdev->dev         = dev;
    mdev->kind        = kind;
    mdev->index       = index;
    mdev->queue_head  = 0;
    mdev->queue_tail  = 0;
    mdev->queue_count = 0;
    mdev->grab_pid    = -1;
    atomic_flag_clear(&mdev->lock);

    if (register_device(dev) != EOK) {
        kfree(dev->dev_node_path);
        kfree(dev);
        kfree(mdev);
        return NULL;
    }

    spinlock_acquire(&mdev_list_lock);
    mdev->next = mdev_list;
    mdev_list  = mdev;
    spinlock_release(&mdev_list_lock);

    return mdev;
}

mdev_device_t *mdev_register_keyboard(void) {
    return mdev_register_device(MDEV_DEVICE_KEYBOARD);
}

mdev_device_t *mdev_register_mouse(void) {
    return mdev_register_device(MDEV_DEVICE_MOUSE);
}

int mdev_add_event(mdev_device_t *dev, uint16_t type, uint16_t code,
                   int32_t value) {
    if (!dev) {
        return -EFAULT;
    }

    if (type != MDEV_EVENT_TYPE_KEY && type != MDEV_EVENT_TYPE_REL) {
        return -EINVAL;
    }

    spinlock_acquire(&dev->lock);

    if (dev->queue_count == MDEV_EVENT_QUEUE_SIZE) {
        dev->queue_tail = (dev->queue_tail + 1) % MDEV_EVENT_QUEUE_SIZE;
        dev->queue_count--;
    }

    mdev_event_t *event = &dev->queue[dev->queue_head];
    event->timestamp    = mdev_now_monotonic();
    event->type         = type;
    event->code         = code;
    event->value        = (uint32_t)value;

    dev->queue_head = (dev->queue_head + 1) % MDEV_EVENT_QUEUE_SIZE;
    dev->queue_count++;

    spinlock_release(&dev->lock);
    return EOK;
}