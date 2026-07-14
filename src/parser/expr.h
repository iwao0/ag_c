#ifndef PARSER_EXPR_H
#define PARSER_EXPR_H

#include "ast.h"
#include "local_declaration_syntax.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_local_registry_t psx_local_registry_t;

node_t *psx_expr_expr(void);
node_t *psx_expr_assign(void);
node_t *psx_expr_expr_in_context(
    psx_semantic_context_t *semantic_context,
    const psx_local_declaration_callbacks_t *local_declarations);
node_t *psx_expr_assign_in_context(
    psx_semantic_context_t *semantic_context,
    const psx_local_declaration_callbacks_t *local_declarations);
node_t *psx_expr_expr_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_local_registry_t *local_registry,
    const psx_local_declaration_callbacks_t *local_declarations);
node_t *psx_expr_assign_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_local_registry_t *local_registry,
    const psx_local_declaration_callbacks_t *local_declarations);

void ps_expr_reset_translation_unit_state(void);

#endif
