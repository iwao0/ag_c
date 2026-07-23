#include "static_hir_initializer.h"

#include "runtime_context.h"
#include "../parser/global_registry.h"
#include "../parser/gvar_public.h"
#include "../parser/symtab.h"
#include "../semantic/initializer_resolution.h"
#include "static_initializer_plan.h"
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

static int type_shape(
    const static_hir_eval_t *eval, psx_type_id_t type_id,
    psx_type_shape_t *out) {
  return eval && out && psx_semantic_type_table_describe(
      ps_lowering_semantic_types(eval->lowering_context), type_id, out);
}

static int node_type_shape(
    const static_hir_eval_t *eval, const psx_hir_node_t *node,
    psx_type_shape_t *out) {
  return node && type_shape(
      eval, psx_hir_node_qual_type(node).type_id, out);
}

static int type_uses_floating_value(const psx_type_shape_t *type) {
  return type && (type->kind == PSX_TYPE_FLOAT ||
                  type->kind == PSX_TYPE_COMPLEX);
}

static long long normalize_integer_cast(
    const static_hir_eval_t *eval, long long value,
    psx_qual_type_t target_type) {
  psx_type_shape_t target = {0};
  if (!type_shape(eval, target_type.type_id, &target) ||
      target.kind != PSX_TYPE_INTEGER)
    return value;
  int byte_width = psx_type_layout_sizeof(
      ps_lowering_semantic_types(eval->lowering_context),
      ps_lowering_record_layouts(eval->lowering_context), target_type.type_id,
      ag_target_info_data_layout(ps_lowering_target(eval->lowering_context)));
  int bits = byte_width * 8;
  if (bits <= 0 || bits >= 64) return value;
  uint64_t mask = (UINT64_C(1) << bits) - 1;
  uint64_t normalized = (uint64_t)value & mask;
  if (!target.is_unsigned &&
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

static global_var_t *find_global_named(
    const static_hir_eval_t *eval, const char *name, int name_len) {
  if (!eval || !eval->global_registry || !name || name_len <= 0)
    return NULL;
  psx_scope_graph_t *scope_graph =
      ps_global_registry_scope_graph(eval->global_registry);
  const psx_scope_declaration_t *declaration =
      psx_scope_graph_lookup_declaration_in_scope(
          scope_graph, PSX_SCOPE_ID_TRANSLATION_UNIT,
          PSX_NAMESPACE_ORDINARY, name, name_len);
  return declaration && declaration->kind == PSX_DECL_GLOBAL_OBJECT
             ? declaration->payload
             : NULL;
}

static global_var_t *find_global(
    const static_hir_eval_t *eval, const psx_hir_node_t *node) {
  size_t name_length = 0;
  const char *name = psx_hir_node_name(node, &name_length);
  if (!eval || !eval->global_registry || !name ||
      name_length == 0 || name_length > INT32_MAX)
    return NULL;
  return find_global_named(eval, name, (int)name_length);
}

static int global_value_is_foldable_extension(
    const global_var_t *global) {
  return global &&
         (global->is_compound_literal ||
          (ps_gvar_decl_qual_type(global).qualifiers &
           PSX_TYPE_QUALIFIER_CONST) != 0);
}

static char *persist_symbol_name(
    const static_hir_eval_t *eval, const char *name, int name_len) {
  if (!eval || !name) return NULL;
  if (name_len == -1) {
    name_len = (int)strlen(name);
  } else {
    global_var_t *global = find_global_named(eval, name, name_len);
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
      psx_type_shape_t target = {0};
      psx_type_shape_t source = {0};
      int has_target = node_type_shape(eval, node, &target);
      int has_source = node_type_shape(eval, lhs, &source);
      long long value = has_source && type_uses_floating_value(&source)
                            ? (long long)eval_const_fp(eval, lhs, ok)
                            : eval_const_int(eval, lhs, ok);
      if (ok && !*ok) return 0;
      if (has_target && target.kind == PSX_TYPE_BOOL) return value != 0;
      if (has_target && target.kind == PSX_TYPE_INTEGER)
        return normalize_integer_cast(
            eval, value, psx_hir_node_qual_type(node));
      if (has_target && target.kind == PSX_TYPE_FLOAT)
        return (long long)eval_const_fp(eval, lhs, ok);
      return value;
    }
    case PSX_HIR_UNARY_PLUS:
      return eval_const_int(eval, lhs, ok);
    case PSX_HIR_NEGATE: {
      long long value = eval_const_int(eval, lhs, ok);
      return !ok || *ok ? -value : 0;
    }
    case PSX_HIR_LOGICAL_NOT: {
      psx_type_shape_t operand_type = {0};
      long long value =
          node_type_shape(eval, lhs, &operand_type) &&
                  type_uses_floating_value(&operand_type)
              ? eval_const_fp(eval, lhs, ok) != 0.0
              : eval_const_int(eval, lhs, ok) != 0;
      return !ok || *ok ? !value : 0;
    }
    case PSX_HIR_BITWISE_NOT: {
      long long value = eval_const_int(eval, lhs, ok);
      return !ok || *ok ? ~value : 0;
    }
    case PSX_HIR_GLOBAL: {
      global_var_t *global = find_global(eval, node);
      psx_type_shape_t type = {0};
      int has_type = global && type_shape(
          eval, ps_gvar_decl_type_id(global), &type);
      if (global_value_is_foldable_extension(global) &&
          global->has_init && !global->init_symbol &&
          !global->init_values && !global->init_fvalues && has_type &&
          type.kind != PSX_TYPE_ARRAY && type.kind != PSX_TYPE_FLOAT &&
          type.kind != PSX_TYPE_COMPLEX)
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
    case PSX_HIR_GT:
    case PSX_HIR_GE:
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
    case PSX_HIR_GT: return left > right;
    case PSX_HIR_GE: return left >= right;
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
    case PSX_HIR_NUMBER: {
      psx_type_shape_t number_type = {0};
      return node_type_shape(eval, node, &number_type) &&
                     type_uses_floating_value(&number_type)
                 ? psx_hir_node_floating_value(node)
                 : (double)psx_hir_node_integer_value(node);
    }
    case PSX_HIR_CAST: {
      psx_type_shape_t target = {0};
      if (node_type_shape(eval, node, &target) &&
          (target.kind == PSX_TYPE_BOOL ||
           target.kind == PSX_TYPE_INTEGER))
        return (double)eval_const_int(eval, node, ok);
      return eval_const_fp(eval, lhs, ok);
    }
    case PSX_HIR_UNARY_PLUS:
      return eval_const_fp(eval, lhs, ok);
    case PSX_HIR_NEGATE: {
      double value = eval_const_fp(eval, lhs, ok);
      return !ok || *ok ? -value : 0.0;
    }
    case PSX_HIR_LOGICAL_NOT:
      return (double)eval_const_int(eval, node, ok);
    case PSX_HIR_BITWISE_NOT:
      return (double)eval_const_int(eval, node, ok);
    case PSX_HIR_GLOBAL: {
      global_var_t *global = find_global(eval, node);
      psx_type_shape_t type = {0};
      if (global_value_is_foldable_extension(global) &&
          global->has_init && !global->init_symbol &&
          !global->init_values && !global->init_fvalues &&
          type_shape(eval, ps_gvar_decl_type_id(global), &type) &&
          type.kind == PSX_TYPE_FLOAT)
        return global->fval;
      if (ok) *ok = 0;
      return 0.0;
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
  return psx_qual_type_layout_sizeof(
      ps_lowering_semantic_types(eval->lowering_context),
      ps_lowering_record_layouts(eval->lowering_context), element,
      ag_target_info_data_layout(ps_lowering_target(eval->lowering_context)));
}

static int type_is_pointer_like(
    const static_hir_eval_t *eval, const psx_hir_node_t *node) {
  psx_type_shape_t type = {0};
  return node_type_shape(eval, node, &type) &&
         (type.kind == PSX_TYPE_POINTER || type.kind == PSX_TYPE_ARRAY);
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

typedef struct {
  static_hir_eval_t eval;
  global_var_t *global;
  psx_qual_type_t root_qual_type;
  psx_initializer_scalar_leaf_list_t leaves;
  psx_static_aggregate_initializer_failure_t failure;
} static_hir_aggregate_t;

static int aggregate_type_size(
    const static_hir_aggregate_t *aggregate,
    psx_type_id_t type_id) {
  return aggregate && type_id != PSX_TYPE_ID_INVALID
             ? psx_type_layout_sizeof(
                   ps_lowering_semantic_types(aggregate->eval.lowering_context),
                   ps_lowering_record_layouts(aggregate->eval.lowering_context),
                   type_id,
                   ag_target_info_data_layout(
                       ps_lowering_target(aggregate->eval.lowering_context)))
             : 0;
}

static const psx_record_decl_t *aggregate_record_decl(
    const static_hir_aggregate_t *aggregate,
    psx_type_id_t type_id) {
  psx_type_shape_t type = {0};
  if (!aggregate || !type_shape(&aggregate->eval, type_id, &type) ||
      (type.kind != PSX_TYPE_STRUCT && type.kind != PSX_TYPE_UNION))
    return NULL;
  return psx_record_decl_table_lookup(
      ps_lowering_record_decls(aggregate->eval.lowering_context),
      type.record_id);
}

static int aggregate_leaf_index_at_offset(
    const psx_initializer_scalar_leaf_list_t *leaves, int offset) {
  if (!leaves) return -1;
  for (int i = 0; i < leaves->count; i++) {
    if (leaves->items[i].relative_offset == offset) return i;
  }
  return -1;
}

static int aggregate_leaf_index_for_target(
    const psx_initializer_scalar_leaf_list_t *leaves,
    const psx_initializer_target_t *target) {
  if (!leaves || !target) return -1;
  if (target->member_ref.declaration) {
    for (int i = 0; i < leaves->count; i++) {
      if (leaves->items[i].relative_offset == target->relative_offset &&
          leaves->items[i].member_ref.record_id ==
              target->member_ref.record_id &&
          leaves->items[i].member_ref.member_index ==
              target->member_ref.member_index)
        return i;
    }
  }
  return aggregate_leaf_index_at_offset(
      leaves, target->relative_offset);
}

typedef struct {
  psx_type_id_t union_type_id;
  int relative_offset;
  int slot_begin;
  int slot_capacity;
  psx_initializer_scalar_leaf_list_t selected_leaves;
} static_hir_union_activation_t;

static int aggregate_target_matches_selected_member(
    const static_hir_aggregate_t *aggregate,
    psx_qual_type_t selected_type, int selected_offset,
    const psx_initializer_scalar_leaf_list_t *selected_leaves,
    const psx_initializer_target_t *target) {
  if (!aggregate || !selected_leaves || !target) return 0;
  if (selected_type.type_id == target->type_id &&
      selected_offset == target->relative_offset)
    return 1;
  for (int i = 0; i < selected_leaves->count; i++) {
    const psx_initializer_scalar_leaf_t *leaf =
        &selected_leaves->items[i];
    if (leaf->relative_offset != target->relative_offset ||
        leaf->qual_type.type_id != target->type_id)
      continue;
    const psx_record_member_decl_t *declaration =
        leaf->member_ref.declaration;
    int leaf_bit_width = declaration && declaration->bit_width > 0
                             ? declaration->bit_width : 0;
    if (target->bit_width > 0 &&
        (leaf_bit_width != target->bit_width ||
         leaf->member_ref.layout.bit_offset != target->bit_offset))
      continue;
    if (target->bit_width == 0 && leaf_bit_width > 0) continue;
    return 1;
  }
  return 0;
}

static int aggregate_make_union_activation(
    const static_hir_aggregate_t *aggregate, psx_qual_type_t union_type,
    int relative_offset, int member_index,
    const psx_initializer_target_t *target,
    static_hir_union_activation_t *activation) {
  psx_type_shape_t shape = {0};
  if (!aggregate || !activation ||
      !type_shape(&aggregate->eval, union_type.type_id, &shape) ||
      shape.kind != PSX_TYPE_UNION)
    return 0;
  const psx_semantic_type_table_t *semantic_types =
      ps_lowering_semantic_types(aggregate->eval.lowering_context);
  const psx_record_decl_table_t *record_decls =
      ps_lowering_record_decls(aggregate->eval.lowering_context);
  const psx_record_layout_table_t *record_layouts =
      ps_lowering_record_layouts(aggregate->eval.lowering_context);
  const ag_data_layout_t *data_layout =
      ps_lowering_data_layout(aggregate->eval.lowering_context);
  const psx_record_decl_t *record =
      psx_record_decl_table_lookup(record_decls, shape.record_id);
  const psx_record_layout_t *layout =
      psx_record_layout_table_lookup(record_layouts, shape.record_id,
                                     data_layout);
  if (!record || !layout || member_index < 0 ||
      member_index >= record->member_count)
    return 0;
  const psx_record_member_layout_t *selected_layout =
      psx_record_layout_member(layout, member_index);
  psx_qual_type_t selected_type =
      psx_semantic_type_table_record_member(
          semantic_types, union_type.type_id, member_index);
  if (!selected_layout || selected_type.type_id == PSX_TYPE_ID_INVALID)
    return 0;
  int selected_offset = relative_offset + selected_layout->offset;
  psx_initializer_scalar_leaf_list_t selected_leaves = {0};
  if (!psx_collect_initializer_scalar_leaves_with_records(
          semantic_types, record_decls, record_layouts, data_layout,
          selected_type, selected_offset, &selected_leaves))
    return 0;
  if (target &&
      !aggregate_target_matches_selected_member(
          aggregate, selected_type, selected_offset, &selected_leaves,
          target)) {
    psx_initializer_scalar_leaf_list_dispose(&selected_leaves);
    return 0;
  }
  int slot_begin =
      aggregate_leaf_index_at_offset(&aggregate->leaves, relative_offset);
  int slot_capacity = psx_initializer_flat_slot_count_with_records(
      semantic_types, record_decls, record_layouts, data_layout,
      union_type.type_id);
  if (slot_begin < 0 || slot_capacity <= 0 ||
      selected_leaves.count <= 0 ||
      selected_leaves.count > slot_capacity ||
      slot_begin + slot_capacity > aggregate->leaves.count) {
    psx_initializer_scalar_leaf_list_dispose(&selected_leaves);
    return 0;
  }
  *activation = (static_hir_union_activation_t){
      .union_type_id = union_type.type_id,
      .relative_offset = relative_offset,
      .slot_begin = slot_begin,
      .slot_capacity = slot_capacity,
      .selected_leaves = selected_leaves,
  };
  return 1;
}

static int aggregate_find_union_activation(
    const static_hir_aggregate_t *aggregate, psx_qual_type_t qual_type,
    int relative_offset, const psx_initializer_target_t *target, int depth,
    static_hir_union_activation_t *activation) {
  psx_type_shape_t shape = {0};
  if (!aggregate || !target || !activation || depth > 64 ||
      !type_shape(&aggregate->eval, qual_type.type_id, &shape))
    return 0;
  const psx_semantic_type_table_t *semantic_types =
      ps_lowering_semantic_types(aggregate->eval.lowering_context);
  const psx_record_decl_table_t *record_decls =
      ps_lowering_record_decls(aggregate->eval.lowering_context);
  const psx_record_layout_table_t *record_layouts =
      ps_lowering_record_layouts(aggregate->eval.lowering_context);
  const ag_data_layout_t *data_layout =
      ps_lowering_data_layout(aggregate->eval.lowering_context);
  if (shape.kind == PSX_TYPE_ARRAY) {
    psx_qual_type_t element =
        psx_semantic_type_table_base(semantic_types, qual_type.type_id);
    int element_size = aggregate_type_size(aggregate, element.type_id);
    if (element.type_id == PSX_TYPE_ID_INVALID || element_size <= 0)
      return 0;
    for (int i = 0; i < shape.array_len; i++) {
      if (aggregate_find_union_activation(
              aggregate, element, relative_offset + i * element_size,
              target, depth + 1, activation))
        return 1;
    }
    return 0;
  }
  if (shape.kind != PSX_TYPE_STRUCT && shape.kind != PSX_TYPE_UNION)
    return 0;
  const psx_record_decl_t *record =
      psx_record_decl_table_lookup(record_decls, shape.record_id);
  const psx_record_layout_t *layout = psx_record_layout_table_lookup(
      record_layouts, shape.record_id, data_layout);
  if (!record || !layout || record->member_count <= 0) return 0;

  /*
   * Search children first. Anonymous aggregate promotion can place several
   * unions at the same byte offset; the resolved entry describes the
   * innermost union that directly owns the selected member.
   */
  for (int i = 0; i < record->member_count; i++) {
    const psx_record_member_layout_t *member_layout =
        psx_record_layout_member(layout, i);
    psx_qual_type_t member_type =
        psx_semantic_type_table_record_member(
            semantic_types, qual_type.type_id, i);
    if (!member_layout || member_type.type_id == PSX_TYPE_ID_INVALID)
      continue;
    if (aggregate_find_union_activation(
            aggregate, member_type,
            relative_offset + member_layout->offset, target, depth + 1,
            activation))
      return 1;
  }

  if (shape.kind != PSX_TYPE_UNION ||
      relative_offset != target->union_relative_offset ||
      target->union_member_index < 0 ||
      target->union_member_index >= record->member_count)
    return 0;
  return aggregate_make_union_activation(
      aggregate, qual_type, relative_offset,
      target->union_member_index, target, activation);
}

static int aggregate_apply_union_activation(
    static_hir_aggregate_t *aggregate,
    static_hir_union_activation_t *activation, int member_index) {
  if (!aggregate || !activation || activation->slot_begin < 0 ||
      activation->slot_capacity <= 0 ||
      activation->selected_leaves.count <= 0)
    return 0;
  int previous_member = ps_gvar_union_init_slot_ordinal(
      aggregate->global, activation->slot_begin);
  if (previous_member != member_index) {
    for (int i = 0; i < activation->slot_capacity; i++)
      ps_gvar_init_slot_clear(
          aggregate->global, activation->slot_begin + i);
  }
  for (int i = 0; i < activation->slot_capacity; i++) {
    int slot = activation->slot_begin + i;
    if (i < activation->selected_leaves.count) {
      aggregate->leaves.items[slot] =
          activation->selected_leaves.items[i];
      ps_gvar_init_slot_set_offset(
          aggregate->global, slot,
          activation->selected_leaves.items[i].relative_offset);
    } else {
      int padding_offset =
          activation->selected_leaves
              .items[activation->selected_leaves.count - 1]
              .relative_offset;
      aggregate->leaves.items[slot] =
          (psx_initializer_scalar_leaf_t){
              .qual_type = {
                  PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
              .relative_offset = padding_offset,
              .string_array_type_id = PSX_TYPE_ID_INVALID,
          };
      /*
       * Keep unused capacity slots ordered before the next object. Some
       * aggregate walkers skip zero-filled union capacity by offset rather
       * than by the selected member's scalar count.
       */
      ps_gvar_init_slot_set_offset(
          aggregate->global, slot, padding_offset);
    }
  }
  ps_gvar_init_slot_set_ordinal(
      aggregate->global, activation->slot_begin, member_index);
  if (!ps_gvar_union_activation_set(
          aggregate->global, activation->union_type_id,
          activation->relative_offset, member_index)) {
    psx_initializer_scalar_leaf_list_dispose(
        &activation->selected_leaves);
    return 0;
  }
  psx_initializer_scalar_leaf_list_dispose(
      &activation->selected_leaves);
  return 1;
}

static int aggregate_prepare_union_target(
    static_hir_aggregate_t *aggregate,
    const psx_initializer_target_t *target) {
  if (!aggregate || !target || target->union_relative_offset < 0 ||
      target->union_member_index < 0)
    return 1;
  static_hir_union_activation_t activation = {0};
  if (!aggregate_find_union_activation(
          aggregate, aggregate->root_qual_type, 0, target, 0,
          &activation))
    return 1;
  return aggregate_apply_union_activation(
      aggregate, &activation, target->union_member_index);
}

static int aggregate_prepare_default_unions(
    static_hir_aggregate_t *aggregate, psx_qual_type_t qual_type,
    int relative_offset, int depth) {
  psx_type_shape_t shape = {0};
  if (!aggregate || depth > 64 ||
      !type_shape(&aggregate->eval, qual_type.type_id, &shape))
    return 0;
  const psx_semantic_type_table_t *semantic_types =
      ps_lowering_semantic_types(aggregate->eval.lowering_context);
  if (shape.kind == PSX_TYPE_ARRAY) {
    psx_qual_type_t element =
        psx_semantic_type_table_base(semantic_types, qual_type.type_id);
    int element_size = aggregate_type_size(aggregate, element.type_id);
    if (element.type_id == PSX_TYPE_ID_INVALID || element_size <= 0)
      return 0;
    for (int i = 0; i < shape.array_len; i++) {
      if (!aggregate_prepare_default_unions(
              aggregate, element, relative_offset + i * element_size,
              depth + 1))
        return 0;
    }
    return 1;
  }
  if (shape.kind != PSX_TYPE_STRUCT && shape.kind != PSX_TYPE_UNION)
    return 1;
  const psx_record_decl_table_t *record_decls =
      ps_lowering_record_decls(aggregate->eval.lowering_context);
  const psx_record_layout_table_t *record_layouts =
      ps_lowering_record_layouts(aggregate->eval.lowering_context);
  const ag_data_layout_t *data_layout =
      ps_lowering_data_layout(aggregate->eval.lowering_context);
  const psx_record_decl_t *record =
      psx_record_decl_table_lookup(record_decls, shape.record_id);
  const psx_record_layout_t *layout = psx_record_layout_table_lookup(
      record_layouts, shape.record_id, data_layout);
  if (!record || !layout || record->member_count <= 0) return 0;
  int first_member = 0;
  int member_count = record->member_count;
  if (shape.kind == PSX_TYPE_UNION) {
    static_hir_union_activation_t activation = {0};
    if (!aggregate_make_union_activation(
            aggregate, qual_type, relative_offset, first_member, NULL,
            &activation) ||
        !aggregate_apply_union_activation(
            aggregate, &activation, first_member))
      return 0;
    member_count = 1;
  }
  for (int i = first_member; i < member_count; i++) {
    const psx_record_member_layout_t *member_layout =
        psx_record_layout_member(layout, i);
    psx_qual_type_t member_type =
        psx_semantic_type_table_record_member(
            semantic_types, qual_type.type_id, i);
    if (!member_layout || member_type.type_id == PSX_TYPE_ID_INVALID ||
        !aggregate_prepare_default_unions(
            aggregate, member_type,
            relative_offset + member_layout->offset, depth + 1))
      return 0;
  }
  return 1;
}

static psx_initializer_target_t aggregate_resolved_entry_target(
    const static_hir_aggregate_t *aggregate,
    const psx_hir_node_t *entry) {
  psx_initializer_target_t target = {
      .type_id = PSX_TYPE_ID_INVALID,
      .union_relative_offset = -1,
      .union_member_index = -1,
  };
  if (!aggregate || !entry ||
      !psx_hir_node_is_resolved_initializer_entry(entry))
    return target;
  target.type_id = psx_hir_node_attached_qual_type(entry).type_id;
  target.relative_offset = psx_hir_node_object_offset(entry);
  if (target.type_id == PSX_TYPE_ID_INVALID ||
      target.relative_offset < 0)
    return (psx_initializer_target_t){0};
  (void)psx_hir_node_initializer_union_member(
      entry, &target.union_relative_offset,
      &target.union_member_index);

  int bit_width = 0;
  int bit_offset = 0;
  int bit_is_signed = 0;
  int has_bitfield = psx_hir_node_bitfield_info(
      entry, &bit_width, &bit_offset, &bit_is_signed);
  (void)bit_is_signed;
  if (has_bitfield) {
    target.bit_width = (unsigned char)bit_width;
    target.bit_offset = (unsigned char)bit_offset;
  }
  for (int i = 0; i < aggregate->leaves.count; i++) {
    const psx_initializer_scalar_leaf_t *leaf =
        &aggregate->leaves.items[i];
    if (leaf->relative_offset != target.relative_offset ||
        leaf->qual_type.type_id != target.type_id)
      continue;
    const psx_record_member_decl_t *declaration =
        leaf->member_ref.declaration;
    int leaf_bit_width = declaration && declaration->bit_width > 0
                             ? declaration->bit_width : 0;
    if (has_bitfield &&
        (leaf_bit_width != bit_width ||
         leaf->member_ref.layout.bit_offset != bit_offset))
      continue;
    if (!has_bitfield && leaf_bit_width > 0) continue;
    target.member_ref = leaf->member_ref;
    return target;
  }
  if (target.union_member_index >= 0) {
    target.member_ref = (psx_initializer_member_ref_t){0};
    return target;
  }
  return (psx_initializer_target_t){0};
}

static void aggregate_mark_union_target(
    static_hir_aggregate_t *aggregate,
    const psx_initializer_target_t *target) {
  if (!aggregate || !target || target->union_relative_offset < 0 ||
      target->union_member_index < 0)
    return;
  int index = aggregate_leaf_index_at_offset(
      &aggregate->leaves, target->union_relative_offset);
  if (index >= 0)
    ps_gvar_init_slot_set_ordinal(
        aggregate->global, index, target->union_member_index);
}

static int aggregate_is_character_array_for_string(
    const static_hir_aggregate_t *aggregate,
    psx_type_id_t type_id, const psx_hir_node_t *string) {
  psx_type_shape_t type = {0};
  if (!aggregate || !string ||
      !type_shape(&aggregate->eval, type_id, &type) ||
      type.kind != PSX_TYPE_ARRAY)
    return 0;
  psx_type_id_t element_id = psx_semantic_type_table_array_leaf(
      ps_lowering_semantic_types(aggregate->eval.lowering_context),
      type_id).type_id;
  psx_type_shape_t element = {0};
  int width = psx_hir_node_object_align(string);
  if (width <= 0) width = 1;
  return type_shape(&aggregate->eval, element_id, &element) &&
         element.kind != PSX_TYPE_POINTER &&
         element.kind != PSX_TYPE_FUNCTION &&
         element.kind != PSX_TYPE_STRUCT &&
         element.kind != PSX_TYPE_UNION &&
         aggregate_type_size(aggregate, element_id) == width;
}

static int aggregate_write_string(
    static_hir_aggregate_t *aggregate,
    psx_type_id_t array_type_id, int relative_offset,
    const psx_hir_node_t *string) {
  psx_type_id_t element_id = psx_semantic_type_table_array_leaf(
      ps_lowering_semantic_types(aggregate->eval.lowering_context),
      array_type_id).type_id;
  int element_size = aggregate_type_size(aggregate, element_id);
  int total_size = aggregate_type_size(aggregate, array_type_id);
  int capacity = element_size > 0 ? total_size / element_size : 0;
  int start = aggregate_leaf_index_at_offset(
      &aggregate->leaves, relative_offset);
  int character_width = psx_hir_node_object_align(string);
  size_t literal_length = 0;
  const char *literal = psx_hir_node_literal_contents(
      string, &literal_length);
  if (element_id == PSX_TYPE_ID_INVALID || capacity <= 0 || start < 0 ||
      character_width <= 0 || element_size != character_width || !literal)
    return 0;
  ps_gvar_init_slots_write_string_units(
      aggregate->global, start, literal, (int)literal_length,
      element_size, capacity);
  return 1;
}

static int aggregate_write_scalar(
    static_hir_aggregate_t *aggregate,
    const psx_initializer_target_t *target,
    const psx_hir_node_t *value) {
  int index = aggregate_leaf_index_for_target(
      &aggregate->leaves, target);
  if (target && target->bit_width > 0)
    index = aggregate_leaf_index_at_offset(
        &aggregate->leaves, target->relative_offset);
  psx_type_shape_t target_type = {0};
  if (index < 0 || index >= aggregate->leaves.count || !value ||
      !type_shape(&aggregate->eval, target->type_id, &target_type))
    return 0;
  const char *symbol = NULL;
  int symbol_len = 0;
  long long integer = 0;
  double floating = 0.0;
  long long offset = 0;
  if (target_type.kind == PSX_TYPE_POINTER ||
      target_type.kind == PSX_TYPE_FUNCTION) {
    int integer_ok = 1;
    integer = eval_const_int(&aggregate->eval, value, &integer_ok);
    if (integer_ok && integer == 0) {
      symbol = NULL;
    } else if (resolve_address(
                   &aggregate->eval, value, &symbol, &symbol_len,
                   &offset)) {
      integer = offset;
      symbol = persist_symbol_name(
          &aggregate->eval, symbol, symbol_len);
      if (!symbol) return 0;
    } else {
      aggregate->failure =
          PSX_STATIC_AGGREGATE_INITIALIZER_FAILURE_NON_CONSTANT;
      return 0;
    }
  } else if (target_type.kind == PSX_TYPE_FLOAT) {
    int ok = 1;
    floating = eval_const_fp(&aggregate->eval, value, &ok);
    if (!ok) {
      aggregate->failure =
          PSX_STATIC_AGGREGATE_INITIALIZER_FAILURE_NON_CONSTANT;
      return 0;
    }
  } else {
    int ok = 1;
    integer = eval_const_int(&aggregate->eval, value, &ok);
    if (!ok) {
      aggregate->failure =
          PSX_STATIC_AGGREGATE_INITIALIZER_FAILURE_NON_CONSTANT;
      return 0;
    }
    if (target_type.kind == PSX_TYPE_BOOL) integer = integer != 0;
  }
  if (target->bit_width > 0) {
    unsigned int width = target->bit_width;
    unsigned int offset = target->bit_offset;
    unsigned long long mask =
        width >= 64 ? ~0ULL : ((1ULL << width) - 1ULL);
    unsigned long long unit_mask =
        offset >= 64 ? 0 : mask << offset;
    unsigned long long packed =
        (unsigned long long)aggregate->global->init_values[index];
    unsigned long long shifted =
        offset >= 64 ? 0 : ((unsigned long long)integer & mask) << offset;
    packed = (packed & ~unit_mask) |
             shifted;
    ps_gvar_init_slot_write(
        aggregate->global, index, (long long)packed, 0.0, NULL, 0);
  } else {
    ps_gvar_init_slot_write(
        aggregate->global, index, integer, floating,
        (char *)symbol, symbol_len);
  }
  if (!symbol && target_type.kind == PSX_TYPE_FLOAT &&
      target->union_member_index >= 0) {
    tk_float_kind_t floating_kind =
        target_type.floating_kind == PSX_FLOATING_KIND_FLOAT
            ? TK_FLOAT_KIND_FLOAT
            : target_type.floating_kind == PSX_FLOATING_KIND_LONG_DOUBLE
                  ? TK_FLOAT_KIND_LONG_DOUBLE
                  : TK_FLOAT_KIND_DOUBLE;
    ps_gvar_init_slot_write_fp_sentinel(
        aggregate->global, index, floating_kind,
        aggregate_type_size(aggregate, target->type_id));
  }
  return 1;
}

static int aggregate_lower_list(
    static_hir_aggregate_t *aggregate,
    psx_type_id_t context_type_id, int context_offset,
    const psx_hir_node_t *list);

static int aggregate_lower_target_value(
    static_hir_aggregate_t *aggregate,
    psx_initializer_target_t *target,
    const psx_hir_node_t *value, int cursor) {
  psx_type_shape_t target_type = {0};
  if (!aggregate || !target || !value ||
      !type_shape(&aggregate->eval, target->type_id, &target_type))
    return 0;
  aggregate_mark_union_target(aggregate, target);
  if (psx_hir_node_kind(value) == PSX_HIR_INITIALIZER_LIST)
    return aggregate_lower_list(
        aggregate, target->type_id, target->relative_offset, value);
  if (psx_hir_node_kind(value) == PSX_HIR_STRING &&
      (aggregate_is_character_array_for_string(
           aggregate, target->type_id, value) ||
       (cursor >= 0 && cursor < aggregate->leaves.count &&
        aggregate_is_character_array_for_string(
            aggregate,
            aggregate->leaves.items[cursor].string_array_type_id,
            value)))) {
    psx_type_id_t string_type_id = target->type_id;
    int string_offset = target->relative_offset;
    if (target_type.kind != PSX_TYPE_ARRAY && cursor >= 0 &&
        cursor < aggregate->leaves.count) {
      const psx_initializer_scalar_leaf_t *leaf =
          &aggregate->leaves.items[cursor];
      if (leaf->string_array_type_id != PSX_TYPE_ID_INVALID) {
        string_type_id = leaf->string_array_type_id;
        string_offset = leaf->string_array_offset;
        target->type_id = leaf->string_array_type_id;
        target->relative_offset = string_offset;
      }
    }
    return aggregate_write_string(
        aggregate, string_type_id, string_offset, value);
  }
  return aggregate_write_scalar(aggregate, target, value);
}

static int aggregate_lower_list(
    static_hir_aggregate_t *aggregate,
    psx_type_id_t context_type_id, int context_offset,
    const psx_hir_node_t *list) {
  (void)context_offset;
  if (!aggregate || !list ||
      psx_hir_node_kind(list) != PSX_HIR_INITIALIZER_LIST ||
      psx_hir_node_attached_qual_type(list).type_id !=
          context_type_id)
    return 0;
  for (size_t ordinal = 0;; ordinal++) {
    const psx_hir_node_t *entry = child_for_edge(
        &aggregate->eval, list,
        PSX_HIR_EDGE_INITIALIZER_ENTRY, ordinal);
    if (!entry) break;
    if (!psx_hir_node_is_resolved_initializer_entry(entry))
      return 0;
    const psx_hir_node_t *value = child_for_edge(
        &aggregate->eval, entry,
        PSX_HIR_EDGE_INITIALIZER_VALUE, 0);
    if (!value) return 0;
    psx_initializer_target_t target =
        aggregate_resolved_entry_target(aggregate, entry);
    if (target.type_id != PSX_TYPE_ID_INVALID &&
        !aggregate_prepare_union_target(aggregate, &target))
      return 0;
    target = aggregate_resolved_entry_target(aggregate, entry);
    int target_cursor = aggregate_leaf_index_for_target(
        &aggregate->leaves, &target);
    if (target.type_id == PSX_TYPE_ID_INVALID || target_cursor < 0 ||
        !aggregate_lower_target_value(
            aggregate, &target, value, target_cursor))
      return 0;
  }
  return 1;
}

static int aggregate_type_contains_float(
    const static_hir_aggregate_t *aggregate,
    psx_type_id_t type_id) {
  psx_type_shape_t type = {0};
  if (!aggregate || !type_shape(&aggregate->eval, type_id, &type)) return 0;
  if (type.kind == PSX_TYPE_FLOAT) return 1;
  if (type.kind == PSX_TYPE_POINTER || type.kind == PSX_TYPE_FUNCTION)
    return 0;
  if (type.kind == PSX_TYPE_ARRAY)
    return aggregate_type_contains_float(
        aggregate, psx_semantic_type_table_base(
                       ps_lowering_semantic_types(
                           aggregate->eval.lowering_context),
                       type_id).type_id);
  const psx_record_decl_t *record = aggregate_record_decl(
      aggregate, type_id);
  if (record) {
    for (int i = 0; i < record->member_count; i++) {
      if (aggregate_type_contains_float(
              aggregate, psx_semantic_type_table_record_member(
                  ps_lowering_semantic_types(
                      aggregate->eval.lowering_context),
                  type_id, i).type_id))
        return 1;
    }
  }
  return 0;
}

int psx_build_static_aggregate_hir_initializer_plan(
    psx_global_registry_t *global_registry,
    psx_lowering_context_t *lowering_context,
    psx_type_id_t type_id, const psx_hir_module_t *hir,
    psx_hir_node_id_t root, token_t *fallback_tok,
    psx_static_aggregate_initializer_plan_t *plan) {
  (void)fallback_tok;
  if (plan) *plan = (psx_static_aggregate_initializer_plan_t){0};
  psx_type_shape_t type = {0};
  if (!global_registry || !lowering_context || !hir ||
      root == PSX_HIR_NODE_ID_INVALID || !plan ||
      !psx_semantic_type_table_describe(
          ps_lowering_semantic_types(lowering_context), type_id, &type) ||
      (type.kind != PSX_TYPE_ARRAY && type.kind != PSX_TYPE_STRUCT &&
       type.kind != PSX_TYPE_UNION))
    return 0;
  global_var_t temporary = {0};
  static_hir_aggregate_t aggregate = {
      .eval = {
          .global_registry = global_registry,
          .lowering_context = lowering_context,
          .hir = hir,
      },
      .global = &temporary,
      .root_qual_type = {
          type_id, PSX_TYPE_QUALIFIER_NONE},
  };
  const psx_hir_node_t *initializer = node_for_id(&aggregate.eval, root);
  if (!initializer ||
      psx_hir_node_kind(initializer) != PSX_HIR_INITIALIZER_LIST ||
      !ps_global_registry_bind_decl_qual_type(
          global_registry, &temporary,
          (psx_qual_type_t){type_id, PSX_TYPE_QUALIFIER_NONE}) ||
      !psx_collect_initializer_scalar_leaves_with_records(
          ps_lowering_semantic_types(lowering_context),
          ps_lowering_record_decls(lowering_context),
          ps_lowering_record_layouts(lowering_context),
          ps_lowering_data_layout(lowering_context),
          ps_gvar_decl_qual_type(&temporary), 0, &aggregate.leaves) ||
      aggregate.leaves.count <= 0) {
    psx_initializer_scalar_leaf_list_dispose(&aggregate.leaves);
    return 0;
  }
  ps_gvar_init_slots_alloc(
      &temporary, aggregate.leaves.count,
      aggregate_type_contains_float(&aggregate, type_id));
  temporary.init_count = aggregate.leaves.count;
  for (int i = 0; i < aggregate.leaves.count; i++) {
    ps_gvar_init_slot_clear(&temporary, i);
    ps_gvar_init_slot_set_offset(
        &temporary, i, aggregate.leaves.items[i].relative_offset);
  }
  int lowered = aggregate_prepare_default_unions(
                    &aggregate, aggregate.root_qual_type, 0, 0) &&
                aggregate_lower_list(
                    &aggregate, type_id, 0, initializer);
  psx_initializer_scalar_leaf_list_dispose(&aggregate.leaves);
  if (!lowered) {
    plan->failure = aggregate.failure;
    return 0;
  }
  *plan = (psx_static_aggregate_initializer_plan_t){
      .values = temporary.init_values,
      .floating_values = temporary.init_fvalues,
      .symbols = temporary.init_value_symbols,
      .symbol_lengths = temporary.init_value_symbol_lens,
      .union_ordinals = temporary.init_union_ordinals,
      .offsets = temporary.init_offsets,
      .union_activations = temporary.init_union_activations,
      .value_count = temporary.init_count,
      .union_activation_count =
          temporary.init_union_activation_count,
      .union_activation_capacity =
          temporary.init_union_activation_capacity,
      .union_ordinal = temporary.union_init_ordinal,
  };
  return plan->value_count > 0;
}

static int lower_string(
    const static_hir_eval_t *eval, global_var_t *global,
    psx_type_id_t type_id, const psx_hir_node_t *node) {
  psx_type_shape_t type = {0};
  if (!type_shape(eval, type_id, &type)) return 0;
  size_t name_length = 0;
  const char *name = psx_hir_node_name(node, &name_length);
  if (type.kind == PSX_TYPE_POINTER) {
    if (!name || name_length == 0) return 0;
    global->init_symbol = persist_symbol_name(eval, name, -1);
    if (!global->init_symbol) return 0;
    global->init_symbol_len = -1;
    return 1;
  }
  if (type.kind != PSX_TYPE_ARRAY) return 0;
  psx_type_id_t element_type_id = psx_semantic_type_table_base(
      ps_lowering_semantic_types(eval->lowering_context),
      type_id).type_id;
  int element_size = psx_type_layout_sizeof(
      ps_lowering_semantic_types(eval->lowering_context),
      ps_lowering_record_layouts(eval->lowering_context), element_type_id,
      ag_target_info_data_layout(ps_lowering_target(eval->lowering_context)));
  int character_width = psx_hir_node_object_align(node);
  if (element_type_id == PSX_TYPE_ID_INVALID || element_size <= 0 ||
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
    global_var_t *global, psx_type_id_t type_id,
    const psx_hir_module_t *hir, psx_hir_node_id_t root) {
  psx_type_shape_t type = {0};
  if (!global_registry || !lowering_context || !global || !hir ||
      root == PSX_HIR_NODE_ID_INVALID ||
      !psx_semantic_type_table_describe(
          ps_lowering_semantic_types(lowering_context), type_id, &type))
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
    return lower_string(&eval, global, type_id, initializer);

  int integer_ok = 1;
  long long integer = eval_const_int(&eval, initializer, &integer_ok);
  if (type.kind == PSX_TYPE_FLOAT || type.kind == PSX_TYPE_COMPLEX) {
    int floating_ok = 1;
    double floating = eval_const_fp(&eval, initializer, &floating_ok);
    if (floating_ok) {
      global->fval = floating;
      return 1;
    }
  }
  if (integer_ok) {
    global->init_val = type.kind == PSX_TYPE_BOOL ? integer != 0 : integer;
    return 1;
  }

  if (type.kind != PSX_TYPE_POINTER) return 0;

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
