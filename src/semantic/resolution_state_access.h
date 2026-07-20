#ifndef SEMANTIC_RESOLUTION_STATE_ACCESS_H
#define SEMANTIC_RESOLUTION_STATE_ACCESS_H

#include <stddef.h>

#include "../parser/node_fwd.h"

typedef struct arena_context_t arena_context_t;
typedef struct psx_node_resolution_state_t psx_node_resolution_state_t;
typedef struct psx_resolution_store_t psx_resolution_store_t;

psx_node_resolution_state_t *ps_node_resolution_state(
    psx_resolution_store_t *store, node_t *node);
const psx_node_resolution_state_t *ps_node_resolution_state_const(
    const psx_resolution_store_t *store, const node_t *node);
int ps_node_has_resolution_state(
    const psx_resolution_store_t *store, const node_t *node);
size_t psx_resolution_node_storage_size(
    const psx_resolution_store_t *store, const node_t *node);
void *psx_resolution_node_alloc_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    size_t node_size);
int ps_node_prepare_resolution_state_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *node);
int ps_node_prepare_resolution_state_for_size_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *node, size_t node_size);
int ps_node_copy_resolution_state_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *destination, const node_t *source);

#endif
