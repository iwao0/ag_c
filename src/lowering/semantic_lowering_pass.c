#include "semantic_lowering_pass.h"

#include "cast_lowering.h"
#include "compound_literal_lowering.h"
#include "initializer_lowering.h"
#include "runtime_initializer_plan.h"
#include "runtime_context.h"
#include "../semantic/resolution_state.h"
#include "../parser/node_utils.h"
#include "../parser/semantic_ctx.h"
#include "../semantic/generic_selection_resolution.h"
#include "../semantic/resolved_function.h"
#include "../semantic/resolved_node.h"
#include "../semantic/resolved_node_kind.h"
#include "../semantic/resolved_object_ref.h"
#include "../semantic/sizeof_query_resolution.h"
#include "../semantic/source_cast_resolution.h"
#include "../semantic/vla_runtime_plan.h"

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_global_registry_t *global_registry;
  psx_local_registry_t *local_registry;
  psx_lowering_context_t *lowering_context;
  const ag_compilation_options_t *options;
} psx_semantic_lowering_context_t;

static node_t *lower_tree(
    const psx_semantic_lowering_context_t *context,
    node_t *node, const token_t *fallback_diag_tok);

static void lower_node_array(
    const psx_semantic_lowering_context_t *context,
    node_t **nodes, const token_t *fallback_diag_tok) {
  if (!nodes) return;
  for (int i = 0; nodes[i]; i++)
    nodes[i] = lower_tree(
        context, nodes[i], fallback_diag_tok);
}

static node_t *lower_initializer(
    const psx_semantic_lowering_context_t *context,
    node_t *syntax, const token_t *fallback_diag_tok) {
  if (!syntax) return NULL;
  if (syntax->kind != ND_INIT_LIST) {
    return lower_tree(
        context, syntax, fallback_diag_tok);
  }
  node_init_list_t *list = (node_init_list_t *)syntax;
  for (int i = 0; i < list->entry_count; i++) {
    psx_initializer_entry_t *entry = &list->entries[i];
    if (entry->designator_count > 0) {
      for (int d = 0; d < entry->designator_count; d++) {
        psx_initializer_designator_t *designator =
            &entry->designators[d];
        if (designator->kind != PSX_INIT_DESIGNATOR_INDEX) continue;
        designator->index_expr = lower_tree(
            context, designator->index_expr, fallback_diag_tok);
        designator->range_end_expr = lower_tree(
            context, designator->range_end_expr, fallback_diag_tok);
      }
    } else {
      for (int d = 0; d < entry->index_expr_count; d++)
        entry->index_exprs[d] = lower_tree(
            context, entry->index_exprs[d], fallback_diag_tok);
    }
    entry->value = lower_initializer(
        context, entry->value, fallback_diag_tok);
  }
  return syntax;
}

static void lower_sizeof_vla_indices(
    const psx_semantic_lowering_context_t *context,
    node_t *operand, const token_t *fallback_diag_tok) {
  if (!operand || operand->kind != ND_SUBSCRIPT) return;
  lower_sizeof_vla_indices(
      context, operand->lhs, fallback_diag_tok);
  operand->rhs = lower_tree(
      context, operand->rhs, fallback_diag_tok);
}

static void lower_source_cast_node(
    const psx_semantic_lowering_context_t *context,
    node_t *node, const token_t *fallback_diag_tok) {
  psx_resolution_store_t *store =
      ps_lowering_resolution_store(context->lowering_context);
  psx_node_resolution_state_t *state =
      ps_node_resolution_state(store, node);
  if (!node || node->kind != ND_CAST || !node->is_source_cast ||
      !state)
    return;
  psx_source_cast_resolution_t *resolution =
      &state->source_cast;
  if (resolution->kind != PSX_SOURCE_CAST_UNRESOLVED) return;
  token_t *diag_tok = node->tok
                          ? node->tok
                          : (token_t *)fallback_diag_tok;
  psx_source_cast_types_resolution_t types_resolution;
  if (!psx_validate_source_cast_qual_types(
          context->lowering_context,
          ps_node_qual_type(store, node),
          ps_node_qual_type(store, node->lhs),
          diag_tok, context->options, &types_resolution))
    return;
  if (!types_resolution.target_is_aggregate) {
    resolution->kind = PSX_SOURCE_CAST_DIRECT_HIR;
    return;
  }
  psx_aggregate_source_cast_plan_t plan;
  if (!psx_plan_validated_aggregate_source_cast(
          context->lowering_context, context->local_registry,
          &types_resolution, diag_tok, &plan))
    return;
  resolution->aggregate_temporary = plan.temporary;
  resolution->aggregate_member_qual_type = plan.member_qual_type;
  resolution->aggregate_member_offset = plan.member_offset;
  resolution->aggregate_member_bit_width = plan.member_bit_width;
  resolution->aggregate_member_bit_offset = plan.member_bit_offset;
  resolution->aggregate_member_bit_is_signed =
      plan.member_bit_is_signed;
  resolution->kind = plan.temporary
                         ? PSX_SOURCE_CAST_AGGREGATE_TEMPORARY
                         : PSX_SOURCE_CAST_AGGREGATE_DIRECT_HIR;
}

static node_t *lower_tree(
    const psx_semantic_lowering_context_t *context,
    node_t *node, const token_t *fallback_diag_tok) {
  if (!node) return NULL;
  psx_resolution_store_t *store =
      ps_lowering_resolution_store(context->lowering_context);
  switch (psx_resolved_object_ref_node_kind(store, node)) {
    case ND_STATIC_ASSERT: {
      node_static_assert_t *assertion =
          (node_static_assert_t *)node;
      assertion->condition = lower_tree(
          context, assertion->condition, fallback_diag_tok);
      return node;
    }
    case ND_VLA_ALLOC:
      return node;
    case ND_COMPOUND_LITERAL: {
      node_compound_literal_t *compound =
          (node_compound_literal_t *)node;
      psx_node_resolution_state_t *state =
          ps_node_resolution_state(store, node);
      psx_compound_literal_resolution_t *resolution =
          state ? &state->compound_literal : NULL;
      if (resolution &&
          resolution->kind != PSX_COMPOUND_LITERAL_UNPLANNED)
        return node;
      node->rhs = lower_initializer(
          context, node->rhs, fallback_diag_tok);
      psx_compound_literal_storage_plan_t plan;
      if (!psx_plan_compound_literal_storage_in_contexts(
          context->semantic_context, context->global_registry,
          context->local_registry, context->lowering_context,
          context->options, compound, fallback_diag_tok, &plan) ||
          !resolution) {
        return node;
      }
      psx_runtime_initializer_plan_t *runtime_initializer = NULL;
      if (plan.initialization_tree) {
        plan.initialization_tree = lower_tree(
            context, plan.initialization_tree, fallback_diag_tok);
        runtime_initializer = psx_build_runtime_initializer_plan(
            context->lowering_context, plan.initialization_tree);
        if (!runtime_initializer) return node;
      }
      resolution->local_object = plan.local_object;
      resolution->global_object = plan.global_object;
      resolution->runtime_initializer = runtime_initializer;
      resolution->direct_initializer_index =
          plan.direct_initializer_index;
      resolution->kind = plan.kind;
      const psx_type_t *result_type = plan.object_type;
      if (result_type) {
        psx_qual_type_t result_qual_type =
            ps_ctx_intern_qual_type_in(
                context->semantic_context, result_type);
        const psx_type_t *canonical_result =
            ps_ctx_type_by_id_in(
                context->semantic_context,
                result_qual_type.type_id);
        if (canonical_result) {
          ps_node_bind_qual_type(
              store, node, canonical_result, result_qual_type);
        }
      }
      return node;
    }
    case ND_GENERIC_SELECTION: {
      node_generic_selection_t *selection =
          (node_generic_selection_t *)node;
      node_t *selected =
          psx_generic_selection_selected_expression(store, selection);
      if (selected)
        lower_tree(context, selected, fallback_diag_tok);
      break;
    }
    case ND_SIZEOF_QUERY: {
      node_sizeof_query_t *query = (node_sizeof_query_t *)node;
      psx_sizeof_runtime_plan_t *plan =
          psx_sizeof_query_runtime_plan(store, query);
      for (int i = 0; plan && i < plan->runtime_bound_count; i++) {
        plan->runtime_bounds[i] = lower_tree(
            context, plan->runtime_bounds[i], fallback_diag_tok);
      }
      if (psx_sizeof_query_evaluates_vla_operand(store, query)) {
        lower_sizeof_vla_indices(
            context, query->operand, fallback_diag_tok);
      }
      break;
    }
    case ND_ALIGNOF_QUERY:
      break;
    case ND_DECL_INIT: {
      node_decl_init_t *init = (node_decl_init_t *)node;
      node->lhs = lower_tree(
          context, node->lhs, fallback_diag_tok);
      if (init->init_kind == PSX_DECL_INIT_LIST) {
        node->rhs = lower_initializer(
            context, node->rhs, fallback_diag_tok);
      } else {
        node->rhs = lower_tree(
            context, node->rhs, fallback_diag_tok);
      }
      node = lower_decl_initializer(
          context->lowering_context, node, context->options);
      break;
    }
    case ND_BLOCK:
      lower_node_array(
          context, ((node_block_t *)node)->body, fallback_diag_tok);
      break;
    case ND_FUNCDEF: {
      node_function_definition_t *function =
          (node_function_definition_t *)node;
      for (int i = 0; i < function->parameter_count; i++)
        function->parameters[i] = lower_tree(
            context, function->parameters[i], fallback_diag_tok);
      node->rhs = lower_tree(
          context, node->rhs, fallback_diag_tok);
      break;
    }
    case ND_FUNCALL: {
      node_function_call_t *call = (node_function_call_t *)node;
      call->callee = lower_tree(
          context, call->callee, fallback_diag_tok);
      for (int i = 0; i < call->argument_count; i++)
        call->arguments[i] = lower_tree(
            context, call->arguments[i], fallback_diag_tok);
      break;
    }
    case ND_SUBSCRIPT:
      node->lhs = lower_tree(
          context, node->lhs, fallback_diag_tok);
      node->rhs = lower_tree(
          context, node->rhs, fallback_diag_tok);
      break;
    case ND_MEMBER_ACCESS:
      node->lhs = lower_tree(
          context, node->lhs, fallback_diag_tok);
      break;
    case ND_UNARY_DEREF:
      node->lhs = lower_tree(
          context, node->lhs, fallback_diag_tok);
      break;
    case ND_UNARY_NEGATE:
    case ND_LOGICAL_NOT:
      node->lhs = lower_tree(
          context, node->lhs, fallback_diag_tok);
      break;
    case ND_ADDR:
      if (node->is_explicit_addr_expr && node->lhs &&
          node->lhs->kind == ND_COMPOUND_LITERAL) {
        psx_compound_literal_require_addressable_storage(
            store, node->lhs);
      }
      node->lhs = lower_tree(
          context, node->lhs, fallback_diag_tok);
      break;
    case ND_CREAL:
    case ND_CIMAG:
      node->lhs = lower_tree(
          context, node->lhs, fallback_diag_tok);
      break;
    case ND_IF:
    case ND_FOR:
    case ND_TERNARY: {
      node_ctrl_t *control = (node_ctrl_t *)node;
      control->init = lower_tree(
          context, control->init, fallback_diag_tok);
      node->lhs = lower_tree(
          context, node->lhs, fallback_diag_tok);
      node->rhs = lower_tree(
          context, node->rhs, fallback_diag_tok);
      control->inc = lower_tree(
          context, control->inc, fallback_diag_tok);
      control->els = lower_tree(
          context, control->els, fallback_diag_tok);
      break;
    }
    default:
      node->lhs = lower_tree(
          context, node->lhs, fallback_diag_tok);
      node->rhs = lower_tree(
          context, node->rhs, fallback_diag_tok);
      lower_source_cast_node(context, node, fallback_diag_tok);
      break;
  }
  return node;
}

node_t *psx_lower_semantic_tree_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    node_t *node, const token_t *fallback_diag_tok) {
  if (!semantic_context || !global_registry || !local_registry ||
      !lowering_context || !options)
    return node;
  const psx_semantic_lowering_context_t context = {
      .semantic_context = semantic_context,
      .global_registry = global_registry,
      .local_registry = local_registry,
      .lowering_context = lowering_context,
      .options = options,
  };
  return lower_tree(&context, node, fallback_diag_tok);
}

node_t *psx_lower_semantic_initializer_syntax_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    node_t *syntax, const token_t *fallback_diag_tok) {
  if (!semantic_context || !global_registry || !local_registry ||
      !lowering_context || !options)
    return syntax;
  const psx_semantic_lowering_context_t context = {
      .semantic_context = semantic_context,
      .global_registry = global_registry,
      .local_registry = local_registry,
      .lowering_context = lowering_context,
      .options = options,
  };
  return lower_initializer(&context, syntax, fallback_diag_tok);
}
