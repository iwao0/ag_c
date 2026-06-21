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

static arena_chunk_t *arena_head;
static size_t total_chunks;
static size_t total_reserved_bytes;  // 現在 live のチャンク総バイト数 (永続+recyclable 合算)
static size_t peak_reserved_bytes;   // 同時 live の最大 (アリーナ reclaim を跨ぐ高水位)
static size_t next_chunk_hint = 16 * 1024;

/* ---- recyclable アリーナ (トークンストリーム経路) ----
 * `#` 指令の無いファイルでは、パーサがカーソルを前進させながらトークンを消費する。
 * 消費済みトークンはもう参照されない (AST は token->str=正規化バッファを指す、唯一の
 * バックトラックは _Generic で式内に収まる) ので、カーソルが通り過ぎたチャンクを解放して
 * トークンのピークメモリを O(ウィンドウ) に抑える。永続データ (predefined マクロ本体等) は
 * g_recyc_mode=0 のとき従来の arena_head 側へ確保され、ここでは解放しない。 */
static arena_chunk_t *recyc_oldest;   // FIFO: 確保が古い順
static arena_chunk_t *recyc_newest;   // bump 先 (最新)
static int g_recyc_mode = 0;          // 1 のとき tk_allocator_calloc は recyclable 側へ
static const unsigned char *recyc_pin = NULL;  // _Generic バックトラック用の解放下限
static arena_chunk_t *recyc_cursor_chunk = NULL; // 直近カーソルが属するチャンク (キャッシュ)

static int ptr_in_chunk(const void *p, const arena_chunk_t *c) {
  const unsigned char *u = p;
  return c && u >= c->data && u < c->data + c->cap;
}

// FIFO 上で a が b 以前 (より古いか同じ) かを返す。
static int chunk_not_after(const arena_chunk_t *a, const arena_chunk_t *b) {
  for (const arena_chunk_t *c = recyc_oldest; c; c = c->next) {
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

static arena_chunk_t *new_chunk(size_t size, size_t hint) {
  size_t cap = hint;
  if (cap < size) cap = align_up(size, 4096);
  arena_chunk_t *chunk = malloc(sizeof(arena_chunk_t) + cap);
  if (!chunk) {
    diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
  }
  chunk->next = NULL;
  chunk->used = 0;
  chunk->cap = cap;
  total_chunks++;
  total_reserved_bytes += sizeof(arena_chunk_t) + cap;
  if (total_reserved_bytes > peak_reserved_bytes) peak_reserved_bytes = total_reserved_bytes;
  return chunk;
}

/** @brief Tokenizer用アリーナから連続領域を確保する。g_recyc_mode のとき recyclable 側。 */
static void *arena_alloc(size_t size) {
  if (size == 0) size = 1;
  size = align_up(size, 8);

  if (g_recyc_mode) {
    if (!recyc_newest || recyc_newest->used + size > recyc_newest->cap) {
      arena_chunk_t *chunk = new_chunk(size, RECYC_CHUNK_CAP);
      if (recyc_newest) recyc_newest->next = chunk; else recyc_oldest = chunk;
      recyc_newest = chunk;
    }
    void *p = recyc_newest->data + recyc_newest->used;
    recyc_newest->used += size;
    return p;
  }

  if (!arena_head || arena_head->used + size > arena_head->cap) {
    arena_chunk_t *chunk = new_chunk(size, next_chunk_hint);
    chunk->next = arena_head;  // 永続側は LIFO (prepend)
    arena_head = chunk;
  }
  void *p = arena_head->data + arena_head->used;
  arena_head->used += size;
  return p;
}

/** @brief recyclable モードを切り替える。streamed トークン生成中だけ 1 にする。 */
void tk_allocator_set_recyclable(int on) { g_recyc_mode = on ? 1 : 0; }

/** @brief _Generic バックトラック等で、この位置より古いトークンの解放を一時的に禁じる。 */
void tk_allocator_recyc_pin(const void *p) { recyc_pin = p; }
void tk_allocator_recyc_unpin(void) { recyc_pin = NULL; }

/** @brief カーソルが指すトークンより前 (古い) の recyclable チャンクを解放する。
 * floor = min(カーソルのチャンク, pin のチャンク)。floor のチャンクとそれ以降は残す。
 * カーソルが同じチャンク内を進む間は何もしない (チャンク跨ぎ時のみ走査)。 */
void tk_allocator_recyc_on_cursor(const void *cursor) {
  if (!g_recyc_mode || !cursor) return;
  if (recyc_cursor_chunk && ptr_in_chunk(cursor, recyc_cursor_chunk)) return; // 同一チャンク内
  // カーソルのチャンクを探す
  arena_chunk_t *cur_chunk = NULL;
  for (arena_chunk_t *c = recyc_oldest; c; c = c->next) {
    if (ptr_in_chunk(cursor, c)) { cur_chunk = c; break; }
  }
  if (!cur_chunk) return;          // recyclable 外 (静的 EOF 等) → 解放しない
  recyc_cursor_chunk = cur_chunk;
  /* floor = カーソルのチャンクの 1 つ前。直前に消費したトークンの構造体フィールド
   * (tok->str 等) をパーサがまだ読むため、1 チャンクぶんのマージンを残す
   * (look-behind は宣言子 1 つ分 ≪ 1 チャンク)。カーソルが最古チャンクなら floor=cur。 */
  arena_chunk_t *floor = cur_chunk;
  for (arena_chunk_t *c = recyc_oldest; c && c->next; c = c->next) {
    if (c->next == cur_chunk) { floor = c; break; }
  }
  if (recyc_pin) {
    for (arena_chunk_t *c = recyc_oldest; c; c = c->next) {
      if (ptr_in_chunk(recyc_pin, c)) {  // pin のチャンクが floor より古ければ採用
        if (chunk_not_after(c, floor)) floor = c;
        break;
      }
    }
  }
  // recyc_oldest .. floor の手前までを解放
  while (recyc_oldest && recyc_oldest != floor) {
    arena_chunk_t *dead = recyc_oldest;
    recyc_oldest = dead->next;
    total_chunks--;
    total_reserved_bytes -= sizeof(arena_chunk_t) + dead->cap;
    free(dead);
  }
}

/** @brief recyclable アリーナを全解放する (コンパイル終了時)。 */
void tk_allocator_recyc_reset(void) {
  for (arena_chunk_t *c = recyc_oldest; c; ) {
    arena_chunk_t *next = c->next;
    total_chunks--;
    total_reserved_bytes -= sizeof(arena_chunk_t) + c->cap;
    free(c);
    c = next;
  }
  recyc_oldest = recyc_newest = recyc_cursor_chunk = NULL;
  recyc_pin = NULL;
}

/** @brief 入力サイズから次チャンクサイズのヒントを更新する。 */
void tk_allocator_set_expected_size(size_t bytes) {
  // Heuristic: expected token arena footprint tends to be multiple of input size.
  // Keep bounded to avoid excessively large chunks.
  size_t hint = align_up(bytes * 3 / 2 + 4096, 4096);
  if (hint < 16 * 1024) hint = 16 * 1024;
  if (hint > 512 * 1024) hint = 512 * 1024;
  next_chunk_hint = hint;
}

/** @brief アリーナ確保 + ゼロ初期化を行う。 */
void *tk_allocator_calloc(size_t n, size_t size) {
  if (n != 0 && size > SIZE_MAX / n) {
    diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
  }
  size_t total = n * size;
  void *p = arena_alloc(total);
  memset(p, 0, total == 0 ? 1 : total);
  return p;
}

/** @brief これまでに確保したチャンク数を返す。 */
size_t tk_allocator_total_chunks(void) {
  return total_chunks;
}

/** @brief 同時 live の最大予約バイト数 (ピーク) を返す。アリーナを reset しない現状では
 * 累計と一致する。将来セグメントごとに reset する経路では「最大の 1 セグメント」を表す。 */
size_t tk_allocator_total_reserved_bytes(void) {
  return peak_reserved_bytes > total_reserved_bytes ? peak_reserved_bytes : total_reserved_bytes;
}
