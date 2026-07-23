#include "static_data_initializer.h"

#include "runtime_context.h"
#include "static_hir_initializer.h"

#include "../parser/global_registry.h"
#include "../parser/symtab.h"

int psx_apply_static_aggregate_initializer_plan(
    global_var_t *global,
    const psx_static_aggregate_initializer_plan_t *plan) {
  if (!global || !plan || plan->value_count <= 0 ||
      !plan->values || !plan->symbols ||
      !plan->symbol_lengths || !plan->union_ordinals || !plan->offsets)
    return 0;
  global->init_values = plan->values;
  global->init_fvalues = plan->floating_values;
  global->init_value_symbols = plan->symbols;
  global->init_value_symbol_lens = plan->symbol_lengths;
  global->init_union_ordinals = plan->union_ordinals;
  global->init_offsets = plan->offsets;
  global->init_union_activations = plan->union_activations;
  global->init_count = plan->value_count;
  global->init_union_activation_count =
      plan->union_activation_count;
  global->init_union_activation_capacity =
      plan->union_activation_capacity;
  global->union_init_ordinal = plan->union_ordinal;
  return 1;
}

int lower_resolved_static_initializer(
    psx_global_registry_t *global_registry,
    psx_lowering_context_t *lowering_context, global_var_t *global,
    const psx_static_initializer_lowering_input_t *initializer,
    psx_static_declaration_initializer_result_t *result) {
  if (result) *result = (psx_static_declaration_initializer_result_t){0};
  const psx_static_initializer_resolution_t *resolution =
      initializer ? initializer->resolution : NULL;
  if (!global_registry || !lowering_context || !global || !resolution ||
      resolution->status != PSX_STATIC_INITIALIZER_OK ||
      resolution->object_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return 0;
  if (!psx_global_registry_note_global_mutation(
          global_registry, global))
    return 0;

  if (resolution->type_completed) {
    if (!ps_global_registry_complete_array_qual_type(
            global_registry, global, resolution->object_qual_type))
      return 0;
    if (result) result->type_completed = 1;
  }

  if (resolution->is_aggregate_initializer) {
    if (!initializer->aggregate_plan ||
        !psx_apply_static_aggregate_initializer_plan(
            global, initializer->aggregate_plan)) {
      return 0;
    }
    global->has_init = 1;
    if (result) result->initialized = 1;
    return 1;
  }

  if (!initializer->initializer_hir ||
      initializer->initializer_hir_root == PSX_HIR_NODE_ID_INVALID ||
      !psx_lower_static_scalar_hir_initializer(
          global_registry, lowering_context, global,
          ps_gvar_decl_type_id(global),
          initializer->initializer_hir,
          initializer->initializer_hir_root)) {
    return 0;
  }
  global->has_init = 1;
  if (result) result->initialized = 1;
  return 1;
}
