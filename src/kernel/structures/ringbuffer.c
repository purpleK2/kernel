#include "ringbuffer.h"

#include <memory/heap/kheap.h>
#include <string.h>

bool rb_init(ringbuffer_t *rb, size_t capacity) {
   if (!rb || capacity == 0) {
        return false;
   }

   rb->buffer = (char *)kmalloc(capacity);
   if (!rb->buffer) {
        return false;
   }
   memset(rb->buffer, 0, capacity);

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

int rb_write(ringbuffer_t *rb, const char *data, size_t len, int flags) {
    (void)flags;
    if (!rb || !data || len == 0) {
        return -1;
    }

    size_t written = 0;
    for (size_t i = 0; i < len; i++) {
        if (rb_push(rb, data[i])) {
            written++;
        } else {
            break;
        }
    }

    return written;
}

int rb_read(ringbuffer_t *rb, char *data, size_t len, int flags) {
    (void)flags;
    if (!rb || !data || len == 0) {
        return -1;
    }

    size_t read = 0;
    for (size_t i = 0; i < len; i++) {
        if (rb_pop(rb, &data[i])) {
            read++;
        } else {
            break;
        }
    }

    return read;
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

bool rb_peek(ringbuffer_t *rb, size_t index, char *out) {
    if (index >= rb->size || !out) return false;
    *out = rb->buffer[(rb->tail + index) % rb->capacity];
    return true;
}
