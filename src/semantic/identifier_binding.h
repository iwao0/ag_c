#ifndef SEMANTIC_IDENTIFIER_BINDING_H
#define SEMANTIC_IDENTIFIER_BINDING_H

#include "../parser/ast.h"
#include "scope_graph.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;

typedef struct {
  psx_semantic_context_t *semantic_context;
  char *function_name;
  int function_name_len;
  const token_t *fallback_diag_tok;
  psx_scope_lookup_point_t lookup_point;
  unsigned char has_lookup_point;
} psx_identifier_binding_request_t;

node_t *psx_bind_identifier_tree_in(
    const psx_identifier_binding_request_t *request,
    node_t *node);
node_t *psx_bind_identifier_initializer_tree_in(
    const psx_identifier_binding_request_t *request,
    node_t *syntax);

#endif
