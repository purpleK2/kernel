#include "futex.h"

#include <memory/heap/kheap.h>

futex_t* futex_list = NULL;
atomic_flag futex_lock = ATOMIC_FLAG_INIT;

futex_t *futex_get(uintptr_t addr) {
    spinlock_acquire(&futex_lock);

    futex_t *f = futex_list;

    while (f) {
        if (f->addr == addr) {
            spinlock_release(&futex_lock);
            return f;
        }
        f = f->next;
    }

    f = kmalloc(sizeof(futex_t));
    f->addr = addr;
    waitqueue_init(&f->wq);

    f->next = futex_list;
    futex_list = f;

    spinlock_release(&futex_lock);

    return f;
}