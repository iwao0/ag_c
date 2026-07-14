#ifndef PSX_TYPE_NAME_RESOLUTION_H
#define PSX_TYPE_NAME_RESOLUTION_H

#include "../parser/ast.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_local_registry_t psx_local_registry_t;

int psx_bind_type_name_ref(psx_type_name_ref_t *type_name);
int psx_bind_type_name_ref_in_context(
    psx_semantic_context_t *semantic_context,
    psx_type_name_ref_t *type_name);
int psx_bind_type_name_ref_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_local_registry_t *local_registry,
    psx_type_name_ref_t *type_name);
const psx_type_t *psx_resolve_bound_type_name_ref(
    psx_type_name_ref_t *type_name);
const psx_type_t *psx_resolve_bound_type_name_ref_in_context(
    psx_semantic_context_t *semantic_context,
    psx_type_name_ref_t *type_name);
const psx_type_t *psx_resolve_bound_type_name_ref_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_local_registry_t *local_registry,
    psx_type_name_ref_t *type_name);

#endif
