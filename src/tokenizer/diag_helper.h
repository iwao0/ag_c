#ifndef TOKENIZER_DIAG_HELPER_H
#define TOKENIZER_DIAG_HELPER_H

#include "../diag/diag.h"
#include "tokenizer.h"

/**
 * @brief Tokenizer入力コンテキスト付きで位置ベース診断を出力する内部ヘルパー。
 */
/* 寛容モード (`#if 0` 偽分岐の skip/行先読み) 中は tk_tolerate_longjmp_if_active が
 * tk_stream_next へ巻き戻すため diag_emit_atf には到達しない。通常時は従来どおり診断+終了。 */
#define TK_DIAG_ATF(id, loc, fmt, ...) \
  do { tk_tolerate_longjmp_if_active(); \
       diag_emit_atf((id), tk_get_user_input_ctx(NULL), (loc), (fmt), ##__VA_ARGS__); } while (0)

/**
 * @brief カタログ既定メッセージで位置ベース診断を出力する内部ヘルパー。
 */
#define TK_DIAG_AT(id, loc) \
  do { tk_tolerate_longjmp_if_active(); \
       diag_emit_atf((id), tk_get_user_input_ctx(NULL), (loc), "%s", diag_message_for((id))); } while (0)

/**
 * @brief カタログ既定メッセージでトークンベース診断を出力する内部ヘルパー。
 */
#define TK_DIAG_TOK(id, tok) \
  diag_emit_tokf((id), (tok), "%s", diag_message_for((id)))

#endif
