#ifndef SEMANTIC_STATIC_INITIALIZER_RESOLUTION_H
#define SEMANTIC_STATIC_INITIALIZER_RESOLUTION_H

#include "../parser/ast.h"
#include "static_initializer_classification.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_qual_type_t type;
  psx_decl_init_kind_t kind;
  node_t *initializer;
  token_t *diag_tok;
  int already_initialized;
} psx_static_initializer_resolution_request_t;

void psx_resolve_static_initializer(
    const psx_static_initializer_resolution_request_t *request,
    psx_static_initializer_resolution_t *resolution);

#endif
