#include "parser_type_compatibility.h"

#include "resolution_state.h"
#include "resolution_state_access.h"
#include "resolution_store.h"
#include "resolved_node_type.h"
#include "type_compatibility_view.h"

psx_qual_type_t psx_resolution_store_intern_type(
    psx_resolution_store_t *store, const psx_type_t *type) {
  psx_semantic_type_table_t *types =
      (psx_semantic_type_table_t *)
          psx_resolution_store_semantic_types(store);
  return types && type
             ? psx_semantic_type_table_intern(types, type)
             : (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                                 PSX_TYPE_QUALIFIER_NONE};
}

const psx_type_t *ps_node_get_type(
    const psx_resolution_store_t *store, const node_t *node) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(store, node);
  if (!state || state->type_binding.kind != PSX_NODE_TYPE_CANONICAL)
    return NULL;
  return psx_type_compatibility_view_for(
      psx_resolution_store_semantic_types(store),
      state->type_binding.canonical_type);
}

void ps_node_bind_type(
    psx_resolution_store_t *store, node_t *node,
    const psx_type_t *type) {
  psx_qual_type_t qual_type = psx_resolution_store_intern_type(store, type);
  if (!ps_node_bind_qual_type(store, node, qual_type)) {
    psx_node_resolution_state_t *state =
        ps_node_resolution_state(store, node);
    if (state) state->type_binding = (psx_node_type_binding_t){0};
  }
}
