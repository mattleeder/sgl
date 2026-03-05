#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <stddef.h>

#include "arena.h"

static size_t grow_arena_capacity(struct ArenaAllocator *arena, size_t target_capacity) {
    // Increase arena capacity by smallest power of 2 that exceeds target_capacity
    size_t new_capacity = arena->capacity;

    while (new_capacity < target_capacity) {
        if (new_capacity > SIZE_MAX / 2)
            return SIZE_MAX;
        new_capacity <<= 1;
    }
    
    return new_capacity;
}

static bool grow_arena(struct ArenaAllocator *arena, size_t new_capacity) {
    if (arena->capacity >= new_capacity) {
        return true;
    }

    void *new_buffer = realloc(arena->buffer, new_capacity);
    if (!new_buffer) {
        fprintf(stderr, "grow_arena: failed to realloc to size %zu", new_capacity);
        return false;
    }

    arena->buffer   = (unsigned char *)new_buffer;
    arena->capacity = new_capacity;
    return true;
}

static bool ensure_arena_has_enough_capacity(struct ArenaAllocator *arena, size_t size) {
    assert(arena->offset <= arena->capacity);

    if (size <= arena->capacity - arena->offset) {
        return true;
    }

    // Arena offset cannot exceed SIZE_MAX
    if (arena->offset > SIZE_MAX - size) {
        return false;
    }

    size_t required_capacity = arena->offset + size;
    size_t new_capacity = grow_arena_capacity(arena, required_capacity);
    return grow_arena(arena, new_capacity);
}

void *arena_alloc_aligned(struct ArenaAllocator *arena, size_t size, size_t alignment) {
    // Alignment must be power of 2
    assert(alignment != 0);
    assert((alignment & (alignment - 1)) == 0);

    // Avoid underflow
    if (alignment == 0) {
        return NULL;
    }

    // Avoid overflow
    if (arena->offset > SIZE_MAX - (alignment - 1)) {
        return NULL;
    }

    size_t aligned_offset = (arena->offset + (alignment - 1)) & ~(alignment -1);
    size_t alignment_bytes = aligned_offset - arena->offset;

    // Avoid overflow
    if (size > SIZE_MAX - aligned_offset) {
        return NULL;
    }

    // size + alignment_bytes wont overflow as aligned_offset >= alignment_bytes
    // and size + aligned_offset does not overflow
    if (!ensure_arena_has_enough_capacity(arena, size + alignment_bytes)) {
        return NULL;
    }

    void *ptr = arena->buffer + aligned_offset;

    arena->offset = aligned_offset + size;

    return ptr;
}

void arena_reset(struct ArenaAllocator *arena) {
    arena->offset = 0;
}

void arena_free(struct ArenaAllocator *arena) {
    free(arena->buffer);
    arena->buffer   = NULL;
    arena->capacity = 0;
    arena->offset   = 0;
}

struct ArenaAllocator arena_new(size_t size) {
    // Has a min capacity of MIN_ARENA_CAPACITY
    if (size < MIN_ARENA_CAPACITY) {
        size = MIN_ARENA_CAPACITY;
    }
    
    struct ArenaAllocator arena;

    void *buffer = malloc(size);

    if (!buffer) {
        // Buffer allocation failed, returning empty arena
        arena.buffer    = NULL;
        arena.capacity  = 0;
        arena.offset    = 0;
    }

    arena.buffer   = (unsigned char *)buffer;
    arena.capacity = size;
    arena.offset   = 0;

    return arena;
}