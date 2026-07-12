#ifndef PREPROCESS_H
#define PREPROCESS_H

#include "../tokenizer/tokenizer.h"

// プリプロセッサのエントリポイント
// トークン列を受け取り、マクロ展開とディレクティブ処理を
// 行った後の新しいトークン列を返す
token_t *preprocess(token_t *tok);
/** @brief 明示Tokenizerコンテキストでプリプロセスを実行する。 */
token_t *preprocess_ctx(tokenizer_context_t *tk_ctx, token_t *tok);

/* トークンストリーム経路 (`#` 指令の無いファイル専用) の遅延プリプロセス生成器。
 * pp_stream_open は predefined マクロを永続側に作り、recyclable 生成器を開き、
 * カーソル前進フックを登録して先頭トークンを返す。frontend stream beginで
 * その先頭から消費すると、前進のたびに先読み materialize + 通過トークン解放が走る。 */
typedef struct pp_stream pp_stream_t;
token_t *pp_stream_open(pp_stream_t **out_s, tokenizer_context_t *tk_ctx, const char *src);
void pp_stream_close(pp_stream_t *s);

#endif
