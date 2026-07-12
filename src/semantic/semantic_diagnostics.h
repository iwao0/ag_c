#ifndef SEMANTIC_DIAGNOSTICS_H
#define SEMANTIC_DIAGNOSTICS_H

#include "../parser/ast.h"
#include "../tokenizer/token.h"

void psx_emit_semantic_warnings(
    node_t *root, node_func_t *current_func,
    const token_t *fallback_diag_tok);

#endif
