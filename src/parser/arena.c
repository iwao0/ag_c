#include "arena.h"
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

struct arena_context_t {
  arena_block_t *head;
  arena_block_t *current;
  size_t reserved_bytes;
  size_t peak_bytes;
};

static char *arena_block_data(arena_block_t *block) {
  return (char *)(block + 1);
}

// 関数ごとに arena をリセットするため、現在量ではなく「最大の 1 関数」を表すピークを返す。
size_t arena_total_reserved_bytes_in(const arena_context_t *context) {
  return context ? context->peak_bytes : 0;
}

size_t arena_current_reserved_bytes_in(const arena_context_t *context) {
  return context ? context->reserved_bytes : 0;
}

static arena_block_t *arena_new_block(
    arena_context_t *context, size_t min_size) {
  size_t cap = min_size > ARENA_BLOCK_SIZE ? min_size : ARENA_BLOCK_SIZE;
  arena_block_t *block = malloc(sizeof(arena_block_t) + cap);
  if (!block) return NULL;
  context->reserved_bytes += sizeof(arena_block_t) + cap;
  if (context->reserved_bytes > context->peak_bytes)
    context->peak_bytes = context->reserved_bytes;
  block->next = NULL;
  block->capacity = cap;
  block->used = 0;
  return block;
}

void *arena_alloc_in(arena_context_t *context, size_t size) {
  if (!context) return NULL;
  // 8-byte alignment
  size = (size + 7) & ~(size_t)7;

  if (!context->current ||
      context->current->used + size > context->current->capacity) {
    arena_block_t *block = arena_new_block(context, size);
    if (!block) return NULL;
    if (context->current)
      context->current->next = block;
    else
      context->head = block;
    context->current = block;
  }

  void *ptr = arena_block_data(context->current) + context->current->used;
  context->current->used += size;
  memset(ptr, 0, size);
  return ptr;
}

arena_checkpoint_t arena_checkpoint_in(arena_context_t *context) {
  return (arena_checkpoint_t){
      .context = context,
      .block = context ? context->current : NULL,
      .used = context && context->current ? context->current->used : 0,
  };
}

void arena_rollback_in(
    arena_context_t *context, arena_checkpoint_t checkpoint) {
  if (!context || (checkpoint.context && checkpoint.context != context)) return;
  arena_block_t *saved = checkpoint.block;
  if (!saved) {
    arena_free_all_in(context);
    return;
  }

  arena_block_t *block = saved->next;
  saved->next = NULL;
  while (block) {
    arena_block_t *next = block->next;
    context->reserved_bytes -= sizeof(arena_block_t) + block->capacity;
    free(block);
    block = next;
  }
  if (checkpoint.used <= saved->used) saved->used = checkpoint.used;
  context->current = saved;
}

void arena_free_all_in(arena_context_t *context) {
  if (!context) return;
  arena_block_t *block = context->head;
  while (block) {
    arena_block_t *next = block->next;
    free(block);
    block = next;
  }
  context->head = NULL;
  context->current = NULL;
  context->reserved_bytes = 0;
}

arena_context_t *arena_context_create(void) {
  return calloc(1, sizeof(arena_context_t));
}

void arena_context_destroy(arena_context_t *context) {
  if (!context) return;
  arena_free_all_in(context);
  free(context);
}
