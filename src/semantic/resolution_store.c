#include "resolution_store.h"

#include <stdint.h>
#include <stdlib.h>

#include "../parser/arena.h"
#include "../parser/ast.h"
#include "resolution_state.h"

enum { PSX_RESOLUTION_STORE_BUCKET_COUNT = 4096 };

typedef struct psx_resolution_binding_t {
  psx_resolution_store_t *store;
  const node_t *node;
  psx_node_resolution_state_t *state;
  size_t node_size;
  struct psx_resolution_binding_t *next;
} psx_resolution_binding_t;

struct psx_resolution_store_t {
  psx_resolution_binding_t
      *buckets[PSX_RESOLUTION_STORE_BUCKET_COUNT];
  psx_semantic_type_table_t *semantic_types;
};

static size_t resolution_bucket(const node_t *node) {
  uintptr_t value = (uintptr_t)node;
  value ^= value >> 17;
  value ^= value >> 9;
  return (size_t)(value % PSX_RESOLUTION_STORE_BUCKET_COUNT);
}

static psx_resolution_binding_t *resolution_binding(
    const psx_resolution_store_t *store, const node_t *node) {
  if (!store || !node) return NULL;
  for (psx_resolution_binding_t *binding =
           store->buckets[resolution_bucket(node)];
       binding; binding = binding->next) {
    if (binding->node == node) return binding;
  }
  return NULL;
}

static void remove_resolution_binding(void *data) {
  psx_resolution_binding_t *binding = data;
  if (!binding) return;
  psx_resolution_store_t *store = binding->store;
  if (store) {
    size_t bucket = resolution_bucket(binding->node);
    psx_resolution_binding_t **slot = &store->buckets[bucket];
    while (*slot && *slot != binding) slot = &(*slot)->next;
    if (*slot) *slot = binding->next;
  }
  free(binding);
}

psx_resolution_store_t *psx_resolution_store_create(void) {
  return calloc(1, sizeof(psx_resolution_store_t));
}

void psx_resolution_store_bind_semantic_types(
    psx_resolution_store_t *store,
    psx_semantic_type_table_t *semantic_types) {
  if (store) store->semantic_types = semantic_types;
}

const psx_semantic_type_table_t *psx_resolution_store_semantic_types(
    const psx_resolution_store_t *store) {
  return store ? store->semantic_types : NULL;
}

void psx_resolution_store_destroy(psx_resolution_store_t *store) {
  if (!store) return;
  for (size_t bucket = 0;
       bucket < PSX_RESOLUTION_STORE_BUCKET_COUNT; bucket++) {
    for (psx_resolution_binding_t *binding = store->buckets[bucket];
         binding; binding = binding->next)
      binding->store = NULL;
  }
  free(store);
}

psx_node_resolution_state_t *psx_resolution_store_lookup(
    psx_resolution_store_t *store, const node_t *node) {
  psx_resolution_binding_t *binding =
      resolution_binding(store, node);
  return binding ? binding->state : NULL;
}

const psx_node_resolution_state_t *psx_resolution_store_lookup_const(
    const psx_resolution_store_t *store, const node_t *node) {
  return psx_resolution_store_lookup(
      (psx_resolution_store_t *)store, node);
}

size_t psx_resolution_store_node_size(
    const psx_resolution_store_t *store, const node_t *node) {
  psx_resolution_binding_t *binding =
      resolution_binding(store, node);
  return binding ? binding->node_size : 0;
}

int psx_resolution_store_prepare_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *node, size_t node_size) {
  if (!store || !arena_context || !node || node_size < sizeof(*node))
    return 0;
  psx_resolution_binding_t *existing =
      resolution_binding(store, node);
  if (existing) {
    if (node_size > existing->node_size)
      existing->node_size = node_size;
    return 1;
  }
  psx_node_resolution_state_t *state = arena_alloc_in(
      arena_context, sizeof(*state));
  psx_resolution_binding_t *binding = calloc(1, sizeof(*binding));
  if (!state || !binding) {
    free(binding);
    return 0;
  }
  *binding = (psx_resolution_binding_t){
      .store = store,
      .node = node,
      .state = state,
      .node_size = node_size,
      .next = store->buckets[resolution_bucket(node)],
  };
  if (!arena_register_cleanup_in(
          arena_context, remove_resolution_binding, binding)) {
    free(binding);
    return 0;
  }
  store->buckets[resolution_bucket(node)] = binding;
  return 1;
}
