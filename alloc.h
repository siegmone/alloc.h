#ifndef ALLOC_H
#define ALLOC_H

#include <assert.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

/* TODO(September 07, 2025): replace all asserts with logging and early safe returns */
/* TODO(September 07, 2025): shorten the 'allocator_' prefix of functions, like 'alloc_' or even 'a_' or 'alc_' */
/* TODO(September 07, 2025): add 'realloc' for all types of allocators */

/* define alignment size */
#define MAX_ALIGN (alignof(max_align_t))

/* helper macros */
#define round_up_to_multiple(_n, _m) ({    \
    typeof(_m) __m = (_m);                 \
    typeof(_n) _a = (_n) + (__m - 1);      \
    _a - (_a % __m);                       \
})

#define max(a,b) (((a)>(b))?(a):(b))
#define min(a,b) (((a)<(b))?(a):(b))

/*********************************** ARENA ***********************************/
/* arena allocator */
typedef struct arena_block arena_block_t ;
struct arena_block {
    arena_block_t *next;
    size_t size, used;
    uint8_t bytes[];
};

#ifndef ALLOC_ARENA_BLOCKSIZE_MIN
#define ALLOC_ARENA_BLOCKSIZE_MIN  (512u)
#endif
#ifndef ALLOC_ARENA_BLOCKSIZE_MAX
#define ALLOC_ARENA_BLOCKSIZE_MAX  (1u<<20)
#endif

typedef struct arena {
    arena_block_t *start, *end;
    size_t block_seq;
} arena_t;

typedef struct arena_marker {
    arena_block_t *block;
    size_t offset;
} arena_marker_t;

typedef struct arena_temp {
    arena_marker_t marker;
    arena_t *arena;
} arena_temp_t;
/*****************************************************************************/

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
typedef void *(*alloc_fn)(allocator_t*, size_t n);
typedef void (*free_fn)(allocator_t*, void *p);

/* tag union allocator type */
typedef struct allocator {
    alloc_fn alloc;
    free_fn free;
    union {
         arena_t arena;
    };
    allocator_stats_t stats;
    allocator_type_t type;
} allocator_t;

/* general allocator functions */
void allocator_init       (allocator_t *a, allocator_type_t type);
void allocator_deinit     (allocator_t *a);
void allocator_dump_stats (allocator_t *a, const char* name);

/* allocator helper macros */
#define allocator_push_array(_a, _T, _n) (_T*)_a->alloc(_a, sizeof(_T)*(_n))
/*****************************************************************************/

/* arena allocator functions */
void  allocator_arena_init    (allocator_t *arena);
void  allocator_arena_deinit  (allocator_t *arena);
void *allocator_arena_alloc   (allocator_t *arena, size_t size);
void  allocator_arena_free    (allocator_t *arena, void *p);
void *allocator_arena_realloc (allocator_t *arena, void *p);

/* scratch/temporary arena and snapshot functions */
void           arena_reset          (arena_t *arena);
arena_marker_t arena_snapshot       (arena_t *arena);
void           arena_rewind         (arena_t *arena, arena_marker_t m);
arena_temp_t   arena_scratch_init   (arena_t *arena);
void           arena_scratch_deinit (arena_temp_t scratch);

#endif /* ALLOC_H */


#ifdef ALLOC_IMPL
/* general allocator functions */
void allocator_init(allocator_t *a, allocator_type_t type) {
    switch (type) {
        case ALLOCATOR_TYPE_ARENA: {
            allocator_arena_init(a);
        }
        break;
        default:
        break;
    }
}

void allocator_deinit(allocator_t *a) {
    switch (a->type) {
        case ALLOCATOR_TYPE_ARENA: {
            allocator_arena_deinit(a);
        }
        break;
        default:
        break;
    }
}

void allocator_dump_stats(allocator_t *a, const char* name) {
    printf("%s stats:\n", name);
    printf("    Used     : %zu bytes\n", a->stats.used);
    printf("    Reserved : %zu bytes\n", a->stats.reserved);
    printf("    Peak     : %zu bytes\n", a->stats.peak);
}

/* arena allocator functions */
static arena_block_t *arena_block_alloc(size_t size) {
    size_t size_bytes = sizeof(arena_block_t) + sizeof(uint8_t) * size;
    arena_block_t *block = (arena_block_t*)calloc(1, size_bytes);
    assert(block != NULL);

    block->next = NULL;
    block->size = size;
    block->used = 0;
    return block;
}

static void arena_block_free(arena_block_t* block) {
    assert(block != NULL);

    free(block);
}

void allocator_arena_init(allocator_t *a) {
    *a = (allocator_t) {
        .alloc = allocator_arena_alloc,
        .free  = allocator_arena_free,
        .arena = {
            .start     = NULL,
            .end       = NULL,
            .block_seq = 0,
        },
        .stats = {
            .peak     = 0,
            .reserved = 0,
            .used     = 0
        },
        .type = ALLOCATOR_TYPE_ARENA,
    };
}

void allocator_arena_deinit(allocator_t *a) {
    assert(a->type == ALLOCATOR_TYPE_ARENA);

    arena_t *arena = &a->arena;
    arena_block_t *block = arena->start;
    while (block != NULL) {
        arena_block_t *next = block->next;

        /* update stats */
        {
            size_t size_bytes = sizeof(arena_block_t) + sizeof(uint8_t) * block->size;
            a->stats.used -= block->used;
            a->stats.reserved -= size_bytes;
        }

        arena_block_free(block);
        block = next;
    }

    arena->start = NULL;
    arena->block_seq = 0;
}

void *allocator_arena_alloc(allocator_t *a, size_t size) {
    assert(a->type == ALLOCATOR_TYPE_ARENA);

    arena_t *arena = &a->arena;
    size = round_up_to_multiple(size, MAX_ALIGN);
    arena_block_t *block = arena->start;
    while (block) {
        if (size + block->used <= block->size) {
            /* found block that can hold the memory */
            /* this helps not to allocate more blocks for small allocations */
            break;
        }
        block = block->next;
    }

    /* did not find a suitable block */
    if (!block) {
        /* from https://github.com/nothings/stb/blob/master/stb_ds.h */
        // compute the next blocksize
        size_t blocksize = arena->block_seq;

        // size is 512, 512, 1024, 1024, 2048, 2048, 4096, 4096, etc., so that
        // there are log(SIZE) allocations to free when we destroy the table
        blocksize = (size_t) (ALLOC_ARENA_BLOCKSIZE_MIN) << (blocksize>>1);

        // if size is under 1M, advance to next blocktype
        if (blocksize < (size_t)(ALLOC_ARENA_BLOCKSIZE_MAX))
          ++arena->block_seq;
        /*************************************************************/

        /* allocate next block */
        if (size > blocksize) {
            /* if the requested size is greater than the current blocksize
             * just allocate the whole size, eventually the blocksize will grow
             * to handle such sizes */
            block = arena_block_alloc(size);
        } else {
            block = arena_block_alloc(blocksize);
        }

        /* push it to the back of block list */
        if (!arena->start) {
            arena->start = block;
        } else {
            arena_block_t *last = arena->start;
            while (last->next) last = last->next;
            last->next = block;
        }
        arena->end = block;

        /* update stats */
        size_t size_bytes = sizeof(arena_block_t) + sizeof(uint8_t) * block->size;
        a->stats.reserved += size_bytes;
    }

    void *ptr = &block->bytes[block->used];
    block->used += size;

    /* update stats */
    a->stats.used += size;
    a->stats.peak = max(a->stats.peak, a->stats.used);

    return ptr;
}

void allocator_arena_free(allocator_t *a, void *p) {
    assert(a->type == ALLOCATOR_TYPE_ARENA);
    /* NO-OP */
}

void *allocator_arena_realloc(allocator_t *a, void *p) {
    assert(a->type == ALLOCATOR_TYPE_ARENA);
    /* NO-OP */
}


void arena_reset(arena_t *arena) {
    for (arena_block_t* b = arena->start; b != NULL; b = b->next) {
        b->used = 0;
    }
    arena->end = arena->start;
}

arena_marker_t arena_snapshot(arena_t *arena) {
    arena_marker_t m;
    if (arena->end == NULL) {
        m.block = arena->end;
        m.offset = 0;
    } else {
        m.block = arena->end;
        m.offset = arena->end->used;
    }

    return m;
}

void arena_rewind(arena_t *arena, arena_marker_t m) {
    if (m.block == NULL) {
        arena_reset(arena);
        return;
    }
    m.block->used = m.offset;
    for (arena_block_t *b = m.block->next; b != NULL; b = b->next) {
        b->used = 0;
    }
    arena->end = m.block;
}

arena_temp_t arena_scratch_init(arena_t *arena) {
    arena_marker_t marker = arena_snapshot(arena);
    arena_temp_t tmp = {
        .arena  = arena,
        .marker = marker,
    };
    return tmp;
}

void arena_scratch_deinit(arena_temp_t scratch) {
    arena_rewind(scratch.arena, scratch.marker);
}

#endif /* ALLOC_IMPL */
