#ifndef SEMANTIC_IDENTIFIER_BINDING_H
#define SEMANTIC_IDENTIFIER_BINDING_H

#include "../parser/ast.h"
#include "../compilation_session.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;

node_t *psx_bind_identifier_tree_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    node_t *node, const token_t *fallback_diag_tok);
node_t *psx_bind_identifier_initializer_tree_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    node_t *syntax, const token_t *fallback_diag_tok);
node_t *psx_bind_identifier_tree_in_session(
    ag_compilation_session_t *session,
    node_t *node, const token_t *fallback_diag_tok);
node_t *psx_bind_identifier_initializer_tree_in_session(
    ag_compilation_session_t *session,
    node_t *syntax, const token_t *fallback_diag_tok);

node_t *psx_bind_identifier_tree(
    node_t *node, const token_t *fallback_diag_tok);
node_t *psx_bind_identifier_initializer_tree(
    node_t *syntax, const token_t *fallback_diag_tok);

#endif
