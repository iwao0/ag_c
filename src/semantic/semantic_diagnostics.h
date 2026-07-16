#ifndef SEMANTIC_DIAGNOSTICS_H
#define SEMANTIC_DIAGNOSTICS_H

#include "../parser/ast.h"
#include "../tokenizer/token.h"
#include "resolved_function.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;

void psx_emit_semantic_warnings(
    psx_semantic_context_t *semantic_context, node_t *root,
    node_function_definition_t *current_func,
    const token_t *fallback_diag_tok);

#endif
