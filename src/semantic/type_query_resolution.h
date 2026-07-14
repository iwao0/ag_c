#ifndef SEMANTIC_TYPE_QUERY_RESOLUTION_H
#define SEMANTIC_TYPE_QUERY_RESOLUTION_H

#include "../parser/ast.h"

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_local_registry_t psx_local_registry_t;

typedef enum {
  PSX_TYPE_QUERY_RESOLUTION_OK = 0,
  PSX_TYPE_QUERY_RESOLUTION_TYPE_UNRESOLVED,
  PSX_TYPE_QUERY_RESOLUTION_NEGATIVE_ARRAY_BOUND,
  PSX_TYPE_QUERY_RESOLUTION_INVALID_ARRAY_BOUND_TARGET,
} psx_type_query_resolution_status_t;

typedef struct {
  psx_type_query_resolution_status_t status;
  int issue_bound_index;
  int *zero_length_bound_indices;
  int zero_length_bound_count;
  node_t *usage_root;
  int evaluates_vla_operand;
} psx_sizeof_query_resolution_t;

void psx_resolve_sizeof_query(
    node_sizeof_query_t *query,
    psx_sizeof_query_resolution_t *resolution);
void psx_resolve_sizeof_query_in_context(
    psx_semantic_context_t *semantic_context,
    node_sizeof_query_t *query,
    psx_sizeof_query_resolution_t *resolution);
void psx_resolve_sizeof_query_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_local_registry_t *local_registry,
    node_sizeof_query_t *query,
    psx_sizeof_query_resolution_t *resolution);
void psx_resolve_alignof_query(node_alignof_query_t *query);
void psx_resolve_alignof_query_in_context(
    psx_semantic_context_t *semantic_context,
    node_alignof_query_t *query);
void psx_resolve_alignof_query_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_local_registry_t *local_registry,
    node_alignof_query_t *query);

#endif
