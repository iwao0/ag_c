#ifndef PSX_TYPE_NAME_RESOLUTION_H
#define PSX_TYPE_NAME_RESOLUTION_H

#include "../parser/ast.h"
#include "declarator_application_types.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_registry_t psx_local_registry_t;

typedef struct {
  psx_qual_type_t base_qual_type;
  const psx_runtime_declarator_application_t *runtime_application;
} psx_type_name_base_resolution_t;

token_t *psx_type_name_restrict_qualifier_token(
    const psx_type_name_ref_t *type_name);
token_t *psx_type_name_atomic_qualifier_token(
    const psx_type_name_ref_t *type_name);
int psx_resolve_type_name_base_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_type_name_ref_t *type_name,
    psx_type_name_base_resolution_t *resolution);
int psx_resolve_type_name_qual_type_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_type_name_ref_t *type_name,
    psx_qual_type_t *qual_type);
/* Resolves variably modified bounds at the captured lookup point for type
 * formation, without returning a runtime evaluation plan to the caller. */
int psx_resolve_runtime_type_name_qual_type_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_type_name_ref_t *type_name,
    psx_qual_type_t *qual_type);
#endif
