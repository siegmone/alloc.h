#ifndef ARENA_H
#define ARENA_H

#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <stdalign.h>

/* helper macros */
#define round_up_to_multiple(_n, _m) ({    \
    typeof(_m) __m = (_m);                 \
    typeof(_n) _a = (_n) + (__m - 1);      \
    _a - (_a % __m);                       \
})

#define max(a,b) (((a)>(b))?(a):(b))
#define min(a,b) (((a)<(b))?(a):(b))

/* define alignment size */
#define MAX_ALIGN (alignof(max_align_t))

/* arena allocator */
typedef struct arena_block arena_block_t ;
struct arena_block {
    arena_block_t *next;
    size_t size, used;
    uint8_t bytes[];
};

#ifndef ARENA_BLOCKSIZE_MIN
#define ARENA_BLOCKSIZE_MIN  (512u)
#endif
#ifndef ARENA_BLOCKSIZE_MAX
#define ARENA_BLOCKSIZE_MAX  (1u<<20)
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

void          *arena_alloc          (arena_t *arena, size_t size);
void           arena_free           (arena_t *arena);
void          *arena_realloc        (arena_t *arena, void *p);
void           arena_init           (arena_t *arena);
void           arena_deinit         (arena_t *arena);
void           arena_reset          (arena_t *arena);
arena_marker_t arena_snapshot       (arena_t *arena);
void           arena_rewind         (arena_t *arena, arena_marker_t m);
arena_temp_t   arena_scratch_init   (arena_t *arena);
void           arena_scratch_deinit (arena_temp_t scratch);

#endif /* ARENA_H */


#ifdef ARENA_IMPL

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
    printf("i want to be free\n");

    free(block);
}

void arena_init(arena_t *arena) {
    arena->block_seq = 0;
    arena->start     = NULL;
    arena->end       = arena->start;
}

void arena_deinit(arena_t *arena) {
    arena_block_t *block = arena->start;
    while (block != NULL) {
        arena_block_t *next = block->next;
        arena_block_free(block);
        block = next;
    }

    arena->start = NULL;
    arena->block_seq = 0;
}

void *arena_alloc(arena_t *arena, size_t size) {
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
        blocksize = (size_t) (ARENA_BLOCKSIZE_MIN) << (blocksize>>1);

        // if size is under 1M, advance to next blocktype
        if (blocksize < (size_t)(ARENA_BLOCKSIZE_MAX))
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

    }

    void *ptr = &block->bytes[block->used];
    block->used += size;

    return ptr;
}

void arena_free(arena_t *arena) {
    /* NO-OP */
}

void *arena_realloc(arena_t *arena, void *p) {
    /* NO-OP */
    return p;
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

#endif /* ARENA_IMPL */
