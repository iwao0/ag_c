#include "node_utils_legacy.h"

#include "type_builder.h"
#include "../semantic/parser_type_compatibility.h"
#include "../semantic/resolution_store.h"
#include "../semantic/resolved_node.h"
#include "../semantic/resolved_node_kind.h"
#include "../semantic/resolved_node_type.h"

static void *resolution_node_alloc_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, size_t size) {
  return psx_resolution_node_alloc_in(store, arena_context, size);
}

static psx_type_t *type_with_self_qualifiers_in(
    arena_context_t *arena_context, const psx_type_t *type,
    int is_const_qualified, int is_volatile_qualified) {
  if (!type) return NULL;
  psx_type_t *copy = arena_alloc_in(arena_context, sizeof(*copy));
  *copy = *type;
  if (copy->kind == PSX_TYPE_ARRAY && copy->base) {
    copy->base = type_with_self_qualifiers_in(
        arena_context, copy->base,
        is_const_qualified, is_volatile_qualified);
    return copy;
  }
  psx_type_qualifiers_t qualifiers = PSX_TYPE_QUALIFIER_NONE;
  if (is_const_qualified) qualifiers |= PSX_TYPE_QUALIFIER_CONST;
  if (is_volatile_qualified) qualifiers |= PSX_TYPE_QUALIFIER_VOLATILE;
  ps_type_add_qualifiers(copy, qualifiers);
  return copy;
}

static int node_type_accepts_vla_runtime_view(
    const psx_resolution_store_t *store, const node_t *node) {
  psx_qual_type_t type = ps_node_qual_type(store, node);
  const psx_semantic_type_table_t *types =
      psx_resolution_store_semantic_types(store);
  return psx_semantic_type_table_qual_type_is_valid(types, type) &&
         psx_semantic_type_table_contains_vla_array(types, type.type_id);
}

static int node_self_has_qualifier(
    const psx_resolution_store_t *store, node_t *node,
    psx_type_qualifiers_t qualifier) {
  return node &&
         (ps_node_qual_type(store, node).qualifiers & qualifier) != 0;
}

static int node_pointee_has_qualifier(
    const psx_resolution_store_t *store, node_t *node,
    psx_type_qualifiers_t qualifier) {
  psx_qual_type_t type = ps_node_qual_type(store, node);
  psx_qual_type_t pointee = psx_semantic_type_table_pointee_value(
      psx_resolution_store_semantic_types(store), type.type_id);
  return node && (pointee.qualifiers & qualifier) != 0;
}

int ps_node_binary_type_op(
    psx_resolution_node_kind_t kind, psx_type_binary_op_t *op) {
  if (!op) return 0;
  switch (kind) {
    case ND_COMMA: *op = PSX_TYPE_BINARY_COMMA; return 1;
    case ND_ADD: *op = PSX_TYPE_BINARY_ADD; return 1;
    case ND_SUB: *op = PSX_TYPE_BINARY_SUB; return 1;
    case ND_MUL: *op = PSX_TYPE_BINARY_MUL; return 1;
    case ND_DIV: *op = PSX_TYPE_BINARY_DIV; return 1;
    case ND_MOD: *op = PSX_TYPE_BINARY_MOD; return 1;
    case ND_BITAND: *op = PSX_TYPE_BINARY_BITAND; return 1;
    case ND_BITXOR: *op = PSX_TYPE_BINARY_BITXOR; return 1;
    case ND_BITOR: *op = PSX_TYPE_BINARY_BITOR; return 1;
    case ND_SHL: *op = PSX_TYPE_BINARY_SHL; return 1;
    case ND_SHR: *op = PSX_TYPE_BINARY_SHR; return 1;
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
    case ND_GT:
    case ND_GE:
      *op = PSX_TYPE_BINARY_COMPARE;
      return 1;
    case ND_LOGAND:
    case ND_LOGOR:
      *op = PSX_TYPE_BINARY_LOGICAL;
      return 1;
    default:
      return 0;
  }
}

node_t *ps_node_new_binary_for_data_layout_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    const ag_data_layout_t *data_layout, psx_resolution_node_kind_t kind,
    node_t *lhs, node_t *rhs) {
  node_t *node = resolution_node_alloc_in(
      store, arena_context, sizeof(*node));
  if (!node) return NULL;
  node->kind = kind;
  node->lhs = lhs;
  node->rhs = rhs;
  psx_type_binary_op_t op;
  const psx_type_t *type =
      ps_node_binary_type_op(kind, &op)
          ? ps_type_binary_result_for_data_layout_in(
                arena_context, data_layout, op, ps_node_get_type(store, lhs),
                ps_node_get_type(store, rhs))
          : NULL;
  if (type) ps_node_bind_type(store, node, type);
  return node;
}

node_t *ps_node_new_shift_trunc_extend_for_width_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand, int left_shift,
    int execution_size, int is_unsigned) {
  const psx_type_t *operand_type = ps_node_get_type(store, operand);
  if (execution_size < 4) execution_size = 4;
  psx_integer_kind_t execution_kind =
      operand_type && operand_type->kind == PSX_TYPE_INTEGER &&
              operand_type->integer_kind == PSX_INTEGER_KIND_LONG_LONG
          ? PSX_INTEGER_KIND_LONG_LONG
      : execution_size >= 8 ? PSX_INTEGER_KIND_LONG
                            : PSX_INTEGER_KIND_INT;
  psx_type_t *execution_type = ps_type_new_integer_kind_in(
      arena_context, execution_kind, is_unsigned ? 1 : 0, 0);
  node_t *shl = resolution_node_alloc_in(
      store, arena_context, sizeof(*shl));
  if (!shl) return NULL;
  shl->kind = ND_SHL;
  shl->lhs = operand;
  shl->rhs = ps_node_new_num_in(store, arena_context, left_shift);
  ps_node_bind_type(store, shl, execution_type);
  node_t *shr = resolution_node_alloc_in(
      store, arena_context, sizeof(*shr));
  if (!shr) return NULL;
  shr->kind = ND_SHR;
  shr->lhs = shl;
  shr->rhs = ps_node_new_num_in(store, arena_context, left_shift);
  ps_node_bind_type(store, shr, execution_type);
  return shr;
}

node_t *ps_node_new_num_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, long long val) {
  node_num_t *number = resolution_node_alloc_in(
      store, arena_context, sizeof(*number));
  if (!number) return NULL;
  number->base.kind = ND_NUM;
  number->float_suffix_kind = TK_FLOAT_SUFFIX_NONE;
  number->val = val;
  node_t *node = &number->base;
  ps_node_bind_type(
      store, node, ps_type_new_integer_kind_in(
                       arena_context, PSX_INTEGER_KIND_INT, 0, 0));
  return node;
}

static node_t *annotate_explicit_type(
    psx_resolution_store_t *store, node_t *node,
    const psx_type_t *type) {
  if (node && type) ps_node_bind_type(store, node, type);
  return node;
}

node_t *ps_node_new_fp_to_int_cast_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand, const psx_type_t *cast_type) {
  node_t *node = resolution_node_alloc_in(
      store, arena_context, sizeof(*node));
  if (!node || !psx_resolution_node_set_kind(store, node, ND_FP_TO_INT))
    return NULL;
  node->lhs = operand;
  return annotate_explicit_type(store, node, cast_type);
}

node_t *ps_node_new_int_to_fp_cast_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand, const psx_type_t *cast_type) {
  node_t *node = resolution_node_alloc_in(
      store, arena_context, sizeof(*node));
  if (!node || !psx_resolution_node_set_kind(store, node, ND_INT_TO_FP))
    return NULL;
  node->lhs = operand;
  return annotate_explicit_type(store, node, cast_type);
}

node_t *ps_node_new_semantic_cast_result_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand, const psx_type_t *cast_type) {
  node_t *wrap = resolution_node_alloc_in(
      store, arena_context, sizeof(*wrap));
  if (!wrap || !psx_resolution_node_set_kind(store, wrap, ND_CAST))
    return NULL;
  wrap->lhs = operand;
  return annotate_explicit_type(store, wrap, cast_type);
}

node_t *ps_node_new_integer_cast_result_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand, const psx_type_t *cast_type) {
  return ps_node_new_integer_cast_result_ex_in(
      store, arena_context, operand, cast_type, 0);
}

node_t *ps_node_new_integer_cast_result_ex_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand, const psx_type_t *cast_type, int widen_zext_i64) {
  node_t *wrap = resolution_node_alloc_in(
      store, arena_context, sizeof(*wrap));
  if (!wrap || !psx_resolution_node_set_kind(store, wrap, ND_CAST))
    return NULL;
  wrap->lhs = operand;
  ps_node_set_widen_zext_i64(store, wrap, widen_zext_i64);
  return annotate_explicit_type(store, wrap, cast_type);
}

node_t *ps_node_new_i64_to_i32_trunc_cast_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand, const psx_type_t *cast_type) {
  int is_unsigned = ps_type_is_unsigned(cast_type);
  node_t *trunc = ps_node_new_shift_trunc_extend_for_width_in(
      store, arena_context, operand, 32, 8, is_unsigned);
  return ps_node_new_integer_cast_result_in(
      store, arena_context, trunc, cast_type);
}

node_t *ps_node_new_pointer_cast_result_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand, const psx_type_t *cast_type) {
  return ps_node_new_semantic_cast_result_in(
      store, arena_context, operand, cast_type);
}

node_t *ps_node_new_aggregate_cast_result_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand, const psx_type_t *cast_type) {
  return ps_node_new_semantic_cast_result_in(
      store, arena_context, operand, cast_type);
}

node_t *ps_node_new_void_cast_result_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand, const psx_type_t *cast_type) {
  return ps_node_new_semantic_cast_result_in(
      store, arena_context, operand, cast_type);
}

static node_t *new_addr_node(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *base) {
  node_t *addr = resolution_node_alloc_in(
      store, arena_context, sizeof(*addr));
  if (!addr || !psx_resolution_node_set_kind(store, addr, ND_ADDR))
    return NULL;
  addr->lhs = base;
  return addr;
}

node_t *ps_node_new_addr_value_for_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand) {
  node_t *addr = new_addr_node(store, arena_context, operand);
  if (!addr) return NULL;
  ps_node_bind_type(
      store, addr, ps_type_address_result_in(
                       arena_context, ps_node_get_type(store, operand)));
  return addr;
}

node_t *ps_node_new_explicit_addr_value_for_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand) {
  if (!operand || psx_resolution_node_kind(store, operand) != ND_ADDR)
    return operand;
  node_t *copy = resolution_node_alloc_in(
      store, arena_context, sizeof(*copy));
  if (!copy) return NULL;
  *copy = *operand;
  ps_node_copy_resolution_state_in(store, arena_context, copy, operand);
  ps_node_clear_expr_type_state(store, copy);
  ps_node_bind_type(
      store, copy, ps_type_address_result_in(
                        arena_context, ps_node_get_type(store, operand->lhs)));
  return copy;
}

node_t *ps_node_new_unary_addr_for_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand) {
  node_t *node = new_addr_node(store, arena_context, operand);
  if (!node) return NULL;
  ps_node_bind_type(
      store, node, ps_type_address_result_in(
                        arena_context, ps_node_get_type(store, operand)));
  return node;
}

static void init_subscript_expr_state(
    psx_resolution_store_t *store, node_t *result) {
  const psx_type_t *type = ps_node_get_type(store, result);
  if (!type || type->kind != PSX_TYPE_ARRAY) return;
  ps_node_set_subscript_uses_base_address(store, result, 1);
}

static void advance_subscript_vla_runtime_view(
    psx_resolution_store_t *store, node_t *result, node_t *base) {
  if (!result || !base ||
      !node_type_accepts_vla_runtime_view(store, result))
    return;
  int frame_off = ps_node_vla_row_stride_frame_off(store, base);
  int remaining = ps_node_vla_strides_remaining(store, base);
  ps_node_set_vla_runtime_view(
      store, result, frame_off != 0 && remaining > 0 ? frame_off + 8 : 0,
      remaining > 0 ? remaining - 1 : 0);
}

node_t *ps_node_new_tag_member_deref_with_layout_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, const ag_target_info_t *target,
    node_t *addr_base, node_t *base, int member_offset,
    const psx_type_t *member_type, int bit_is_signed,
    int bit_width, int bit_offset) {
  if (!member_type) return NULL;
  psx_qual_type_t member_identity =
      psx_resolution_store_intern_type(store, member_type);
  if (member_identity.type_id == PSX_TYPE_ID_INVALID) return NULL;
  node_t *addr = ps_node_new_binary_for_data_layout_in(
      store, arena_context, ag_target_info_data_layout(target), ND_ADD,
      addr_base, ps_node_new_num_in(store, arena_context, member_offset));
  node_t *deref = resolution_node_alloc_in(
      store, arena_context, sizeof(*deref));
  if (!deref || !psx_resolution_node_set_kind(store, deref, ND_DEREF))
    return NULL;
  deref->lhs = addr;
  int mem_array_len = ps_type_array_flat_element_count(member_type);
  const psx_type_t *member_value_type = ps_type_array_leaf_type(member_type);
  int mem_is_ptr = member_value_type &&
                   member_value_type->kind == PSX_TYPE_POINTER;
  int member_is_const =
      node_pointee_has_qualifier(
          store, base, PSX_TYPE_QUALIFIER_CONST) ||
      (!ps_node_value_is_pointer_like(store, base) &&
       node_self_has_qualifier(store, base, PSX_TYPE_QUALIFIER_CONST));
  int member_is_volatile =
      node_pointee_has_qualifier(
          store, base, PSX_TYPE_QUALIFIER_VOLATILE) ||
      (!ps_node_value_is_pointer_like(store, base) &&
       node_self_has_qualifier(store, base, PSX_TYPE_QUALIFIER_VOLATILE));
  ps_node_set_bitfield_info(
      store, deref, bit_width, bit_offset, bit_is_signed);
  ps_node_bind_type(
      store, deref, type_with_self_qualifiers_in(
                        arena_context, member_type,
                        member_is_const, member_is_volatile));
  ps_node_set_scalar_ptr_member_lvalue(
      store, deref, mem_is_ptr && mem_array_len <= 0);
  return deref;
}

node_t *ps_node_new_unary_deref_for_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *operand) {
  const psx_type_t *result_type =
      ps_type_dereference_result(ps_node_get_type(store, operand));
  node_t *result = resolution_node_alloc_in(
      store, arena_context, sizeof(*result));
  if (!result || !psx_resolution_node_set_kind(store, result, ND_DEREF))
    return NULL;
  result->lhs = operand;
  if (result_type) ps_node_bind_type(store, result, result_type);
  return result;
}

node_t *ps_node_new_subscript_deref_for_in(
    psx_resolution_store_t *store,
    arena_context_t *arena_context, const ag_target_info_t *target,
    node_t *base, node_t *base_addr, node_t *scaled_offset) {
  const psx_type_t *base_type = ps_node_get_type(store, base);
  const psx_type_t *result_type = ps_type_subscript_result_in(
      arena_context, base_type);
  node_t *result = resolution_node_alloc_in(
      store, arena_context, sizeof(*result));
  if (!result || !psx_resolution_node_set_kind(store, result, ND_DEREF))
    return NULL;
  result->lhs = ps_node_new_binary_for_data_layout_in(
      store, arena_context, ag_target_info_data_layout(target), ND_ADD,
      base_addr, scaled_offset);
  if (result_type) {
    ps_node_bind_type(store, result, result_type);
    advance_subscript_vla_runtime_view(store, result, base);
    init_subscript_expr_state(store, result);
  }
  return result;
}

const psx_type_t *ps_node_array_decay_pointer_arith_type_in(
    const psx_resolution_store_t *store,
    arena_context_t *arena_context, node_t *node) {
  if (!node ||
      (psx_resolution_node_kind(store, node) != ND_DEREF &&
       psx_resolution_node_kind(store, node) != ND_ADDR))
    return NULL;
  const psx_type_t *type = ps_node_get_type(store, node);
  const psx_type_t *base =
      type && type->kind == PSX_TYPE_ARRAY && type->base
          ? type->base
          : NULL;
  return base ? ps_type_address_result_in(arena_context, base) : NULL;
}

node_t *ps_node_new_assign_in(
    psx_resolution_store_t *store, arena_context_t *arena_context,
    node_t *lhs, node_t *rhs) {
  node_t *node = resolution_node_alloc_in(
      store, arena_context, sizeof(*node));
  if (!node) return NULL;
  node->kind = ND_ASSIGN;
  node->lhs = lhs;
  node->rhs = rhs;
  ps_node_bind_type(store, node, ps_node_get_type(store, lhs));
  return node;
}
