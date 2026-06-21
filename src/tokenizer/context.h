#ifndef TOKENIZER_CONTEXT_H
#define TOKENIZER_CONTEXT_H

#include "tokenizer.h"

/*
 * Tokenizer 内部の実行コンテキスト / カーソル基盤。
 * 実体 (tk_active_ctx / tk_cursor_hook) は tokenizer.c が定義し、token 生成コア
 * (tokenizer.c)・数値リテラル解析 (number.c)・カーソル消費 API (cursor.c) が共有する。
 * consume/expect はパース中に多用されるため、ここでは static inline で提供して
 * クロス TU の呼び出しオーバーヘッドを避ける (公開ヘッダには出さない内部基盤)。
 */

/* 実行中セッションの active context。未設定 (非トークナイズ中) では既定 context。 */
extern tokenizer_context_t *tk_active_ctx;
/* カーソル前進フック (ストリーミング materialize/解放)。未登録なら NULL。 */
extern void (*tk_cursor_hook)(token_t *);

static inline tokenizer_context_t *tk_runtime_ctx(void) {
  return tk_active_ctx ? tk_active_ctx : tk_get_default_context();
}

static inline tokenizer_context_t *tk_effective_ctx(tokenizer_context_t *ctx) {
  return ctx ? ctx : tk_runtime_ctx();
}

/* カーソルを 1 つ前進させ、登録済みフックがあれば次トークンに対し呼ぶ。 */
static inline void tk_advance_current_token(tokenizer_context_t *ctx, token_t *cur) {
  token_t *nx = cur ? cur->next : NULL;
  ctx->current_token = nx;
  if (tk_cursor_hook && nx) tk_cursor_hook(nx);
}

#endif
