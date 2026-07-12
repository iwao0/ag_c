#ifndef SEMANTIC_STATIC_INITIALIZER_RESOLUTION_H
#define SEMANTIC_STATIC_INITIALIZER_RESOLUTION_H

#include "../parser/ast.h"
#include "../parser/type.h"

typedef enum {
  PSX_STATIC_INITIALIZER_OK = 0,
  PSX_STATIC_INITIALIZER_INVALID,
  PSX_STATIC_INITIALIZER_DUPLICATE_DEFINITION,
  PSX_STATIC_INITIALIZER_ARRAY_COMPLETION_FAILED,
  PSX_STATIC_INITIALIZER_INVALID_SCALAR_LIST,
} psx_static_initializer_status_t;

typedef struct {
  psx_type_t *type;
  psx_decl_init_kind_t kind;
  node_t *initializer;
  token_t *diag_tok;
  int already_initialized;
} psx_static_initializer_resolution_request_t;

typedef struct {
  psx_static_initializer_status_t status;
  psx_type_t *type;
  psx_decl_init_kind_t kind;
  node_t *initializer;
  int is_aggregate_initializer;
  int type_completed;
} psx_static_initializer_resolution_t;

void psx_resolve_static_initializer(
    const psx_static_initializer_resolution_request_t *request,
    psx_static_initializer_resolution_t *resolution);

#endif
