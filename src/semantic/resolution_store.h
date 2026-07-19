#ifndef SEMANTIC_RESOLUTION_STORE_H
#define SEMANTIC_RESOLUTION_STORE_H

#include <stddef.h>

typedef struct arena_context_t arena_context_t;
typedef struct node_t node_t;
typedef struct psx_node_resolution_state_t psx_node_resolution_state_t;
typedef struct psx_resolution_store_t psx_resolution_store_t;

psx_resolution_store_t *psx_resolution_store_create(void);
void psx_resolution_store_destroy(psx_resolution_store_t *store);

psx_node_resolution_state_t *psx_resolution_store_lookup(
    psx_resolution_store_t *store, const node_t *node);
const psx_node_resolution_state_t *psx_resolution_store_lookup_const(
    const psx_resolution_store_t *store, const node_t *node);
size_t psx_resolution_store_node_size(
    const psx_resolution_store_t *store, const node_t *node);

int psx_resolution_store_prepare_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *node, size_t node_size);

#endif
