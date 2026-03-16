#include "allocator.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct arena_chunk_t arena_chunk_t;
struct arena_chunk_t {
  arena_chunk_t *next;
  size_t used;
  size_t cap;
  unsigned char data[];
};

static arena_chunk_t *arena_head;
static size_t total_chunks;
static size_t total_reserved_bytes;
static size_t next_chunk_hint = 16 * 1024;

static size_t align_up(size_t n, size_t align) {
  return (n + align - 1) & ~(align - 1);
}

static void *arena_alloc(size_t size) {
  if (size == 0) size = 1;
  size = align_up(size, 8);

  if (!arena_head || arena_head->used + size > arena_head->cap) {
    size_t cap = next_chunk_hint;
    if (cap < size) cap = align_up(size, 4096);
    arena_chunk_t *chunk = malloc(sizeof(arena_chunk_t) + cap);
    if (!chunk) {
      fprintf(stderr, "メモリ確保に失敗しました\n");
      exit(1);
    }
    chunk->next = arena_head;
    chunk->used = 0;
    chunk->cap = cap;
    arena_head = chunk;
    total_chunks++;
    total_reserved_bytes += sizeof(arena_chunk_t) + cap;
  }

  void *p = arena_head->data + arena_head->used;
  arena_head->used += size;
  return p;
}

void tk_allocator_set_expected_size(size_t bytes) {
  // Heuristic: expected token arena footprint tends to be multiple of input size.
  // Keep bounded to avoid excessively large chunks.
  size_t hint = align_up(bytes * 3 / 2 + 4096, 4096);
  if (hint < 16 * 1024) hint = 16 * 1024;
  if (hint > 512 * 1024) hint = 512 * 1024;
  next_chunk_hint = hint;
}

void *tk_allocator_calloc(size_t n, size_t size) {
  if (n != 0 && size > SIZE_MAX / n) {
    fprintf(stderr, "メモリ確保に失敗しました\n");
    exit(1);
  }
  size_t total = n * size;
  void *p = arena_alloc(total);
  memset(p, 0, total == 0 ? 1 : total);
  return p;
}

size_t tk_allocator_total_chunks(void) {
  return total_chunks;
}

size_t tk_allocator_total_reserved_bytes(void) {
  return total_reserved_bytes;
}
