#include "static_hir_initializer.h"

#include "runtime_context.h"
#include "../parser/global_registry.h"
#include "../parser/gvar_public.h"
#include "../parser/symtab.h"
#include "../parser/type.h"
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

typedef struct {
  static_hir_eval_t eval;
  global_var_t *global;
  psx_initializer_scalar_leaf_list_t leaves;
} static_hir_aggregate_t;

static int aggregate_type_size(
    const static_hir_aggregate_t *aggregate,
    const psx_type_t *type) {
  return aggregate && type
             ? ps_type_sizeof_id_with_records(
                   ps_lowering_semantic_types(
                       aggregate->eval.lowering_context),
                   ps_lowering_record_layouts(
                       aggregate->eval.lowering_context),
                   ps_lowering_type_id(
                       aggregate->eval.lowering_context, type),
                   ps_lowering_target(
                       aggregate->eval.lowering_context))
             : 0;
}

static const psx_record_decl_t *aggregate_record_decl(
    const static_hir_aggregate_t *aggregate,
    const psx_type_t *type) {
  return aggregate && type && ps_type_is_tag_aggregate(type)
             ? psx_record_decl_table_lookup(
                   ps_lowering_record_decls(
                       aggregate->eval.lowering_context),
                   ps_type_record_id(type))
             : NULL;
}

static const psx_record_member_layout_t *aggregate_member_layout(
    const static_hir_aggregate_t *aggregate,
    const psx_type_t *type, int member_index) {
  if (!aggregate || !type || member_index < 0) return NULL;
  const psx_record_layout_t *layout = psx_record_layout_table_lookup(
      ps_lowering_record_layouts(aggregate->eval.lowering_context),
      ps_type_record_id(type),
      ps_lowering_target(aggregate->eval.lowering_context));
  return psx_record_layout_member(layout, member_index);
}

static psx_initializer_member_ref_t aggregate_member_ref(
    const static_hir_aggregate_t *aggregate,
    const psx_type_t *type, int member_index,
    const psx_record_member_decl_t *declaration) {
  psx_initializer_member_ref_t ref = {
      .declaration = declaration,
      .record_id = ps_type_record_id(type),
      .member_index = member_index,
  };
  const psx_record_member_layout_t *layout = aggregate_member_layout(
      aggregate, type, member_index);
  if (layout) ref.layout = *layout;
  return ref;
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

static psx_initializer_target_t aggregate_positional_target(
    const static_hir_aggregate_t *aggregate,
    const psx_type_t *context_type, int context_offset,
    int cursor, int preserve_subobject) {
  psx_initializer_target_t target = {
      .first_array_index = -1,
      .first_member_index = -1,
      .union_relative_offset = -1,
      .union_member_index = -1,
  };
  if (!aggregate || !context_type || cursor < 0 ||
      cursor >= aggregate->leaves.count)
    return target;
  const psx_initializer_scalar_leaf_t *leaf =
      &aggregate->leaves.items[cursor];
  target.type = leaf->type;
  target.type_id = leaf->type_id;
  target.relative_offset = leaf->relative_offset;
  target.member_ref = leaf->member_ref;
  const psx_record_decl_t *record = aggregate_record_decl(
      aggregate, context_type);
  if (context_type->kind == PSX_TYPE_UNION &&
      record && record->member_count > 0) {
    const psx_record_member_decl_t *member = &record->members[0];
    if (preserve_subobject) {
      const psx_record_member_layout_t *layout =
          aggregate_member_layout(aggregate, context_type, 0);
      if (!layout) return (psx_initializer_target_t){0};
      target.type = psx_record_member_decl_type(member);
      target.type_id = ps_lowering_type_id(
          aggregate->eval.lowering_context, target.type);
      target.relative_offset = context_offset + layout->offset;
      target.member_ref = aggregate_member_ref(
          aggregate, context_type, 0, member);
    }
    target.first_member_index = 0;
    target.union_relative_offset = context_offset;
    target.union_member_index = 0;
    return target;
  }
  if (!preserve_subobject) return target;
  if (context_type->kind == PSX_TYPE_STRUCT && record) {
    for (int i = 0; i < record->member_count; i++) {
      const psx_record_member_decl_t *member = &record->members[i];
      const psx_type_t *member_type = psx_record_member_decl_type(member);
      const psx_record_member_layout_t *layout =
          aggregate_member_layout(aggregate, context_type, i);
      if (!layout || !member_type ||
          context_offset + layout->offset != leaf->relative_offset)
        continue;
      if (member_type->kind != PSX_TYPE_ARRAY &&
          !ps_type_is_tag_aggregate(member_type))
        continue;
      target.type = member_type;
      target.type_id = ps_lowering_type_id(
          aggregate->eval.lowering_context, member_type);
      target.relative_offset = context_offset + layout->offset;
      target.member_ref = aggregate_member_ref(
          aggregate, context_type, i, member);
      target.first_member_index = i;
      return target;
    }
    return target;
  }
  if (context_type->kind != PSX_TYPE_ARRAY || !context_type->base)
    return target;
  int child_size = aggregate_type_size(aggregate, context_type->base);
  if (child_size <= 0 || leaf->relative_offset < context_offset)
    return target;
  int child_index = (leaf->relative_offset - context_offset) / child_size;
  int child_offset = context_offset + child_index * child_size;
  if (child_index < 0 || child_index >= context_type->array_len ||
      child_offset != leaf->relative_offset)
    return target;
  target.type = context_type->base;
  target.type_id = ps_lowering_type_id(
      aggregate->eval.lowering_context, target.type);
  target.relative_offset = child_offset;
  target.member_ref = (psx_initializer_member_ref_t){0};
  target.first_array_index = child_index;
  return target;
}

static int aggregate_member_index(
    const psx_record_decl_t *record,
    const psx_hir_node_t *designator) {
  size_t name_length = 0;
  const char *name = psx_hir_node_name(designator, &name_length);
  if (!record || !name || name_length == 0 || name_length > INT32_MAX)
    return -1;
  for (int i = 0; i < record->member_count; i++) {
    const psx_record_member_decl_t *member = &record->members[i];
    if (member->len == (int)name_length && member->name &&
        memcmp(member->name, name, name_length) == 0)
      return i;
  }
  return -1;
}

static psx_initializer_target_t aggregate_designated_target(
    const static_hir_aggregate_t *aggregate,
    const psx_hir_node_t *entry,
    const psx_type_t *context_type, int context_offset) {
  psx_initializer_target_t target = {
      .type = context_type,
      .type_id = ps_lowering_type_id(
          aggregate->eval.lowering_context, context_type),
      .relative_offset = context_offset,
      .first_array_index = -1,
      .first_member_index = -1,
      .union_relative_offset = -1,
      .union_member_index = -1,
  };
  for (size_t ordinal = 0;; ordinal++) {
    const psx_hir_node_t *designator = child_for_edge(
        &aggregate->eval, entry, PSX_HIR_EDGE_DESIGNATOR, ordinal);
    if (!designator) break;
    if (psx_hir_node_kind(designator) == PSX_HIR_INDEX_DESIGNATOR) {
      if (!target.type || target.type->kind != PSX_TYPE_ARRAY ||
          !target.type->base)
        return (psx_initializer_target_t){0};
      const psx_hir_node_t *index_node = child_for_edge(
          &aggregate->eval, designator,
          PSX_HIR_EDGE_DESIGNATOR_INDEX, 0);
      int ok = 1;
      long long index = eval_const_int(&aggregate->eval, index_node, &ok);
      if (!ok || index < 0 || index >= target.type->array_len)
        return (psx_initializer_target_t){0};
      if (target.first_array_index < 0)
        target.first_array_index = (int)index;
      psx_qual_type_t element = psx_semantic_type_table_base(
          ps_lowering_semantic_types(aggregate->eval.lowering_context),
          target.type_id);
      int element_size = ps_type_sizeof_id_with_records(
          ps_lowering_semantic_types(aggregate->eval.lowering_context),
          ps_lowering_record_layouts(aggregate->eval.lowering_context),
          element.type_id,
          ps_lowering_target(aggregate->eval.lowering_context));
      if (element.type_id == PSX_TYPE_ID_INVALID || element_size <= 0)
        return (psx_initializer_target_t){0};
      target.relative_offset += (int)index * element_size;
      target.type_id = element.type_id;
      target.type = psx_semantic_type_table_lookup(
          ps_lowering_semantic_types(aggregate->eval.lowering_context),
          element.type_id);
      target.member_ref = (psx_initializer_member_ref_t){0};
      continue;
    }
    if (psx_hir_node_kind(designator) !=
        PSX_HIR_MEMBER_DESIGNATOR)
      return (psx_initializer_target_t){0};
    const psx_type_t *aggregate_type = target.type;
    const psx_record_decl_t *record = aggregate_record_decl(
        aggregate, aggregate_type);
    int member_index = aggregate_member_index(record, designator);
    const psx_record_member_layout_t *layout = aggregate_member_layout(
        aggregate, aggregate_type, member_index);
    if (!record || member_index < 0 || !layout)
      return (psx_initializer_target_t){0};
    if (target.first_member_index < 0)
      target.first_member_index = member_index;
    if (aggregate_type->kind == PSX_TYPE_UNION) {
      target.union_relative_offset = target.relative_offset;
      target.union_member_index = member_index;
    }
    const psx_record_member_decl_t *member =
        &record->members[member_index];
    target.relative_offset += layout->offset;
    target.type_id = psx_semantic_type_table_record_member(
        ps_lowering_semantic_types(aggregate->eval.lowering_context),
        target.type_id, member_index).type_id;
    target.type = psx_record_member_decl_type(member);
    target.member_ref = aggregate_member_ref(
        aggregate, aggregate_type, member_index, member);
  }
  return target;
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
    const psx_type_t *type, const psx_hir_node_t *string) {
  if (!aggregate || !type || type->kind != PSX_TYPE_ARRAY || !string)
    return 0;
  const psx_type_t *element = ps_type_array_leaf_type(type);
  int width = psx_hir_node_object_align(string);
  if (width <= 0) width = 1;
  return element && element->kind != PSX_TYPE_POINTER &&
         element->kind != PSX_TYPE_FUNCTION &&
         !ps_type_is_tag_aggregate(element) &&
         aggregate_type_size(aggregate, element) == width;
}

static int aggregate_write_string(
    static_hir_aggregate_t *aggregate,
    const psx_type_t *array_type, int relative_offset,
    const psx_hir_node_t *string) {
  const psx_type_t *element = ps_type_array_leaf_type(array_type);
  int element_size = aggregate_type_size(aggregate, element);
  int total_size = aggregate_type_size(aggregate, array_type);
  int capacity = element_size > 0 ? total_size / element_size : 0;
  int start = aggregate_leaf_index_at_offset(
      &aggregate->leaves, relative_offset);
  int character_width = psx_hir_node_object_align(string);
  size_t literal_length = 0;
  const char *literal = psx_hir_node_literal_contents(
      string, &literal_length);
  if (!element || capacity <= 0 || start < 0 ||
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
  if (index < 0 || index >= aggregate->leaves.count ||
      !target->type || !value)
    return 0;
  const char *symbol = NULL;
  int symbol_len = 0;
  long long integer = 0;
  double floating = 0.0;
  long long offset = 0;
  if (target->type->kind == PSX_TYPE_POINTER ||
      target->type->kind == PSX_TYPE_FUNCTION) {
    if (resolve_address(
            &aggregate->eval, value, &symbol, &symbol_len, &offset)) {
      integer = offset;
      symbol = persist_symbol_name(
          &aggregate->eval, symbol, symbol_len);
      if (!symbol) return 0;
    }
  } else if (target->type->kind == PSX_TYPE_FLOAT) {
    int ok = 1;
    floating = eval_const_fp(&aggregate->eval, value, &ok);
    if (!ok) floating = 0.0;
  } else {
    int ok = 1;
    integer = eval_const_int(&aggregate->eval, value, &ok);
    if (!ok) integer = 0;
    if (target->type->kind == PSX_TYPE_BOOL) integer = integer != 0;
  }
  ps_gvar_init_slot_write(
      aggregate->global, index, integer, floating,
      (char *)symbol, symbol_len);
  if (!symbol && target->type->kind == PSX_TYPE_FLOAT &&
      target->union_member_index >= 0) {
    ps_gvar_init_slot_write_fp_sentinel(
        aggregate->global, index,
        ps_type_floating_token_kind(target->type),
        aggregate_type_size(aggregate, target->type));
  }
  return 1;
}

static int aggregate_lower_list(
    static_hir_aggregate_t *aggregate,
    const psx_type_t *context_type, int context_offset,
    const psx_hir_node_t *list) {
  if (!aggregate || !context_type || !list ||
      psx_hir_node_kind(list) != PSX_HIR_INITIALIZER_LIST)
    return 0;
  int cursor = aggregate_leaf_index_at_offset(
      &aggregate->leaves, context_offset);
  if (cursor < 0) cursor = 0;
  for (size_t ordinal = 0;; ordinal++) {
    const psx_hir_node_t *entry = child_for_edge(
        &aggregate->eval, list,
        PSX_HIR_EDGE_INITIALIZER_ENTRY, ordinal);
    if (!entry) break;
    const psx_hir_node_t *value = child_for_edge(
        &aggregate->eval, entry,
        PSX_HIR_EDGE_INITIALIZER_VALUE, 0);
    if (!value) return 0;
    int has_designator = child_for_edge(
        &aggregate->eval, entry, PSX_HIR_EDGE_DESIGNATOR, 0) != NULL;
    psx_initializer_target_t target = has_designator
        ? aggregate_designated_target(
              aggregate, entry, context_type, context_offset)
        : aggregate_positional_target(
              aggregate, context_type, context_offset, cursor,
              psx_hir_node_kind(value) == PSX_HIR_INITIALIZER_LIST);
    if (!target.type) return 0;
    aggregate_mark_union_target(aggregate, &target);
    if (psx_hir_node_kind(value) == PSX_HIR_INITIALIZER_LIST) {
      if (!aggregate_lower_list(
              aggregate, target.type, target.relative_offset, value))
        return 0;
    } else if (psx_hir_node_kind(value) == PSX_HIR_STRING &&
               (aggregate_is_character_array_for_string(
                    aggregate, target.type, value) ||
                (cursor >= 0 && cursor < aggregate->leaves.count &&
                 aggregate_is_character_array_for_string(
                     aggregate,
                     aggregate->leaves.items[cursor].string_array_type,
                     value)))) {
      const psx_type_t *string_type = target.type;
      int string_offset = target.relative_offset;
      if (string_type->kind != PSX_TYPE_ARRAY && cursor >= 0 &&
          cursor < aggregate->leaves.count) {
        const psx_initializer_scalar_leaf_t *leaf =
            &aggregate->leaves.items[cursor];
        if (leaf->string_array_type) {
          string_type = leaf->string_array_type;
          string_offset = leaf->string_array_offset;
          target.type = string_type;
          target.type_id = leaf->string_array_type_id;
          target.relative_offset = string_offset;
        }
      }
      if (!aggregate_write_string(
              aggregate, string_type, string_offset, value))
        return 0;
    } else if (!aggregate_write_scalar(aggregate, &target, value)) {
      return 0;
    }
    cursor = psx_initializer_leaf_cursor_after_target_with_records(
        ps_lowering_semantic_types(aggregate->eval.lowering_context),
        ps_lowering_record_layouts(aggregate->eval.lowering_context),
        ps_lowering_target(aggregate->eval.lowering_context),
        &aggregate->leaves, &target);
  }
  return 1;
}

static int aggregate_type_contains_float(
    const static_hir_aggregate_t *aggregate,
    const psx_type_t *type) {
  if (!aggregate || !type) return 0;
  if (type->kind == PSX_TYPE_FLOAT) return 1;
  if (type->kind == PSX_TYPE_POINTER || type->kind == PSX_TYPE_FUNCTION)
    return 0;
  if (type->kind == PSX_TYPE_ARRAY)
    return aggregate_type_contains_float(aggregate, type->base);
  const psx_record_decl_t *record = aggregate_record_decl(
      aggregate, type);
  if (record) {
    for (int i = 0; i < record->member_count; i++) {
      if (aggregate_type_contains_float(
              aggregate,
              psx_record_member_decl_type(&record->members[i])))
        return 1;
    }
  }
  return 0;
}

int psx_build_static_aggregate_hir_initializer_plan(
    psx_global_registry_t *global_registry,
    psx_lowering_context_t *lowering_context,
    const psx_type_t *type, const psx_hir_module_t *hir,
    psx_hir_node_id_t root, token_t *fallback_tok,
    psx_static_aggregate_initializer_plan_t *plan) {
  (void)fallback_tok;
  if (plan) *plan = (psx_static_aggregate_initializer_plan_t){0};
  if (!global_registry || !lowering_context || !type || !hir ||
      root == PSX_HIR_NODE_ID_INVALID || !plan ||
      (type->kind != PSX_TYPE_ARRAY && !ps_type_is_tag_aggregate(type)))
    return 0;
  global_var_t temporary = {0};
  static_hir_aggregate_t aggregate = {
      .eval = {
          .global_registry = global_registry,
          .lowering_context = lowering_context,
          .hir = hir,
      },
      .global = &temporary,
  };
  const psx_hir_node_t *initializer = node_for_id(&aggregate.eval, root);
  if (!initializer ||
      psx_hir_node_kind(initializer) != PSX_HIR_INITIALIZER_LIST ||
      !ps_global_registry_bind_decl_type(
          global_registry, &temporary, type) ||
      !psx_collect_initializer_scalar_leaves_with_records(
          ps_lowering_semantic_types(lowering_context),
          ps_lowering_record_decls(lowering_context),
          ps_lowering_record_layouts(lowering_context),
          ps_lowering_target(lowering_context),
          ps_gvar_decl_type_id(&temporary), 0, &aggregate.leaves) ||
      aggregate.leaves.count <= 0) {
    psx_initializer_scalar_leaf_list_dispose(&aggregate.leaves);
    return 0;
  }
  ps_gvar_init_slots_alloc(
      &temporary, aggregate.leaves.count,
      aggregate_type_contains_float(&aggregate, type));
  temporary.init_count = aggregate.leaves.count;
  for (int i = 0; i < aggregate.leaves.count; i++)
    ps_gvar_init_slot_clear(&temporary, i);
  int lowered = aggregate_lower_list(
      &aggregate, type, 0, initializer);
  psx_initializer_scalar_leaf_list_dispose(&aggregate.leaves);
  if (!lowered) return 0;
  *plan = (psx_static_aggregate_initializer_plan_t){
      .values = temporary.init_values,
      .floating_values = temporary.init_fvalues,
      .symbols = temporary.init_value_symbols,
      .symbol_lengths = temporary.init_value_symbol_lens,
      .union_ordinals = temporary.init_union_ordinals,
      .value_count = temporary.init_count,
      .union_ordinal = temporary.union_init_ordinal,
  };
  return plan->value_count > 0;
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
