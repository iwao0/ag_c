#ifndef SEMANTIC_LOCAL_DECLARATION_RESOLUTION_H
#define SEMANTIC_LOCAL_DECLARATION_RESOLUTION_H

#include "declaration_resolution.h"
#include "record_layout.h"
#include "type_identity.h"

typedef struct arena_context_t arena_context_t;

typedef enum {
  PSX_LOCAL_DECLARATION_OK = 0,
  PSX_LOCAL_DECLARATION_INVALID,
  PSX_LOCAL_DECLARATION_VOID_OBJECT,
  PSX_LOCAL_DECLARATION_INCOMPLETE_OBJECT,
  PSX_LOCAL_DECLARATION_INCOMPLETE_ARRAY_NEEDS_INITIALIZER,
  PSX_LOCAL_DECLARATION_VLA_INITIALIZER_FORBIDDEN,
} psx_local_declaration_status_t;

typedef enum {
  PSX_LOCAL_STORAGE_COMPLETE = 0,
  PSX_LOCAL_STORAGE_INCOMPLETE_ARRAY,
  PSX_LOCAL_STORAGE_VLA_OBJECT,
  PSX_LOCAL_STORAGE_POINTER_TO_VLA,
} psx_local_storage_kind_t;

typedef struct {
  psx_semantic_expr_id_t expression_id;
  long long constant_value;
  int is_constant;
} psx_local_vla_dimension_t;

typedef struct {
  arena_context_t *arena_context;
  const psx_semantic_type_table_t *semantic_types;
  const psx_record_layout_table_t *record_layouts;
  psx_type_id_t type_id;
  const ag_data_layout_t *data_layout;
  const psx_runtime_declarator_application_t *application;
  int has_initializer;
} psx_local_declaration_resolution_request_t;

typedef struct {
  psx_local_declaration_status_t status;
  psx_local_storage_kind_t storage_kind;
  psx_local_vla_dimension_t *dimensions;
  int dimension_count;
  int pointer_indirections;
} psx_local_declaration_resolution_t;

void psx_resolve_local_declaration(
    const psx_local_declaration_resolution_request_t *request,
    psx_local_declaration_resolution_t *resolution);

#endif
