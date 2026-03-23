#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "../tokenizer/token.h"

// プログラム全体をパースする（複数の文を返す）
node_t **ps_program(void);
// 先頭トークンを明示指定してプログラム全体をパースする
node_t **ps_program_from(token_t *start);

// 単一の式をパースしてASTのルートを返す
node_t *ps_expr(void);
// 先頭トークンを明示指定して単一式をパースする
node_t *ps_expr_from(token_t *start);

#endif
