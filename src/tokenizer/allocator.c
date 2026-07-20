#include "allocator.h"
#include "../diag/diag.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct arena_chunk_t arena_chunk_t;
struct arena_chunk_t {
  arena_chunk_t *next;
  size_t used;
  size_t cap;
  unsigned char data[];
};

static unsigned char *chunk_data(arena_chunk_t *c) {
  return (unsigned char *)(c + 1);
}

static const unsigned char *chunk_data_const(const arena_chunk_t *c) {
  return (const unsigned char *)(c + 1);
}

/* ---- recyclable アリーナ (トークンストリーム経路) ----
 * `#` 指令の無いファイルでは、パーサがカーソルを前進させながらトークンを消費する。
 * 消費済みトークンはもう参照されない (AST は token->str=正規化バッファを指す、唯一の
 * バックトラックは _Generic で式内に収まる) ので、カーソルが通り過ぎたチャンクを解放して
 * トークンのピークメモリを O(ウィンドウ) に抑える。永続データ (predefined マクロ本体等) は
 * g_recyc_mode=0 のとき従来の arena_head 側へ確保され、ここでは解放しない。 */
struct tk_allocator_context_t {
  ag_diagnostic_context_t *diagnostic_context;
  arena_chunk_t *arena_head;
  size_t total_chunks;
  size_t total_reserved_bytes;
  size_t peak_reserved_bytes;
  size_t next_chunk_hint;
  arena_chunk_t *recyc_oldest;
  arena_chunk_t *recyc_newest;
  int recyc_mode;
  const unsigned char *recyc_pin;
  const unsigned char *recyc_stream_pin;
  arena_chunk_t *recyc_cursor_chunk;
};

tk_allocator_context_t *tk_allocator_context_create(
    ag_diagnostic_context_t *diagnostic_context) {
  tk_allocator_context_t *ctx = calloc(1, sizeof(*ctx));
  if (ctx) {
    ctx->diagnostic_context = diagnostic_context;
    ctx->next_chunk_hint = 16 * 1024;
  }
  return ctx;
}

static void free_chunk_list(arena_chunk_t *head) {
  while (head) {
    arena_chunk_t *next = head->next;
    free(head);
    head = next;
  }
}

void tk_allocator_context_destroy(tk_allocator_context_t *ctx) {
  if (!ctx) return;
  free_chunk_list(ctx->arena_head);
  free_chunk_list(ctx->recyc_oldest);
  free(ctx);
}

void tk_allocator_bind_diagnostic_context_in(
    tk_allocator_context_t *ctx,
    ag_diagnostic_context_t *diagnostic_context) {
  if (ctx) ctx->diagnostic_context = diagnostic_context;
}

ag_diagnostic_context_t *tk_allocator_diagnostics(
    const tk_allocator_context_t *ctx) {
  return ctx ? ctx->diagnostic_context : NULL;
}

static int ptr_in_chunk(const void *p, const arena_chunk_t *c) {
  const unsigned char *u = p;
  const unsigned char *base = c ? chunk_data_const(c) : NULL;
  return c && u >= base && u < base + c->cap;
}

// FIFO 上で a が b 以前 (より古いか同じ) かを返す。
static int chunk_not_after(
    const tk_allocator_context_t *ctx,
    const arena_chunk_t *a, const arena_chunk_t *b) {
  for (const arena_chunk_t *c = ctx->recyc_oldest; c; c = c->next) {
    if (c == a) return 1;       // a を先に見つけた → a は b 以前
    if (c == b) return 0;
  }
  return 0;
}

/** @brief 指定アラインメント境界へ切り上げる。 */
static size_t align_up(size_t n, size_t align) {
  return (n + align - 1) & ~(align - 1);
}

/* recyclable アリーナはカーソル通過分を解放して O(ウィンドウ) に保つので、チャンクを
 * 小さく固定するとウィンドウ (= 数チャンク) も小さくなる。64KB ≈ 1300 トークンで、
 * パーサの look-behind (宣言子 1 つ分 < 数十トークン) に対するマージン (1 チャンク) は
 * 十分。永続側 (マクロ本体等) は従来どおり入力サイズ連動の大きいチャンク。 */
#define RECYC_CHUNK_CAP (64 * 1024)

static arena_chunk_t *new_chunk(
    tk_allocator_context_t *ctx, size_t size, size_t hint) {
  size_t cap = hint;
  if (cap < size) cap = align_up(size, 4096);
  arena_chunk_t *chunk = malloc(sizeof(arena_chunk_t) + cap);
  if (!chunk) {
    diag_emit_internalf_in(
        ctx->diagnostic_context, DIAG_ERR_INTERNAL_OOM, "%s",
        diag_message_for_in(
            ctx->diagnostic_context, DIAG_ERR_INTERNAL_OOM));
  }
  chunk->next = NULL;
  chunk->used = 0;
  chunk->cap = cap;
  ctx->total_chunks++;
  ctx->total_reserved_bytes += sizeof(arena_chunk_t) + cap;
  if (ctx->total_reserved_bytes > ctx->peak_reserved_bytes)
    ctx->peak_reserved_bytes = ctx->total_reserved_bytes;
  return chunk;
}

/** @brief Tokenizer用アリーナから連続領域を確保する。g_recyc_mode のとき recyclable 側。 */
static void *arena_alloc(tk_allocator_context_t *ctx, size_t size) {
  if (size == 0) size = 1;
  size = align_up(size, 8);

  void *result = NULL;
  if (ctx->recyc_mode) {
    if (!ctx->recyc_newest ||
        ctx->recyc_newest->used + size > ctx->recyc_newest->cap) {
      arena_chunk_t *chunk = new_chunk(ctx, size, RECYC_CHUNK_CAP);
      if (ctx->recyc_newest) ctx->recyc_newest->next = chunk;
      else ctx->recyc_oldest = chunk;
      ctx->recyc_newest = chunk;
    }
    result = chunk_data(ctx->recyc_newest) + ctx->recyc_newest->used;
    ctx->recyc_newest->used += size;
  } else {
    if (!ctx->arena_head || ctx->arena_head->used + size > ctx->arena_head->cap) {
      arena_chunk_t *chunk = new_chunk(ctx, size, ctx->next_chunk_hint);
      chunk->next = ctx->arena_head;  // 永続側は LIFO (prepend)
      ctx->arena_head = chunk;
    }
    result = chunk_data(ctx->arena_head) + ctx->arena_head->used;
    ctx->arena_head->used += size;
  }
  return result;
}

/** @brief recyclable モードを切り替える。streamed トークン生成中だけ 1 にする。 */
void tk_allocator_set_recyclable_in(
    tk_allocator_context_t *ctx, int on) {
  if (ctx) ctx->recyc_mode = on ? 1 : 0;
}

/** @brief _Generic バックトラック等で、この位置より古いトークンの解放を一時的に禁じる。 */
void tk_allocator_recyc_pin_in(
    tk_allocator_context_t *ctx, const void *p) {
  if (ctx) ctx->recyc_pin = p;
}
void tk_allocator_recyc_unpin_in(tk_allocator_context_t *ctx) {
  if (ctx) ctx->recyc_pin = NULL;
}
void tk_allocator_recyc_stream_pin_in(
    tk_allocator_context_t *ctx, const void *p) {
  if (ctx) ctx->recyc_stream_pin = p;
}
void tk_allocator_recyc_stream_unpin_in(tk_allocator_context_t *ctx) {
  if (ctx) ctx->recyc_stream_pin = NULL;
}

static void recyc_apply_pin_floor(
    const tk_allocator_context_t *ctx,
    const unsigned char *pin, arena_chunk_t **floor) {
  if (!pin || !floor || !*floor) return;
  for (arena_chunk_t *c = ctx->recyc_oldest; c; c = c->next) {
    if (ptr_in_chunk(pin, c)) {
      if (chunk_not_after(ctx, c, *floor)) *floor = c;
      break;
    }
  }
}

/** @brief カーソルが指すトークンより前 (古い) の recyclable チャンクを解放する。
 * floor = min(カーソルのチャンク, pin のチャンク)。floor のチャンクとそれ以降は残す。
 * カーソルが同じチャンク内を進む間は何もしない (チャンク跨ぎ時のみ走査)。 */
void tk_allocator_recyc_on_cursor_in(
    tk_allocator_context_t *ctx, const void *cursor) {
  if (!ctx || !ctx->recyc_mode || !cursor) return;
  if (ctx->recyc_cursor_chunk &&
      ptr_in_chunk(cursor, ctx->recyc_cursor_chunk)) return; // 同一チャンク内
  // カーソルのチャンクを探す
  arena_chunk_t *cur_chunk = NULL;
  for (arena_chunk_t *c = ctx->recyc_oldest; c; c = c->next) {
    if (ptr_in_chunk(cursor, c)) { cur_chunk = c; break; }
  }
  if (!cur_chunk) return;          // recyclable 外 (静的 EOF 等) → 解放しない
  ctx->recyc_cursor_chunk = cur_chunk;
  /* floor = カーソルのチャンクの数個前。直前に消費したトークンの構造体フィールド
   * (tok->str 等) や preprocessor の指令処理中に materialize した一時 token をまだ読むため、
   * 複数チャンクぶんのマージンを残す。 */
  arena_chunk_t *floor = cur_chunk;
  for (int keep = 0; keep < 4; keep++) {
    arena_chunk_t *prev = NULL;
    for (arena_chunk_t *c = ctx->recyc_oldest; c && c->next; c = c->next) {
      if (c->next == floor) { prev = c; break; }
    }
    if (!prev) break;
    floor = prev;
  }
  recyc_apply_pin_floor(ctx, ctx->recyc_pin, &floor);
  recyc_apply_pin_floor(ctx, ctx->recyc_stream_pin, &floor);
  // recyc_oldest .. floor の手前までを解放
  while (ctx->recyc_oldest && ctx->recyc_oldest != floor) {
    arena_chunk_t *dead = ctx->recyc_oldest;
    ctx->recyc_oldest = dead->next;
    ctx->total_chunks--;
    ctx->total_reserved_bytes -= sizeof(arena_chunk_t) + dead->cap;
    free(dead);
  }
}

/** @brief recyclable アリーナを全解放する (コンパイル終了時)。 */
void tk_allocator_recyc_reset_in(tk_allocator_context_t *ctx) {
  if (!ctx) return;
  for (arena_chunk_t *c = ctx->recyc_oldest; c; ) {
    arena_chunk_t *next = c->next;
    ctx->total_chunks--;
    ctx->total_reserved_bytes -= sizeof(arena_chunk_t) + c->cap;
    free(c);
    c = next;
  }
  ctx->recyc_oldest = ctx->recyc_newest = ctx->recyc_cursor_chunk = NULL;
  ctx->recyc_pin = NULL;
  ctx->recyc_stream_pin = NULL;
}

void tk_allocator_reset_translation_unit_in(tk_allocator_context_t *ctx) {
  if (!ctx) return;
  tk_allocator_set_recyclable_in(ctx, 0);
  tk_allocator_recyc_reset_in(ctx);
  for (arena_chunk_t *c = ctx->arena_head; c; ) {
    arena_chunk_t *next = c->next;
    ctx->total_chunks--;
    ctx->total_reserved_bytes -= sizeof(arena_chunk_t) + c->cap;
    free(c);
    c = next;
  }
  ctx->arena_head = NULL;
  ctx->next_chunk_hint = 16 * 1024;
}

/** @brief 入力サイズから次チャンクサイズのヒントを更新する。 */
void tk_allocator_set_expected_size_in(
    tk_allocator_context_t *ctx, size_t bytes) {
  if (!ctx) return;
  // Heuristic: expected token arena footprint tends to be multiple of input size.
  // Keep bounded to avoid excessively large chunks.
  size_t hint = align_up(bytes * 3 / 2 + 4096, 4096);
  if (hint < 16 * 1024) hint = 16 * 1024;
  if (hint > 512 * 1024) hint = 512 * 1024;
  ctx->next_chunk_hint = hint;
}

/** @brief アリーナ確保 + ゼロ初期化を行う。 */
void *tk_allocator_calloc_in(
    tk_allocator_context_t *ctx, size_t n, size_t size) {
  if (!ctx) return NULL;
  if (n != 0 && size > SIZE_MAX / n) {
    diag_emit_internalf_in(
        ctx->diagnostic_context, DIAG_ERR_INTERNAL_OOM, "%s",
        diag_message_for_in(
            ctx->diagnostic_context, DIAG_ERR_INTERNAL_OOM));
  }
  size_t total = n * size;
  void *p = arena_alloc(ctx, total);
  memset(p, 0, total == 0 ? 1 : total);
  return p;
}

/** @brief これまでに確保したチャンク数を返す。 */
size_t tk_allocator_total_chunks_in(const tk_allocator_context_t *ctx) {
  return ctx ? ctx->total_chunks : 0;
}

/** @brief 同時 live の最大予約バイト数 (ピーク) を返す。アリーナを reset しない現状では
 * 累計と一致する。将来セグメントごとに reset する経路では「最大の 1 セグメント」を表す。 */
size_t tk_allocator_total_reserved_bytes_in(
    const tk_allocator_context_t *ctx) {
  if (!ctx) return 0;
  return ctx->peak_reserved_bytes > ctx->total_reserved_bytes
             ? ctx->peak_reserved_bytes : ctx->total_reserved_bytes;
}
