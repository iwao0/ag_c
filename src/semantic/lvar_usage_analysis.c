#include "lvar_usage_analysis.h"

#include "../parser/node_resolution_state.h"
#include "../diag/diag.h"
#include "../parser/local_registry.h"
#include "../parser/lvar_public.h"
#include "../parser/node_utils.h"
#include "generic_selection_resolution.h"
#include "source_cast_resolution.h"

static int is_aggregate_lvar(node_t *node) {
  return node && node->kind == ND_LVAR &&
         ps_type_is_tag_aggregate(ps_node_get_type(node));
}

static int is_dereference(const node_t *node) {
  return node &&
         (node->kind == ND_UNARY_DEREF ||
          node->kind == ND_DEREF);
}

static node_t *assigned_aggregate_lvar_from_member_base(node_t *base);

static node_t *assigned_aggregate_lvar_from_member_address(node_t *address) {
  if (!address) return NULL;
  if (address->kind == ND_COMMA && address->rhs)
    return assigned_aggregate_lvar_from_member_address(address->rhs);
  if ((address->kind == ND_ADD || address->kind == ND_SUB) && address->lhs)
    return assigned_aggregate_lvar_from_member_address(address->lhs);
  if (address->kind == ND_ADDR && address->lhs)
    return assigned_aggregate_lvar_from_member_base(address->lhs);
  return NULL;
}

static node_t *assigned_aggregate_lvar_from_member_base(node_t *base) {
  if (!base) return NULL;
  if (is_aggregate_lvar(base)) return base;
  if (base->kind == ND_MEMBER_ACCESS) {
    node_member_access_t *access = (node_member_access_t *)base;
    return access->from_pointer
               ? NULL
               : assigned_aggregate_lvar_from_member_base(base->lhs);
  }
  if (base->kind == ND_COMMA && base->rhs)
    return assigned_aggregate_lvar_from_member_base(base->rhs);
  if (is_dereference(base) && base->lhs)
    return assigned_aggregate_lvar_from_member_address(base->lhs);
  return NULL;
}

static node_t *assigned_lvar_from_target(node_t *target) {
  if (!target) return NULL;
  if (target->kind == ND_LVAR) return target;
  if (target->kind == ND_MEMBER_ACCESS) {
    node_member_access_t *access = (node_member_access_t *)target;
    return access->from_pointer
               ? NULL
               : assigned_aggregate_lvar_from_member_base(target->lhs);
  }
  if (is_dereference(target) && target->lhs &&
      target->lhs->kind == ND_ADDR && target->lhs->lhs &&
      target->lhs->lhs->kind == ND_LVAR)
    return target->lhs->lhs;
  if (is_dereference(target))
    return assigned_aggregate_lvar_from_member_address(target->lhs);
  return NULL;
}

static void record_initialized(
    psx_local_registry_t *local_registry,
    node_t *target, psx_lvar_usage_region_t *region) {
  lvar_t *var = ps_node_lvar_symbol(assigned_lvar_from_target(target));
  if (var)
    ps_decl_record_lvar_usage_in_region_in(
        local_registry, var, PSX_LVAR_USAGE_INITIALIZED, region);
}

static void record_address_taken(
    psx_local_registry_t *local_registry,
    node_t *operand, psx_lvar_usage_region_t *region) {
  if (!operand) return;
  if (operand->kind == ND_COMMA && operand->rhs) {
    record_address_taken(local_registry, operand->rhs, region);
    return;
  }
  if (operand->kind == ND_LVAR) {
    lvar_t *var = ps_node_lvar_symbol(operand);
    if (var)
      ps_decl_record_lvar_usage_in_region_in(
          local_registry, var, PSX_LVAR_USAGE_ADDRESS_TAKEN, region);
    return;
  }
  if (operand->kind == ND_MEMBER_ACCESS) {
    node_member_access_t *access = (node_member_access_t *)operand;
    if (!access->from_pointer) {
      node_t *lvar =
          assigned_aggregate_lvar_from_member_base(operand->lhs);
      lvar_t *var = ps_node_lvar_symbol(lvar);
      if (var)
        ps_decl_record_lvar_usage_in_region_in(
            local_registry, var, PSX_LVAR_USAGE_ADDRESS_TAKEN, region);
    }
    return;
  }
  if (operand->kind == ND_ADDR && operand->lhs) {
    if (operand->lhs->kind == ND_LVAR) {
      lvar_t *var = ps_node_lvar_symbol(operand->lhs);
      if (var)
        ps_decl_record_lvar_usage_in_region_in(
            local_registry, var, PSX_LVAR_USAGE_ADDRESS_TAKEN, region);
      return;
    }
    record_address_taken(local_registry, operand->lhs, region);
    return;
  }
  if (is_dereference(operand)) {
    node_t *lvar =
        assigned_aggregate_lvar_from_member_address(operand->lhs);
    lvar_t *var = ps_node_lvar_symbol(lvar);
    if (var)
      ps_decl_record_lvar_usage_in_region_in(
          local_registry, var, PSX_LVAR_USAGE_ADDRESS_TAKEN, region);
  }
}

static void collect_array(
    psx_local_registry_t *local_registry,
    node_t **nodes, psx_lvar_usage_region_t *region) {
  if (!nodes) return;
  for (int i = 0; nodes[i]; i++)
    psx_collect_lvar_usage_events_in(local_registry, nodes[i], region);
}

static void collect_sizeof_vla_indices(
    psx_local_registry_t *local_registry,
    node_t *operand, psx_lvar_usage_region_t *region) {
  if (!operand || operand->kind != ND_SUBSCRIPT) return;
  collect_sizeof_vla_indices(
      local_registry, operand->lhs, region);
  psx_collect_lvar_usage_events_in(
      local_registry, operand->rhs, region);
}

void psx_collect_lvar_usage_events_in(
    psx_local_registry_t *local_registry,
    node_t *node, psx_lvar_usage_region_t *inherited_region) {
  if (!local_registry || !node) return;
  psx_lvar_usage_region_t *region =
      node->usage_region ? node->usage_region : inherited_region;
  if (node->records_lvar_usage && node->usage_lvar) {
    ps_decl_record_lvar_usage_in_region_in(
        local_registry, node->usage_lvar,
        node->lvar_usage_unevaluated
            ? PSX_LVAR_USAGE_UNEVALUATED
            : PSX_LVAR_USAGE_EVALUATED,
        region);
  }
  switch (node->kind) {
    case ND_COMPOUND_LITERAL: {
      psx_compound_literal_resolution_t *resolution =
          node->resolution_state
              ? &node->resolution_state->compound_literal : NULL;
      if (!resolution || !resolution->is_planned) {
        psx_collect_lvar_usage_events_in(
            local_registry, node->rhs, region);
        return;
      }
      psx_collect_lvar_usage_events_in(
          local_registry, resolution->runtime_initialization, region);
      psx_collect_lvar_usage_events_in(
          local_registry, resolution->direct_value, region);
      return;
    }
    case ND_ASSIGN:
      record_initialized(local_registry, node->lhs, region);
      psx_collect_lvar_usage_events_in(local_registry, node->lhs, region);
      psx_collect_lvar_usage_events_in(local_registry, node->rhs, region);
      return;
    case ND_ADDR:
      psx_collect_lvar_usage_events_in(local_registry, node->lhs, region);
      if (node->is_explicit_addr_expr)
        record_address_taken(local_registry, node, region);
      return;
    case ND_BLOCK:
      collect_array(local_registry, ((node_block_t *)node)->body, region);
      return;
    case ND_FUNCDEF: {
      node_function_definition_t *function =
          (node_function_definition_t *)node;
      for (int i = 0; i < function->parameter_count; i++)
        psx_collect_lvar_usage_events_in(
            local_registry, function->parameters[i], region);
      psx_collect_lvar_usage_events_in(local_registry, node->rhs, region);
      return;
    }
    case ND_FUNCALL: {
      node_function_call_t *call = (node_function_call_t *)node;
      psx_collect_lvar_usage_events_in(
          local_registry, call->callee, region);
      for (int i = 0; i < call->argument_count; i++)
        psx_collect_lvar_usage_events_in(
            local_registry, call->arguments[i], region);
      return;
    }
    case ND_GENERIC_SELECTION: {
      node_generic_selection_t *selection =
          (node_generic_selection_t *)node;
      psx_collect_lvar_usage_events_in(
          local_registry,
          psx_generic_selection_selected_expression(selection), region);
      return;
    }
    case ND_CAST:
      if (node->is_source_cast) {
        node_t *lowered = psx_source_cast_lowered_value(
            (node_source_cast_t *)node);
        if (lowered) {
          psx_collect_lvar_usage_events_in(
              local_registry, lowered, region);
          return;
        }
      }
      psx_collect_lvar_usage_events_in(
          local_registry, node->lhs, region);
      return;
    case ND_SIZEOF_QUERY: {
      node_sizeof_query_t *query = (node_sizeof_query_t *)node;
      psx_collect_lvar_usage_events_in(
          local_registry, query->runtime_size_expr, region);
      if (query->evaluates_vla_operand) {
        collect_sizeof_vla_indices(
            local_registry, query->operand, region);
      }
      return;
    }
    case ND_IF:
    case ND_FOR:
    case ND_TERNARY: {
      node_ctrl_t *control = (node_ctrl_t *)node;
      psx_collect_lvar_usage_events_in(
          local_registry, control->init, region);
      psx_collect_lvar_usage_events_in(local_registry, node->lhs, region);
      psx_collect_lvar_usage_events_in(local_registry, node->rhs, region);
      psx_collect_lvar_usage_events_in(
          local_registry, control->inc, region);
      psx_collect_lvar_usage_events_in(
          local_registry, control->els, region);
      return;
    }
    default:
      psx_collect_lvar_usage_events_in(local_registry, node->lhs, region);
      psx_collect_lvar_usage_events_in(local_registry, node->rhs, region);
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
    ag_diagnostic_context_t *diagnostics,
    psx_local_registry_t *local_registry,
    node_function_definition_t *function,
    const token_t *fallback_diag_tok) {
  if (!local_registry || !function) return;
  psx_collect_lvar_usage_events_in(
      local_registry, (node_t *)function, NULL);
  record_preinitialized_locals(local_registry, function);
  ps_decl_replay_lvar_usage_events_in(
      local_registry, function->lvars);
  emit_usage_warnings(diagnostics, function, fallback_diag_tok);
}
