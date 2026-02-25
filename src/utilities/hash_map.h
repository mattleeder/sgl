#ifndef sql_hash_map
#define sql_hash_map

#include <stdint.h>
#include <string.h>

struct HashMapNode {
    struct HashMapNode  *next;
    void                *key;
    void                *value;
};

struct HashMap {
    struct HashMapNode  **data;

    size_t  (*hash_function)(const void *key);
    bool    (*equality_function)(const void *a, const void *b);

    float               load_factor;
    size_t              buckets_used;
    size_t              buckets_capacity;
    size_t              element_count;
    size_t              key_size;
    size_t              value_size;
};

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
    for (int i = 0; i < len; i++)
        hash = ((hash << 5) + hash) + (unsigned char)string[i];
    return hash;
}

static inline size_t hash_string(const char *string) {
    return hash_djb2(string);
}

static inline size_t equals_string(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

#define DEFINE_TYPED_HASH_MAP(key_type, value_type, name_pascal, name_snake)                                        \
                                                                                                                    \
static inline struct HashMap *hash_map_##name_snake##_new(                                                          \
    size_t initial_capacity,                                                                                        \
    float load_factor,                                                                                              \
    size_t (*hash_function)(const void *),                                                                          \
    bool (*equality_function)(const void *, const void *)                                                           \
) {                                                                                                                 \
    return hash_map_new(                                                                                            \
        initial_capacity,                                                                                           \
        load_factor,                                                                                                \
        sizeof(key_type),                                                                                           \
        sizeof(value_type),                                                                                         \
        hash_function,                                                                                              \
        equality_function                                                                                           \
    );                                                                                                              \
}                                                                                                                   \
                                                                                                                    \
static inline bool hash_map_##name_snake##_set(struct HashMap *hash_map, const key_type *key, const value_type *value) {   \
    return hash_map_set(hash_map, key, value);                                                                      \
}                                                                                                                   \
                                                                                                                    \
static inline value_type *hash_map_##name_snake##_get(struct HashMap *hash_map, const key_type *key) {              \
    return (value_type *)hash_map_get(hash_map, key);                                                               \
}                                                                                                                   \
                                                                                                                    \
static inline bool hash_map_##name_snake##_contains(struct HashMap *hash_map, const key_type *key) {                \
    return hash_map_contains(hash_map, key);                                                                        \
}                                                                                                                   \
                                                                                                                    \
static inline key_type **hash_map_##name_snake##_get_keys_alloc(struct HashMap *hash_map, size_t *out_count) {      \
    return (key_type **)hash_map_get_keys_alloc(hash_map, out_count);                                               \
}                                                                                                                   \
                                                                                                                    \
static inline void hash_map_##name_snake##_free(struct HashMap *hash_map) {      \
    return hash_map_free(hash_map);                                              \
}

void hash_map_init(
    struct  HashMap *hash_map,
    size_t  initial_capacity, 
    float   load_factor,
    size_t  key_size,
    size_t  value_size,
    size_t  (*hash_function)(const void *key), 
    bool    (*equality_function)(const void *a, const void *b));

struct HashMap *hash_map_new(
    size_t  initial_capacity, 
    float   load_factor,
    size_t  key_size,
    size_t  value_size,
    size_t  (*hash_function)(const void *key), 
    bool    (*equality_function)(const void *a, const void *b));

bool hash_map_grow(struct HashMap *hash_map);
bool hash_map_set(struct HashMap *hash_map, const void *key, const void *value);
void *hash_map_get(struct HashMap *hash_map, const void *key);
bool hash_map_contains(struct HashMap *hash_map, const void *key);
void hash_map_free(struct HashMap *hash_map);
void **hash_map_get_keys_alloc(struct HashMap *hash_map, size_t *out_count);

#endif