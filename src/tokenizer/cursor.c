/*
 * パーサ向けトークンカーソル API。既にトークン化されたストリームを「消費 (consume)」
 * 「期待 (expect)」「EOF 判定」する操作群で、トークンを生成するコア (tokenizer.c) とは
 * 役割が別。実行コンテキスト解決とカーソル前進は context.h の共有基盤を使う。
 */
#include "context.h"
#include "diag_helper.h"
#include "punctuator.h"
#include <limits.h>

static token_t *require_current_token(tokenizer_context_t *ctx, diag_error_id_t id) {
  token_t *cur = ctx ? ctx->current_token : NULL;
  if (!cur) {
    diag_emit_tokf(id, NULL, "%s", diag_message_for(id));
  }
  return cur;
}

/** @brief 次トークンが指定1文字なら消費して true を返す。 */
bool tk_consume(char op) {
  return tk_consume_ctx(NULL, op);
}

bool tk_consume_ctx(tokenizer_context_t *ctx, char op) {
  tokenizer_context_t *use_ctx = tk_effective_ctx(ctx);
  token_t *cur = use_ctx ? use_ctx->current_token : NULL;
  token_kind_t kind = punctuator_kind_for_char(op);
  if (!cur || kind == TK_EOF || cur->kind != kind)
    return false;
  tk_advance_current_token(use_ctx, cur);
  return true;
}

/** @brief 次トークンが指定記号文字列なら消費して true を返す。 */
bool tk_consume_str(const char *op) {
  return tk_consume_str_ctx(NULL, op);
}

bool tk_consume_str_ctx(tokenizer_context_t *ctx, const char *op) {
  tokenizer_context_t *use_ctx = tk_effective_ctx(ctx);
  token_t *cur = use_ctx ? use_ctx->current_token : NULL;
  token_kind_t kind = punctuator_kind_for_str(op);
  if (!cur || kind == TK_EOF || cur->kind != kind)
    return false;
  tk_advance_current_token(use_ctx, cur);
  return true;
}

/** @brief 次トークンが識別子なら消費して返す。 */
token_ident_t *tk_consume_ident(void) {
  return tk_consume_ident_ctx(NULL);
}

token_ident_t *tk_consume_ident_ctx(tokenizer_context_t *ctx) {
  tokenizer_context_t *use_ctx = tk_effective_ctx(ctx);
  token_t *cur = use_ctx ? use_ctx->current_token : NULL;
  if (!cur || cur->kind != TK_IDENT)
    return NULL;
  token_ident_t *tok = (token_ident_t *)cur;
  tk_advance_current_token(use_ctx, cur);
  return tok;
}

/** @brief 次トークンが指定1文字であることを期待して消費する。 */
void tk_expect(char op) {
  tk_expect_ctx(NULL, op);
}

void tk_expect_ctx(tokenizer_context_t *ctx, char op) {
  tokenizer_context_t *use_ctx = tk_effective_ctx(ctx);
  token_t *cur = require_current_token(use_ctx, DIAG_ERR_TOKENIZER_EXPECTED_TOKEN);
  token_kind_t kind = punctuator_kind_for_char(op);
  if (kind == TK_EOF || cur->kind != kind) {
    diag_emit_tokf(DIAG_ERR_TOKENIZER_EXPECTED_TOKEN, cur, "%s: '%c'",
                   diag_message_for(DIAG_ERR_TOKENIZER_EXPECTED_TOKEN), op);
  }
  tk_advance_current_token(use_ctx, cur);
}

/** @brief 次トークンが整数であることを期待し int 値を返す。 */
int tk_expect_number(void) {
  return tk_expect_number_ctx(NULL);
}

int tk_expect_number_ctx(tokenizer_context_t *ctx) {
  tokenizer_context_t *use_ctx = tk_effective_ctx(ctx);
  token_t *cur = require_current_token(use_ctx, DIAG_ERR_TOKENIZER_EXPECTED_INTEGER);
  if (cur->kind != TK_NUM) {
    TK_DIAG_TOK(DIAG_ERR_TOKENIZER_EXPECTED_INTEGER, cur);
  }
  if (tk_as_num(cur)->num_kind != TK_NUM_KIND_INT) {
    TK_DIAG_TOK(DIAG_ERR_TOKENIZER_EXPECTED_INTEGER, cur);
  }
  long long n = tk_as_num_int(cur)->val;
  if (n < INT_MIN || n > INT_MAX) {
    TK_DIAG_TOK(DIAG_ERR_TOKENIZER_EXPECTED_INTEGER, cur);
  }
  int val = (int)n;
  tk_advance_current_token(use_ctx, cur);
  return val;
}

/** @brief 現在トークンが EOF かを返す。 */
bool tk_at_eof(void) { return tk_at_eof_ctx(NULL); }

bool tk_at_eof_ctx(tokenizer_context_t *ctx) {
  tokenizer_context_t *use_ctx = tk_effective_ctx(ctx);
  token_t *cur = use_ctx ? use_ctx->current_token : NULL;
  return cur && cur->kind == TK_EOF;
}
