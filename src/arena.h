#include <stddef.h>

#define MAX_ALIGN ((size_t)alignof(max_align_t))
#define MIN_ARENA_CAPACITY (MAX_ALIGN)
#define ARENA_ALLOC_TYPE(arena, Type) \
    ((Type *)arena_alloc_aligned(arena, sizeof(Type), alignof(Type)))

#define ARENA_ALLOC_TYPE_CHECKED(arena, Type) \
    ((Type *)arena_alloc_aligned_checked(arena, sizeof(Type), alignof(Type)))

struct ArenaAllocator {
    unsigned char   *buffer;
    size_t          capacity;
    size_t          offset;
};

struct ArenaAllocator arena_new(size_t size);
void arena_free(struct ArenaAllocator *arena);
void arena_reset(struct ArenaAllocator *arena);
void *arena_alloc_aligned(struct ArenaAllocator *arena, size_t size, size_t alignment);