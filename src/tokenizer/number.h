#ifndef TOKENIZER_NUMBER_H
#define TOKENIZER_NUMBER_H

#include <stdint.h>

#include "token.h"

typedef struct tokenizer_context_t tokenizer_context_t;

/* 数値リテラル解析の中間表現。ソーステキストを整数/浮動の共通表現へ変換した結果で、
 * トークン構築 (tokenize_number_literal) はこれを読んでトークンへ書き写す。
 * フィールドは 8B → 4B(enum) → 1B の順に並べて内部パディングを詰めている (sizeof=48)。 */
struct parsed_num_t {
  long long val;
  unsigned long long uval;
  double fval;
  tk_float_kind_t fp_kind;
  tk_float_suffix_kind_t float_suffix_kind;
  tk_int_size_t int_size;
  tk_char_width_t char_width;
  tk_char_prefix_kind_t char_prefix_kind;
  bool is_unsigned;
  uint8_t int_base;
};
typedef struct parsed_num_t parsed_num_t;

/**
 * @brief 数値リテラル本体を解析し、整数/浮動の共通表現 (parsed_num_t) へ変換する。
 * @param pp 入力カーソル。解析後は消費後位置へ更新。
 * @param num 解析結果の出力先。
 * @warning 不正な基数/サフィックス/範囲外は診断終了する。
 */
void tk_parse_number_literal_ctx(
    tokenizer_context_t *ctx, char **pp, parsed_num_t *num);

#endif
