#include "ringbuffer.h"

#include <memory/heap/kheap.h>

bool rb_init(ringbuffer_t *rb, size_t capacity) {
   if (!rb || capacity == 0) {
        return false;
   }

   rb->buffer = (char *)kmalloc(capacity);
   if (!rb->buffer) {
        return false;
   }

    rb->capacity = capacity;
    rb->head = 0;
    rb->tail = 0;
    rb->size = 0;

    return true;
}

void rb_free(ringbuffer_t *rb) {
    if (!rb) {
        return;
    }
    kfree(rb->buffer);
    rb->buffer = NULL;
    rb->capacity = 0;
    rb->head = 0;
    rb->tail = 0;
    rb->size = 0;
}

bool rb_push(ringbuffer_t *rb, char value) {
    if (!rb || rb_is_full(rb)) {
        return false;
    }

    rb->buffer[rb->head] = value;
    rb->head = (rb->head + 1) % rb->capacity;
    rb->size++;

    return true;
}

bool rb_pop(ringbuffer_t *rb, char *out) {
    if (!rb || rb_is_empty(rb) || !out)
        return false;

    *out = rb->buffer[rb->tail];
    rb->tail = (rb->tail + 1) % rb->capacity;
    rb->size--;

    return true;
}


bool rb_is_empty(const ringbuffer_t *rb) {
    return rb->size == 0;
}

bool rb_is_full(const ringbuffer_t *rb) {
    return rb->size == rb->capacity;
}

size_t rb_size(const ringbuffer_t *rb) {
    return rb->size;
}

size_t rb_capacity(const ringbuffer_t *rb) {
    return rb->capacity;
}