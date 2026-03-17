#ifndef PARSER_H
#define PARSER_H

#include "ast.h"

// プログラム全体をパースする（複数の文を返す）
node_t **ps_program(void);

// 単一の式をパースしてASTのルートを返す
node_t *ps_expr(void);

#endif
