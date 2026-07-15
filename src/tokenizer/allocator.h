#ifndef TOKENIZER_ALLOCATOR_PUBLIC_H
#define TOKENIZER_ALLOCATOR_PUBLIC_H

#include <stddef.h>

typedef struct tk_allocator_context_t tk_allocator_context_t;
typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;

tk_allocator_context_t *tk_allocator_context_create(
    ag_diagnostic_context_t *diagnostic_context);
void tk_allocator_context_destroy(tk_allocator_context_t *ctx);
tk_allocator_context_t *tk_allocator_default_context(void);

/** @brief 入力サイズ見積りを設定してチャンク戦略を調整する。 */
void tk_allocator_set_expected_size_in(
    tk_allocator_context_t *ctx, size_t bytes);
/** @brief Tokenizer用アリーナからゼロ初期化メモリを確保する。 */
void *tk_allocator_calloc_in(
    tk_allocator_context_t *ctx, size_t n, size_t size);
/** @brief 確保済みチャンク数を返す。 */
size_t tk_allocator_total_chunks_in(const tk_allocator_context_t *ctx);
/** @brief 同時 live の最大予約バイト数 (ピーク) を返す。 */
size_t tk_allocator_total_reserved_bytes_in(
    const tk_allocator_context_t *ctx);

/* ---- recyclable アリーナ (トークンストリーム経路) ---- */
/** @brief recyclable モード切替。1 のとき calloc は recyclable 側へ確保。 */
void tk_allocator_set_recyclable_in(
    tk_allocator_context_t *ctx, int on);
/** @brief カーソル前進時に呼ぶ。カーソルが通り過ぎた古い recyclable チャンクを解放する。 */
void tk_allocator_recyc_on_cursor_in(
    tk_allocator_context_t *ctx, const void *cursor);
/** @brief _Generic バックトラック中、この位置より古いトークンの解放を禁じる/解除する。 */
void tk_allocator_recyc_pin_in(
    tk_allocator_context_t *ctx, const void *p);
void tk_allocator_recyc_unpin_in(tk_allocator_context_t *ctx);
void tk_allocator_recyc_stream_pin_in(
    tk_allocator_context_t *ctx, const void *p);
void tk_allocator_recyc_stream_unpin_in(tk_allocator_context_t *ctx);
/** @brief recyclable アリーナを全解放する (コンパイル終了時)。 */
void tk_allocator_recyc_reset_in(tk_allocator_context_t *ctx);
/** @brief 永続・recyclable の両アリーナを全解放する (翻訳単位の開始時)。 */
void tk_allocator_reset_translation_unit_in(tk_allocator_context_t *ctx);

#endif
