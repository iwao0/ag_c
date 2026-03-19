#ifndef TOKENIZER_DIAG_HELPER_H
#define TOKENIZER_DIAG_HELPER_H

#include "../../diag/diag.h"
#include "../tokenizer.h"

/**
 * @brief Tokenizer入力コンテキスト付きで位置ベース診断を出力する内部ヘルパー。
 */
#define TK_DIAG_ATF(id, loc, fmt, ...) \
  diag_emit_atf((id), tk_get_user_input(), (loc), (fmt), ##__VA_ARGS__)

#endif
