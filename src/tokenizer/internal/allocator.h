#ifndef TOKENIZER_ALLOCATOR_H
#define TOKENIZER_ALLOCATOR_H

#include <stddef.h>

/** @brief 入力サイズ見積りを設定してチャンク戦略を調整する。 */
void tk_allocator_set_expected_size(size_t bytes);
/** @brief Tokenizer用アリーナからゼロ初期化メモリを確保する。 */
void *tk_allocator_calloc(size_t n, size_t size);
/** @brief 確保済みチャンク数を返す。 */
size_t tk_allocator_total_chunks(void);
/** @brief 予約済み総バイト数を返す。 */
size_t tk_allocator_total_reserved_bytes(void);

#endif
