#ifndef FRONTEND_SEMANTIC_PIPELINE_H
#define FRONTEND_SEMANTIC_PIPELINE_H

#include "../parser/ast.h"
#include "../tokenizer/token.h"

void psx_frontend_analyze_function(
    node_t *function, const token_t *fallback_diag_tok);
node_t *psx_frontend_analyze_expression(
    node_t *expression, const token_t *fallback_diag_tok);
node_t *psx_frontend_analyze_initializer_syntax(
    node_t *syntax, const token_t *fallback_diag_tok);
void psx_frontend_analyze_program(node_t **program);

#endif
