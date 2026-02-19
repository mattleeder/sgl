#ifndef sql_memory
#define sql_memory

#include <stdint.h>
#include <stdio.h>
#include <limits.h>

#define MIN_CAPACITY 8

static inline size_t grow_capacity(size_t capacity) {
    size_t new_capacity = 0;
    if (capacity < MIN_CAPACITY) return MIN_CAPACITY;
    if (capacity > SIZE_MAX / 2) return SIZE_MAX;
    return capacity * 2;
}

#define GROW_ARRAY(type, pointer, oldCount, newCount) \
    ((type*)reallocate(pointer, sizeof(type) * (oldCount), \
        sizeof(type) * (newCount)))

#define FREE_ARRAY(type, pointer, oldCount) \
    (reallocate(pointer, sizeof(type) * (oldCount), 0))

void* reallocate(void *pointer, size_t old_size, size_t new_size);

#endif