#ifndef TOKENIZER_ESCAPE_PUBLIC_H
#define TOKENIZER_ESCAPE_PUBLIC_H

#include <stdint.h>

/**
 * @brief エスケープシーケンスをデコードする。
 * @param s 入力文字列。
 * @param len 入力長。
 * @param i 開始位置（`s[*i] == '\\'` を想定）。成功時は消費後位置へ進む。
 * @param out デコード結果のコードポイント。
 * @return 成功時 1、開始位置がエスケープでない場合 0。
 */
int tk_parse_escape_value(const char *s, int len, int *i, uint32_t *out);

#endif
