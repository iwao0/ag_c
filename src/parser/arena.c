#include "internal/arena.h"
#include <stdlib.h>
#include <string.h>

#define ARENA_BLOCK_SIZE (64 * 1024) // 64KB per block

typedef struct arena_block_t arena_block_t;
struct arena_block_t {
  arena_block_t *next;
  size_t capacity;
  size_t used;
  char data[];
};

static arena_block_t *arena_head = NULL;
static arena_block_t *arena_current = NULL;

static arena_block_t *arena_new_block(size_t min_size) {
  size_t cap = min_size > ARENA_BLOCK_SIZE ? min_size : ARENA_BLOCK_SIZE;
  arena_block_t *block = malloc(sizeof(arena_block_t) + cap);
  block->next = NULL;
  block->capacity = cap;
  block->used = 0;
  return block;
}

void *arena_alloc(size_t size) {
  // 8-byte alignment
  size = (size + 7) & ~(size_t)7;

  if (!arena_current || arena_current->used + size > arena_current->capacity) {
    arena_block_t *block = arena_new_block(size);
    if (arena_current)
      arena_current->next = block;
    else
      arena_head = block;
    arena_current = block;
  }

  void *ptr = arena_current->data + arena_current->used;
  arena_current->used += size;
  memset(ptr, 0, size);
  return ptr;
}

void arena_free_all(void) {
  arena_block_t *block = arena_head;
  while (block) {
    arena_block_t *next = block->next;
    free(block);
    block = next;
  }
  arena_head = NULL;
  arena_current = NULL;
}
