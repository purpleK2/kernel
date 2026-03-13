#ifndef FUTEX_H
#define FUTEX_H 1

#include <structures/waitqueue.h>
#include <stdatomic.h>
#include <stdint.h>

typedef struct futex {
    uintptr_t addr;
    waitqueue_t wq;
    struct futex *next;
} futex_t;

extern futex_t* futex_list;
extern atomic_flag futex_lock;

futex_t *futex_get(uintptr_t addr);

#endif // FUTEX_H