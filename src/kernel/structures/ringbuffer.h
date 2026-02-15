#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include <stdbool.h>
#include <stddef.h>
typedef struct ringbuffer {
    char *buffer;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t size;
} ringbuffer_t;

bool rb_init(ringbuffer_t *rb, size_t capacity);
void rb_free(ringbuffer_t *rb);

bool rb_push(ringbuffer_t *rb, char value);
bool rb_pop(ringbuffer_t *rb, char *out);

int rb_write(ringbuffer_t *rb, const char *data, size_t len, int flags);
int rb_read(ringbuffer_t *rb, char *data, size_t len, int flags);

bool rb_is_empty(const ringbuffer_t *rb);
bool rb_is_full(const ringbuffer_t *rb);

bool rb_peek(ringbuffer_t *rb, size_t index, char *out);

size_t rb_size(const ringbuffer_t *rb);
size_t rb_capacity(const ringbuffer_t *rb);

#endif // RINGBUFFER_H