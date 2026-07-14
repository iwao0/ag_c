#ifndef PARSER_ARENA_H
#define PARSER_ARENA_H

#include <stddef.h>

typedef struct arena_context_t arena_context_t;

arena_context_t *arena_context_create(void);
void arena_context_destroy(arena_context_t *context);
arena_context_t *arena_context_activate(arena_context_t *context);
arena_context_t *arena_context_active(void);

typedef struct {
  arena_context_t *context;
  void *block;
  size_t used;
} arena_checkpoint_t;

// ゼロクリア済みメモリを返すアリーナアロケータ
void *arena_alloc_in(arena_context_t *context, size_t size);
void *arena_alloc(size_t size);

arena_checkpoint_t arena_checkpoint_in(arena_context_t *context);
arena_checkpoint_t arena_checkpoint(void);
void arena_rollback_in(
    arena_context_t *context, arena_checkpoint_t checkpoint);
void arena_rollback(arena_checkpoint_t checkpoint);

// アリーナ全体を解放する（プログラム終了時）
void arena_free_all_in(arena_context_t *context);
void arena_free_all(void);

// これまでに malloc 予約したブロックの総バイト数 (メモリ計測用)。
size_t arena_total_reserved_bytes_in(const arena_context_t *context);
size_t arena_total_reserved_bytes(void);
size_t arena_current_reserved_bytes_in(const arena_context_t *context);

#endif
