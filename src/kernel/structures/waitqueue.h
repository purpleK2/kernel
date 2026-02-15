#ifndef WAITQUEUE_H
#define WAITQUEUE_H 1

#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>

#include <scheduler/scheduler.h>

typedef struct waitqueue {
    tcb_t       *head;
    tcb_t       *tail;
    size_t       count;
    atomic_flag  lock;
} waitqueue_t;

static inline void waitqueue_init(waitqueue_t *wq) {
    wq->head  = NULL;
    wq->tail  = NULL;
    wq->count = 0;
    wq->lock  = (atomic_flag)ATOMIC_FLAG_INIT;
}

static inline void waitqueue_prepare_wait(waitqueue_t *wq) {
    tcb_t *me = get_current_tcb();
    if (!me) {
        return;
    }
    if (me->on_waitqueue) {
        return;
    }
    
    spinlock_acquire(&wq->lock);
    me->wq_next      = NULL;
    me->on_waitqueue  = 1;
    me->state         = THREAD_WAITING;
    if (wq->tail) {
        wq->tail->wq_next = me;
    } else {
        wq->head = me;
    }
    wq->tail = me;
    wq->count++;
    
    spinlock_release(&wq->lock);
}

static inline void waitqueue_sleep(waitqueue_t *wq) {
    waitqueue_prepare_wait(wq);
    while (get_current_tcb()->state == THREAD_WAITING) {
        __asm__ volatile("sti; hlt" ::: "memory");
    }
}

static inline bool waitqueue_wake_one(waitqueue_t *wq) {
    spinlock_acquire(&wq->lock);

    tcb_t *thread = wq->head;
    if (!thread) {
        spinlock_release(&wq->lock);
        return false;
    }

    wq->head = thread->wq_next;
    if (!wq->head)
        wq->tail = NULL;
    wq->count--;

    thread->wq_next     = NULL;
    thread->on_waitqueue = 0;
    thread->state        = THREAD_READY;

    spinlock_release(&wq->lock);

    scheduler_enqueue(thread);
    return true;
}

static inline size_t waitqueue_wake_all(waitqueue_t *wq) {
    spinlock_acquire(&wq->lock);
    size_t woken = 0;
    tcb_t *cur = wq->head;
    
    
    while (cur) {
        tcb_t *next = cur->wq_next;
        cur->wq_next     = NULL;
        cur->on_waitqueue = 0;
        cur->state        = THREAD_READY;
        scheduler_enqueue(cur);
        woken++;
        cur = next;
    }
    wq->head  = NULL;
    wq->tail  = NULL;
    wq->count = 0;
    spinlock_release(&wq->lock);
    
    return woken;
}

static inline size_t waitqueue_wake_expired(waitqueue_t *wq, uint64_t current_tick) {
    spinlock_acquire(&wq->lock);

    size_t  woken      = 0;
    tcb_t **pp         = &wq->head;
    tcb_t  *last_valid = NULL;

    while (*pp) {
        tcb_t *thread = *pp;

        if (thread->wakeup_tick != 0 &&
            thread->wakeup_tick <= current_tick) {
            /* Unlink from the queue. */
            *pp = thread->wq_next;
            wq->count--;

            thread->wakeup_tick  = 0;
            thread->wq_next      = NULL;
            thread->on_waitqueue  = 0;
            thread->state         = THREAD_READY;

            scheduler_enqueue(thread);
            woken++;
        } else {
            last_valid = thread;
            pp = &thread->wq_next;
        }
    }

    wq->tail = last_valid;

    spinlock_release(&wq->lock);
    return woken;
}

#endif // WAITQUEUE_H
