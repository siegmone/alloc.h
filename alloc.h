#ifndef ALLOC_H
#define ALLOC_H

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#define ARENA_IMPL
#include "allocators/arena.h"

/* TODO(September 07, 2025): replace all asserts with logging and early safe returns */
/* TODO(September 07, 2025): shorten the 'allocator_' prefix of functions, like 'alloc_' or even 'a_' or 'alc_' */
/* TODO(September 07, 2025): add 'realloc' for all types of allocators */
/* TODO(September 08, 2025): make a default allocator */
/* TODO(September 08, 2025): implement freelist */
/* TODO(September 08, 2025): rightn now the "arena" is a hybrid beteween a bump allocator and an arena, so decide which to implement */

/********************************** GENERAL **********************************/
/* allocator definition and allocator_type */
typedef struct allocator allocator_t;

typedef enum allocator_type {
    ALLOCATOR_TYPE_ARENA,
} allocator_type_t;

typedef struct allocator_stats {
    size_t used;
    size_t reserved;
    size_t peak;
} allocator_stats_t;

/* alloc and free function pointers */
typedef void *(*alloc_fn)  (allocator_t*, size_t n);
typedef void  (*free_fn)   (allocator_t*, void *p);
typedef void *(*realloc_fn)(allocator_t*, void *p);

/* tag union allocator type */
typedef struct allocator {
    alloc_fn   alloc;
    free_fn    free;
    realloc_fn realloc;
    union {
         arena_t arena;
    };
    allocator_stats_t stats;
    allocator_type_t  type;
} allocator_t;

/* general allocator functions */
void allocator_init       (allocator_t *a, allocator_type_t type);
void allocator_deinit     (allocator_t *a);
void allocator_dump_stats (allocator_t *a, const char* name);

/* allocator helper macros */
#define allocator_push_array(_a, _T, _n) (_T*)_a->alloc(_a, sizeof(_T)*(_n))
#define allocator_push_struct(_a, _T)    allocator_push_array(_a, _T, 1)

/*****************************************************************************/

/* arena allocator functions */
void *allocator_arena_alloc   (allocator_t *arena, size_t size);
void  allocator_arena_free    (allocator_t *arena, void *p);
void *allocator_arena_realloc (allocator_t *arena, void *p);

#endif /* ALLOC_H */


#ifdef ALLOC_IMPL
/* general allocator functions */
void allocator_init(allocator_t *a, allocator_type_t type) {
    *a = (allocator_t){
        .stats = {
            .peak     = 0,
            .reserved = 0,
            .used     = 0,
        },
        .type = type,
    };
    switch (type) {
        case ALLOCATOR_TYPE_ARENA: {
            a->alloc   = allocator_arena_alloc;
            a->free    = allocator_arena_free;
            a->realloc = allocator_arena_realloc;
            arena_init(&a->arena);
        }
        break;
        default:
        break;
    }
}

void allocator_deinit(allocator_t *a) {
    switch (a->type) {
        case ALLOCATOR_TYPE_ARENA: {
            arena_deinit(&a->arena);
        }
        break;
        default:
        break;
    }
    /* update stats */
    {
        a->stats.used = 0;
        a->stats.reserved = 0;
    }
}

void allocator_dump_stats(allocator_t *a, const char* name) {
    printf("%s stats:\n", name);
    printf("    Used     : %zu bytes\n", a->stats.used);
    printf("    Reserved : %zu bytes\n", a->stats.reserved);
    printf("    Peak     : %zu bytes\n", a->stats.peak);
}

/* arena allocator functions */
void *allocator_arena_alloc(allocator_t *a, size_t size) {
    arena_block_t *end = a->arena.end;
    void *ptr = arena_alloc(&a->arena, size);

    /* update stats */
    if (a->arena.end != end) {
        /* new block allocated */
        arena_block_t *new_end = a->arena.end;
        size_t size_bytes = sizeof(arena_block_t) + sizeof(uint8_t) * new_end->size;
        a->stats.reserved += size_bytes;
    }
    a->stats.used += size;
    a->stats.peak = max(a->stats.peak, a->stats.used);

    return ptr;
}

void allocator_arena_free(allocator_t *a, void *p) {
    arena_free(&a->arena);
}

void *allocator_arena_realloc(allocator_t *a, void *p) {
    return arena_realloc(&a->arena, p);
}

#endif /* ALLOC_IMPL */
