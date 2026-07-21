#ifndef PARSER_ARENA_H
#define PARSER_ARENA_H

#include <stddef.h>

typedef struct arena_context_t arena_context_t;
typedef void (*arena_cleanup_fn)(void *data);

arena_context_t *arena_context_create(void);
void arena_context_destroy(arena_context_t *context);

typedef struct {
  arena_context_t *context;
  void *block;
  void *cleanup;
  size_t used;
} arena_checkpoint_t;

// ゼロクリア済みメモリを返すアリーナアロケータ
void *arena_alloc_in(arena_context_t *context, size_t size);
int arena_register_cleanup_in(
    arena_context_t *context, arena_cleanup_fn cleanup, void *data);

arena_checkpoint_t arena_checkpoint_in(arena_context_t *context);
void arena_rollback_in(
    arena_context_t *context, arena_checkpoint_t checkpoint);

// アリーナ全体を解放する（プログラム終了時）
void arena_free_all_in(arena_context_t *context);

// これまでに malloc 予約したブロックの総バイト数 (メモリ計測用)。
size_t arena_total_reserved_bytes_in(const arena_context_t *context);
size_t arena_current_reserved_bytes_in(const arena_context_t *context);

// Test support: fail arena_alloc_in() after the requested number of
// successful allocation calls. The limit belongs to this arena only.
void arena_fail_allocations_after_in(
    arena_context_t *context, size_t successful_allocations);
void arena_clear_allocation_failure_in(arena_context_t *context);

#endif
