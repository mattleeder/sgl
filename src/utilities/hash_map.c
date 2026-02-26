#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>

#include "hash_map.h"
#include "../memory.h"


// @TODO: consider using something like an arena allocater for HashMap


void hash_map_init(struct HashMap *hash_map,
    size_t initial_capacity, 
    float load_factor,
    size_t key_size,
    size_t value_size,
    size_t (*hash_function)(const void *key), 
    bool (*equality_function)(const void *a, const void *b)) {

    struct HashMapNode **buckets = calloc(initial_capacity, sizeof(*buckets));
    if (!buckets) {
        fprintf(stderr, "hash_map_init: **buckets malloc failed.\n");
        exit(1);
    }

    assert(key_size > 0);
    assert(value_size > 0);
    assert(load_factor > 0);
    assert(load_factor <= 1.00);

    hash_map->data              = buckets;

    hash_map->hash_function     = hash_function;
    hash_map->equality_function = equality_function;

    hash_map->load_factor       = load_factor;
    hash_map->buckets_used      = 0;
    hash_map->buckets_capacity  = initial_capacity;
    hash_map->element_count     = 0;
    hash_map->key_size          = key_size;
    hash_map->value_size        = value_size;
}

struct HashMap *hash_map_new(size_t initial_capacity, 
    float load_factor,
    size_t key_size,
    size_t value_size,
    size_t (*hash_function)(const void *key), 
    bool (*equality_function)(const void *a, const void *b)) 
{

    struct HashMap *hash_map = malloc(sizeof(struct HashMap));
    if (!hash_map) {
        fprintf(stderr, "hash_map_new: failed to malloc *hash_map.\n");
        exit(1);
    }

    hash_map_init(
        hash_map,
        initial_capacity,
        load_factor,
        key_size,
        value_size,
        hash_function,
        equality_function
    );

    return hash_map;
}

bool hash_map_grow(struct HashMap *hash_map) {
    fprintf(stderr, "HASH_MAP GROW GROW GROW\n");
    assert(hash_map);

    size_t old_capacity = hash_map->buckets_capacity;
    size_t new_capacity = grow_capacity(old_capacity);
    struct HashMapNode **old_data = hash_map->data;
    struct HashMapNode **new_data = calloc(new_capacity, sizeof(*new_data));

    if (!new_data) {
        fprintf(stderr, "hash_map_grow: **new_data malloc failed.\n");
        exit(1);
    }

    hash_map->data              = new_data;
    hash_map->buckets_used      = 0;
    hash_map->buckets_capacity  = new_capacity;

    assert(hash_map->buckets_capacity > 0);

    // hash_map->element_count doesnt change during grow
    for (size_t i = 0; i < old_capacity; i++) {
        struct HashMapNode *curr = old_data[i];
        struct HashMapNode *next;
        while (curr) {
            next = curr->next;

            size_t bucket_number = hash_map->hash_function(curr->key) % hash_map->buckets_capacity;
            if (!new_data[bucket_number]) hash_map->buckets_used++;
            curr->next = new_data[bucket_number];
            new_data[bucket_number] = curr;

            curr = next;
        }
    }

    free(old_data);
    return true;
}

bool hash_map_set(struct HashMap *hash_map, const void *key, const void *value) {
    assert(hash_map);
    assert(hash_map->buckets_capacity > 0);
    assert(hash_map->data);

    size_t bucket_number = hash_map->hash_function(key) % hash_map->buckets_capacity;
    fprintf(stderr, "bucket_number: %zu\n", bucket_number);

    struct HashMapNode *old_head = hash_map->data[bucket_number];
    struct HashMapNode *curr = old_head;

    while (curr) {
        if (hash_map->equality_function(curr->key, key)) {
            memcpy(curr->value, value, hash_map->value_size);
            return true;
        }
        curr = curr->next;
    }

    struct HashMapNode *new_node = malloc(sizeof(*new_node));
    if (!new_node) {
        fprintf(stderr, "hash_map_set: failed to malloc *new_node.\n");
        exit(1);
    }

    hash_map->element_count++;

    new_node->next  = old_head;
    new_node->key   = malloc(hash_map->key_size);
    new_node->value = malloc(hash_map->value_size);

    if (!new_node->key) {
        fprintf(stderr, "hash_map_set: failed to malloc new_node->key.\n");
        exit(1);
    }

    if (!new_node->value) {
        fprintf(stderr, "hash_map_set: failed to malloc new_node->value.\n");
        exit(1);
    }

    memcpy(new_node->key, key, hash_map->key_size);
    memcpy(new_node->value, value, hash_map->value_size);

    hash_map->data[bucket_number] = new_node;

    if (!old_head) hash_map->buckets_used++;

    assert((float)hash_map->buckets_capacity > 0);
    if (((float)hash_map->buckets_used / (float)hash_map->buckets_capacity) >= hash_map->load_factor) {
        hash_map_grow(hash_map);
    }

    if (hash_map->value_size == sizeof(size_t)) {
        fprintf(stderr, "Set (size_t): %zu\n", *(size_t *)value);
    } else if (hash_map->value_size == sizeof(bool)) {
        fprintf(stderr, "Set (bool): %d\n", *(bool *)value);
    }


    return true;
}

void *hash_map_get(struct HashMap *hash_map, const void *key) {
    assert(hash_map);
    assert(hash_map->buckets_capacity > 0);
    assert(hash_map->data);
    assert(key);
    
    size_t bucket_number = hash_map->hash_function(key) % hash_map->buckets_capacity;
    fprintf(stderr, "bucket_number: %zu\n", bucket_number);

    struct HashMapNode *curr_node = hash_map->data[bucket_number];

    while (curr_node) {
        if (hash_map->equality_function(curr_node->key, key)) {
            break;
        }
        curr_node = curr_node->next;
    }

    if (curr_node) {

        const void *value = curr_node->value;
        if (hash_map->value_size == sizeof(size_t)) {
            fprintf(stderr, "Value (size_t): %zu\n", *(size_t *)value);
        } else if (hash_map->value_size == sizeof(bool)) {
            fprintf(stderr, "Value (bool): %d\n", *(bool *)value);
        }

    } else {
        fprintf(stderr, "value: NULL\n");
    }

    return curr_node == NULL ? NULL : curr_node->value;
}

bool hash_map_contains(struct HashMap *hash_map, const void *key) {
    assert(hash_map);
    assert(hash_map->buckets_capacity > 0);
    assert(hash_map->data);
    assert(key);

    fprintf(stderr, "hash_map->hash_function(key): %zu\n", hash_map->hash_function(key));
    size_t bucket_number = hash_map->hash_function(key) % hash_map->buckets_capacity;

    struct HashMapNode *curr_node = hash_map->data[bucket_number];

    while (curr_node) {
        if (hash_map->equality_function(curr_node->key, key)) {
            break;
        }
        curr_node = curr_node->next;
    }

    return curr_node != NULL;
}

void hash_map_free(struct HashMap *hash_map) {
    for (size_t i = 0; i < hash_map->buckets_capacity; i++) {
        struct HashMapNode *curr = hash_map->data[i];
        struct HashMapNode *next;

        while (curr) {
            next = curr->next;
            free(curr->key);
            free(curr->value);
            free(curr);
            curr = next;
        }
    }

    free(hash_map->data);
    hash_map->data = NULL;

    hash_map->hash_function     = NULL;
    hash_map->equality_function = NULL;

    hash_map->load_factor         = 0;
    hash_map->buckets_used        = 0;
    hash_map->buckets_capacity    = 0;
    hash_map->element_count       = 0;
}

void **hash_map_get_keys_alloc(struct HashMap *hash_map, size_t *out_count) {
    assert(hash_map);
    assert(hash_map->data);
    assert(out_count);

    void **array = malloc(hash_map->element_count * sizeof(*array));
    if (!array) {
        fprintf(stderr, "hash_map_get_keys: failed to malloc **array.\n");
        exit(1);
    }

    size_t idx = 0;
    struct HashMapNode *curr;
    for (size_t i = 0; i < hash_map->buckets_capacity; i++) {
        curr = hash_map->data[i];
        while (curr) {
            array[idx] = curr->key;
            idx++;
            curr = curr->next;
        }
    }

    *out_count = hash_map->element_count;

    return array;
}

/* #define DEFINE_HASH_MAP(key_type, value_type, name_pascal, name_snake, hash_function, equals_function)                          \
                                                                                                                                \
struct name_pascal##HashMapNode {                                                                                               \
    struct name_pascal##HashMapNode     *next;                                                                                  \
    key_type                            key;                                                                                    \
    value_type                          value;                                                                                  \
};                                                                                                                              \
                                                                                                                                \
DEFINE_VECTOR(struct name_pascal##HashMapNode*, name_pascal##HashMapBuckets, name_snake##_hash_map_buckets)                     \
                                                                                                                                \
struct name_pascal##HashMap {                                                                                                   \
    struct name_pascal##HashMapBuckets  buckets;                                                                                \
    float                               load_factor;                                                                            \
    size_t                              buckets_used;                                                                           \
    size_t                              element_count;                                                                          \
};                                                                                                                              \
                                                                                                                                \
static void hash_map_##name_snake##_init(struct name_pascal##HashMap *hash_map, size_t initial_capacity, float load_factor) {   \
    assert(hash_map);                                                                                                           \
    init_##name_snake##_hash_map_buckets(&hash_map->buckets);                                                                   \
    if (initial_capacity == 0) {                                                                                                \
        initial_capacity = grow_capacity(0);                                                                                    \
        fprintf(stderr, "hash_map_" #name_snake "_init: cannot have 0 initial capacity, setting to 1.\n");                      \
    }                                                                                                                           \
                                                                                                                                \
    hash_map->load_factor   = load_factor;                                                                                      \
    hash_map->buckets_used  = 0;                                                                                                \
    hash_map->element_count = 0;                                                                                                \
                                                                                                                                \
    for (int i = 0; i < initial_capacity; i++) {                                                                                \
        push_##name_snake##_hash_map_buckets(&hash_map->buckets, NULL);                                                         \
    }                                                                                                                           \
}                                                                                                                               \
                                                                                                                                \
static bool grow_##name_snake_##hash_map(struct name_pascal##HashMap *hash_map) {                                               \
    assert(hash_map);                                                                                                           \
                                                                                                                                \
    size_t old_capacity                         = hash_map->buckets.capacity;                                                   \
    size_t new_capacity                         = grow_capacity(old_capacity);                                                  \
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
    assert(hash_map->buckets.capacity > 0);                                                                                     \
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
static bool hash_map_##name_snake##_set(struct name_pascal##HashMap *hash_map, key_type key, value_type value) {                \
    assert(hash_map);                                                                                                           \
    assert(hash_map->buckets.capacity > 0);                                                                                     \
    assert(hash_map->buckets.data);                                                                                             \
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
    hash_map->element_count++;                                                                                                  \
                                                                                                                                \
    new_node->key       = key;                                                                                                  \
    new_node->next      = old_head;                                                                                             \
    new_node->value     = value;                                                                                                \
                                                                                                                                \
    hash_map->buckets.data[bucket_number] = new_node;                                                                           \
                                                                                                                                \
    if (old_head == NULL) hash_map->buckets_used++;                                                                             \
                                                                                                                                \
    assert((float)hash_map->buckets.capacity > 0);                                                                              \
    if (((float)hash_map->buckets_used / (float)hash_map->buckets.capacity) >= hash_map->load_factor)  {                        \
        grow_##name_snake_##hash_map(hash_map);                                                                                 \
    }                                                                                                                           \
                                                                                                                                \
    return true;                                                                                                                \
}                                                                                                                               \
                                                                                                                                \
static bool hash_map_##name_snake##_get(struct name_pascal##HashMap *hash_map, key_type key, value_type *out) {                 \
    assert(hash_map->buckets.capacity > 0);                                                                                     \
    size_t bucket_number = hash_function(key) % hash_map->buckets.capacity;                                                     \
                                                                                                                                \
    struct name_pascal##HashMapNode *curr_node = hash_map->buckets.data[bucket_number];                                         \
                                                                                                                                \
    while (curr_node != NULL) {                                                                                                 \
        if (equals_function(curr_node->key, key)) {                                                                             \
            break;                                                                                                              \
        }                                                                                                                       \
        curr_node = curr_node->next;                                                                                            \
    }                                                                                                                           \
                                                                                                                                \
    if (curr_node == NULL) {                                                                                                    \
        return false;                                                                                                           \
    }                                                                                                                           \
                                                                                                                                \
    *out = curr_node->value;                                                                                                    \
    return true;                                                                                                                \
}                                                                                                                               \
                                                                                                                                \
static bool hash_map_##name_snake##_contains(struct name_pascal##HashMap *hash_map, key_type key) {                             \
    assert(hash_map->buckets.capacity > 0);                                                                                     \
    size_t bucket_number = hash_function(key) % hash_map->buckets.capacity;                                                     \
                                                                                                                                \
    struct name_pascal##HashMapNode *curr_node = hash_map->buckets.data[bucket_number];                                         \
                                                                                                                                \
    while (curr_node != NULL) {                                                                                                 \
        if (equals_function(curr_node->key, key)) {                                                                             \
            break;                                                                                                              \
        }                                                                                                                       \
        curr_node = curr_node->next;                                                                                            \
    }                                                                                                                           \
                                                                                                                                \
    if (curr_node == NULL) {                                                                                                    \
        return false;                                                                                                           \
    }                                                                                                                           \
                                                                                                                                \
    return true;                                                                                                                \
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
    free(hash_map->buckets.data);                                                                                               \
    hash_map->buckets.data      = NULL;                                                                                         \
    hash_map->buckets.capacity  = 0;                                                                                            \
    hash_map->buckets.count     = 0;                                                                                            \
    hash_map->buckets_used      = 0;                                                                                            \
    hash_map->load_factor       = 0.0f;                                                                                         \
}                                                                                                                               \
                                                                                                                                \
static key_type *hash_map_##name_snake##_keys_to_array(struct name_pascal##HashMap *hash_map, size_t *out_count) {              \
    assert(hash_map);                                                                                                           \
    assert(hash_map->buckets.data);                                                                                             \
    assert(out_count);                                                                                                          \
    key_type *array = malloc(hash_map->element_count * sizeof(key_type));                                                       \
    if (!array) {                                                                                                               \
        fprintf(stderr, "hash_map" #name_snake "_to_array: failed to malloc *array.\n");                                        \
        exit(1);                                                                                                                \
    }                                                                                                                           \
                                                                                                                                \
    size_t idx = 0;                                                                                                             \
    struct name_pascal##HashMapNode *curr;                                                                                      \
    for (size_t i = 0; i < hash_map->buckets.capacity; i++) {                                                                   \
        curr = hash_map->buckets.data[i];                                                                                       \
        while (curr != NULL) {                                                                                                  \
            array[idx] = curr->key;                                                                                             \
            idx++;                                                                                                              \
            curr = curr->next;                                                                                                  \
        }                                                                                                                       \
    }                                                                                                                           \
                                                                                                                                \
    *out_count = hash_map->element_count;                                                                                       \
                                                                                                                                \
    return array;                                                                                                               \
} */