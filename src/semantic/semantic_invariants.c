#include "semantic_invariants.h"

#include <string.h>

static int is_raw_expression_kind(node_kind_t kind) {
  switch (kind) {
    case ND_IDENTIFIER:
    case ND_UNARY_NEGATE:
    case ND_UNARY_DEREF:
    case ND_SUBSCRIPT:
    case ND_MEMBER_ACCESS:
    case ND_GENERIC_SELECTION:
    case ND_SIZEOF_QUERY:
    case ND_ALIGNOF_QUERY:
    case ND_COMPOUND_LITERAL:
    case ND_INIT_LIST:
    case ND_DECL_INIT:
      return 1;
    default:
      return 0;
  }
}

static int expression_kind_requires_type(node_kind_t kind) {
  switch (kind) {
    case ND_ADD: case ND_SUB: case ND_MUL: case ND_DIV: case ND_MOD:
    case ND_EQ: case ND_NE: case ND_LT: case ND_LE:
    case ND_BITAND: case ND_BITXOR: case ND_BITOR:
    case ND_SHL: case ND_SHR: case ND_LOGAND: case ND_LOGOR:
    case ND_TERNARY: case ND_COMMA: case ND_ASSIGN:
    case ND_LVAR: case ND_PRE_INC: case ND_PRE_DEC:
    case ND_POST_INC: case ND_POST_DEC:
    case ND_FUNCALL: case ND_FUNCREF:
    case ND_DEREF: case ND_ADDR: case ND_STRING: case ND_NUM: case ND_GVAR:
    case ND_FP_TO_INT: case ND_INT_TO_FP: case ND_FNEG:
    case ND_VA_ARG_AREA: case ND_CAST: case ND_CREAL: case ND_CIMAG:
    case ND_STMT_EXPR:
      return 1;
    default:
      return 0;
  }
}

static int fail(psx_semantic_invariant_failure_t *failure,
                psx_semantic_invariant_status_t status,
                const node_t *node) {
  if (failure) {
    failure->status = status;
    failure->node = node;
  }
  return 0;
}

static int is_implicit_function_result_type(const psx_type_t *type) {
  return type && type->kind == PSX_TYPE_INTEGER &&
         type->scalar_kind == TK_INT && type->size == 4 &&
         !type->is_unsigned;
}

static int validate_node(const node_t *node,
                         psx_semantic_invariant_failure_t *failure) {
  if (!node) return 1;
  if (is_raw_expression_kind(node->kind))
    return fail(failure, PSX_SEMANTIC_INVARIANT_RAW_EXPRESSION, node);
  if (expression_kind_requires_type(node->kind) && !node->type)
    return fail(failure, PSX_SEMANTIC_INVARIANT_MISSING_CANONICAL_TYPE, node);
  if (node->type && !ps_type_is_well_formed(node->type))
    return fail(failure, PSX_SEMANTIC_INVARIANT_INVALID_CANONICAL_TYPE, node);
  if (node->kind == ND_FUNCREF &&
      (node->type->kind != PSX_TYPE_POINTER || !node->type->base ||
       node->type->base->kind != PSX_TYPE_FUNCTION)) {
    return fail(failure, PSX_SEMANTIC_INVARIANT_INVALID_CALLABLE_TYPE, node);
  }
  if (node->kind == ND_FUNCDEF) {
    const node_function_definition_t *function =
        (const node_function_definition_t *)node;
    if (!function->signature ||
        function->signature->kind != PSX_TYPE_FUNCTION ||
        !ps_type_is_well_formed(function->signature) ||
        node->type != function->signature->base) {
      return fail(
          failure, PSX_SEMANTIC_INVARIANT_INVALID_CALLABLE_TYPE, node);
    }
  }
  if (node->kind == ND_FUNCALL) {
    const node_function_call_t *call =
        (const node_function_call_t *)node;
    if (call->callee &&
        !ps_type_callable_function(call->callee->type)) {
      return fail(
          failure, PSX_SEMANTIC_INVARIANT_INVALID_CALLABLE_TYPE, node);
    }
    if (call->callee_type) {
      if (call->callee_type->kind != PSX_TYPE_FUNCTION ||
          !ps_type_is_well_formed(call->callee_type) ||
          node->type != call->callee_type->base) {
        return fail(
            failure, PSX_SEMANTIC_INVARIANT_INVALID_CALLABLE_TYPE, node);
      }
    } else if (!node->is_implicit_func_decl ||
               !is_implicit_function_result_type(node->type)) {
      return fail(
          failure, PSX_SEMANTIC_INVARIANT_INVALID_CALLABLE_TYPE, node);
    }
  }
  int runtime_stride_off =
      node->type_state.vla_runtime.row_stride_frame_off;
  int runtime_strides_remaining =
      node->type_state.vla_runtime.strides_remaining;
  if ((runtime_stride_off != 0 &&
       (runtime_stride_off < 0 || !node->type ||
        !ps_type_contains_vla_array(node->type))) ||
      (runtime_stride_off == 0 && runtime_strides_remaining != 0) ||
      runtime_strides_remaining < 0) {
    return fail(
        failure, PSX_SEMANTIC_INVARIANT_INVALID_VLA_RUNTIME_VIEW, node);
  }

  switch (node->kind) {
    case ND_BLOCK: {
      node_t *const *body = ((const node_block_t *)node)->body;
      for (int i = 0; body && body[i]; i++) {
        if (!validate_node(body[i], failure)) return 0;
      }
      return 1;
    }
    case ND_FUNCDEF: {
      const node_function_definition_t *function =
          (const node_function_definition_t *)node;
      for (int i = 0; i < function->parameter_count; i++) {
        if (!validate_node(function->parameters[i], failure)) return 0;
      }
      break;
    }
    case ND_FUNCALL: {
      const node_function_call_t *call =
          (const node_function_call_t *)node;
      if (!validate_node(call->callee, failure)) return 0;
      for (int i = 0; i < call->argument_count; i++) {
        if (!validate_node(call->arguments[i], failure)) return 0;
      }
      break;
    }
    case ND_IF:
    case ND_FOR:
    case ND_TERNARY: {
      const node_ctrl_t *control = (const node_ctrl_t *)node;
      if (!validate_node(control->init, failure) ||
          !validate_node(control->inc, failure) ||
          !validate_node(control->els, failure))
        return 0;
      break;
    }
    case ND_INIT_LIST: {
      const node_init_list_t *list = (const node_init_list_t *)node;
      for (int i = 0; i < list->entry_count; i++) {
        const psx_initializer_entry_t *entry = &list->entries[i];
        if (!validate_node(entry->value, failure)) return 0;
        for (int d = 0; d < entry->designator_count; d++) {
          if (!validate_node(entry->designators[d].index_expr, failure) ||
              !validate_node(entry->designators[d].range_end_expr, failure))
            return 0;
        }
        for (int d = 0; d < entry->index_expr_count; d++) {
          if (!validate_node(entry->index_exprs[d], failure)) return 0;
        }
      }
      return 1;
    }
    default:
      break;
  }

  return validate_node(node->lhs, failure) &&
         validate_node(node->rhs, failure);
}

int psx_semantic_tree_has_canonical_expression_types(
    const node_t *root, psx_semantic_invariant_failure_t *failure) {
  if (failure) memset(failure, 0, sizeof(*failure));
  return validate_node(root, failure);
}
