#ifndef sql_memory
#define sql_memory

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <stdbool.h>
#include <assert.h>

#define MIN_CAPACITY 8

struct MemVec {
    size_t  count;
    size_t  capacity;
};

static inline size_t grow_capacity(size_t capacity) {
    if (capacity < MIN_CAPACITY) return MIN_CAPACITY;
    if (capacity > SIZE_MAX / 2) return SIZE_MAX;
    return capacity * 2;
}

#define GROW_ARRAY(type, pointer, oldCount, newCount) \
    ((type*)reallocate(pointer, sizeof(type) * (oldCount), \
        sizeof(type) * (newCount)))

#define FREE_ARRAY(type, pointer, oldCount) \
    (reallocate(pointer, sizeof(type) * (oldCount), 0))

static inline void mem_vec_init(struct MemVec *mem_vec) {
    mem_vec->count =    0;
    mem_vec->capacity = 0;
}

void* reallocate(void *pointer, size_t old_size, size_t new_size);

#define DEFINE_VECTOR(type, name_pascal, name_snake)                                            \
struct name_pascal {                                                                            \
    size_t  count;                                                                              \
    size_t  capacity;                                                                           \
    type    *data;                                                                              \
};                                                                                              \
                                                                                                \
static inline void vector_##name_snake##_init(struct name_pascal *v) {                                   \
    v->count    = 0;                                                                            \
    v->capacity = 0;                                                                            \
    v->data     = NULL;                                                                         \
}                                                                                               \
                                                                                                \
static inline struct name_pascal *vector_##name_snake##_new() {                                          \
                                                                                                \
    struct name_pascal *vector = malloc(sizeof(struct name_pascal));                                          \
    if (!vector) {                                                                              \
        fprintf(stderr, "init_" #name_snake ": failed to malloc *vector\n");                    \
        exit(1);                                                                                \
    }                                                                                           \
                                                                                                \
    vector_##name_snake##_init(vector);                                                                  \
    return vector;                                                                              \
}                                                                                               \
                                                                                                \
static inline void vector_##name_snake##_free(struct name_pascal *v) {                                   \
    free(v->data);                                                                              \
    vector_##name_snake##_init(v);                                                                       \
}                                                                                               \
                                                                                                \
static inline void vector_##name_snake##_push(struct name_pascal *v, type value) {                       \
    if (v == NULL) {                                                                            \
        fprintf(stderr, "push_" #name_snake ": Writing to NULL  " #name_snake " pointer.\n");   \
        exit(1);                                                                                \
    }                                                                                           \
                                                                                                \
    if (v->capacity < v->count + 1) {                                                           \
       size_t old_capacity      = v->capacity;                                                  \
       v->capacity              = grow_capacity(old_capacity);                                  \
       v->data                  = GROW_ARRAY(type, v->data, old_capacity, v->capacity);         \
    }                                                                                           \
                                                                                                \
    v->data[v->count] = value;                                                                  \
    v->count++;                                                                                 \
}



#endif