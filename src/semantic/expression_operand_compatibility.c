#include "expression_operand_compatibility.h"

#include "../parser/node_utils.h"
#include "../parser/semantic_ctx.h"
#include "../parser/type.h"
#include "resolved_node_type.h"

#include <string.h>

static psx_qual_type_t compatibility_node_qual_type(
    psx_semantic_context_t *semantic_context,
    const node_t *node) {
  if (!semantic_context || !node)
    return (psx_qual_type_t){
        PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  psx_resolution_store_t *store =
      ps_ctx_resolution_store(semantic_context);
  psx_qual_type_t type = ps_node_qual_type(store, node);
  return type.type_id != PSX_TYPE_ID_INVALID
             ? type
             : ps_ctx_intern_qual_type_in(
                   semantic_context,
                   ps_node_get_type(store, node));
}

static const psx_type_t *compatibility_result_type(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t result) {
  return semantic_context && result.type_id != PSX_TYPE_ID_INVALID
             ? ps_ctx_type_by_id_in(
                   semantic_context, result.type_id)
             : NULL;
}

static int compatibility_unary_operator(
    psx_resolution_node_kind_t kind,
    psx_type_arithmetic_unary_op_t *operator) {
  if (!operator) return 0;
  switch (kind) {
    case ND_UNARY_NEGATE:
      *operator = PSX_TYPE_UNARY_NEGATE;
      return 1;
    case ND_CREAL:
      *operator = PSX_TYPE_UNARY_REAL;
      return 1;
    case ND_CIMAG:
      *operator = PSX_TYPE_UNARY_IMAGINARY;
      return 1;
    default:
      return 0;
  }
}

psx_deref_operand_status_t psx_resolve_deref_operand(
    const psx_resolution_store_t *store, node_t *operand) {
  if (!operand) return PSX_DEREF_OPERAND_NOT_POINTER;
  const psx_type_t *type = ps_node_get_type(store, operand);
  if (!type || !ps_type_is_pointer_like(type))
    return PSX_DEREF_OPERAND_NOT_POINTER;
  if (type->base && type->base->kind == PSX_TYPE_VOID)
    return PSX_DEREF_OPERAND_VOID_POINTER;
  return PSX_DEREF_OPERAND_OK;
}

const psx_type_t *psx_resolve_indirection_result_type(
    psx_semantic_context_t *semantic_context, node_t *operand) {
  return compatibility_result_type(
      semantic_context,
      psx_resolve_indirection_result_qual_type_in(
          semantic_context,
          compatibility_node_qual_type(
              semantic_context, operand)));
}

const psx_type_t *psx_resolve_arithmetic_unary_result_type(
    psx_semantic_context_t *semantic_context,
    psx_resolution_node_kind_t kind, node_t *operand) {
  psx_type_arithmetic_unary_op_t operator;
  if (!compatibility_unary_operator(kind, &operator)) return NULL;
  return compatibility_result_type(
      semantic_context,
      psx_resolve_arithmetic_unary_result_qual_type_in(
          semantic_context, operator,
          compatibility_node_qual_type(
              semantic_context, operand)));
}

const psx_type_t *psx_resolve_binary_result_type(
    psx_semantic_context_t *semantic_context,
    psx_resolution_node_kind_t kind, node_t *lhs, node_t *rhs) {
  psx_type_binary_op_t operator;
  if (!ps_node_binary_type_op(kind, &operator)) return NULL;
  return compatibility_result_type(
      semantic_context,
      psx_resolve_binary_result_qual_type_in(
          semantic_context, operator,
          compatibility_node_qual_type(semantic_context, lhs),
          compatibility_node_qual_type(semantic_context, rhs)));
}

const psx_type_t *psx_resolve_conditional_result_type(
    psx_semantic_context_t *semantic_context,
    node_t *then_expr, node_t *else_expr) {
  return compatibility_result_type(
      semantic_context,
      psx_resolve_conditional_result_qual_type_in(
          semantic_context,
          compatibility_node_qual_type(
              semantic_context, then_expr),
          compatibility_node_qual_type(
              semantic_context, else_expr)));
}

const psx_type_t *psx_resolve_sequence_result_type(
    psx_semantic_context_t *semantic_context, node_t *value) {
  return compatibility_result_type(
      semantic_context,
      compatibility_node_qual_type(semantic_context, value));
}

const psx_type_t *psx_resolve_address_result_type(
    psx_semantic_context_t *semantic_context, node_t *operand) {
  return compatibility_result_type(
      semantic_context,
      psx_resolve_address_result_qual_type_in(
          semantic_context,
          compatibility_node_qual_type(
              semantic_context, operand)));
}

const psx_type_t *psx_resolve_incdec_result_type(
    psx_semantic_context_t *semantic_context, node_t *operand) {
  return compatibility_result_type(
      semantic_context,
      psx_resolve_incdec_result_qual_type_in(
          semantic_context,
          compatibility_node_qual_type(
              semantic_context, operand)));
}

static int is_subscript_pointer_operand(
    const psx_resolution_store_t *store, node_t *node) {
  const psx_type_t *type = ps_node_get_type(store, node);
  return type && (type->kind == PSX_TYPE_POINTER ||
                  type->kind == PSX_TYPE_ARRAY);
}

void psx_resolve_subscript_operands(
    const psx_resolution_store_t *store,
    node_t *left, node_t *right,
    psx_subscript_operands_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->base = left;
  resolution->index = right;
  int left_is_pointer = is_subscript_pointer_operand(store, left);
  int right_is_pointer = is_subscript_pointer_operand(store, right);
  if (!left_is_pointer && !right_is_pointer) {
    resolution->status = PSX_SUBSCRIPT_OPERANDS_INVALID;
    return;
  }
  if (!left_is_pointer && right_is_pointer) {
    resolution->base = right;
    resolution->index = left;
    resolution->swapped = 1;
  }
  resolution->status = PSX_SUBSCRIPT_OPERANDS_OK;
}
