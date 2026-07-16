#include "semantic_lowering_pass.h"

#include "assignment_lowering.h"
#include "cast_lowering.h"
#include "compound_literal_lowering.h"
#include "expr_lowering.h"
#include "initializer_lowering.h"
#include "member_access_lowering.h"
#include "runtime_context.h"
#include "subscript_lowering.h"
#include "complex_part_lowering.h"
#include "../parser/node_utils.h"

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

static node_t *lower_additive_expression_node(
    const psx_semantic_lowering_context_t *context, node_t *node) {
  if (!node ||
      (node->source_op != TK_PLUS && node->source_op != TK_MINUS) ||
      (node->kind != ND_ADD && node->kind != ND_SUB)) {
    return node;
  }
  token_t *source_tok = node->tok;
  node_t *lowered = lower_additive_expression(
      context->lowering_context, node->kind, node->lhs, node->rhs);
  if (!lowered || lowered == node) return node;
  if (!lowered->tok) lowered->tok = source_tok;
  return lowered;
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

static node_t *lower_source_cast_node(
    const psx_semantic_lowering_context_t *context,
    node_t *node, const token_t *fallback_diag_tok) {
  if (!node || node->kind != ND_CAST || !node->is_source_cast) return node;
  return lower_source_cast_expression(
      context->lowering_context, context->local_registry,
      node, (token_t *)fallback_diag_tok, context->options);
}

static node_t *lower_tree(
    const psx_semantic_lowering_context_t *context,
    node_t *node, const token_t *fallback_diag_tok) {
  if (!node) return NULL;
  switch (node->kind) {
    case ND_COMPOUND_LITERAL:
      node->rhs = lower_initializer(
          context, node->rhs, fallback_diag_tok);
      node = lower_compound_literal_expression_in_contexts(
          context->semantic_context, context->global_registry,
          context->local_registry, context->lowering_context,
          context->options,
          node, fallback_diag_tok);
      return node->kind == ND_COMPOUND_LITERAL
                 ? node
                 : lower_tree(
                       context, node, fallback_diag_tok);
    case ND_GENERIC_SELECTION: {
      node_generic_selection_t *selection =
          (node_generic_selection_t *)node;
      int selected = selection->selected_index;
      if (selected >= 0 && selected < selection->association_count) {
        selection->associations[selected].expression = lower_tree(
            context, selection->associations[selected].expression,
            fallback_diag_tok);
      }
      break;
    }
    case ND_SIZEOF_QUERY: {
      node_sizeof_query_t *query = (node_sizeof_query_t *)node;
      if (query->runtime_size_expr) {
        query->runtime_size_expr = lower_tree(
            context, query->runtime_size_expr, fallback_diag_tok);
      }
      if (query->evaluates_vla_operand) {
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
      return lower_subscript_expression(context->lowering_context, node);
    case ND_MEMBER_ACCESS:
      node->lhs = lower_tree(
          context, node->lhs, fallback_diag_tok);
      return lower_member_access_expression_in(
          context->lowering_context, context->local_registry,
          (node_member_access_t *)node, fallback_diag_tok);
    case ND_UNARY_DEREF:
      node->lhs = lower_tree(
          context, node->lhs, fallback_diag_tok);
      break;
    case ND_UNARY_NEGATE:
      node->lhs = lower_tree(
          context, node->lhs, fallback_diag_tok);
      break;
    case ND_CREAL:
    case ND_CIMAG: {
      node->lhs = lower_tree(
          context, node->lhs, fallback_diag_tok);
      node_t *lowered = lower_complex_part_expression(
          context->lowering_context, node);
      return lowered == node
                 ? node
                 : lower_tree(
                       context, lowered, fallback_diag_tok);
    }
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
      node = lower_source_cast_node(context, node, fallback_diag_tok);
      node = lower_aggregate_address_expression(
          context->lowering_context, node);
      node = lower_additive_expression_node(context, node);
      node = lower_compound_assignment_expression(
          context->lowering_context, context->local_registry, node);
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

static const psx_type_t *call_function_type(node_function_call_t *call) {
  if (!call) return NULL;
  return ps_type_callable_function(call->callee_type);
}

static void lower_call_arguments(
    psx_lowering_context_t *lowering_context,
    node_function_call_t *call, const token_t *fallback_diag_tok,
    const ag_compilation_options_t *options) {
  const psx_type_t *function = call_function_type(call);
  if (!call || !function || function->kind != PSX_TYPE_FUNCTION) return;
  int count = call->argument_count < function->param_count
                  ? call->argument_count : function->param_count;
  if (count > 16) count = 16;
  for (int i = 0; i < count; i++) {
    call->arguments[i] = lower_implicit_value_conversion(
        lowering_context, call->arguments[i], function->param_types[i],
        call->base.tok ? call->base.tok
                       : (token_t *)fallback_diag_tok,
        options);
  }
}

static void lower_assignment_conversion(
    psx_lowering_context_t *lowering_context,
    node_t *node, const token_t *fallback_diag_tok,
    const ag_compilation_options_t *options) {
  if (!node || node->kind != ND_ASSIGN || !node->lhs || !node->rhs)
    return;
  node->rhs = lower_implicit_value_conversion(
      lowering_context, node->rhs, ps_node_get_type(node->lhs),
      node->tok ? node->tok : (token_t *)fallback_diag_tok,
      options);
}

void psx_lower_implicit_conversions(
    psx_lowering_context_t *lowering_context,
    node_t *node, node_function_definition_t *current_func,
    const token_t *fallback_diag_tok,
    const ag_compilation_options_t *options) {
  if (!node) return;
  switch (node->kind) {
    case ND_BLOCK:
      for (node_t **body = ((node_block_t *)node)->body;
           body && *body; body++) {
        psx_lower_implicit_conversions(
            lowering_context, *body, current_func,
            fallback_diag_tok, options);
      }
      break;
    case ND_FUNCDEF: {
      node_function_definition_t *function =
          (node_function_definition_t *)node;
      for (int i = 0; i < function->parameter_count; i++)
        psx_lower_implicit_conversions(
            lowering_context, function->parameters[i], function,
            fallback_diag_tok, options);
      psx_lower_implicit_conversions(
          lowering_context, node->rhs, function,
          fallback_diag_tok, options);
      break;
    }
    case ND_FUNCALL: {
      node_function_call_t *call = (node_function_call_t *)node;
      psx_lower_implicit_conversions(
          lowering_context, call->callee, current_func,
          fallback_diag_tok, options);
      for (int i = 0; i < call->argument_count; i++)
        psx_lower_implicit_conversions(
            lowering_context, call->arguments[i], current_func,
            fallback_diag_tok, options);
      lower_call_arguments(
          lowering_context, call, fallback_diag_tok, options);
      break;
    }
    case ND_GENERIC_SELECTION: {
      node_generic_selection_t *selection =
          (node_generic_selection_t *)node;
      int selected = selection->selected_index;
      if (selected >= 0 && selected < selection->association_count) {
        psx_lower_implicit_conversions(
            lowering_context,
            selection->associations[selected].expression,
            current_func, fallback_diag_tok, options);
      }
      break;
    }
    case ND_SIZEOF_QUERY: {
      node_sizeof_query_t *query = (node_sizeof_query_t *)node;
      psx_lower_implicit_conversions(
          lowering_context, query->runtime_size_expr, current_func,
          fallback_diag_tok, options);
      if (query->evaluates_vla_operand) {
        node_t *operand = query->operand;
        while (operand && operand->kind == ND_SUBSCRIPT) {
          psx_lower_implicit_conversions(
              lowering_context, operand->rhs, current_func,
              fallback_diag_tok, options);
          operand = operand->lhs;
        }
      }
      break;
    }
    case ND_IF:
    case ND_FOR:
    case ND_TERNARY: {
      node_ctrl_t *control = (node_ctrl_t *)node;
      psx_lower_implicit_conversions(
          lowering_context, control->init, current_func,
          fallback_diag_tok, options);
      psx_lower_implicit_conversions(
          lowering_context, node->lhs, current_func,
          fallback_diag_tok, options);
      psx_lower_implicit_conversions(
          lowering_context, node->rhs, current_func,
          fallback_diag_tok, options);
      psx_lower_implicit_conversions(
          lowering_context, control->inc, current_func,
          fallback_diag_tok, options);
      psx_lower_implicit_conversions(
          lowering_context, control->els, current_func,
          fallback_diag_tok, options);
      break;
    }
    case ND_RETURN:
      psx_lower_implicit_conversions(
          lowering_context, node->lhs, current_func,
          fallback_diag_tok, options);
      if (current_func && node->lhs) {
        node->lhs = lower_implicit_value_conversion(
            lowering_context, node->lhs,
            ps_function_definition_return_type(current_func),
            node->tok ? node->tok : (token_t *)fallback_diag_tok,
            options);
      }
      break;
    default:
      psx_lower_implicit_conversions(
          lowering_context, node->lhs, current_func,
          fallback_diag_tok, options);
      psx_lower_implicit_conversions(
          lowering_context, node->rhs, current_func,
          fallback_diag_tok, options);
      lower_assignment_conversion(
          lowering_context, node, fallback_diag_tok, options);
      break;
  }
}
