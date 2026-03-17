#ifndef TOKENIZER_PUNCTUATOR_H
#define TOKENIZER_PUNCTUATOR_H

#include "token.h"
#include <stdbool.h>

/** @brief 記号文字列が完全一致した場合の token kind を返す。 */
token_kind_t punctuator_kind_for_str(const char *op);

/**
 * @brief 最長一致ルールで記号をマッチする。
 * @param p 入力位置。
 * @param out_kind マッチした token kind。
 * @param out_len 消費バイト数。
 * @return マッチ時 true。
 */
bool match_punctuator(const char *p, token_kind_t *out_kind, int *out_len);

#endif
