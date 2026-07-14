#ifndef LOWERING_COMPOUND_LITERAL_LOWERING_H
#define LOWERING_COMPOUND_LITERAL_LOWERING_H

#include "../parser/ast.h"

typedef struct psx_local_registry_t psx_local_registry_t;

node_t *lower_compound_literal_expression_in(
    psx_local_registry_t *local_registry,
    node_t *node, const token_t *fallback_diag_tok);

node_t *lower_compound_literal_expression(
    node_t *node, const token_t *fallback_diag_tok);
void psx_compound_literal_lowering_reset_translation_unit_state(void);

#endif
