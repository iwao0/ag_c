#ifndef SEMANTIC_CONTROL_FLOW_VALIDATION_H
#define SEMANTIC_CONTROL_FLOW_VALIDATION_H

#include "../parser/ast.h"
#include "../tokenizer/token.h"

void psx_validate_control_flow(
    node_t *node, const token_t *fallback_diag_tok);
void psx_emit_unreachable_warnings(
    node_t *node, const token_t *fallback_diag_tok);

#endif
