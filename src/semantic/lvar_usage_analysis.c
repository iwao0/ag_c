#include "lvar_usage_analysis.h"

#include "resolution_state.h"
#include "../diag/diag.h"
#include "../parser/local_registry.h"
#include "../parser/lvar_public.h"
#include "../parser/node_utils.h"
#include "generic_selection_resolution.h"
#include "sizeof_query_resolution.h"
#include "resolved_node_kind.h"
#include "resolved_node.h"
#include "resolved_node_type.h"
#include "resolved_object_ref.h"
#include "vla_runtime_plan.h"
#include "source_cast_resolution.h"

static psx_resolution_node_kind_t resolved_node_kind(
    const psx_resolution_store_t *store, const node_t *node) {
  return psx_resolved_object_ref_node_kind(store, node);
}

static int is_aggregate_lvar(
    const psx_resolution_store_t *store, node_t *node) {
  return node && resolved_node_kind(store, node) == ND_LVAR &&
         ps_type_is_tag_aggregate(ps_node_get_type(store, node));
}

static int is_dereference(
    const psx_resolution_store_t *store, const node_t *node) {
  return node &&
         (node->kind == ND_UNARY_DEREF ||
          psx_resolution_node_kind(store, node) == ND_DEREF);
}

static node_t *assigned_aggregate_lvar_from_member_base(
    const psx_resolution_store_t *store, node_t *base);

static node_t *assigned_aggregate_lvar_from_member_address(
    const psx_resolution_store_t *store, node_t *address) {
  if (!address) return NULL;
  if (address->kind == ND_COMMA && address->rhs)
    return assigned_aggregate_lvar_from_member_address(store, address->rhs);
  if ((address->kind == ND_ADD || address->kind == ND_SUB) && address->lhs)
    return assigned_aggregate_lvar_from_member_address(store, address->lhs);
  if (resolved_node_kind(store, address) == ND_ADDR && address->lhs)
    return assigned_aggregate_lvar_from_member_base(store, address->lhs);
  return NULL;
}

static node_t *assigned_aggregate_lvar_from_member_base(
    const psx_resolution_store_t *store, node_t *base) {
  if (!base) return NULL;
  if (is_aggregate_lvar(store, base)) return base;
  if (base->kind == ND_MEMBER_ACCESS) {
    node_member_access_t *access = (node_member_access_t *)base;
    return access->from_pointer
               ? NULL
               : assigned_aggregate_lvar_from_member_base(store, base->lhs);
  }
  if (base->kind == ND_COMMA && base->rhs)
    return assigned_aggregate_lvar_from_member_base(store, base->rhs);
  if (is_dereference(store, base) && base->lhs)
    return assigned_aggregate_lvar_from_member_address(store, base->lhs);
  return NULL;
}

static node_t *assigned_lvar_from_target(
    const psx_resolution_store_t *store, node_t *target) {
  if (!target) return NULL;
  if (resolved_node_kind(store, target) == ND_LVAR) return target;
  if (target->kind == ND_MEMBER_ACCESS) {
    node_member_access_t *access = (node_member_access_t *)target;
    return access->from_pointer
               ? NULL
               : assigned_aggregate_lvar_from_member_base(
                     store, target->lhs);
  }
  if (is_dereference(store, target) && target->lhs &&
      (target->lhs->kind == ND_ADDRESS_OF ||
       resolved_node_kind(store, target->lhs) == ND_ADDR) &&
      target->lhs->lhs &&
      resolved_node_kind(store, target->lhs->lhs) == ND_LVAR)
    return target->lhs->lhs;
  if (is_dereference(store, target))
    return assigned_aggregate_lvar_from_member_address(store, target->lhs);
  return NULL;
}

static void record_initialized(
    const psx_resolution_store_t *store,
    psx_local_registry_t *local_registry,
    node_t *target, psx_lvar_usage_region_t *region) {
  lvar_t *var = ps_node_lvar_symbol(
      store, assigned_lvar_from_target(store, target));
  if (var)
    ps_decl_record_lvar_usage_in_region_in(
        local_registry, var, PSX_LVAR_USAGE_INITIALIZED, region);
}

static void record_address_taken(
    const psx_resolution_store_t *store,
    psx_local_registry_t *local_registry,
    node_t *operand, psx_lvar_usage_region_t *region) {
  if (!operand) return;
  if (operand->kind == ND_COMMA && operand->rhs) {
    record_address_taken(store, local_registry, operand->rhs, region);
    return;
  }
  if (resolved_node_kind(store, operand) == ND_LVAR) {
    lvar_t *var = ps_node_lvar_symbol(store, operand);
    if (var)
      ps_decl_record_lvar_usage_in_region_in(
          local_registry, var, PSX_LVAR_USAGE_ADDRESS_TAKEN, region);
    return;
  }
  if (operand->kind == ND_MEMBER_ACCESS) {
    node_member_access_t *access = (node_member_access_t *)operand;
    if (!access->from_pointer) {
      node_t *lvar =
          assigned_aggregate_lvar_from_member_base(store, operand->lhs);
      lvar_t *var = ps_node_lvar_symbol(store, lvar);
      if (var)
        ps_decl_record_lvar_usage_in_region_in(
            local_registry, var, PSX_LVAR_USAGE_ADDRESS_TAKEN, region);
    }
    return;
  }
  if ((operand->kind == ND_ADDRESS_OF ||
       resolved_node_kind(store, operand) == ND_ADDR) &&
      operand->lhs) {
    if (resolved_node_kind(store, operand->lhs) == ND_LVAR) {
      lvar_t *var = ps_node_lvar_symbol(store, operand->lhs);
      if (var)
        ps_decl_record_lvar_usage_in_region_in(
            local_registry, var, PSX_LVAR_USAGE_ADDRESS_TAKEN, region);
      return;
    }
    record_address_taken(store, local_registry, operand->lhs, region);
    return;
  }
  if (is_dereference(store, operand)) {
    node_t *lvar =
        assigned_aggregate_lvar_from_member_address(store, operand->lhs);
    lvar_t *var = ps_node_lvar_symbol(store, lvar);
    if (var)
      ps_decl_record_lvar_usage_in_region_in(
          local_registry, var, PSX_LVAR_USAGE_ADDRESS_TAKEN, region);
  }
}

static void collect_array(
    const psx_resolution_store_t *store,
    psx_local_registry_t *local_registry,
    node_t **nodes, psx_lvar_usage_region_t *region) {
  if (!nodes) return;
  for (int i = 0; nodes[i]; i++)
    psx_collect_lvar_usage_events_in(
        store, local_registry, nodes[i], region);
}

static void collect_sizeof_vla_indices(
    const psx_resolution_store_t *store,
    psx_local_registry_t *local_registry,
    node_t *operand, psx_lvar_usage_region_t *region) {
  if (!operand || operand->kind != ND_SUBSCRIPT) return;
  collect_sizeof_vla_indices(
      store, local_registry, operand->lhs, region);
  psx_collect_lvar_usage_events_in(
      store, local_registry, operand->rhs, region);
}

void psx_collect_lvar_usage_events_in(
    const psx_resolution_store_t *store,
    psx_local_registry_t *local_registry,
    node_t *node, psx_lvar_usage_region_t *inherited_region) {
  if (!local_registry || !node) return;
  psx_lvar_usage_region_t *region =
      ps_node_lvar_usage_region(store, node)
          ? ps_node_lvar_usage_region(store, node) : inherited_region;
  lvar_t *usage_lvar = ps_node_lvar_usage_symbol(store, node);
  if (ps_node_records_lvar_usage(store, node) && usage_lvar) {
    ps_decl_record_lvar_usage_in_region_in(
        local_registry, usage_lvar,
        ps_node_lvar_usage_is_unevaluated(store, node)
            ? PSX_LVAR_USAGE_UNEVALUATED
            : PSX_LVAR_USAGE_EVALUATED,
        region);
  }
  switch (psx_resolution_node_kind(store, node)) {
    case ND_COMPOUND_LITERAL: {
      psx_collect_lvar_usage_events_in(
          store, local_registry, node->rhs, region);
      return;
    }
    case ND_INIT_LIST: {
      node_init_list_t *list = (node_init_list_t *)node;
      for (int i = 0; i < list->entry_count; i++) {
        psx_collect_lvar_usage_events_in(
            store, local_registry, list->entries[i].value, region);
        for (int d = 0; d < list->entries[i].index_expr_count; d++) {
          psx_collect_lvar_usage_events_in(
              store, local_registry,
              list->entries[i].index_exprs[d], region);
        }
      }
      return;
    }
    case ND_ASSIGN:
      record_initialized(store, local_registry, node->lhs, region);
      psx_collect_lvar_usage_events_in(
          store, local_registry, node->lhs, region);
      psx_collect_lvar_usage_events_in(
          store, local_registry, node->rhs, region);
      return;
    case ND_ADDRESS_OF:
      psx_collect_lvar_usage_events_in(
          store, local_registry, node->lhs, region);
      record_address_taken(store, local_registry, node, region);
      return;
    case ND_ADDR:
      psx_collect_lvar_usage_events_in(
          store, local_registry, node->lhs, region);
      return;
    case ND_BLOCK:
      collect_array(
          store, local_registry, ((node_block_t *)node)->body, region);
      return;
    case ND_STATIC_ASSERT:
      return;
    case ND_VLA_ALLOC:
      return;
    case ND_FUNCDEF: {
      node_function_definition_t *function =
          (node_function_definition_t *)node;
      for (int i = 0; i < function->parameter_count; i++)
        psx_collect_lvar_usage_events_in(
            store, local_registry, function->parameters[i], region);
      psx_collect_lvar_usage_events_in(
          store, local_registry, node->rhs, region);
      return;
    }
    case ND_FUNCALL: {
      node_function_call_t *call = (node_function_call_t *)node;
      psx_collect_lvar_usage_events_in(
          store, local_registry, call->callee, region);
      for (int i = 0; i < call->argument_count; i++)
        psx_collect_lvar_usage_events_in(
            store, local_registry, call->arguments[i], region);
      return;
    }
    case ND_GENERIC_SELECTION: {
      node_generic_selection_t *selection =
          (node_generic_selection_t *)node;
      psx_collect_lvar_usage_events_in(
          store, local_registry,
          psx_generic_selection_selected_expression(store, selection),
          region);
      return;
    }
    case ND_CAST:
      psx_collect_lvar_usage_events_in(
          store, local_registry, node->lhs, region);
      return;
    case ND_SIZEOF_QUERY: {
      node_sizeof_query_t *query = (node_sizeof_query_t *)node;
      const psx_sizeof_runtime_plan_t *plan =
          psx_sizeof_query_runtime_plan_const(store, query);
      for (int i = 0; plan && i < plan->runtime_bound_count; i++) {
        psx_collect_lvar_usage_events_in(
            store, local_registry, plan->runtime_bounds[i], region);
      }
      if (psx_sizeof_query_evaluates_vla_operand(store, query)) {
        collect_sizeof_vla_indices(
            store, local_registry, query->operand, region);
      }
      return;
    }
    case ND_IF:
    case ND_FOR:
    case ND_TERNARY: {
      node_ctrl_t *control = (node_ctrl_t *)node;
      psx_collect_lvar_usage_events_in(
          store, local_registry, control->init, region);
      psx_collect_lvar_usage_events_in(
          store, local_registry, node->lhs, region);
      psx_collect_lvar_usage_events_in(
          store, local_registry, node->rhs, region);
      psx_collect_lvar_usage_events_in(
          store, local_registry, control->inc, region);
      psx_collect_lvar_usage_events_in(
          store, local_registry, control->els, region);
      return;
    }
    default:
      psx_collect_lvar_usage_events_in(
          store, local_registry, node->lhs, region);
      psx_collect_lvar_usage_events_in(
          store, local_registry, node->rhs, region);
      return;
  }
}

static void record_preinitialized_locals(
    psx_local_registry_t *local_registry,
    node_function_definition_t *function) {
  if (!function) return;
  for (lvar_t *var = function->lvars; var; var = ps_lvar_next_all(var)) {
    psx_lvar_registry_view_t view = ps_lvar_registry_view(var);
    if (view.is_param) {
      ps_decl_record_lvar_usage_in_region_in(
          local_registry, var, PSX_LVAR_USAGE_INITIALIZED, NULL);
    } else if (view.is_static_local) {
      ps_decl_record_lvar_usage_in_region_in(
          local_registry, var,
          PSX_LVAR_USAGE_INITIALIZED, view.decl_region);
    }
  }
}

static void emit_usage_warnings(
    ag_diagnostic_context_t *diagnostics,
    node_function_definition_t *function,
    const token_t *fallback) {
  if (!function) return;
  for (lvar_t *var = function->lvars; var; var = ps_lvar_next_all(var)) {
    psx_lvar_registry_view_t view = ps_lvar_registry_view(var);
    if (view.suppress_unreachable_warnings) continue;
    if (!view.is_used && !view.is_unevaluated_used &&
        !view.is_address_taken && !view.is_param &&
        view.name && view.name[0] != '_') {
      diag_warn_tokf_in(
          diagnostics, DIAG_WARN_PARSER_UNUSED_VARIABLE, fallback,
          diag_warn_message_for_in(
              diagnostics, DIAG_WARN_PARSER_UNUSED_VARIABLE),
          view.name_len, view.name);
    } else if (view.is_used && !view.is_initialized &&
               !view.is_param && !view.is_array) {
      diag_warn_tokf_in(
          diagnostics, DIAG_WARN_PARSER_UNINITIALIZED_VARIABLE, fallback,
          diag_warn_message_for_in(
              diagnostics, DIAG_WARN_PARSER_UNINITIALIZED_VARIABLE),
          view.name_len, view.name);
    }
  }
}

void psx_analyze_function_lvar_usage_in(
    const psx_resolution_store_t *store,
    ag_diagnostic_context_t *diagnostics,
    psx_local_registry_t *local_registry,
    node_function_definition_t *function,
    const token_t *fallback_diag_tok) {
  if (!local_registry || !function) return;
  psx_collect_lvar_usage_events_in(
      store, local_registry, (node_t *)function, NULL);
  record_preinitialized_locals(local_registry, function);
  ps_decl_replay_lvar_usage_events_in(
      local_registry, function->lvars);
  emit_usage_warnings(diagnostics, function, fallback_diag_tok);
}
