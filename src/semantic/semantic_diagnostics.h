#ifndef SEMANTIC_DIAGNOSTICS_H
#define SEMANTIC_DIAGNOSTICS_H

#include "../parser/ast.h"
#include "../tokenizer/token.h"

typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;

void psx_emit_semantic_warnings(
    ag_diagnostic_context_t *diagnostics, node_t *root,
    node_function_definition_t *current_func,
    const token_t *fallback_diag_tok);

#endif
