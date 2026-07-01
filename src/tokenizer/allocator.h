#ifndef TOKENIZER_ALLOCATOR_PUBLIC_H
#define TOKENIZER_ALLOCATOR_PUBLIC_H

#include <stddef.h>

/** @brief 入力サイズ見積りを設定してチャンク戦略を調整する。 */
void tk_allocator_set_expected_size(size_t bytes);
/** @brief Tokenizer用アリーナからゼロ初期化メモリを確保する。 */
void *tk_allocator_calloc(size_t n, size_t size);
/** @brief 確保済みチャンク数を返す。 */
size_t tk_allocator_total_chunks(void);
/** @brief 同時 live の最大予約バイト数 (ピーク) を返す。 */
size_t tk_allocator_total_reserved_bytes(void);

/* ---- recyclable アリーナ (トークンストリーム経路) ---- */
/** @brief recyclable モード切替。1 のとき tk_allocator_calloc は recyclable 側へ確保。 */
void tk_allocator_set_recyclable(int on);
/** @brief カーソル前進時に呼ぶ。カーソルが通り過ぎた古い recyclable チャンクを解放する。 */
void tk_allocator_recyc_on_cursor(const void *cursor);
/** @brief _Generic バックトラック中、この位置より古いトークンの解放を禁じる/解除する。 */
void tk_allocator_recyc_pin(const void *p);
void tk_allocator_recyc_unpin(void);
void tk_allocator_recyc_stream_pin(const void *p);
void tk_allocator_recyc_stream_unpin(void);
/** @brief recyclable アリーナを全解放する (コンパイル終了時)。 */
void tk_allocator_recyc_reset(void);

#endif
