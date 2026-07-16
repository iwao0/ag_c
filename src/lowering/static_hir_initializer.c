#include "static_hir_initializer.h"

#include "runtime_context.h"
#include "../parser/global_registry.h"
#include "../parser/gvar_public.h"
#include "../parser/node_utils.h"
#include "../parser/symtab.h"
#include "../parser/type.h"
#include "../semantic/type_identity.h"
#include "../type_layout.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  psx_global_registry_t *global_registry;
  psx_lowering_context_t *lowering_context;
  const psx_hir_module_t *hir;
} static_hir_eval_t;

static const psx_hir_node_t *node_for_id(
    const static_hir_eval_t *eval, psx_hir_node_id_t id) {
  return eval ? psx_hir_module_lookup(eval->hir, id) : NULL;
}

static const psx_hir_node_t *child_for_edge(
    const static_hir_eval_t *eval, const psx_hir_node_t *node,
    psx_hir_edge_kind_t edge, size_t ordinal) {
  if (!eval || !node) return NULL;
  size_t found = 0;
  for (size_t i = 0; i < psx_hir_node_child_count(node); i++) {
    if (psx_hir_node_child_edge_at(node, i) != edge) continue;
    if (found++ == ordinal)
      return node_for_id(eval, psx_hir_node_child_at(node, i));
  }
  return NULL;
}

static const psx_type_t *node_type(
    const static_hir_eval_t *eval, const psx_hir_node_t *node) {
  if (!eval || !node) return NULL;
  return psx_semantic_type_table_lookup(
      ps_lowering_semantic_types(eval->lowering_context),
      psx_hir_node_qual_type(node).type_id);
}

static int type_uses_floating_value(const psx_type_t *type) {
  return type && (type->kind == PSX_TYPE_FLOAT ||
                  type->kind == PSX_TYPE_COMPLEX);
}

static long long normalize_integer_cast(
    const static_hir_eval_t *eval, long long value,
    psx_qual_type_t target_type) {
  const psx_type_t *target = psx_semantic_type_table_lookup(
      ps_lowering_semantic_types(eval->lowering_context),
      target_type.type_id);
  if (!target || target->kind != PSX_TYPE_INTEGER) return value;
  int byte_width = ps_type_sizeof_id_with_records(
      ps_lowering_semantic_types(eval->lowering_context),
      ps_lowering_record_layouts(eval->lowering_context),
      target_type.type_id, ps_lowering_target(eval->lowering_context));
  int bits = byte_width * 8;
  if (bits <= 0 || bits >= 64) return value;
  uint64_t mask = (UINT64_C(1) << bits) - 1;
  uint64_t normalized = (uint64_t)value & mask;
  if (!ps_type_is_unsigned(target) &&
      (normalized & (UINT64_C(1) << (bits - 1))))
    normalized |= ~mask;
  return (long long)normalized;
}

static long long eval_const_int(
    const static_hir_eval_t *eval, const psx_hir_node_t *node, int *ok);
static double eval_const_fp(
    const static_hir_eval_t *eval, const psx_hir_node_t *node, int *ok);
static int resolve_address(
    const static_hir_eval_t *eval, const psx_hir_node_t *node,
    const char **symbol, int *symbol_len, long long *offset);
static int pointer_stride(
    const static_hir_eval_t *eval, const psx_hir_node_t *pointer);
static int type_is_pointer_like(
    const static_hir_eval_t *eval, const psx_hir_node_t *node);

static global_var_t *find_global(
    const static_hir_eval_t *eval, const psx_hir_node_t *node) {
  size_t name_length = 0;
  const char *name = psx_hir_node_name(node, &name_length);
  if (!eval || !eval->global_registry || !name ||
      name_length == 0 || name_length > INT32_MAX)
    return NULL;
  return ps_find_global_var_in(
      eval->global_registry, (char *)name, (int)name_length);
}

static char *persist_symbol_name(
    const static_hir_eval_t *eval, const char *name, int name_len) {
  if (!eval || !name) return NULL;
  if (name_len == -1) {
    name_len = (int)strlen(name);
  } else {
    global_var_t *global = ps_find_global_var_in(
        eval->global_registry, (char *)name, name_len);
    if (global) return ps_gvar_name(global);
  }
  if (name_len <= 0) return NULL;
  char *copy = malloc((size_t)name_len + 1);
  if (!copy) return NULL;
  memcpy(copy, name, (size_t)name_len);
  copy[name_len] = '\0';
  return copy;
}

static long long eval_const_int(
    const static_hir_eval_t *eval, const psx_hir_node_t *node, int *ok) {
  if (!node) {
    if (ok) *ok = 0;
    return 0;
  }
  const psx_hir_node_t *lhs =
      child_for_edge(eval, node, PSX_HIR_EDGE_LHS, 0);
  const psx_hir_node_t *rhs =
      child_for_edge(eval, node, PSX_HIR_EDGE_RHS, 0);
  switch (psx_hir_node_kind(node)) {
    case PSX_HIR_NUMBER:
      return psx_hir_node_integer_value(node);
    case PSX_HIR_CAST: {
      const psx_type_t *target = node_type(eval, node);
      const psx_type_t *source = node_type(eval, lhs);
      long long value = type_uses_floating_value(source)
                            ? (long long)eval_const_fp(eval, lhs, ok)
                            : eval_const_int(eval, lhs, ok);
      if (ok && !*ok) return 0;
      if (target && target->kind == PSX_TYPE_BOOL) return value != 0;
      if (target && target->kind == PSX_TYPE_INTEGER)
        return normalize_integer_cast(
            eval, value, psx_hir_node_qual_type(node));
      if (target && target->kind == PSX_TYPE_FLOAT)
        return (long long)eval_const_fp(eval, lhs, ok);
      return value;
    }
    case PSX_HIR_NEGATE: {
      long long value = eval_const_int(eval, lhs, ok);
      return !ok || *ok ? -value : 0;
    }
    case PSX_HIR_GLOBAL: {
      global_var_t *global = find_global(eval, node);
      const psx_type_t *type =
          global ? ps_gvar_get_decl_type(global) : NULL;
      if (global && global->has_init && !global->init_symbol &&
          !global->init_values && !global->init_fvalues && type &&
          type->kind != PSX_TYPE_ARRAY && type->kind != PSX_TYPE_FLOAT &&
          type->kind != PSX_TYPE_COMPLEX)
        return global->init_val;
      if (ok) *ok = 0;
      return 0;
    }
    case PSX_HIR_COMMA:
      (void)eval_const_int(eval, lhs, ok);
      if (ok && !*ok) return 0;
      return eval_const_int(eval, rhs, ok);
    case PSX_HIR_TERNARY: {
      long long condition = eval_const_int(eval, lhs, ok);
      if (ok && !*ok) return 0;
      const psx_hir_node_t *otherwise =
          child_for_edge(eval, node, PSX_HIR_EDGE_ELSE, 0);
      return condition ? eval_const_int(eval, rhs, ok)
                       : eval_const_int(eval, otherwise, ok);
    }
    case PSX_HIR_ADD:
    case PSX_HIR_SUB:
    case PSX_HIR_MUL:
    case PSX_HIR_DIV:
    case PSX_HIR_MOD:
    case PSX_HIR_SHL:
    case PSX_HIR_SHR:
    case PSX_HIR_BITAND:
    case PSX_HIR_BITXOR:
    case PSX_HIR_BITOR:
    case PSX_HIR_EQ:
    case PSX_HIR_NE:
    case PSX_HIR_LT:
    case PSX_HIR_LE:
    case PSX_HIR_LOGAND:
    case PSX_HIR_LOGOR:
      break;
    default:
      if (ok) *ok = 0;
      return 0;
  }
  if (psx_hir_node_kind(node) == PSX_HIR_SUB) {
    const char *left_symbol = NULL;
    const char *right_symbol = NULL;
    int left_len = 0;
    int right_len = 0;
    long long left_offset = 0;
    long long right_offset = 0;
    if (resolve_address(
            eval, lhs, &left_symbol, &left_len, &left_offset) &&
        resolve_address(
            eval, rhs, &right_symbol, &right_len, &right_offset) &&
        left_symbol && right_symbol && left_len == right_len &&
        (left_len == -1
             ? strcmp(left_symbol, right_symbol) == 0
             : (left_len > 0 &&
                memcmp(
                    left_symbol, right_symbol,
                    (size_t)left_len) == 0))) {
      long long difference = left_offset - right_offset;
      if (type_is_pointer_like(eval, lhs) &&
          type_is_pointer_like(eval, rhs)) {
        int stride = pointer_stride(eval, lhs);
        if (stride <= 0 || difference % stride != 0) {
          if (ok) *ok = 0;
          return 0;
        }
        difference /= stride;
      }
      return difference;
    }
  }
  long long left = eval_const_int(eval, lhs, ok);
  if (ok && !*ok) return 0;
  long long right = eval_const_int(eval, rhs, ok);
  if ((psx_hir_node_kind(node) == PSX_HIR_DIV ||
       psx_hir_node_kind(node) == PSX_HIR_MOD) &&
      right == 0) {
    if (ok) *ok = 0;
    return 0;
  }
  switch (psx_hir_node_kind(node)) {
    case PSX_HIR_ADD: return left + right;
    case PSX_HIR_SUB: return left - right;
    case PSX_HIR_MUL: return left * right;
    case PSX_HIR_DIV: return left / right;
    case PSX_HIR_MOD: return left % right;
    case PSX_HIR_SHL: return left << right;
    case PSX_HIR_SHR: return left >> right;
    case PSX_HIR_BITAND: return left & right;
    case PSX_HIR_BITXOR: return left ^ right;
    case PSX_HIR_BITOR: return left | right;
    case PSX_HIR_EQ: return left == right;
    case PSX_HIR_NE: return left != right;
    case PSX_HIR_LT: return left < right;
    case PSX_HIR_LE: return left <= right;
    case PSX_HIR_LOGAND: return left && right;
    case PSX_HIR_LOGOR: return left || right;
    default:
      if (ok) *ok = 0;
      return 0;
  }
}

static double eval_const_fp(
    const static_hir_eval_t *eval, const psx_hir_node_t *node, int *ok) {
  if (!node) {
    if (ok) *ok = 0;
    return 0.0;
  }
  const psx_hir_node_t *lhs =
      child_for_edge(eval, node, PSX_HIR_EDGE_LHS, 0);
  const psx_hir_node_t *rhs =
      child_for_edge(eval, node, PSX_HIR_EDGE_RHS, 0);
  switch (psx_hir_node_kind(node)) {
    case PSX_HIR_NUMBER:
      return type_uses_floating_value(node_type(eval, node))
                 ? psx_hir_node_floating_value(node)
                 : (double)psx_hir_node_integer_value(node);
    case PSX_HIR_CAST: {
      const psx_type_t *target = node_type(eval, node);
      if (target && (target->kind == PSX_TYPE_BOOL ||
                     target->kind == PSX_TYPE_INTEGER))
        return (double)eval_const_int(eval, node, ok);
      return eval_const_fp(eval, lhs, ok);
    }
    case PSX_HIR_NEGATE: {
      double value = eval_const_fp(eval, lhs, ok);
      return !ok || *ok ? -value : 0.0;
    }
    case PSX_HIR_ADD:
    case PSX_HIR_SUB:
    case PSX_HIR_MUL:
    case PSX_HIR_DIV:
      break;
    default:
      if (ok) *ok = 0;
      return 0.0;
  }
  double left = eval_const_fp(eval, lhs, ok);
  if (ok && !*ok) return 0.0;
  double right = eval_const_fp(eval, rhs, ok);
  if (ok && !*ok) return 0.0;
  switch (psx_hir_node_kind(node)) {
    case PSX_HIR_ADD: return left + right;
    case PSX_HIR_SUB: return left - right;
    case PSX_HIR_MUL: return left * right;
    case PSX_HIR_DIV:
      if (right != 0.0) return left / right;
      if (ok) *ok = 0;
      return 0.0;
    default:
      if (ok) *ok = 0;
      return 0.0;
  }
}

static int pointer_stride(
    const static_hir_eval_t *eval, const psx_hir_node_t *pointer) {
  if (!eval || !pointer) return 0;
  psx_qual_type_t element = psx_semantic_type_table_base(
      ps_lowering_semantic_types(eval->lowering_context),
      psx_hir_node_qual_type(pointer).type_id);
  if (element.type_id == PSX_TYPE_ID_INVALID) return 0;
  return ps_type_sizeof_id_with_records(
      ps_lowering_semantic_types(eval->lowering_context),
      ps_lowering_record_layouts(eval->lowering_context),
      element.type_id, ps_lowering_target(eval->lowering_context));
}

static int type_is_pointer_like(
    const static_hir_eval_t *eval, const psx_hir_node_t *node) {
  const psx_type_t *type = node_type(eval, node);
  return type && (type->kind == PSX_TYPE_POINTER ||
                  type->kind == PSX_TYPE_ARRAY);
}

static int resolve_address(
    const static_hir_eval_t *eval, const psx_hir_node_t *node,
    const char **symbol, int *symbol_len, long long *offset) {
  if (!eval || !node || !symbol || !symbol_len || !offset) return 0;
  const psx_hir_node_t *lhs =
      child_for_edge(eval, node, PSX_HIR_EDGE_LHS, 0);
  const psx_hir_node_t *rhs =
      child_for_edge(eval, node, PSX_HIR_EDGE_RHS, 0);
  psx_hir_node_kind_t kind = psx_hir_node_kind(node);
  switch (kind) {
    case PSX_HIR_ADDRESS:
    case PSX_HIR_DEREF:
    case PSX_HIR_CAST:
      return resolve_address(eval, lhs, symbol, symbol_len, offset);
    case PSX_HIR_FUNCTION_REF:
    case PSX_HIR_GLOBAL:
    case PSX_HIR_STRING: {
      size_t length = 0;
      const char *name = psx_hir_node_name(node, &length);
      if (!name || length == 0 || length > INT32_MAX) return 0;
      *symbol = name;
      *symbol_len = kind == PSX_HIR_STRING ? -1 : (int)length;
      return 1;
    }
    case PSX_HIR_SUBSCRIPT: {
      if (!resolve_address(eval, lhs, symbol, symbol_len, offset))
        return 0;
      int ok = 1;
      long long index = eval_const_int(eval, rhs, &ok);
      int stride = pointer_stride(eval, lhs);
      if (!ok || stride <= 0) return 0;
      *offset += index * stride;
      return 1;
    }
    case PSX_HIR_MEMBER_ACCESS:
      if (!resolve_address(eval, lhs, symbol, symbol_len, offset))
        return 0;
      *offset += psx_hir_node_member_offset(node);
      return 1;
    case PSX_HIR_ADD: {
      int ok = 1;
      if (resolve_address(eval, lhs, symbol, symbol_len, offset)) {
        long long addend = eval_const_int(eval, rhs, &ok);
        if (!ok) return 0;
        if (type_is_pointer_like(eval, lhs)) {
          int stride = pointer_stride(eval, lhs);
          if (stride <= 0) return 0;
          addend *= stride;
        }
        *offset += addend;
        return 1;
      }
      if (resolve_address(eval, rhs, symbol, symbol_len, offset)) {
        long long addend = eval_const_int(eval, lhs, &ok);
        if (!ok) return 0;
        if (type_is_pointer_like(eval, rhs)) {
          int stride = pointer_stride(eval, rhs);
          if (stride <= 0) return 0;
          addend *= stride;
        }
        *offset += addend;
        return 1;
      }
      return 0;
    }
    case PSX_HIR_SUB: {
      int ok = 1;
      if (!resolve_address(eval, lhs, symbol, symbol_len, offset))
        return 0;
      long long addend = eval_const_int(eval, rhs, &ok);
      if (!ok) return 0;
      if (type_is_pointer_like(eval, lhs)) {
        int stride = pointer_stride(eval, lhs);
        if (stride <= 0) return 0;
        addend *= stride;
      }
      *offset -= addend;
      return 1;
    }
    default:
      return 0;
  }
}

static int lower_string(
    const static_hir_eval_t *eval, global_var_t *global,
    const psx_type_t *type, const psx_hir_node_t *node) {
  size_t name_length = 0;
  const char *name = psx_hir_node_name(node, &name_length);
  if (type->kind == PSX_TYPE_POINTER) {
    if (!name || name_length == 0) return 0;
    global->init_symbol = persist_symbol_name(eval, name, -1);
    if (!global->init_symbol) return 0;
    global->init_symbol_len = -1;
    return 1;
  }
  if (type->kind != PSX_TYPE_ARRAY) return 0;
  const psx_type_t *element = ps_type_array_leaf_type(type);
  psx_type_id_t element_type_id = psx_semantic_type_table_base(
      ps_lowering_semantic_types(eval->lowering_context),
      ps_lowering_type_id(eval->lowering_context, type)).type_id;
  int element_size = ps_type_sizeof_id_with_records(
      ps_lowering_semantic_types(eval->lowering_context),
      ps_lowering_record_layouts(eval->lowering_context),
      element_type_id, ps_lowering_target(eval->lowering_context));
  int character_width = psx_hir_node_object_align(node);
  if (!element || element_size <= 0 ||
      character_width <= 0 || element_size != character_width)
    return 0;
  size_t literal_length = 0;
  const char *literal =
      psx_hir_node_literal_contents(node, &literal_length);
  int total_units = psx_hir_node_object_size(node) / character_width;
  if (!literal || total_units <= 0) return 0;
  ps_gvar_init_slots_alloc(global, total_units, 0);
  ps_gvar_init_slots_write_string_units(
      global, 0, literal, (int)literal_length,
      element_size, total_units - 1);
  ps_gvar_init_slot_write(
      global, total_units - 1, 0, 0.0, NULL, 0);
  global->init_count = total_units;
  return 1;
}

int psx_lower_static_scalar_hir_initializer(
    psx_global_registry_t *global_registry,
    psx_lowering_context_t *lowering_context,
    global_var_t *global, const psx_type_t *type,
    const psx_hir_module_t *hir, psx_hir_node_id_t root) {
  if (!global_registry || !lowering_context || !global || !type ||
      !hir || root == PSX_HIR_NODE_ID_INVALID)
    return 0;
  static_hir_eval_t eval = {
      .global_registry = global_registry,
      .lowering_context = lowering_context,
      .hir = hir,
  };
  const psx_hir_node_t *initializer = node_for_id(&eval, root);
  if (!initializer || psx_hir_node_role(initializer) !=
                          PSX_HIR_ROLE_EXPRESSION)
    return 0;
  if (psx_hir_node_kind(initializer) == PSX_HIR_STRING)
    return lower_string(&eval, global, type, initializer);

  int integer_ok = 1;
  long long integer = eval_const_int(&eval, initializer, &integer_ok);
  if (type->kind == PSX_TYPE_FLOAT || type->kind == PSX_TYPE_COMPLEX) {
    int floating_ok = 1;
    double floating = eval_const_fp(&eval, initializer, &floating_ok);
    if (floating_ok) {
      global->fval = floating;
      return 1;
    }
  }
  if (integer_ok) {
    global->init_val = type->kind == PSX_TYPE_BOOL ? integer != 0 : integer;
    return 1;
  }

  const char *symbol = NULL;
  int symbol_len = 0;
  long long offset = 0;
  if (!resolve_address(
          &eval, initializer, &symbol, &symbol_len, &offset))
    return 0;
  global->init_symbol = persist_symbol_name(
      &eval, symbol, symbol_len);
  if (!global->init_symbol) return 0;
  global->init_symbol_len = symbol_len;
  global->init_symbol_offset = offset;
  return 1;
}
