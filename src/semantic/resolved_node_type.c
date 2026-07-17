#include "resolved_node_type.h"

#include <stddef.h>
#include <stdlib.h>

#include "../parser/arena.h"
#include "../parser/ast.h"
#include "resolution_state.h"

typedef struct psx_work_resolution_state_binding_t {
  const node_t *node;
  psx_node_resolution_state_t *state;
  size_t node_size;
  struct psx_work_resolution_state_binding_t *next;
} psx_work_resolution_state_binding_t;

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
enum { PSX_WORK_RESOLUTION_BUCKET_COUNT = 4096 };

static _Thread_local psx_work_resolution_state_binding_t
    *work_resolution_states[PSX_WORK_RESOLUTION_BUCKET_COUNT];
static _Thread_local psx_external_resolution_state_binding_t
    *external_resolution_states;

static size_t work_resolution_bucket(
    const node_t *node) {
  size_t value = (size_t)node;
  value ^= value >> 17;
  value ^= value >> 9;
  return value % PSX_WORK_RESOLUTION_BUCKET_COUNT;
}

static psx_work_resolution_state_binding_t *work_resolution_binding(
    const node_t *node) {
  if (!node) return NULL;
  for (psx_work_resolution_state_binding_t *binding =
           work_resolution_states[work_resolution_bucket(node)];
       binding; binding = binding->next) {
    if (binding->node == node) return binding;
  }
  return NULL;
}

static void remove_work_resolution_binding(void *data) {
  psx_work_resolution_state_binding_t *binding = data;
  if (!binding) return;
  size_t bucket = work_resolution_bucket(binding->node);
  psx_work_resolution_state_binding_t **slot =
      &work_resolution_states[bucket];
  while (*slot && *slot != binding) slot = &(*slot)->next;
  if (*slot) *slot = binding->next;
  free(binding);
}

static int register_work_resolution_state(
    arena_context_t *arena_context, node_t *node,
    size_t node_size) {
  psx_node_resolution_state_t *state = arena_alloc_in(
      arena_context, sizeof(*state));
  psx_work_resolution_state_binding_t *binding = malloc(
      sizeof(*binding));
  if (!state || !binding) {
    free(binding);
    return 0;
  }
  *binding = (psx_work_resolution_state_binding_t){
      .node = node,
      .state = state,
      .node_size = node_size,
      .next = work_resolution_states[work_resolution_bucket(node)],
  };
  if (!arena_register_cleanup_in(
          arena_context, remove_work_resolution_binding, binding)) {
    free(binding);
    return 0;
  }
  work_resolution_states[work_resolution_bucket(node)] = binding;
  return 1;
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

static void remove_external_resolution_binding(void *data) {
  psx_external_resolution_state_binding_t *binding = data;
  if (!binding) return;
  psx_external_resolution_state_binding_t **slot =
      &external_resolution_states;
  while (*slot && *slot != binding) slot = &(*slot)->next;
  if (*slot) *slot = binding->next;
  free(binding);
}

void *psx_resolution_node_alloc_in(
    arena_context_t *arena_context, size_t node_size) {
  if (!arena_context || node_size < sizeof(node_t)) return NULL;
  node_t *node = arena_alloc_in(arena_context, node_size);
  return node && register_work_resolution_state(
                     arena_context, node, node_size)
             ? node : NULL;
}

psx_node_resolution_state_t *ps_node_resolution_state(node_t *node) {
  psx_work_resolution_state_binding_t *work =
      work_resolution_binding(node);
  if (work) return work->state;
  return external_resolution_state(node);
}

const psx_node_resolution_state_t *ps_node_resolution_state_const(
    const node_t *node) {
  return ps_node_resolution_state((node_t *)node);
}

int ps_node_has_resolution_state(const node_t *node) {
  return ps_node_resolution_state_const(node) != NULL;
}

psx_lvar_usage_region_t *ps_node_lvar_usage_region(
    const node_t *node) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(node);
  return state ? state->lvar_usage.region : NULL;
}

void ps_node_set_lvar_usage_region(
    node_t *node, psx_lvar_usage_region_t *region) {
  psx_node_resolution_state_t *state =
      ps_node_resolution_state(node);
  if (state) state->lvar_usage.region = region;
}

lvar_t *ps_node_lvar_usage_symbol(const node_t *node) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(node);
  return state ? state->lvar_usage.local : NULL;
}

int ps_node_records_lvar_usage(const node_t *node) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(node);
  return state && state->lvar_usage.records_usage;
}

void ps_node_record_lvar_usage(node_t *node, lvar_t *local) {
  psx_node_resolution_state_t *state =
      ps_node_resolution_state(node);
  if (!state) return;
  state->lvar_usage.local = local;
  state->lvar_usage.records_usage = local ? 1 : 0;
}

size_t psx_resolution_node_storage_size(const node_t *node) {
  psx_work_resolution_state_binding_t *work =
      work_resolution_binding(node);
  if (work) return work->node_size;
  psx_external_resolution_state_binding_t *binding =
      external_resolution_binding(node);
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
  psx_work_resolution_state_binding_t *work =
      work_resolution_binding(node);
  if (work) {
    if (node_size > work->node_size)
      work->node_size = node_size;
    return 1;
  }
  psx_external_resolution_state_binding_t *existing =
      external_resolution_binding(node);
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
  if (!arena_register_cleanup_in(
          arena_context, remove_external_resolution_binding,
          binding)) {
    free(binding);
    return 0;
  }
  external_resolution_states = binding;
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
