#ifndef SEMANTIC_LITERAL_RESOLUTION_H
#define SEMANTIC_LITERAL_RESOLUTION_H

#include "type_identity.h"

typedef struct node_num_t node_num_t;
typedef struct node_string_t node_string_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_resolution_store_t psx_resolution_store_t;

typedef struct {
  psx_qual_type_t qual_type;
  char *string_label;
} psx_literal_semantic_resolution_t;

int psx_resolve_number_literal_semantics_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    const node_num_t *literal,
    psx_literal_semantic_resolution_t *resolution);
int psx_resolve_string_literal_semantics_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    const node_string_t *literal,
    psx_literal_semantic_resolution_t *resolution);

void psx_string_literal_bind_label(
    psx_resolution_store_t *store,
    node_string_t *literal, char *label);
char *psx_string_literal_label(
    const psx_resolution_store_t *store,
    const node_string_t *literal);

#endif
