#ifndef sql_memory
#define sql_memory

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <stdbool.h>

#define MIN_CAPACITY 8

struct MemVec {
    size_t  count;
    size_t  capacity;
};

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

static inline void mem_vec_init(struct MemVec *mem_vec) {
    mem_vec->count =    0;
    mem_vec->capacity = 0;
}

void* reallocate(void *pointer, size_t old_size, size_t new_size);

// DEFINE_VECTOR(struct Column, columns)
#define DEFINE_VECTOR(type, name_pascal, name_snake)                                            \
struct name_pascal {                                                                            \
    size_t  count;                                                                              \
    size_t  capacity;                                                                           \
    type    *data;                                                                              \
};                                                                                              \
                                                                                                \
static void init_##name_snake(struct name_pascal *v) {                                          \
    v->count    = 0;                                                                            \
    v->capacity = 0;                                                                            \
    v->data     = NULL;                                                                         \
}                                                                                               \
                                                                                                \
static void free_##name_snake(struct name_pascal *v) {                                          \
    free(v->data);                                                                              \
    init_##name_snake(v);                                                                       \
}                                                                                               \
                                                                                                \
static void push_##name_snake(struct name_pascal *v, type value) {                              \
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

static inline size_t hash_int(int key) {
    return key;
}

static inline size_t hash_u64(uint64_t key) {
    return key;
}

static inline size_t hash_djb2(const char *string) {
    size_t hash = 5381;
    for (; *string; string++)
        hash = ((hash << 5) + hash) + (unsigned char)*string;
    return hash;
}

static inline size_t hash_djb2_unterminated(const char *string, size_t len) {
    size_t hash = 5381;
    for (int i = 0; i < len; string++)
        hash = ((hash << 5) + hash) + (unsigned char)string[i];
    return hash;
}

static inline size_t hash_string(const char *string) {
    return hash_djb2(string);
}

static inline size_t equals_string(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}


#define DEFINE_HASH_MAP(key_type, value_type, name_pascal, name_snake, hash_function, equals_function)                          \
                                                                                                                                \
struct name_pascal##HashMapNode {                                                                                               \
    struct name_pascal##HashMapNode     *next;                                                                                  \
    key_type                            key;                                                                                    \
    value_type                          *value;                                                                                 \
};                                                                                                                              \
                                                                                                                                \
DEFINE_VECTOR(struct name_pascal##HashMapNode*, name_pascal##HashMapBuckets, name_snake##_hash_map_buckets)                     \
                                                                                                                                \
struct name_pascal##HashMap {                                                                                                   \
    struct name_pascal##HashMapBuckets  buckets;                                                                                \
    float                               load_factor;                                                                            \
    size_t                              buckets_used;                                                                           \
};                                                                                                                              \
                                                                                                                                \
static void hash_map_##name_snake##_init(struct name_pascal##HashMap *hash_map, size_t initial_capacity, float load_factor) {   \
    init_##name_snake##_hash_map_buckets(&hash_map->buckets);                                                                   \
    if (initial_capacity == 0) {                                                                                                \
        initial_capacity = 1;                                                                                                   \
        fprintf(stderr, "hash_map_" #name_snake "_init: cannot have 0 initial capacity, setting to 1.\n");                      \
    }                                                                                                                           \
                                                                                                                                \
    hash_map->load_factor  = load_factor;                                                                                       \
    hash_map->buckets_used = 0;                                                                                                 \
                                                                                                                                \
    for (int i = 0; i < initial_capacity; i++) {                                                                                \
        push_##name_snake##_hash_map_buckets(&hash_map->buckets, NULL);                                                         \
    }                                                                                                                           \
}                                                                                                                               \
                                                                                                                                \
static bool grow_##name_snake_##hash_map(struct name_pascal##HashMap *hash_map) {                                               \
    size_t old_capacity                         = hash_map->buckets.capacity;                                                   \
    size_t new_capacity                         = grow(old_capacity);                                                           \
    struct name_pascal##HashMapNode **old_data  = hash_map->buckets.data;                                                       \
    struct name_pascal##HashMapNode **new_data  = calloc(new_capacity, sizeof(*new_data));                                      \
                                                                                                                                \
    if (!new_data) {                                                                                                            \
        fprintf(stderr, "grow_" #name_snake "hash_map: *new_data malloc failed.\n");                                            \
        exit(1);                                                                                                                \
    }                                                                                                                           \
                                                                                                                                \
    hash_map->buckets.data      = new_data;                                                                                     \
    hash_map->buckets.capacity  = new_capacity;                                                                                 \
    hash_map->buckets.count     = hash_map->buckets.capacity;                                                                   \
    hash_map->buckets_used      = 0;                                                                                            \
                                                                                                                                \
    for (int i = 0; i < old_capacity; i++) {                                                                                    \
        struct name_pascal##HashMapNode *curr = old_data[i];                                                                    \
        struct name_pascal##HashMapNode *next;                                                                                  \
        while (curr != NULL) {                                                                                                  \
            next = curr->next;                                                                                                  \
                                                                                                                                \
            size_t bucket_number = hash_function(curr->key) % new_capacity;                                                     \
            if (new_data[bucket_number] == NULL) hash_map->buckets_used++;                                                      \
            curr->next = new_data[bucket_number];                                                                               \
            new_data[bucket_number] = curr;                                                                                     \
                                                                                                                                \
            curr = next;                                                                                                        \
        }                                                                                                                       \
    }                                                                                                                           \
                                                                                                                                \
    free(old_data);                                                                                                             \
    return true;                                                                                                                \
}                                                                                                                               \
                                                                                                                                \
static bool hash_map_##name_snake##_set(struct name_pascal##HashMap *hash_map, key_type key, value_type *value) {               \
    size_t bucket_number = hash_function(key) % hash_map->buckets.capacity;                                                     \
                                                                                                                                \
    struct name_pascal##HashMapNode *old_head = hash_map->buckets.data[bucket_number];                                          \
    struct name_pascal##HashMapNode *curr = old_head;                                                                           \
                                                                                                                                \
    while (curr != NULL) {                                                                                                      \
        if (equals_function(curr->key, key)) {                                                                                  \
            curr->value = value;                                                                                                \
            return true;                                                                                                        \
        }                                                                                                                       \
        curr = curr->next;                                                                                                      \
    }                                                                                                                           \
                                                                                                                                \
    struct name_pascal##HashMapNode *new_node = malloc(sizeof(struct name_pascal##HashMapNode));                                \
    if (!new_node) {                                                                                                            \
        fprintf(stderr, "hash_map_ " #name_snake "_set: failed to malloc *new_node.\n");                                        \
        exit(1);                                                                                                                \
    }                                                                                                                           \
                                                                                                                                \
    new_node->key       = key;                                                                                                  \
    new_node->next      = old_head;                                                                                             \
    new_node->value     = value;                                                                                                \
                                                                                                                                \
    hash_map->buckets.data[bucket_number] = new_node;                                                                           \
                                                                                                                                \
    if (old_head == NULL) hash_map->buckets_used++;                                                                             \
                                                                                                                                \
    if (((float)hash_map->buckets_used / (float)hash_map->buckets.capacity) >= hash_map->load_factor)  {                        \
        grow_##name_snake_##hash_map(hash_map);                                                                                 \
    }                                                                                                                           \
                                                                                                                                \
    return true;                                                                                                                \
}                                                                                                                               \
                                                                                                                                \
static value_type *hash_map_##name_snake##_get(struct name_pascal##HashMap *hash_map, key_type key) {                           \
    size_t bucket_number = hash_function(key) % hash_map->buckets.capacity;                                                     \
                                                                                                                                \
    struct name_pascal##HashMapNode *curr_node = hash_map->buckets.data[bucket_number];                                         \
                                                                                                                                \
    while (curr_node != NULL) {                                                                                                 \
        if (equals_function(curr_node->key, key)) {                                                                                  \
            break;                                                                                                              \
        }                                                                                                                       \
        curr_node = curr_node->next;                                                                                            \
    }                                                                                                                           \
                                                                                                                                \
    return curr_node ? curr_node->value : NULL;                                                                                 \
}                                                                                                                               \
                                                                                                                                \
static void hash_map_##name_snake##_free(struct name_pascal##HashMap *hash_map) {                                               \
    for (int i = 0; i < hash_map->buckets.capacity; i++) {                                                                      \
        struct name_pascal##HashMapNode *curr = hash_map->buckets.data[i];                                                      \
        struct name_pascal##HashMapNode *next;                                                                                  \
                                                                                                                                \
        while (curr != NULL) {                                                                                                  \
            next = curr->next;                                                                                                  \
            free(curr);                                                                                                         \
            curr = next;                                                                                                        \
        }                                                                                                                       \
    }                                                                                                                           \
                                                                                                                                \
    hash_map->buckets.data      = NULL;                                                                                         \
    hash_map->buckets.capacity  = 0;                                                                                            \
    hash_map->buckets.count     = 0;                                                                                            \
    hash_map->buckets_used      = 0;                                                                                            \
    hash_map->load_factor       = 0.0f;                                                                                         \
}

#endif