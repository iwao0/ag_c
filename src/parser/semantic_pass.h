#ifndef PARSER_SEMANTIC_PASS_H
#define PARSER_SEMANTIC_PASS_H

#include "ast.h"
#include "../tokenizer/token.h"

void psx_semantic_analyze_function(node_t *func, const token_t *fallback_diag_tok);
void psx_semantic_analyze_expression(node_t *expr,
                                     const token_t *fallback_diag_tok);
void psx_semantic_analyze_initializer_syntax(
    node_t *syntax, const token_t *fallback_diag_tok);
void psx_semantic_analyze_program(node_t **program);

#endif
