#include "resolved_node_type.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../parser/arena.h"
#include "../parser/ast.h"
#include "resolution_state.h"

static const uint64_t PSX_RESOLUTION_NODE_PREFIX_MAGIC =
    UINT64_C(0x5053585245534f4c);

typedef union {
  max_align_t alignment;
  struct {
    uint64_t magic;
    size_t node_size;
    psx_node_resolution_state_t state;
  } payload;
} psx_resolution_node_prefix_t;

typedef struct psx_external_resolution_state_binding_t {
  const node_t *node;
  psx_node_resolution_state_t *state;
  size_t node_size;
  struct psx_external_resolution_state_binding_t *next;
} psx_external_resolution_state_binding_t;

/*
 * Stack-backed nodes only exist in compatibility tests and a few isolated
 * semantic helpers. Resolution work nodes use the inline prefix below.
 */
static _Thread_local psx_external_resolution_state_binding_t
    *external_resolution_states;

static psx_resolution_node_prefix_t *resolution_node_prefix(
    const node_t *node) {
  if (!node || !node->is_resolution_work_node) return NULL;
  psx_resolution_node_prefix_t *prefix =
      (psx_resolution_node_prefix_t *)node - 1;
  return prefix->payload.magic == PSX_RESOLUTION_NODE_PREFIX_MAGIC
             ? prefix : NULL;
}

static psx_node_resolution_state_t *external_resolution_state(
    const node_t *node) {
  for (psx_external_resolution_state_binding_t *binding =
           external_resolution_states;
       binding; binding = binding->next) {
    if (binding->node == node) return binding->state;
  }
  return NULL;
}

static psx_external_resolution_state_binding_t *
external_resolution_binding(const node_t *node) {
  for (psx_external_resolution_state_binding_t *binding =
           external_resolution_states;
       binding; binding = binding->next) {
    if (binding->node == node) return binding;
  }
  return NULL;
}

void *psx_resolution_node_alloc_in(
    arena_context_t *arena_context, size_t node_size) {
  if (!arena_context || node_size < sizeof(node_t)) return NULL;
  psx_resolution_node_prefix_t *prefix = arena_alloc_in(
      arena_context, sizeof(*prefix) + node_size);
  if (!prefix) return NULL;
  prefix->payload.magic = PSX_RESOLUTION_NODE_PREFIX_MAGIC;
  prefix->payload.node_size = node_size;
  node_t *node = (node_t *)(prefix + 1);
  node->is_resolution_work_node = 1;
  return node;
}

psx_node_resolution_state_t *ps_node_resolution_state(node_t *node) {
  psx_resolution_node_prefix_t *prefix = resolution_node_prefix(node);
  if (prefix) return &prefix->payload.state;
  return node && node->has_external_resolution_state
             ? external_resolution_state(node) : NULL;
}

const psx_node_resolution_state_t *ps_node_resolution_state_const(
    const node_t *node) {
  return ps_node_resolution_state((node_t *)node);
}

int ps_node_has_resolution_state(const node_t *node) {
  return ps_node_resolution_state_const(node) != NULL;
}

size_t psx_resolution_node_storage_size(const node_t *node) {
  psx_resolution_node_prefix_t *prefix =
      resolution_node_prefix(node);
  if (prefix) return prefix->payload.node_size;
  psx_external_resolution_state_binding_t *binding =
      node && node->has_external_resolution_state
          ? external_resolution_binding(node) : NULL;
  return binding ? binding->node_size : 0;
}

const psx_type_t *ps_node_get_type(const node_t *node) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(node);
  return state ? state->type : NULL;
}

psx_qual_type_t ps_node_qual_type(const node_t *node) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(node);
  return state ? state->qual_type
               : (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                                   PSX_TYPE_QUALIFIER_NONE};
}

int ps_node_prepare_resolution_state_in(
    arena_context_t *arena_context, node_t *node) {
  return ps_node_prepare_resolution_state_for_size_in(
      arena_context, node, sizeof(*node));
}

int ps_node_prepare_resolution_state_for_size_in(
    arena_context_t *arena_context, node_t *node, size_t node_size) {
  if (!node || !arena_context) return 0;
  if (node_size < sizeof(*node)) return 0;
  psx_resolution_node_prefix_t *prefix =
      resolution_node_prefix(node);
  if (prefix) {
    if (node_size > prefix->payload.node_size)
      prefix->payload.node_size = node_size;
    return 1;
  }
  psx_external_resolution_state_binding_t *existing =
      node->has_external_resolution_state
          ? external_resolution_binding(node) : NULL;
  if (existing) {
    if (node_size > existing->node_size)
      existing->node_size = node_size;
    return 1;
  }
  psx_external_resolution_state_binding_t *binding = calloc(
      1, sizeof(*binding));
  psx_node_resolution_state_t *state = arena_alloc_in(
      arena_context, sizeof(*state));
  if (!binding || !state) {
    free(binding);
    return 0;
  }
  binding->node = node;
  binding->state = state;
  binding->node_size = node_size;
  binding->next = external_resolution_states;
  external_resolution_states = binding;
  node->has_external_resolution_state = 1;
  return 1;
}

int ps_node_copy_resolution_state_in(
    arena_context_t *arena_context, node_t *destination,
    const node_t *source) {
  if (!destination) return 0;
  const psx_node_resolution_state_t *source_state =
      ps_node_resolution_state_const(source);
  psx_node_resolution_state_t *destination_state =
      ps_node_resolution_state(destination);
  if (!source_state) {
    if (destination_state)
      *destination_state = (psx_node_resolution_state_t){0};
    return 1;
  }
  if (!ps_node_prepare_resolution_state_in(
          arena_context, destination))
    return 0;
  destination_state = ps_node_resolution_state(destination);
  *destination_state = *source_state;
  return 1;
}
