#include "arena.h"
#include <stdlib.h>
#include <string.h>

#define ARENA_BLOCK_SIZE (64 * 1024) // 64KB per block

typedef struct arena_block_t arena_block_t;
typedef struct arena_cleanup_t arena_cleanup_t;
struct arena_block_t {
  arena_block_t *next;
  size_t capacity;
  size_t used;
  char data[];
};

struct arena_cleanup_t {
  arena_cleanup_fn cleanup;
  void *data;
  arena_cleanup_t *next;
};

struct arena_context_t {
  arena_block_t *head;
  arena_block_t *current;
  arena_cleanup_t *cleanups;
  size_t reserved_bytes;
  size_t peak_bytes;
  size_t allocation_count;
  size_t fail_after_allocation_count;
  int allocation_failure_enabled;
};

static void arena_run_cleanups_until(
    arena_context_t *context, arena_cleanup_t *saved) {
  while (context && context->cleanups != saved) {
    arena_cleanup_t *entry = context->cleanups;
    if (!entry) break;
    context->cleanups = entry->next;
    entry->cleanup(entry->data);
    free(entry);
  }
}

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

void arena_fail_allocations_after_in(
    arena_context_t *context, size_t successful_allocations) {
  if (!context) return;
  context->allocation_count = 0;
  context->fail_after_allocation_count = successful_allocations;
  context->allocation_failure_enabled = 1;
}

void arena_clear_allocation_failure_in(arena_context_t *context) {
  if (!context) return;
  context->allocation_count = 0;
  context->fail_after_allocation_count = 0;
  context->allocation_failure_enabled = 0;
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
  if (context->allocation_failure_enabled) {
    if (context->allocation_count >=
        context->fail_after_allocation_count)
      return NULL;
    context->allocation_count++;
  }
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

int arena_register_cleanup_in(
    arena_context_t *context, arena_cleanup_fn cleanup, void *data) {
  if (!context || !cleanup) return 0;
  arena_cleanup_t *entry = malloc(sizeof(*entry));
  if (!entry) return 0;
  *entry = (arena_cleanup_t){
      .cleanup = cleanup,
      .data = data,
      .next = context->cleanups,
  };
  context->cleanups = entry;
  return 1;
}

arena_checkpoint_t arena_checkpoint_in(arena_context_t *context) {
  return (arena_checkpoint_t){
      .context = context,
      .block = context ? context->current : NULL,
      .cleanup = context ? context->cleanups : NULL,
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

  arena_run_cleanups_until(
      context, (arena_cleanup_t *)checkpoint.cleanup);

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
  arena_run_cleanups_until(context, NULL);
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
