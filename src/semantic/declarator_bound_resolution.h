#ifndef SEMANTIC_DECLARATOR_BOUND_RESOLUTION_H
#define SEMANTIC_DECLARATOR_BOUND_RESOLUTION_H

#include "../parser/local_registry.h"
#include "typed_hir_tree.h"

typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct node_t node_t;
typedef struct token_t token_t;

typedef struct {
  const psx_typed_hir_tree_t *typed_expression;
  long long constant_value;
  unsigned char is_constant;
} psx_declarator_bound_resolution_t;

int psx_resolve_declarator_bound_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const node_t *syntax_expression,
    const psx_local_lookup_point_t *lookup_point,
    const token_t *fallback_diag_tok,
    psx_declarator_bound_resolution_t *resolution);

#endif
