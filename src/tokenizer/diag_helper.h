#ifndef TOKENIZER_DIAG_HELPER_H
#define TOKENIZER_DIAG_HELPER_H

#include "../diag/diag.h"
#include "context.h"
#include "tokenizer.h"

#define TK_DIAG_MESSAGE_IN(ctx, id) \
  diag_message_for_in(tk_context_diagnostics((ctx)), (id))
#define TK_DIAG_MESSAGE(id) \
  TK_DIAG_MESSAGE_IN(tk_runtime_ctx(), (id))
#define TK_DIAG_TEXT_IN(ctx, id) \
  diag_text_for_in(tk_context_diagnostics((ctx)), (id))
#define TK_DIAG_TEXT(id) \
  TK_DIAG_TEXT_IN(tk_runtime_ctx(), (id))

/**
 * @brief Tokenizer入力コンテキスト付きで位置ベース診断を出力する内部ヘルパー。
 */
/* 寛容モード (`#if 0` 偽分岐の skip/行先読み) 中は tk_tolerate_longjmp_if_active が
 * tk_stream_next へ巻き戻すため diag_emit_atf には到達しない。通常時は従来どおり診断+終了。 */
#define TK_DIAG_ATF_IN(ctx, id, loc, fmt, ...) \
  do { \
    tokenizer_context_t *tk_diag_ctx__ = (ctx); \
    ag_diagnostic_context_t *tk_diagnostics__ = \
        tk_context_diagnostics(tk_diag_ctx__); \
    tk_tolerate_longjmp_if_active(); \
    diag_emit_atf_in( \
        tk_diagnostics__, (id), tk_get_user_input_ctx(tk_diag_ctx__), \
        (loc), (fmt), ##__VA_ARGS__); \
  } while (0)
#define TK_DIAG_ATF(id, loc, fmt, ...) \
  TK_DIAG_ATF_IN(tk_runtime_ctx(), (id), (loc), (fmt), ##__VA_ARGS__)

/**
 * @brief カタログ既定メッセージで位置ベース診断を出力する内部ヘルパー。
 */
#define TK_DIAG_AT_IN(ctx, id, loc) \
  TK_DIAG_ATF_IN( \
      (ctx), (id), (loc), "%s", TK_DIAG_MESSAGE_IN((ctx), (id)))
#define TK_DIAG_AT(id, loc) \
  TK_DIAG_AT_IN(tk_runtime_ctx(), (id), (loc))

/**
 * @brief カタログ既定メッセージでトークンベース診断を出力する内部ヘルパー。
 */
#define TK_DIAG_TOK_IN(ctx, id, tok) \
  diag_emit_tokf_in( \
      tk_context_diagnostics((ctx)), (id), (tok), "%s", \
      TK_DIAG_MESSAGE_IN((ctx), (id)))
#define TK_DIAG_TOK(id, tok) \
  TK_DIAG_TOK_IN(tk_runtime_ctx(), (id), (tok))

#endif
