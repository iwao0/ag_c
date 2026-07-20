#ifndef SEMANTIC_STATIC_INITIALIZER_RESOLUTION_H
#define SEMANTIC_STATIC_INITIALIZER_RESOLUTION_H

#include "../parser/ast.h"
#include "../parser/type.h"
#include "../hir/hir.h"
#include "type_identity.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_static_aggregate_initializer_plan_t
    psx_static_aggregate_initializer_plan_t;

typedef enum {
  PSX_STATIC_INITIALIZER_OK = 0,
  PSX_STATIC_INITIALIZER_INVALID,
  PSX_STATIC_INITIALIZER_DUPLICATE_DEFINITION,
  PSX_STATIC_INITIALIZER_ARRAY_COMPLETION_FAILED,
  PSX_STATIC_INITIALIZER_INVALID_SCALAR_LIST,
} psx_static_initializer_status_t;

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_qual_type_t type;
  psx_decl_init_kind_t kind;
  node_t *initializer;
  token_t *diag_tok;
  int already_initialized;
} psx_static_initializer_resolution_request_t;

typedef struct {
  psx_static_initializer_status_t status;
  psx_qual_type_t object_qual_type;
  psx_decl_init_kind_t kind;
  node_t *initializer;
  const psx_hir_module_t *initializer_hir;
  psx_hir_node_id_t initializer_hir_root;
  const psx_static_aggregate_initializer_plan_t *aggregate_plan;
  int is_aggregate_initializer;
  int type_completed;
} psx_static_initializer_resolution_t;

void psx_resolve_static_initializer(
    const psx_static_initializer_resolution_request_t *request,
    psx_static_initializer_resolution_t *resolution);

#endif
