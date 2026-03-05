#include <stddef.h>

#define MAX_ALIGN ((size_t)alignof(max_align_t))
#define MIN_ARENA_CAPACITY (MAX_ALIGN)

struct ArenaAllocator {
    unsigned char   *buffer;
    size_t          capacity;
    size_t          offset;
};

struct ArenaAllocator arena_new(size_t size);
void arena_free(struct ArenaAllocator *arena);
void arena_reset(struct ArenaAllocator *arena);
void *arena_alloc_aligned(struct ArenaAllocator *arena, size_t size, size_t alignment);