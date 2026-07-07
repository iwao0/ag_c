#ifndef TOKENIZER_CONTEXT_H
#define TOKENIZER_CONTEXT_H

#include "tokenizer.h"

/*
 * Tokenizer 内部の実行コンテキスト / カーソル基盤。
 * 実体は tokenizer.c が定義し、token 生成コア (tokenizer.c)・数値リテラル解析
 * (number.c)・カーソル消費 API (cursor.c) が共有する。
 * 公開ヘッダには出さない内部基盤だが、正本の実体は tokenizer.c に閉じる。
 */

tokenizer_context_t *tk_runtime_ctx(void);
tokenizer_context_t *tk_effective_ctx(tokenizer_context_t *ctx);
void tk_advance_current_token(tokenizer_context_t *ctx, token_t *cur);

#endif
