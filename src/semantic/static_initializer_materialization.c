#include "static_initializer_materialization.h"

#include "../lowering/static_data_initializer.h"
#include "resolution_work_tree_internal.h"
#include "resolved_node_kind.h"

int psx_materialize_static_aggregate_initializer_plan(
    const psx_resolution_work_tree_t *work_tree,
    psx_global_registry_t *global_registry,
    psx_lowering_context_t *lowering_context,
    const psx_type_t *type, token_t *diag_tok,
    psx_static_aggregate_initializer_plan_t *plan) {
  if (plan) *plan = (psx_static_aggregate_initializer_plan_t){0};
  const node_t *root =
      psx_resolution_work_tree_compatibility_root(work_tree);
  if (!work_tree ||
      psx_resolution_work_tree_phase(work_tree) <
          PSX_RESOLUTION_WORK_FINALIZED ||
      !global_registry || !lowering_context || !type || !plan ||
      !root || root->kind != ND_INIT_LIST)
    return 0;
  return psx_build_static_aggregate_initializer_plan(
      global_registry, lowering_context, type,
      (node_init_list_t *)root, diag_tok, plan);
}
