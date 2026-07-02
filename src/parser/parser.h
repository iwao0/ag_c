#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "../tokenizer/token.h"
#include "../tokenizer/tokenizer.h"

// プログラム全体をパースする（複数の文を返す）
node_t **ps_program(void);
// 先頭トークンを明示指定してプログラム全体をパースする
node_t **ps_program_from(token_t *start);
// Tokenizerコンテキストを明示してプログラム全体をパースする
node_t **ps_program_ctx(tokenizer_context_t *tk_ctx, token_t *start);

// 関数ごとストリーミングパース。ps_stream_begin で開始し、ps_next_function を
// EOF (NULL 返却) まで繰り返すと、関数定義 AST を 1 つずつ返す。非関数のトップレベル
// 宣言 (グローバル変数・typedef・タグ) は呼び出し中に副作用として処理される。
// 1 関数ぶんの AST だけを保持して codegen→解放できるので、AST のピークメモリを抑える。
void ps_stream_begin(tokenizer_context_t *tk_ctx, token_t *start);
node_t *ps_next_function(void);
// 直前に ps_next_function で取り出した関数の codegen 後に呼び、その関数の AST
// (parser arena) を解放する。次の ps_next_function は新たに arena を使う。
void ps_free_processed_ast(void);
void ps_reset_translation_unit_state(void);

// 単一の式をパースしてASTのルートを返す
node_t *ps_expr(void);
// 先頭トークンを明示指定して単一式をパースする
node_t *ps_expr_from(token_t *start);
// Tokenizerコンテキストを明示して単一式をパースする
node_t *ps_expr_ctx(tokenizer_context_t *tk_ctx, token_t *start);

#endif
