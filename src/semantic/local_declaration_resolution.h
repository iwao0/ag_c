#ifndef SEMANTIC_LOCAL_DECLARATION_RESOLUTION_H
#define SEMANTIC_LOCAL_DECLARATION_RESOLUTION_H

#include "declaration_application.h"

#define PSX_LOCAL_DECLARATION_MAX_VLA_DIMS 8

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
  node_t *expression;
  long long constant_value;
  int is_constant;
} psx_local_vla_dimension_t;

typedef struct {
  psx_type_t *type;
  const psx_runtime_declarator_application_t *application;
  int has_initializer;
} psx_local_declaration_resolution_request_t;

typedef struct {
  psx_local_declaration_status_t status;
  psx_local_storage_kind_t storage_kind;
  int element_size;
  psx_local_vla_dimension_t
      dimensions[PSX_LOCAL_DECLARATION_MAX_VLA_DIMS];
  int dimension_count;
  node_t *pointer_row_dimension;
} psx_local_declaration_resolution_t;

void psx_resolve_local_declaration(
    const psx_local_declaration_resolution_request_t *request,
    psx_local_declaration_resolution_t *resolution);

#endif
