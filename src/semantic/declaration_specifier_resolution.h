#ifndef SEMANTIC_DECLARATION_SPECIFIER_RESOLUTION_H
#define SEMANTIC_DECLARATION_SPECIFIER_RESOLUTION_H

#include "../parser/declaration_syntax.h"
#include "declarator_application_types.h"

typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_type_t psx_type_t;

typedef enum {
  PSX_DECL_SPECIFIER_VALUE_OK = 0,
  PSX_DECL_SPECIFIER_VALUE_NOT_SUPPORTED,
  PSX_DECL_SPECIFIER_VALUE_INVALID,
} psx_decl_specifier_value_status_t;

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_global_registry_t *global_registry;
  psx_local_registry_t *local_registry;
  const psx_parsed_decl_specifier_t *syntax;
  int is_standalone_tag;
} psx_decl_specifier_value_request_t;

typedef struct {
  psx_decl_specifier_value_status_t status;
  const psx_type_t *base_type;
  const psx_runtime_declarator_application_t *typedef_runtime_application;
  int tag_member_count;
  int tag_size;
  int tag_alignment;
  int requested_alignment;
} psx_decl_specifier_value_resolution_t;

void psx_resolve_decl_specifier_value_in_contexts(
    const psx_decl_specifier_value_request_t *request,
    psx_decl_specifier_value_resolution_t *resolution);

#endif
