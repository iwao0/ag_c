#include "lvar_usage_analysis.h"

#include "../diag/diag.h"
#include "../parser/lvar_public.h"
#include "../parser/node_utils.h"

static int is_aggregate_lvar(node_t *node) {
  return node && node->kind == ND_LVAR &&
         ps_node_aggregate_value_size(node) > 0;
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
  if (base->kind == ND_COMMA && base->rhs)
    return assigned_aggregate_lvar_from_member_base(base->rhs);
  if (base->kind == ND_DEREF && base->lhs)
    return assigned_aggregate_lvar_from_member_address(base->lhs);
  return NULL;
}

static node_t *assigned_lvar_from_target(node_t *target) {
  if (!target) return NULL;
  if (target->kind == ND_LVAR) return target;
  if (target->kind == ND_DEREF && target->lhs &&
      target->lhs->kind == ND_ADDR && target->lhs->lhs &&
      target->lhs->lhs->kind == ND_LVAR)
    return target->lhs->lhs;
  if (target->kind == ND_DEREF)
    return assigned_aggregate_lvar_from_member_address(target->lhs);
  return NULL;
}

static void record_initialized(
    node_t *target, psx_lvar_usage_region_t *region) {
  lvar_t *var = ps_node_lvar_symbol(assigned_lvar_from_target(target));
  if (var)
    ps_decl_record_lvar_usage_in_region(
        var, PSX_LVAR_USAGE_INITIALIZED, region);
}

static void record_address_taken(
    node_t *operand, psx_lvar_usage_region_t *region) {
  if (!operand) return;
  if (operand->kind == ND_COMMA && operand->rhs) {
    record_address_taken(operand->rhs, region);
    return;
  }
  if (operand->kind == ND_LVAR) {
    lvar_t *var = ps_node_lvar_symbol(operand);
    if (var)
      ps_decl_record_lvar_usage_in_region(
          var, PSX_LVAR_USAGE_ADDRESS_TAKEN, region);
    return;
  }
  if (operand->kind == ND_ADDR && operand->lhs) {
    if (operand->lhs->kind == ND_LVAR) {
      lvar_t *var = ps_node_lvar_symbol(operand->lhs);
      if (var)
        ps_decl_record_lvar_usage_in_region(
            var, PSX_LVAR_USAGE_ADDRESS_TAKEN, region);
      return;
    }
    record_address_taken(operand->lhs, region);
    return;
  }
  if (operand->kind == ND_DEREF) {
    node_t *lvar =
        assigned_aggregate_lvar_from_member_address(operand->lhs);
    lvar_t *var = ps_node_lvar_symbol(lvar);
    if (var)
      ps_decl_record_lvar_usage_in_region(
          var, PSX_LVAR_USAGE_ADDRESS_TAKEN, region);
  }
}

static void collect_array(
    node_t **nodes, psx_lvar_usage_region_t *region) {
  if (!nodes) return;
  for (int i = 0; nodes[i]; i++)
    psx_collect_lvar_usage_events(nodes[i], region);
}

void psx_collect_lvar_usage_events(
    node_t *node, psx_lvar_usage_region_t *inherited_region) {
  if (!node) return;
  psx_lvar_usage_region_t *region =
      node->usage_region ? node->usage_region : inherited_region;
  if (node->records_lvar_usage && node->usage_lvar) {
    ps_decl_record_lvar_usage_in_region(
        node->usage_lvar,
        node->lvar_usage_unevaluated
            ? PSX_LVAR_USAGE_UNEVALUATED
            : PSX_LVAR_USAGE_EVALUATED,
        region);
  }
  switch (node->kind) {
    case ND_ASSIGN:
      record_initialized(node->lhs, region);
      psx_collect_lvar_usage_events(node->lhs, region);
      psx_collect_lvar_usage_events(node->rhs, region);
      return;
    case ND_ADDR:
      psx_collect_lvar_usage_events(node->lhs, region);
      if (node->is_explicit_addr_expr)
        record_address_taken(node, region);
      return;
    case ND_BLOCK:
      collect_array(((node_block_t *)node)->body, region);
      return;
    case ND_FUNCDEF: {
      node_function_definition_t *function =
          (node_function_definition_t *)node;
      for (int i = 0; i < function->parameter_count; i++)
        psx_collect_lvar_usage_events(function->parameters[i], region);
      psx_collect_lvar_usage_events(node->rhs, region);
      return;
    }
    case ND_FUNCALL: {
      node_function_call_t *call = (node_function_call_t *)node;
      psx_collect_lvar_usage_events(call->callee, region);
      for (int i = 0; i < call->argument_count; i++)
        psx_collect_lvar_usage_events(call->arguments[i], region);
      return;
    }
    case ND_IF:
    case ND_FOR:
    case ND_TERNARY: {
      node_ctrl_t *control = (node_ctrl_t *)node;
      psx_collect_lvar_usage_events(control->init, region);
      psx_collect_lvar_usage_events(node->lhs, region);
      psx_collect_lvar_usage_events(node->rhs, region);
      psx_collect_lvar_usage_events(control->inc, region);
      psx_collect_lvar_usage_events(control->els, region);
      return;
    }
    default:
      psx_collect_lvar_usage_events(node->lhs, region);
      psx_collect_lvar_usage_events(node->rhs, region);
      return;
  }
}

static void record_preinitialized_locals(
    node_function_definition_t *function) {
  if (!function) return;
  for (lvar_t *var = function->lvars; var; var = ps_lvar_next_all(var)) {
    psx_lvar_registry_view_t view = ps_lvar_registry_view(var);
    if (view.is_param) {
      ps_decl_record_lvar_usage_in_region(
          var, PSX_LVAR_USAGE_INITIALIZED, NULL);
    } else if (view.is_static_local) {
      ps_decl_record_lvar_usage_in_region(
          var, PSX_LVAR_USAGE_INITIALIZED, view.decl_region);
    }
  }
}

static void emit_usage_warnings(
    node_function_definition_t *function,
    const token_t *fallback) {
  if (!function) return;
  for (lvar_t *var = function->lvars; var; var = ps_lvar_next_all(var)) {
    psx_lvar_registry_view_t view = ps_lvar_registry_view(var);
    if (view.suppress_unreachable_warnings) continue;
    if (!view.is_used && !view.is_unevaluated_used &&
        !view.is_address_taken && !view.is_param &&
        view.name && view.name[0] != '_') {
      diag_warn_tokf(
          DIAG_WARN_PARSER_UNUSED_VARIABLE, fallback,
          diag_warn_message_for(DIAG_WARN_PARSER_UNUSED_VARIABLE),
          view.name_len, view.name);
    } else if (view.is_used && !view.is_initialized &&
               !view.is_param && !view.is_array) {
      diag_warn_tokf(
          DIAG_WARN_PARSER_UNINITIALIZED_VARIABLE, fallback,
          diag_warn_message_for(DIAG_WARN_PARSER_UNINITIALIZED_VARIABLE),
          view.name_len, view.name);
    }
  }
}

void psx_analyze_function_lvar_usage(
    node_function_definition_t *function,
    const token_t *fallback_diag_tok) {
  if (!function) return;
  psx_collect_lvar_usage_events((node_t *)function, NULL);
  record_preinitialized_locals(function);
  ps_decl_replay_lvar_usage_events(function->lvars);
  emit_usage_warnings(function, fallback_diag_tok);
}
