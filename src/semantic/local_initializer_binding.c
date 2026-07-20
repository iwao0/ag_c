#include "local_initializer_binding.h"

#include "../parser/lvar_public.h"
#include "../parser/node_utils.h"
#include "resolved_object_ref.h"

node_t *psx_bind_local_initializer_target_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context,
    const psx_semantic_type_table_t *semantic_types, lvar_t *var,
    node_t *initializer, psx_decl_init_kind_t initializer_kind,
    token_t *initializer_tok) {
  if (!arena_context || !var || !initializer) return NULL;
  node_t *target =
      ps_lvar_is_array(var) || ps_lvar_is_tag_aggregate(var)
          ? psx_node_new_lvar_object_ref_for_in(
                store, arena_context, semantic_types, var)
          : ps_node_new_lvar_expr_ref_for_in(
                store, arena_context, semantic_types, var);
  if (!target) return NULL;
  return psx_node_new_raw_decl_initializer_in(
      arena_context, target, initializer,
      initializer_kind, initializer_tok);
}
