#include "constant_expression.h"

#include "../parser/gvar_public.h"
#include "../parser/node_utils.h"
#include "../parser/symtab.h"
#include <string.h>

long long psx_eval_const_int(node_t *node, int *ok) {
  if (!node) {
    if (ok) *ok = 0;
    return 0;
  }
  switch (node->kind) {
    case ND_NUM:
      return ((node_num_t *)node)->val;
    case ND_CAST:
      if (node->type && node->type->kind == PSX_TYPE_VOID) {
        if (ok) *ok = 0;
        return 0;
      }
      return psx_eval_const_int(node->lhs, ok);
    case ND_GVAR: {
      node_gvar_t *ref = (node_gvar_t *)node;
      global_var_t *global = ps_find_global_var(ref->name, ref->name_len);
      if (global && global->name_len == ref->name_len &&
          memcmp(global->name, ref->name, (size_t)global->name_len) == 0) {
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
      (void)psx_eval_const_int(node->lhs, ok);
      if (ok && !*ok) return 0;
      return psx_eval_const_int(node->rhs, ok);
    case ND_TERNARY: {
      long long condition = psx_eval_const_int(node->lhs, ok);
      if (ok && !*ok) return 0;
      node_ctrl_t *ternary = (node_ctrl_t *)node;
      return condition ? psx_eval_const_int(node->rhs, ok)
                       : psx_eval_const_int(ternary->els, ok);
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
            node->lhs, &left_symbol, &left_len, &left_offset) &&
        psx_resolve_static_address_constant(
            node->rhs, &right_symbol, &right_len, &right_offset) &&
        left_symbol && right_symbol && left_len == right_len &&
        (left_len == -1
             ? left_symbol == right_symbol
             : (left_len > 0 &&
                memcmp(left_symbol, right_symbol, (size_t)left_len) == 0))) {
      return left_offset - right_offset;
    }
  }

  long long left = psx_eval_const_int(node->lhs, ok);
  if (ok && !*ok) return 0;
  long long right = psx_eval_const_int(node->rhs, ok);
  switch (node->kind) {
    case ND_ADD: return left + right;
    case ND_SUB: return left - right;
    case ND_MUL: return left * right;
    case ND_DIV: return left / right;
    case ND_MOD: return left % right;
    case ND_SHL: return left << right;
    case ND_SHR: return left >> right;
    case ND_BITAND: return left & right;
    case ND_BITXOR: return left ^ right;
    case ND_BITOR: return left | right;
    case ND_EQ: return left == right;
    case ND_NE: return left != right;
    case ND_LT: return left < right;
    case ND_LE: return left <= right;
    case ND_LOGAND: return left && right;
    case ND_LOGOR: return left || right;
    default:
      if (ok) *ok = 0;
      return 0;
  }
}

double psx_eval_const_fp(node_t *node, int *ok) {
  if (!node) {
    if (ok) *ok = 0;
    return 0.0;
  }
  switch (node->kind) {
    case ND_NUM: {
      node_num_t *number = (node_num_t *)node;
      return node->fp_kind != TK_FLOAT_KIND_NONE
                 ? number->fval : (double)number->val;
    }
    case ND_CAST:
      return psx_eval_const_fp(node->lhs, ok);
    case ND_ADD: {
      double left = psx_eval_const_fp(node->lhs, ok);
      if (ok && !*ok) return 0.0;
      double right = psx_eval_const_fp(node->rhs, ok);
      return !ok || *ok ? left + right : 0.0;
    }
    case ND_SUB: {
      double left = psx_eval_const_fp(node->lhs, ok);
      if (ok && !*ok) return 0.0;
      double right = psx_eval_const_fp(node->rhs, ok);
      return !ok || *ok ? left - right : 0.0;
    }
    case ND_MUL: {
      double left = psx_eval_const_fp(node->lhs, ok);
      if (ok && !*ok) return 0.0;
      double right = psx_eval_const_fp(node->rhs, ok);
      return !ok || *ok ? left * right : 0.0;
    }
    case ND_DIV: {
      double left = psx_eval_const_fp(node->lhs, ok);
      if (ok && !*ok) return 0.0;
      double right = psx_eval_const_fp(node->rhs, ok);
      if ((ok && !*ok) || right == 0.0) {
        if (ok) *ok = 0;
        return 0.0;
      }
      return left / right;
    }
    case ND_FNEG: {
      double value = psx_eval_const_fp(node->lhs, ok);
      return !ok || *ok ? -value : 0.0;
    }
    default:
      if (ok) *ok = 0;
      return 0.0;
  }
}

int psx_resolve_static_address_constant(
    node_t *node, char **symbol, int *symbol_len, long long *offset) {
  if (!node || !symbol || !symbol_len || !offset) return 0;
  switch (node->kind) {
    case ND_ADDR:
      if (node->lhs && node->lhs->kind == ND_GVAR) {
        node_gvar_t *global = (node_gvar_t *)node->lhs;
        *symbol = global->name;
        *symbol_len = global->name_len;
        return 1;
      }
      if (node->lhs && node->lhs->kind == ND_DEREF) {
        return psx_resolve_static_address_constant(
            node->lhs->lhs, symbol, symbol_len, offset);
      }
      return 0;
    case ND_CAST:
      return psx_resolve_static_address_constant(
          node->lhs, symbol, symbol_len, offset);
    case ND_FUNCREF: {
      node_funcref_t *function = (node_funcref_t *)node;
      *symbol = function->funcname;
      *symbol_len = function->funcname_len;
      return 1;
    }
    case ND_GVAR: {
      node_gvar_t *global = (node_gvar_t *)node;
      *symbol = global->name;
      *symbol_len = global->name_len;
      return 1;
    }
    case ND_STRING: {
      node_string_t *string = (node_string_t *)node;
      *symbol = string->string_label;
      *symbol_len = -1;
      return 1;
    }
    case ND_ADD: {
      int ok = 1;
      if (psx_resolve_static_address_constant(
              node->lhs, symbol, symbol_len, offset)) {
        long long addend = psx_eval_const_int(node->rhs, &ok);
        if (!ok) return 0;
        *offset += addend;
        return 1;
      }
      if (psx_resolve_static_address_constant(
              node->rhs, symbol, symbol_len, offset)) {
        long long addend = psx_eval_const_int(node->lhs, &ok);
        if (!ok) return 0;
        *offset += addend;
        return 1;
      }
      return 0;
    }
    case ND_SUB: {
      int ok = 1;
      if (!psx_resolve_static_address_constant(
              node->lhs, symbol, symbol_len, offset)) return 0;
      long long addend = psx_eval_const_int(node->rhs, &ok);
      if (!ok) return 0;
      *offset -= addend;
      return 1;
    }
    default:
      return 0;
  }
}
