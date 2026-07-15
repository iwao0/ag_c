#ifndef SEMANTIC_CONTROL_FLOW_VALIDATION_H
#define SEMANTIC_CONTROL_FLOW_VALIDATION_H

#include "../parser/ast.h"
#include "../tokenizer/token.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;

void psx_validate_control_flow(
    psx_semantic_context_t *semantic_context,
    node_t *node, const token_t *fallback_diag_tok);
void psx_emit_unreachable_warnings(
    psx_semantic_context_t *semantic_context,
    node_t *node, const token_t *fallback_diag_tok);

#endif
