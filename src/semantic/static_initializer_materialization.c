#include "static_initializer_materialization.h"

#include "../lowering/static_hir_initializer.h"
#include "../lowering/static_initializer_plan.h"
#include "typed_hir_materialization.h"

int psx_materialize_static_aggregate_initializer_plan(
    const psx_typed_hir_tree_t *typed_tree,
    psx_global_registry_t *global_registry,
    psx_lowering_context_t *lowering_context,
    const psx_type_t *type, token_t *diag_tok,
    psx_static_aggregate_initializer_plan_t *plan) {
  if (plan) *plan = (psx_static_aggregate_initializer_plan_t){0};
  if (!typed_tree || !global_registry || !lowering_context ||
      !type || !plan)
    return 0;
  psx_hir_module_t *hir = psx_hir_module_create();
  if (!hir) return 0;
  psx_resolved_hir_build_failure_t failure;
  psx_hir_node_id_t root = psx_typed_hir_tree_emit(
      hir, typed_tree, &failure);
  int built = root != PSX_HIR_NODE_ID_INVALID &&
              psx_build_static_aggregate_hir_initializer_plan(
                  global_registry, lowering_context, type,
                  hir, root, diag_tok, plan);
  psx_hir_module_destroy(hir);
  return built;
}
