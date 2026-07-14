#include "semantic_lowering_pass.h"

#include "assignment_lowering.h"
#include "alignof_lowering.h"
#include "cast_lowering.h"
#include "compound_literal_lowering.h"
#include "expr_lowering.h"
#include "generic_selection_lowering.h"
#include "initializer_lowering.h"
#include "member_access_lowering.h"
#include "sizeof_lowering.h"
#include "subscript_lowering.h"
#include "unary_deref_lowering.h"
#include "unary_operator_lowering.h"
#include "../parser/node_utils.h"

#include "../parser/semantic_ctx.h"

static void lower_node_array(
    node_t **nodes, const token_t *fallback_diag_tok) {
  if (!nodes) return;
  for (int i = 0; nodes[i]; i++)
    nodes[i] = psx_lower_semantic_tree(nodes[i], fallback_diag_tok);
}

node_t *psx_lower_semantic_initializer_syntax(
    node_t *syntax, const token_t *fallback_diag_tok) {
  if (!syntax) return NULL;
  if (syntax->kind != ND_INIT_LIST) {
    return psx_lower_semantic_tree(syntax, fallback_diag_tok);
  }
  node_init_list_t *list = (node_init_list_t *)syntax;
  for (int i = 0; i < list->entry_count; i++) {
    psx_initializer_entry_t *entry = &list->entries[i];
    if (entry->designator_count > 0) {
      for (int d = 0; d < entry->designator_count; d++) {
        psx_initializer_designator_t *designator =
            &entry->designators[d];
        if (designator->kind != PSX_INIT_DESIGNATOR_INDEX) continue;
        designator->index_expr = psx_lower_semantic_tree(
            designator->index_expr, fallback_diag_tok);
        designator->range_end_expr = psx_lower_semantic_tree(
            designator->range_end_expr, fallback_diag_tok);
      }
    } else {
      for (int d = 0; d < entry->index_expr_count; d++)
        entry->index_exprs[d] = psx_lower_semantic_tree(
            entry->index_exprs[d], fallback_diag_tok);
    }
    entry->value = psx_lower_semantic_initializer_syntax(
        entry->value, fallback_diag_tok);
  }
  return syntax;
}

static node_t *lower_additive_expression_node(node_t *node) {
  if (!node ||
      (node->source_op != TK_PLUS && node->source_op != TK_MINUS) ||
      (node->kind != ND_ADD && node->kind != ND_SUB)) {
    return node;
  }
  token_t *source_tok = node->tok;
  node_t *lowered = lower_additive_expression(
      node->kind, node->lhs, node->rhs);
  if (!lowered || lowered == node) return node;
  if (!lowered->tok) lowered->tok = source_tok;
  return lowered;
}

static node_t *lower_sizeof_vla_indices(
    node_t *operand, const token_t *fallback_diag_tok) {
  if (!operand || operand->kind != ND_SUBSCRIPT) return NULL;
  node_t *prefix = lower_sizeof_vla_indices(
      operand->lhs, fallback_diag_tok);
  node_t *index = psx_lower_semantic_tree(
      operand->rhs, fallback_diag_tok);
  return prefix ? ps_node_new_binary(ND_COMMA, prefix, index) : index;
}

static node_t *lower_source_cast_node(
    node_t *node, const token_t *fallback_diag_tok) {
  if (!node || node->kind != ND_CAST || !node->is_source_cast) return node;
  return lower_source_cast_expression(
      node, (token_t *)fallback_diag_tok);
}

node_t *psx_lower_semantic_tree(
    node_t *node, const token_t *fallback_diag_tok) {
  if (!node) return NULL;
  switch (node->kind) {
    case ND_COMPOUND_LITERAL:
      node->rhs = psx_lower_semantic_initializer_syntax(
          node->rhs, fallback_diag_tok);
      node = lower_compound_literal_expression(node, fallback_diag_tok);
      return node->kind == ND_COMPOUND_LITERAL
                 ? node
                 : psx_lower_semantic_tree(node, fallback_diag_tok);
    case ND_GENERIC_SELECTION: {
      node_t *selected = lower_generic_selection_expression(node);
      return selected == node
                 ? node
                 : psx_lower_semantic_tree(selected, fallback_diag_tok);
    }
    case ND_SIZEOF_QUERY: {
      node_sizeof_query_t *query = (node_sizeof_query_t *)node;
      if (query->runtime_size_expr) {
        query->runtime_size_expr = psx_lower_semantic_tree(
            query->runtime_size_expr, fallback_diag_tok);
      }
      node_t *prefix = query->evaluates_vla_operand
                           ? lower_sizeof_vla_indices(
                                 query->operand, fallback_diag_tok)
                           : NULL;
      return psx_lower_semantic_tree(
          lower_sizeof_query_expression(query, prefix),
          fallback_diag_tok);
    }
    case ND_ALIGNOF_QUERY:
      return lower_alignof_query_expression(
          (node_alignof_query_t *)node);
    case ND_DECL_INIT: {
      node_decl_init_t *init = (node_decl_init_t *)node;
      node->lhs = psx_lower_semantic_tree(node->lhs, fallback_diag_tok);
      if (init->init_kind == PSX_DECL_INIT_LIST) {
        node->rhs = psx_lower_semantic_initializer_syntax(
            node->rhs, fallback_diag_tok);
      } else {
        node->rhs = psx_lower_semantic_tree(node->rhs, fallback_diag_tok);
      }
      node = lower_decl_initializer(node);
      break;
    }
    case ND_BLOCK:
      lower_node_array(
          ((node_block_t *)node)->body, fallback_diag_tok);
      break;
    case ND_FUNCDEF: {
      node_func_t *function = (node_func_t *)node;
      for (int i = 0; i < function->nargs; i++)
        function->args[i] = psx_lower_semantic_tree(
            function->args[i], fallback_diag_tok);
      node->rhs = psx_lower_semantic_tree(node->rhs, fallback_diag_tok);
      break;
    }
    case ND_FUNCALL: {
      node_func_t *call = (node_func_t *)node;
      call->callee = psx_lower_semantic_tree(
          call->callee, fallback_diag_tok);
      for (int i = 0; i < call->nargs; i++)
        call->args[i] = psx_lower_semantic_tree(
            call->args[i], fallback_diag_tok);
      break;
    }
    case ND_SUBSCRIPT:
      node->lhs = psx_lower_semantic_tree(node->lhs, fallback_diag_tok);
      node->rhs = psx_lower_semantic_tree(node->rhs, fallback_diag_tok);
      return lower_subscript_expression(node);
    case ND_MEMBER_ACCESS:
      node->lhs = psx_lower_semantic_tree(
          node->lhs, fallback_diag_tok);
      return lower_member_access_expression(
          (node_member_access_t *)node, fallback_diag_tok);
    case ND_UNARY_DEREF:
      node->lhs = psx_lower_semantic_tree(node->lhs, fallback_diag_tok);
      return lower_unary_deref_expression(node);
    case ND_UNARY_NEGATE:
      node->lhs = psx_lower_semantic_tree(node->lhs, fallback_diag_tok);
      return psx_lower_semantic_tree(
          lower_unary_negate_expression(node), fallback_diag_tok);
    case ND_CREAL:
    case ND_CIMAG: {
      node->lhs = psx_lower_semantic_tree(node->lhs, fallback_diag_tok);
      node_t *lowered = lower_complex_part_expression(node);
      return lowered == node
                 ? node
                 : psx_lower_semantic_tree(lowered, fallback_diag_tok);
    }
    case ND_IF:
    case ND_FOR:
    case ND_TERNARY: {
      node_ctrl_t *control = (node_ctrl_t *)node;
      control->init = psx_lower_semantic_tree(
          control->init, fallback_diag_tok);
      node->lhs = psx_lower_semantic_tree(node->lhs, fallback_diag_tok);
      node->rhs = psx_lower_semantic_tree(node->rhs, fallback_diag_tok);
      control->inc = psx_lower_semantic_tree(
          control->inc, fallback_diag_tok);
      control->els = psx_lower_semantic_tree(
          control->els, fallback_diag_tok);
      break;
    }
    default:
      node->lhs = psx_lower_semantic_tree(node->lhs, fallback_diag_tok);
      node->rhs = psx_lower_semantic_tree(node->rhs, fallback_diag_tok);
      node = lower_source_cast_node(node, fallback_diag_tok);
      node = lower_aggregate_address_expression(node);
      node = lower_additive_expression_node(node);
      node = lower_compound_assignment_expression(node);
      break;
  }
  return node;
}

static const psx_type_t *call_function_type(node_func_t *call) {
  if (!call) return NULL;
  return ps_type_callable_function(call->function_type);
}

static void lower_call_arguments(
    node_func_t *call, const token_t *fallback_diag_tok) {
  const psx_type_t *function = call_function_type(call);
  if (!call || !function || function->kind != PSX_TYPE_FUNCTION) return;
  int count = call->nargs < function->param_count
                  ? call->nargs : function->param_count;
  if (count > 16) count = 16;
  for (int i = 0; i < count; i++) {
    call->args[i] = lower_implicit_value_conversion(
        call->args[i], function->param_types[i],
        call->base.tok ? call->base.tok
                       : (token_t *)fallback_diag_tok);
  }
}

static void lower_assignment_conversion(
    node_t *node, const token_t *fallback_diag_tok) {
  if (!node || node->kind != ND_ASSIGN || !node->lhs || !node->rhs)
    return;
  node->rhs = lower_implicit_value_conversion(
      node->rhs, ps_node_get_type(node->lhs),
      node->tok ? node->tok : (token_t *)fallback_diag_tok);
}

void psx_lower_implicit_conversions(
    node_t *node, node_func_t *current_func,
    const token_t *fallback_diag_tok) {
  if (!node) return;
  switch (node->kind) {
    case ND_BLOCK:
      for (node_t **body = ((node_block_t *)node)->body;
           body && *body; body++) {
        psx_lower_implicit_conversions(
            *body, current_func, fallback_diag_tok);
      }
      break;
    case ND_FUNCDEF: {
      node_func_t *function = (node_func_t *)node;
      for (int i = 0; i < function->nargs; i++)
        psx_lower_implicit_conversions(
            function->args[i], function, fallback_diag_tok);
      psx_lower_implicit_conversions(
          node->rhs, function, fallback_diag_tok);
      break;
    }
    case ND_FUNCALL: {
      node_func_t *call = (node_func_t *)node;
      psx_lower_implicit_conversions(
          call->callee, current_func, fallback_diag_tok);
      for (int i = 0; i < call->nargs; i++)
        psx_lower_implicit_conversions(
            call->args[i], current_func, fallback_diag_tok);
      lower_call_arguments(call, fallback_diag_tok);
      break;
    }
    case ND_IF:
    case ND_FOR:
    case ND_TERNARY: {
      node_ctrl_t *control = (node_ctrl_t *)node;
      psx_lower_implicit_conversions(
          control->init, current_func, fallback_diag_tok);
      psx_lower_implicit_conversions(
          node->lhs, current_func, fallback_diag_tok);
      psx_lower_implicit_conversions(
          node->rhs, current_func, fallback_diag_tok);
      psx_lower_implicit_conversions(
          control->inc, current_func, fallback_diag_tok);
      psx_lower_implicit_conversions(
          control->els, current_func, fallback_diag_tok);
      break;
    }
    case ND_RETURN:
      psx_lower_implicit_conversions(
          node->lhs, current_func, fallback_diag_tok);
      if (current_func && node->lhs) {
        node->lhs = lower_implicit_value_conversion(
            node->lhs, ps_node_get_type((node_t *)current_func),
            node->tok ? node->tok : (token_t *)fallback_diag_tok);
      }
      break;
    default:
      psx_lower_implicit_conversions(
          node->lhs, current_func, fallback_diag_tok);
      psx_lower_implicit_conversions(
          node->rhs, current_func, fallback_diag_tok);
      lower_assignment_conversion(node, fallback_diag_tok);
      break;
  }
}
