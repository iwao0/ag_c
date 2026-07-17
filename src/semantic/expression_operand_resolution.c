#include "expression_operand_resolution.h"

#include "../parser/node_utils.h"
#include "../parser/semantic_ctx.h"
#include "../parser/type.h"
#include "../parser/type_builder.h"
#include "resolved_node_type.h"

#include <string.h>

static psx_qual_type_t invalid_qual_type(void) {
  return (psx_qual_type_t){
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
}

static psx_qual_type_t intern_result_type(
    psx_semantic_context_t *semantic_context,
    const psx_type_t *type) {
  if (!semantic_context || !type) return invalid_qual_type();
  psx_qual_type_t result =
      ps_ctx_intern_qual_type_in(semantic_context, type);
  return ps_ctx_type_by_id_in(semantic_context, result.type_id)
             ? result : invalid_qual_type();
}

static const psx_type_t *resolve_arithmetic_unary_result_type_value(
    psx_semantic_context_t *semantic_context,
    psx_resolution_node_kind_t kind,
    const psx_type_t *type) {
  if (!semantic_context || !type) return NULL;
  if (kind == ND_UNARY_NEGATE) {
    if (type->kind == PSX_TYPE_BOOL ||
        (type->kind == PSX_TYPE_INTEGER &&
         ps_type_integer_rank(type) < 3))
      return ps_type_new_integer_kind_in(
          ps_ctx_arena(semantic_context),
          PSX_INTEGER_KIND_INT, 0, 0);
    if (type->kind == PSX_TYPE_INTEGER || type->kind == PSX_TYPE_FLOAT ||
        type->kind == PSX_TYPE_COMPLEX)
      return ps_type_clone_in(ps_ctx_arena(semantic_context), type);
    return NULL;
  }
  if (kind != ND_CREAL && kind != ND_CIMAG) return NULL;
  if (type->kind == PSX_TYPE_COMPLEX) {
    psx_floating_kind_t floating_kind =
        type->floating_kind != PSX_FLOATING_KIND_NONE
            ? type->floating_kind
            : PSX_FLOATING_KIND_DOUBLE;
    return ps_type_new_floating_in(
        ps_ctx_arena(semantic_context), floating_kind, 0);
  }
  if (type->kind == PSX_TYPE_FLOAT || type->kind == PSX_TYPE_INTEGER ||
      type->kind == PSX_TYPE_BOOL)
    return ps_type_clone_in(ps_ctx_arena(semantic_context), type);
  return NULL;
}

static const psx_type_t *resolve_binary_result_type_value(
    psx_semantic_context_t *semantic_context,
    psx_resolution_node_kind_t kind,
    const psx_type_t *lhs,
    const psx_type_t *rhs) {
  psx_type_binary_op_t op;
  if (!semantic_context || !ps_node_binary_type_op(kind, &op)) return NULL;
  return ps_type_binary_result_for_target_in(
      ps_ctx_arena(semantic_context), ps_ctx_target_info(semantic_context), op,
      lhs, rhs);
}

static const psx_type_t *resolve_conditional_result_type_value(
    psx_semantic_context_t *semantic_context,
    const psx_type_t *then_type,
    const psx_type_t *else_type) {
  if (!semantic_context) return NULL;
  return ps_type_conditional_result_for_target_in(
      ps_ctx_arena(semantic_context), ps_ctx_target_info(semantic_context),
      then_type, else_type);
}

psx_qual_type_t psx_resolve_arithmetic_unary_result_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_resolution_node_kind_t kind,
    psx_qual_type_t operand_type) {
  if (!semantic_context ||
      operand_type.type_id == PSX_TYPE_ID_INVALID)
    return invalid_qual_type();
  const psx_type_t *type = ps_ctx_type_by_id_in(
      semantic_context, operand_type.type_id);
  return intern_result_type(
      semantic_context,
      resolve_arithmetic_unary_result_type_value(
          semantic_context, kind, type));
}

psx_qual_type_t psx_resolve_binary_result_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_resolution_node_kind_t kind,
    psx_qual_type_t lhs_type,
    psx_qual_type_t rhs_type) {
  if (!semantic_context ||
      lhs_type.type_id == PSX_TYPE_ID_INVALID ||
      rhs_type.type_id == PSX_TYPE_ID_INVALID)
    return invalid_qual_type();
  const psx_type_t *lhs = ps_ctx_type_by_id_in(
      semantic_context, lhs_type.type_id);
  const psx_type_t *rhs = ps_ctx_type_by_id_in(
      semantic_context, rhs_type.type_id);
  return intern_result_type(
      semantic_context,
      resolve_binary_result_type_value(
          semantic_context, kind, lhs, rhs));
}

psx_qual_type_t psx_resolve_conditional_result_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t then_type,
    psx_qual_type_t else_type) {
  if (!semantic_context ||
      then_type.type_id == PSX_TYPE_ID_INVALID ||
      else_type.type_id == PSX_TYPE_ID_INVALID)
    return invalid_qual_type();
  const psx_type_t *then_value = ps_ctx_type_by_id_in(
      semantic_context, then_type.type_id);
  const psx_type_t *else_value = ps_ctx_type_by_id_in(
      semantic_context, else_type.type_id);
  return intern_result_type(
      semantic_context,
      resolve_conditional_result_type_value(
          semantic_context, then_value, else_value));
}

int psx_qual_type_is_scalar_in(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t type) {
  return semantic_context && type.type_id != PSX_TYPE_ID_INVALID &&
         ps_type_is_scalar(
             ps_ctx_type_by_id_in(semantic_context, type.type_id));
}

psx_deref_operand_status_t psx_resolve_deref_operand(node_t *operand) {
  if (!operand) return PSX_DEREF_OPERAND_NOT_POINTER;
  const psx_type_t *type = ps_node_get_type(operand);
  if (!type || !ps_type_is_pointer_like(type))
    return PSX_DEREF_OPERAND_NOT_POINTER;
  if (type->base && type->base->kind == PSX_TYPE_VOID)
    return PSX_DEREF_OPERAND_VOID_POINTER;
  return PSX_DEREF_OPERAND_OK;
}

const psx_type_t *psx_resolve_indirection_result_type(
    psx_semantic_context_t *semantic_context, node_t *operand) {
  const psx_type_t *type = ps_node_get_type(operand);
  if (!type || !type->base ||
      (type->kind != PSX_TYPE_POINTER && type->kind != PSX_TYPE_ARRAY))
    return NULL;
  return ps_type_clone_in(ps_ctx_arena(semantic_context), type->base);
}

const psx_type_t *psx_resolve_arithmetic_unary_result_type(
    psx_semantic_context_t *semantic_context,
    psx_resolution_node_kind_t kind, node_t *operand) {
  return resolve_arithmetic_unary_result_type_value(
      semantic_context, kind, ps_node_get_type(operand));
}

const psx_type_t *psx_resolve_binary_result_type(
    psx_semantic_context_t *semantic_context,
    psx_resolution_node_kind_t kind, node_t *lhs, node_t *rhs) {
  return resolve_binary_result_type_value(
      semantic_context, kind,
      ps_node_get_type(lhs), ps_node_get_type(rhs));
}

const psx_type_t *psx_resolve_conditional_result_type(
    psx_semantic_context_t *semantic_context,
    node_t *then_expr, node_t *else_expr) {
  return resolve_conditional_result_type_value(
      semantic_context,
      ps_node_get_type(then_expr), ps_node_get_type(else_expr));
}

const psx_type_t *psx_resolve_sequence_result_type(
    psx_semantic_context_t *semantic_context, node_t *value) {
  return ps_type_clone_in(
      ps_ctx_arena(semantic_context), ps_node_get_type(value));
}

const psx_type_t *psx_resolve_address_result_type(
    psx_semantic_context_t *semantic_context, node_t *operand) {
  const psx_type_t *operand_type = ps_node_get_type(operand);
  if (!operand_type) return NULL;
  psx_type_t *base = ps_type_clone_in(
      ps_ctx_arena(semantic_context), operand_type);
  return ps_type_new_pointer_in(ps_ctx_arena(semantic_context), base);
}

const psx_type_t *psx_resolve_incdec_result_type(
    psx_semantic_context_t *semantic_context, node_t *operand) {
  const psx_type_t *type = ps_node_get_type(operand);
  if (!type) return NULL;
  if (ps_type_is_pointer(type) || type->kind == PSX_TYPE_BOOL ||
      type->kind == PSX_TYPE_INTEGER || type->kind == PSX_TYPE_FLOAT)
    return ps_type_clone_in(ps_ctx_arena(semantic_context), type);
  return NULL;
}

static int is_subscript_pointer_operand(node_t *node) {
  const psx_type_t *type = ps_node_get_type(node);
  return type && (type->kind == PSX_TYPE_POINTER ||
                  type->kind == PSX_TYPE_ARRAY);
}

void psx_resolve_subscript_operands(
    node_t *left, node_t *right,
    psx_subscript_operands_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->base = left;
  resolution->index = right;
  int left_is_pointer = is_subscript_pointer_operand(left);
  int right_is_pointer = is_subscript_pointer_operand(right);
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
