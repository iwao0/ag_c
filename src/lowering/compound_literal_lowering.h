#ifndef LOWERING_COMPOUND_LITERAL_LOWERING_H
#define LOWERING_COMPOUND_LITERAL_LOWERING_H

#include "../parser/ast.h"

typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;

node_t *lower_compound_literal_expression_in(
    psx_local_registry_t *local_registry,
    node_t *node, const token_t *fallback_diag_tok);
node_t *lower_compound_literal_expression_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    node_t *node, const token_t *fallback_diag_tok);

node_t *lower_compound_literal_expression(
    node_t *node, const token_t *fallback_diag_tok);
void psx_compound_literal_lowering_reset_translation_unit_state(void);

#endif
