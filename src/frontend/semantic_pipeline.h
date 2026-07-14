#ifndef FRONTEND_SEMANTIC_PIPELINE_H
#define FRONTEND_SEMANTIC_PIPELINE_H

#include "../parser/ast.h"
#include "../tokenizer/token.h"
#include "../compiler_context.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;

void psx_frontend_analyze_function_in_context(
    psx_semantic_context_t *semantic_context,
    node_t *function, const token_t *fallback_diag_tok);
void psx_frontend_analyze_function_in_compiler_context(
    ag_compiler_context_t *compiler_context,
    node_t *function, const token_t *fallback_diag_tok);
node_t *psx_frontend_analyze_expression_in_context(
    psx_semantic_context_t *semantic_context,
    node_t *expression, const token_t *fallback_diag_tok);
node_t *psx_frontend_analyze_initializer_syntax_in_context(
    psx_semantic_context_t *semantic_context,
    node_t *syntax, const token_t *fallback_diag_tok);
void psx_frontend_analyze_program_in_context(
    psx_semantic_context_t *semantic_context, node_t **program);

void psx_frontend_analyze_function(
    node_t *function, const token_t *fallback_diag_tok);
node_t *psx_frontend_analyze_expression(
    node_t *expression, const token_t *fallback_diag_tok);
node_t *psx_frontend_analyze_initializer_syntax(
    node_t *syntax, const token_t *fallback_diag_tok);
void psx_frontend_analyze_program(node_t **program);

#endif
