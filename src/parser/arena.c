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

static char *arena_block_data(arena_block_t *block) {
  return (char *)(block + 1);
}

static arena_block_t *arena_head = NULL;
static arena_block_t *arena_current = NULL;
static size_t arena_reserved_bytes = 0;  // メモリ計測用: 現在の予約バイト数
static size_t arena_peak_bytes = 0;      // 同: 同時予約量の最大 (関数ごとリセットを跨ぐ)

// 関数ごとに arena をリセットするため、現在量ではなく「最大の 1 関数」を表すピークを返す。
size_t arena_total_reserved_bytes(void) { return arena_peak_bytes; }

static arena_block_t *arena_new_block(size_t min_size) {
  size_t cap = min_size > ARENA_BLOCK_SIZE ? min_size : ARENA_BLOCK_SIZE;
  arena_reserved_bytes += sizeof(arena_block_t) + cap;
  if (arena_reserved_bytes > arena_peak_bytes) arena_peak_bytes = arena_reserved_bytes;
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

  void *ptr = arena_block_data(arena_current) + arena_current->used;
  arena_current->used += size;
  memset(ptr, 0, size);
  return ptr;
}

arena_checkpoint_t arena_checkpoint(void) {
  return (arena_checkpoint_t){
      .block = arena_current,
      .used = arena_current ? arena_current->used : 0,
  };
}

void arena_rollback(arena_checkpoint_t checkpoint) {
  arena_block_t *saved = checkpoint.block;
  if (!saved) {
    arena_free_all();
    return;
  }

  arena_block_t *block = saved->next;
  saved->next = NULL;
  while (block) {
    arena_block_t *next = block->next;
    arena_reserved_bytes -= sizeof(arena_block_t) + block->capacity;
    free(block);
    block = next;
  }
  if (checkpoint.used <= saved->used) saved->used = checkpoint.used;
  arena_current = saved;
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
  arena_reserved_bytes = 0;
}
