#include "constant_expression.h"

#include "alignof_query_resolution.h"
#include "generic_selection_resolution.h"
#include "integer_constant_evaluation.h"
#include "literal_resolution.h"
#include "resolved_node_kind.h"
#include "resolved_object_ref.h"
#include "sizeof_query_resolution.h"
#include "../parser/gvar_public.h"
#include "../parser/node_utils.h"
#include "../parser/symtab.h"
#include <string.h>

static int type_uses_floating_value(const psx_type_t *type) {
  return type && (type->kind == PSX_TYPE_FLOAT ||
                  type->kind == PSX_TYPE_COMPLEX);
}

long long psx_eval_const_int(
    const psx_resolution_store_t *store, node_t *node, int *ok) {
  if (!node) {
    if (ok) *ok = 0;
    return 0;
  }
  switch (psx_resolved_object_ref_node_kind(store, node)) {
    case ND_NUM:
      return ((node_num_t *)node)->val;
    case ND_CAST: {
      if (ps_node_value_is_void(store, node)) {
        if (ok) *ok = 0;
        return 0;
      }
      if (!node->is_source_cast)
        return psx_eval_const_int(store, node->lhs, ok);
      const psx_type_t *target = ps_node_get_type(store, node);
      const psx_type_t *source = ps_node_get_type(store, node->lhs);
      long long result = type_uses_floating_value(source)
                             ? (long long)psx_eval_const_fp(
                                   store, node->lhs, ok)
                             : psx_eval_const_int(store, node->lhs, ok);
      if (ok && !*ok) return 0;
      if (target && (target->kind == PSX_TYPE_BOOL ||
                     target->kind == PSX_TYPE_INTEGER)) {
        long long normalized;
        if (!psx_normalize_integer_constant_cast(
                target, result, &normalized)) {
          if (ok) *ok = 0;
          return 0;
        }
        return normalized;
      }
      if (target && target->kind == PSX_TYPE_FLOAT)
        return (long long)psx_eval_const_fp(store, node->lhs, ok);
      return result;
    }
    case ND_UNARY_NEGATE: {
      long long value = psx_eval_const_int(store, node->lhs, ok);
      return !ok || *ok ? -value : 0;
    }
    case ND_LOGICAL_NOT: {
      const psx_type_t *operand_type = ps_node_get_type(store, node->lhs);
      long long value = type_uses_floating_value(operand_type)
                            ? psx_eval_const_fp(store, node->lhs, ok) != 0.0
                            : psx_eval_const_int(store, node->lhs, ok) != 0;
      return !ok || *ok ? !value : 0;
    }
    case ND_BITWISE_NOT: {
      long long value = psx_eval_const_int(store, node->lhs, ok);
      return !ok || *ok ? ~value : 0;
    }
    case ND_SIZEOF_QUERY: {
      node_sizeof_query_t *query = (node_sizeof_query_t *)node;
      int resolved_size = psx_sizeof_query_resolved_size(store, query);
      if (psx_sizeof_query_runtime_plan_const(store, query) ||
          psx_sizeof_query_runtime_size_slot(store, query) != 0 ||
          resolved_size <= 0) {
        if (ok) *ok = 0;
        return 0;
      }
      return resolved_size;
    }
    case ND_ALIGNOF_QUERY: {
      node_alignof_query_t *query = (node_alignof_query_t *)node;
      int alignment =
          psx_alignof_query_resolved_alignment(store, query);
      if (alignment <= 0) {
        if (ok) *ok = 0;
        return 0;
      }
      return alignment;
    }
    case ND_GENERIC_SELECTION: {
      node_generic_selection_t *selection =
          (node_generic_selection_t *)node;
      node_t *selected =
          psx_generic_selection_selected_expression(store, selection);
      if (!selected) {
        if (ok) *ok = 0;
        return 0;
      }
      return psx_eval_const_int(store, selected, ok);
    }
    case ND_GVAR: {
      global_var_t *global = psx_resolved_object_ref_global(store, node);
      int name_len = 0;
      char *name = psx_resolved_object_ref_name(store, node, &name_len);
      if (global && name && global->name_len == name_len &&
          memcmp(global->name, name, (size_t)global->name_len) == 0) {
        const psx_type_t *type = ps_gvar_get_decl_type(global);
        if (global->has_init && !global->init_symbol &&
            !global->init_values && !global->init_fvalues && type &&
            type->kind != PSX_TYPE_ARRAY && type->kind != PSX_TYPE_FLOAT &&
            type->kind != PSX_TYPE_COMPLEX) {
          return global->init_val;
        }
      }
      if (ok) *ok = 0;
      return 0;
    }
    case ND_COMMA:
      (void)psx_eval_const_int(store, node->lhs, ok);
      if (ok && !*ok) return 0;
      return psx_eval_const_int(store, node->rhs, ok);
    case ND_TERNARY: {
      long long condition = psx_eval_const_int(store, node->lhs, ok);
      if (ok && !*ok) return 0;
      node_ctrl_t *ternary = (node_ctrl_t *)node;
      return condition ? psx_eval_const_int(store, node->rhs, ok)
                       : psx_eval_const_int(store, ternary->els, ok);
    }
    case ND_ADD: case ND_SUB: case ND_MUL: case ND_DIV: case ND_MOD:
    case ND_SHL: case ND_SHR:
    case ND_BITAND: case ND_BITXOR: case ND_BITOR:
    case ND_EQ: case ND_NE: case ND_LT: case ND_LE:
    case ND_LOGAND: case ND_LOGOR:
      break;
    default:
      if (ok) *ok = 0;
      return 0;
  }

  if (node->kind == ND_SUB) {
    char *left_symbol = NULL;
    char *right_symbol = NULL;
    int left_len = 0;
    int right_len = 0;
    long long left_offset = 0;
    long long right_offset = 0;
    if (psx_resolve_static_address_constant(
            store, node->lhs, &left_symbol, &left_len, &left_offset) &&
        psx_resolve_static_address_constant(
            store, node->rhs, &right_symbol, &right_len, &right_offset) &&
        left_symbol && right_symbol && left_len == right_len &&
        (left_len == -1
             ? left_symbol == right_symbol
             : (left_len > 0 &&
                memcmp(left_symbol, right_symbol, (size_t)left_len) == 0))) {
      return left_offset - right_offset;
    }
  }

  long long left = psx_eval_const_int(store, node->lhs, ok);
  if (ok && !*ok) return 0;
  long long right = psx_eval_const_int(store, node->rhs, ok);
  long long result;
  if (psx_apply_integer_constant_binary(
          (psx_syntax_node_kind_t)
              psx_resolved_object_ref_node_kind(store, node),
          left, right, &result))
    return result;
  if (ok) *ok = 0;
  return 0;
}

double psx_eval_const_fp(
    const psx_resolution_store_t *store, node_t *node, int *ok) {
  if (!node) {
    if (ok) *ok = 0;
    return 0.0;
  }
  switch (psx_resolved_object_ref_node_kind(store, node)) {
    case ND_NUM: {
      node_num_t *number = (node_num_t *)node;
      return ps_node_value_fp_kind(store, node) != TK_FLOAT_KIND_NONE
                 ? number->fval : (double)number->val;
    }
    case ND_CAST: {
      if (!node->is_source_cast)
        return psx_eval_const_fp(store, node->lhs, ok);
      const psx_type_t *target = ps_node_get_type(store, node);
      if (target && (target->kind == PSX_TYPE_BOOL ||
                     target->kind == PSX_TYPE_INTEGER))
        return (double)psx_eval_const_int(store, node, ok);
      return psx_eval_const_fp(store, node->lhs, ok);
    }
    case ND_UNARY_NEGATE: {
      double value = psx_eval_const_fp(store, node->lhs, ok);
      return !ok || *ok ? -value : 0.0;
    }
    case ND_LOGICAL_NOT:
      return (double)psx_eval_const_int(store, node, ok);
    case ND_BITWISE_NOT:
      return (double)psx_eval_const_int(store, node, ok);
    case ND_ADD: {
      double left = psx_eval_const_fp(store, node->lhs, ok);
      if (ok && !*ok) return 0.0;
      double right = psx_eval_const_fp(store, node->rhs, ok);
      return !ok || *ok ? left + right : 0.0;
    }
    case ND_SUB: {
      double left = psx_eval_const_fp(store, node->lhs, ok);
      if (ok && !*ok) return 0.0;
      double right = psx_eval_const_fp(store, node->rhs, ok);
      return !ok || *ok ? left - right : 0.0;
    }
    case ND_MUL: {
      double left = psx_eval_const_fp(store, node->lhs, ok);
      if (ok && !*ok) return 0.0;
      double right = psx_eval_const_fp(store, node->rhs, ok);
      return !ok || *ok ? left * right : 0.0;
    }
    case ND_DIV: {
      double left = psx_eval_const_fp(store, node->lhs, ok);
      if (ok && !*ok) return 0.0;
      double right = psx_eval_const_fp(store, node->rhs, ok);
      if ((ok && !*ok) || right == 0.0) {
        if (ok) *ok = 0;
        return 0.0;
      }
      return left / right;
    }
    default:
      if (ok) *ok = 0;
      return 0.0;
  }
}

int psx_resolve_static_address_constant(
    const psx_resolution_store_t *store, node_t *node,
    char **symbol, int *symbol_len, long long *offset) {
  if (!node || !symbol || !symbol_len || !offset) return 0;
  switch (psx_resolved_object_ref_node_kind(store, node)) {
    case ND_ADDR:
      if (node->lhs &&
          psx_resolved_object_ref_node_kind(store, node->lhs) == ND_GVAR) {
        *symbol = psx_resolved_object_ref_name(
            store, node->lhs, symbol_len);
        if (!*symbol) return 0;
        return 1;
      }
      if (node->lhs &&
          psx_resolution_node_kind(store, node->lhs) == ND_DEREF) {
        return psx_resolve_static_address_constant(
            store, node->lhs->lhs, symbol, symbol_len, offset);
      }
      return 0;
    case ND_CAST: {
      return psx_resolve_static_address_constant(
          store, node->lhs, symbol, symbol_len, offset);
    }
    case ND_FUNCREF: {
      *symbol = psx_resolved_object_ref_name(store, node, symbol_len);
      return *symbol != NULL;
    }
    case ND_GVAR: {
      *symbol = psx_resolved_object_ref_name(store, node, symbol_len);
      return *symbol != NULL;
    }
    case ND_STRING: {
      node_string_t *string = (node_string_t *)node;
      *symbol = psx_string_literal_label(store, string);
      *symbol_len = -1;
      return 1;
    }
    case ND_ADD: {
      int ok = 1;
      if (psx_resolve_static_address_constant(
              store, node->lhs, symbol, symbol_len, offset)) {
        long long addend = psx_eval_const_int(store, node->rhs, &ok);
        if (!ok) return 0;
        *offset += addend;
        return 1;
      }
      if (psx_resolve_static_address_constant(
              store, node->rhs, symbol, symbol_len, offset)) {
        long long addend = psx_eval_const_int(store, node->lhs, &ok);
        if (!ok) return 0;
        *offset += addend;
        return 1;
      }
      return 0;
    }
    case ND_SUB: {
      int ok = 1;
      if (!psx_resolve_static_address_constant(
              store, node->lhs, symbol, symbol_len, offset)) return 0;
      long long addend = psx_eval_const_int(store, node->rhs, &ok);
      if (!ok) return 0;
      *offset -= addend;
      return 1;
    }
    default:
      return 0;
  }
}
