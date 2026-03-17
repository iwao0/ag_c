#ifndef TOKENIZER_LITERALS_H
#define TOKENIZER_LITERALS_H

#include <stdbool.h>
#include <stdint.h>

#include "token.h"

/** @brief `\\uXXXX` / `\\UXXXXXXXX` 形式のUCN開始かを判定する。 */
bool tk_starts_with_ucn(const char *p, int *len);
/** @brief UCNをコードポイントにパースする。 */
bool tk_parse_ucn_codepoint(const char *p, uint32_t *out, int *consumed);
/** @brief C11で許可されるUCNコードポイントかを判定する。 */
bool tk_is_valid_ucn_codepoint(uint32_t cp);
/** @brief UnicodeコードポイントをUTF-8へエンコードする。 */
int tk_encode_utf8(uint32_t cp, char out[4]);

/** @brief 文字列/文字定数中の1つのエスケープを読み取って値を返す。 */
int tk_read_escape_char(char **pp);
/** @brief 文字列/文字定数中の1つのエスケープを値化せずにスキップする。 */
void tk_skip_escape_in_literal(char **pp);

/** @brief 文字列接頭辞（L/u/U/u8）を解析する。 */
void tk_parse_string_prefix(
    const char *p,
    int *prefix_len,
    tk_string_prefix_kind_t *prefix_kind,
    tk_char_width_t *char_width);
/** @brief 文字定数接頭辞（L/u/U）を解析する。 */
void tk_parse_char_prefix(
    const char *p,
    int *prefix_len,
    tk_char_prefix_kind_t *prefix_kind,
    tk_char_width_t *char_width);
/** @brief 識別子中のUCNをUTF-8へ展開する。 */
void tk_decode_identifier_ucn(char *start, int len, char **out_str, int *out_len, bool *has_ucn);

#endif
