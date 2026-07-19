#include "scope_graph.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
  psx_scope_id_t id;
  psx_scope_id_t parent_id;
  psx_scope_id_t order_root_id;
  psx_scope_kind_t kind;
  uint32_t next_declaration_order;
} psx_scope_node_t;

struct psx_scope_graph_t {
  psx_scope_node_t *scopes;
  size_t scope_count;
  size_t scope_capacity;
  psx_scope_declaration_t *declarations;
  size_t declaration_count;
  size_t declaration_capacity;
  psx_scope_id_t current_scope;
};

static int ensure_scope_capacity(psx_scope_graph_t *graph, size_t count) {
  if (count <= graph->scope_capacity) return 1;
  size_t capacity = graph->scope_capacity ? graph->scope_capacity * 2 : 16;
  while (capacity < count) capacity *= 2;
  psx_scope_node_t *scopes = realloc(
      graph->scopes, capacity * sizeof(*scopes));
  if (!scopes) return 0;
  graph->scopes = scopes;
  graph->scope_capacity = capacity;
  return 1;
}

static int ensure_declaration_capacity(
    psx_scope_graph_t *graph, size_t count) {
  if (count <= graph->declaration_capacity) return 1;
  size_t capacity =
      graph->declaration_capacity ? graph->declaration_capacity * 2 : 32;
  while (capacity < count) capacity *= 2;
  psx_scope_declaration_t *declarations = realloc(
      graph->declarations, capacity * sizeof(*declarations));
  if (!declarations) return 0;
  graph->declarations = declarations;
  graph->declaration_capacity = capacity;
  return 1;
}

psx_scope_graph_t *psx_scope_graph_create(void) {
  psx_scope_graph_t *graph = calloc(1, sizeof(*graph));
  if (!graph) return NULL;
  if (!ensure_scope_capacity(graph, 1)) {
    free(graph);
    return NULL;
  }
  psx_scope_graph_reset(graph);
  return graph;
}

void psx_scope_graph_destroy(psx_scope_graph_t *graph) {
  if (!graph) return;
  free(graph->declarations);
  free(graph->scopes);
  free(graph);
}

void psx_scope_graph_reset(psx_scope_graph_t *graph) {
  if (!graph || !ensure_scope_capacity(graph, 1)) return;
  graph->scope_count = 1;
  graph->declaration_count = 0;
  graph->current_scope = PSX_SCOPE_ID_TRANSLATION_UNIT;
  graph->scopes[0] = (psx_scope_node_t){
      .id = PSX_SCOPE_ID_TRANSLATION_UNIT,
      .parent_id = PSX_SCOPE_ID_INVALID,
      .order_root_id = PSX_SCOPE_ID_TRANSLATION_UNIT,
      .kind = PSX_SCOPE_TRANSLATION_UNIT,
  };
}

psx_scope_id_t psx_scope_graph_current_scope(
    const psx_scope_graph_t *graph) {
  return graph ? graph->current_scope : PSX_SCOPE_ID_INVALID;
}

psx_scope_id_t psx_scope_graph_next_scope_id(
    const psx_scope_graph_t *graph) {
  return graph ? (psx_scope_id_t)graph->scope_count
               : PSX_SCOPE_ID_INVALID;
}

psx_scope_kind_t psx_scope_graph_scope_kind(
    const psx_scope_graph_t *graph, psx_scope_id_t scope_id) {
  return graph && scope_id < graph->scope_count
             ? graph->scopes[scope_id].kind
             : PSX_SCOPE_TRANSLATION_UNIT;
}

psx_scope_id_t psx_scope_graph_create_scope_at(
    psx_scope_graph_t *graph, psx_scope_id_t parent_scope,
    psx_scope_kind_t kind) {
  if (!graph || graph->scope_count >= (size_t)PSX_SCOPE_ID_INVALID ||
      parent_scope >= graph->scope_count ||
      !ensure_scope_capacity(graph, graph->scope_count + 1))
    return PSX_SCOPE_ID_INVALID;
  psx_scope_id_t id = (psx_scope_id_t)graph->scope_count;
  psx_scope_id_t order_root =
      kind == PSX_SCOPE_FUNCTION ||
              kind == PSX_SCOPE_FUNCTION_PROTOTYPE ||
              kind == PSX_SCOPE_RECORD
          ? id
          : graph->scopes[parent_scope].order_root_id;
  graph->scopes[graph->scope_count++] = (psx_scope_node_t){
      .id = id,
      .parent_id = parent_scope,
      .order_root_id = order_root,
      .kind = kind,
  };
  return id;
}

psx_scope_id_t psx_scope_graph_enter_scope(
    psx_scope_graph_t *graph, psx_scope_kind_t kind) {
  psx_scope_id_t id = psx_scope_graph_create_scope_at(
      graph, graph ? graph->current_scope : PSX_SCOPE_ID_INVALID,
      kind);
  if (id == PSX_SCOPE_ID_INVALID) return id;
  graph->current_scope = id;
  return id;
}

int psx_scope_graph_leave_scope(psx_scope_graph_t *graph) {
  if (!graph || graph->current_scope == PSX_SCOPE_ID_TRANSLATION_UNIT ||
      graph->current_scope >= graph->scope_count)
    return 0;
  graph->current_scope = graph->scopes[graph->current_scope].parent_id;
  return 1;
}

int psx_scope_graph_scope_is_visible_from(
    const psx_scope_graph_t *graph, psx_scope_id_t declaration_scope,
    psx_scope_id_t reference_scope) {
  if (!graph || declaration_scope >= graph->scope_count ||
      reference_scope >= graph->scope_count)
    return 0;
  for (;;) {
    if (reference_scope == declaration_scope) return 1;
    if (reference_scope == PSX_SCOPE_ID_TRANSLATION_UNIT) return 0;
    reference_scope = graph->scopes[reference_scope].parent_id;
    if (reference_scope == PSX_SCOPE_ID_INVALID) return 0;
  }
}

psx_scope_id_t psx_scope_graph_nearest_scope_of_kind(
    const psx_scope_graph_t *graph, psx_scope_id_t scope_id,
    psx_scope_kind_t kind) {
  if (!graph || scope_id >= graph->scope_count)
    return PSX_SCOPE_ID_INVALID;
  for (;;) {
    if (graph->scopes[scope_id].kind == kind) return scope_id;
    scope_id = graph->scopes[scope_id].parent_id;
    if (scope_id == PSX_SCOPE_ID_INVALID)
      return PSX_SCOPE_ID_INVALID;
  }
}

uint32_t psx_scope_graph_reserve_declaration_order(
    psx_scope_graph_t *graph) {
  if (!graph || graph->current_scope >= graph->scope_count) return 0;
  psx_scope_id_t root =
      graph->scopes[graph->current_scope].order_root_id;
  return ++graph->scopes[root].next_declaration_order;
}

psx_scope_lookup_point_t psx_scope_graph_capture_lookup_point(
    const psx_scope_graph_t *graph) {
  if (!graph) {
    return (psx_scope_lookup_point_t){
        .scope_id = PSX_SCOPE_ID_INVALID,
    };
  }
  return (psx_scope_lookup_point_t){
      .scope_id = graph->current_scope,
      .declaration_order =
          graph->scopes[graph->scopes[graph->current_scope].order_root_id]
              .next_declaration_order,
  };
}

static uint32_t reserve_declaration_order_at(
    psx_scope_graph_t *graph, psx_scope_id_t scope_id) {
  psx_scope_id_t root = graph->scopes[scope_id].order_root_id;
  return ++graph->scopes[root].next_declaration_order;
}

static psx_decl_id_t declare_at(
    psx_scope_graph_t *graph, psx_scope_id_t scope_id,
    psx_c_namespace_t name_space, psx_scope_decl_kind_t kind,
    const char *name, int name_len, void *payload,
    int advances_source_order) {
  if (!graph || scope_id >= graph->scope_count ||
      name_space < 0 || name_space >= PSX_NAMESPACE_COUNT ||
      !name || name_len <= 0 ||
      graph->declaration_count >= (size_t)UINT32_MAX - 1 ||
      !ensure_declaration_capacity(graph, graph->declaration_count + 1))
    return PSX_DECL_ID_INVALID;
  psx_decl_id_t id = (psx_decl_id_t)graph->declaration_count + 1;
  graph->declarations[graph->declaration_count++] =
      (psx_scope_declaration_t){
          .id = id,
          .scope_id = scope_id,
          .name_space = name_space,
          .kind = kind,
          .name = name,
          .name_len = name_len,
          .declaration_order = advances_source_order
                                   ? reserve_declaration_order_at(
                                         graph, scope_id)
                                   : graph->scopes[
                                         graph->scopes[scope_id]
                                             .order_root_id]
                                         .next_declaration_order,
          .payload = payload,
      };
  return id;
}

psx_decl_id_t psx_scope_graph_declare_at(
    psx_scope_graph_t *graph, psx_scope_id_t scope_id,
    psx_c_namespace_t name_space, psx_scope_decl_kind_t kind,
    const char *name, int name_len, void *payload) {
  return declare_at(
      graph, scope_id, name_space, kind, name, name_len, payload, 1);
}

psx_decl_id_t psx_scope_graph_declare_synthetic_at(
    psx_scope_graph_t *graph, psx_scope_id_t scope_id,
    psx_c_namespace_t name_space, psx_scope_decl_kind_t kind,
    const char *name, int name_len, void *payload) {
  return declare_at(
      graph, scope_id, name_space, kind, name, name_len, payload, 0);
}

psx_decl_id_t psx_scope_graph_declare(
    psx_scope_graph_t *graph, psx_c_namespace_t name_space,
    psx_scope_decl_kind_t kind, const char *name, int name_len,
    void *payload) {
  return psx_scope_graph_declare_at(
      graph, graph ? graph->current_scope : PSX_SCOPE_ID_INVALID,
      name_space, kind, name, name_len, payload);
}

const psx_scope_declaration_t *psx_scope_graph_declaration(
    const psx_scope_graph_t *graph, psx_decl_id_t declaration_id) {
  if (!graph || declaration_id == PSX_DECL_ID_INVALID ||
      declaration_id > graph->declaration_count)
    return NULL;
  const psx_scope_declaration_t *declaration =
      &graph->declarations[declaration_id - 1];
  return declaration->kind != PSX_DECL_UNKNOWN ? declaration : NULL;
}

size_t psx_scope_graph_declaration_count(
    const psx_scope_graph_t *graph) {
  return graph ? graph->declaration_count : 0;
}

const psx_scope_declaration_t *psx_scope_graph_declaration_at(
    const psx_scope_graph_t *graph, size_t index) {
  if (!graph || index >= graph->declaration_count) return NULL;
  const psx_scope_declaration_t *declaration =
      &graph->declarations[index];
  return declaration->kind != PSX_DECL_UNKNOWN ? declaration : NULL;
}

void psx_scope_graph_forget_declaration(
    psx_scope_graph_t *graph, psx_decl_id_t declaration_id) {
  if (!graph || declaration_id == PSX_DECL_ID_INVALID ||
      declaration_id > graph->declaration_count)
    return;
  psx_scope_declaration_t *declaration =
      &graph->declarations[declaration_id - 1];
  declaration->kind = PSX_DECL_UNKNOWN;
  declaration->name = NULL;
  declaration->name_len = 0;
  declaration->payload = NULL;
}

int psx_scope_graph_rehome_declaration_at(
    psx_scope_graph_t *graph, psx_decl_id_t declaration_id,
    psx_scope_id_t scope_id) {
  if (!graph || declaration_id == PSX_DECL_ID_INVALID ||
      declaration_id > graph->declaration_count ||
      scope_id >= graph->scope_count)
    return 0;
  psx_scope_declaration_t *declaration =
      &graph->declarations[declaration_id - 1];
  if (declaration->kind == PSX_DECL_UNKNOWN) return 0;
  declaration->scope_id = scope_id;
  declaration->declaration_order =
      graph->scopes[graph->scopes[scope_id].order_root_id]
          .next_declaration_order;
  return 1;
}

static int declaration_name_matches(
    const psx_scope_declaration_t *declaration,
    psx_c_namespace_t name_space, const char *name, int name_len) {
  return declaration && declaration->kind != PSX_DECL_UNKNOWN &&
         declaration->name_space == name_space && declaration->name &&
         declaration->name_len == name_len &&
         memcmp(declaration->name, name, (size_t)name_len) == 0;
}

psx_decl_id_t psx_scope_graph_lookup(
    const psx_scope_graph_t *graph, psx_c_namespace_t name_space,
    const char *name, int name_len, psx_scope_lookup_point_t point) {
  if (!graph || !name || name_len <= 0 ||
      point.scope_id >= graph->scope_count)
    return PSX_DECL_ID_INVALID;
  for (size_t index = graph->declaration_count; index > 0; index--) {
    const psx_scope_declaration_t *declaration =
        &graph->declarations[index - 1];
    int same_order_domain =
        graph->scopes[declaration->scope_id].order_root_id ==
        graph->scopes[point.scope_id].order_root_id;
    if ((name_space != PSX_NAMESPACE_LABEL && same_order_domain &&
         declaration->declaration_order > point.declaration_order) ||
        !declaration_name_matches(
            declaration, name_space, name, name_len) ||
        !psx_scope_graph_scope_is_visible_from(
            graph, declaration->scope_id, point.scope_id))
      continue;
    return declaration->id;
  }
  return PSX_DECL_ID_INVALID;
}

psx_decl_id_t psx_scope_graph_lookup_in_scope(
    const psx_scope_graph_t *graph, psx_scope_id_t scope_id,
    psx_c_namespace_t name_space, const char *name, int name_len) {
  if (!graph || scope_id >= graph->scope_count || !name || name_len <= 0)
    return PSX_DECL_ID_INVALID;
  for (size_t index = graph->declaration_count; index > 0; index--) {
    const psx_scope_declaration_t *declaration =
        &graph->declarations[index - 1];
    if (declaration->scope_id == scope_id &&
        declaration_name_matches(declaration, name_space, name, name_len))
      return declaration->id;
  }
  return PSX_DECL_ID_INVALID;
}

const psx_scope_declaration_t *psx_scope_graph_lookup_declaration_in_scope(
    const psx_scope_graph_t *graph, psx_scope_id_t scope_id,
    psx_c_namespace_t name_space, const char *name, int name_len) {
  return psx_scope_graph_declaration(
      graph, psx_scope_graph_lookup_in_scope(
                 graph, scope_id, name_space, name, name_len));
}

int psx_scope_graph_checkpoint_begin(
    const psx_scope_graph_t *graph,
    psx_scope_graph_checkpoint_t *checkpoint) {
  if (!graph || !checkpoint || checkpoint->active) return 0;
  uint32_t *declaration_orders = calloc(
      graph->scope_count, sizeof(*declaration_orders));
  if (!declaration_orders) return 0;
  for (size_t index = 0; index < graph->scope_count; index++)
    declaration_orders[index] = graph->scopes[index].next_declaration_order;
  *checkpoint = (psx_scope_graph_checkpoint_t){
      .scope_count = graph->scope_count,
      .declaration_count = graph->declaration_count,
      .current_scope = graph->current_scope,
      .declaration_orders = declaration_orders,
      .declaration_order_count = graph->scope_count,
      .active = 1,
  };
  return 1;
}

void psx_scope_graph_checkpoint_commit(
    psx_scope_graph_checkpoint_t *checkpoint) {
  if (!checkpoint) return;
  free(checkpoint->declaration_orders);
  *checkpoint = (psx_scope_graph_checkpoint_t){0};
}

void psx_scope_graph_checkpoint_rollback(
    psx_scope_graph_t *graph, psx_scope_graph_checkpoint_t *checkpoint) {
  if (!graph || !checkpoint || !checkpoint->active) return;
  graph->scope_count = checkpoint->scope_count;
  graph->declaration_count = checkpoint->declaration_count;
  graph->current_scope = checkpoint->current_scope;
  size_t order_count = checkpoint->declaration_order_count;
  if (order_count > graph->scope_count) order_count = graph->scope_count;
  for (size_t index = 0; index < order_count; index++)
    graph->scopes[index].next_declaration_order =
        checkpoint->declaration_orders[index];
  free(checkpoint->declaration_orders);
  *checkpoint = (psx_scope_graph_checkpoint_t){0};
}
