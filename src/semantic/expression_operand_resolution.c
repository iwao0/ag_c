#include "expression_operand_resolution.h"

#include "../parser/semantic_ctx.h"
#include "../parser/type.h"
#include "../parser/type_builder.h"

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

static const psx_type_t *resolve_integer_promotion_type_value(
    psx_semantic_context_t *semantic_context,
    const psx_type_t *type) {
  if (!semantic_context || !type ||
      (type->kind != PSX_TYPE_BOOL &&
       type->kind != PSX_TYPE_INTEGER))
    return NULL;
  if (type->kind == PSX_TYPE_BOOL || ps_type_integer_rank(type) < 3)
    return ps_type_new_integer_kind_in(
        ps_ctx_arena(semantic_context),
        PSX_INTEGER_KIND_INT, 0, 0);
  return ps_type_clone_in(ps_ctx_arena(semantic_context), type);
}

static const psx_type_t *resolve_arithmetic_unary_result_type_value(
    psx_semantic_context_t *semantic_context,
    psx_type_arithmetic_unary_op_t operator,
    const psx_type_t *type) {
  if (!semantic_context || !type) return NULL;
  if (operator == PSX_TYPE_UNARY_PLUS ||
      operator == PSX_TYPE_UNARY_NEGATE) {
    if (type->kind == PSX_TYPE_BOOL || type->kind == PSX_TYPE_INTEGER)
      return resolve_integer_promotion_type_value(
          semantic_context, type);
    if (type->kind == PSX_TYPE_FLOAT || type->kind == PSX_TYPE_COMPLEX)
      return ps_type_clone_in(ps_ctx_arena(semantic_context), type);
    return NULL;
  }
  if (operator != PSX_TYPE_UNARY_REAL &&
      operator != PSX_TYPE_UNARY_IMAGINARY)
    return NULL;
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
    psx_type_binary_op_t operator,
    const psx_type_t *lhs,
    const psx_type_t *rhs) {
  if (!semantic_context) return NULL;
  return ps_type_binary_result_for_target_in(
      ps_ctx_arena(semantic_context), ps_ctx_target_info(semantic_context),
      operator, lhs, rhs);
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
    psx_type_arithmetic_unary_op_t operator,
    psx_qual_type_t operand_type) {
  if (!semantic_context ||
      operand_type.type_id == PSX_TYPE_ID_INVALID)
    return invalid_qual_type();
  const psx_type_t *type = ps_ctx_type_by_id_in(
      semantic_context, operand_type.type_id);
  return intern_result_type(
      semantic_context,
      resolve_arithmetic_unary_result_type_value(
          semantic_context, operator, type));
}

psx_qual_type_t psx_resolve_logical_not_result_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t operand_type) {
  if (!psx_qual_type_is_scalar_in(semantic_context, operand_type))
    return invalid_qual_type();
  return intern_result_type(
      semantic_context,
      ps_type_new_integer_kind_in(
          ps_ctx_arena(semantic_context),
          PSX_INTEGER_KIND_INT, 0, 0));
}

psx_qual_type_t psx_resolve_bitwise_not_result_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t operand_type) {
  if (!semantic_context ||
      operand_type.type_id == PSX_TYPE_ID_INVALID)
    return invalid_qual_type();
  const psx_type_t *type = ps_ctx_type_by_id_in(
      semantic_context, operand_type.type_id);
  return intern_result_type(
      semantic_context,
      resolve_integer_promotion_type_value(
          semantic_context, type));
}

psx_qual_type_t psx_resolve_binary_result_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_type_binary_op_t operator,
    psx_qual_type_t lhs_type,
    psx_qual_type_t rhs_type) {
  if (!semantic_context ||
      rhs_type.type_id == PSX_TYPE_ID_INVALID ||
      (operator != PSX_TYPE_BINARY_COMMA &&
       lhs_type.type_id == PSX_TYPE_ID_INVALID))
    return invalid_qual_type();
  const psx_type_t *lhs =
      lhs_type.type_id != PSX_TYPE_ID_INVALID
          ? ps_ctx_type_by_id_in(
                semantic_context, lhs_type.type_id)
          : NULL;
  const psx_type_t *rhs = ps_ctx_type_by_id_in(
      semantic_context, rhs_type.type_id);
  return intern_result_type(
      semantic_context,
      resolve_binary_result_type_value(
          semantic_context, operator, lhs, rhs));
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

void psx_resolve_conditional_qual_types_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t condition_type,
    psx_qual_type_t then_type,
    psx_qual_type_t else_type,
    psx_conditional_types_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_CONDITIONAL_TYPES_INVALID;
  resolution->result_qual_type = invalid_qual_type();
  if (!semantic_context ||
      condition_type.type_id == PSX_TYPE_ID_INVALID ||
      then_type.type_id == PSX_TYPE_ID_INVALID ||
      else_type.type_id == PSX_TYPE_ID_INVALID)
    return;
  const psx_type_t *condition = ps_ctx_type_by_id_in(
      semantic_context, condition_type.type_id);
  if (!condition || !ps_type_is_scalar(condition)) {
    resolution->status =
        PSX_CONDITIONAL_CONDITION_NOT_SCALAR;
    return;
  }
  resolution->result_qual_type =
      psx_resolve_conditional_result_qual_type_in(
          semantic_context, then_type, else_type);
  if (resolution->result_qual_type.type_id ==
      PSX_TYPE_ID_INVALID) {
    resolution->status =
        PSX_CONDITIONAL_BRANCH_TYPES_INCOMPATIBLE;
    return;
  }
  resolution->status = PSX_CONDITIONAL_TYPES_OK;
}

int psx_qual_type_is_scalar_in(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t type) {
  return semantic_context && type.type_id != PSX_TYPE_ID_INVALID &&
         ps_type_is_scalar(
             ps_ctx_type_by_id_in(semantic_context, type.type_id));
}

void psx_resolve_control_expression_qual_type_in(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t type,
    psx_control_expression_requirement_t requirement,
    psx_control_expression_status_t *status) {
  if (!status) return;
  *status = PSX_CONTROL_EXPRESSION_INVALID;
  if (!semantic_context || type.type_id == PSX_TYPE_ID_INVALID)
    return;
  const psx_type_t *canonical = ps_ctx_type_by_id_in(
      semantic_context, type.type_id);
  if (!canonical) return;
  if (requirement == PSX_CONTROL_EXPRESSION_REQUIRES_INTEGER) {
    *status = canonical->kind == PSX_TYPE_BOOL ||
                      canonical->kind == PSX_TYPE_INTEGER
                  ? PSX_CONTROL_EXPRESSION_OK
                  : PSX_CONTROL_EXPRESSION_NOT_INTEGER;
    return;
  }
  *status = ps_type_is_scalar(canonical)
                ? PSX_CONTROL_EXPRESSION_OK
                : PSX_CONTROL_EXPRESSION_NOT_SCALAR;
}

psx_deref_operand_status_t psx_resolve_deref_operand_qual_type_in(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t operand_type) {
  if (!semantic_context || operand_type.type_id == PSX_TYPE_ID_INVALID)
    return PSX_DEREF_OPERAND_NOT_POINTER;
  const psx_type_t *type = ps_ctx_type_by_id_in(
      semantic_context, operand_type.type_id);
  if (!type || !ps_type_is_pointer_like(type))
    return PSX_DEREF_OPERAND_NOT_POINTER;
  psx_qual_type_t pointee = psx_semantic_type_table_base(
      ps_ctx_semantic_type_table_in(semantic_context),
      operand_type.type_id);
  const psx_type_t *pointee_type = ps_ctx_type_by_id_in(
      semantic_context, pointee.type_id);
  return pointee_type && pointee_type->kind == PSX_TYPE_VOID
             ? PSX_DEREF_OPERAND_VOID_POINTER
             : PSX_DEREF_OPERAND_OK;
}

psx_qual_type_t psx_resolve_indirection_result_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t operand_type) {
  if (!semantic_context ||
      operand_type.type_id == PSX_TYPE_ID_INVALID)
    return invalid_qual_type();
  const psx_type_t *canonical = ps_ctx_type_by_id_in(
      semantic_context, operand_type.type_id);
  if (!canonical || (canonical->kind != PSX_TYPE_POINTER &&
                     canonical->kind != PSX_TYPE_ARRAY))
    return invalid_qual_type();
  return psx_semantic_type_table_base(
      ps_ctx_semantic_type_table_in(semantic_context),
      operand_type.type_id);
}

psx_qual_type_t psx_resolve_address_result_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t operand_type) {
  if (!semantic_context ||
      operand_type.type_id == PSX_TYPE_ID_INVALID)
    return invalid_qual_type();
  return ps_ctx_intern_pointer_to_qual_type_in(
      semantic_context, operand_type);
}

void psx_resolve_address_operand_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t operand_type,
    psx_address_operand_category_t category,
    int operand_is_bitfield,
    psx_address_operand_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_ADDRESS_OPERAND_INVALID;
  resolution->result_qual_type = invalid_qual_type();
  if (!semantic_context ||
      operand_type.type_id == PSX_TYPE_ID_INVALID)
    return;
  if (category == PSX_ADDRESS_OPERAND_NOT_ADDRESSABLE) {
    resolution->status =
        PSX_ADDRESS_OPERAND_REQUIRES_ADDRESSABLE_VALUE;
    return;
  }
  if (operand_is_bitfield) {
    resolution->status = PSX_ADDRESS_OPERAND_IS_BITFIELD;
    return;
  }
  resolution->result_qual_type =
      psx_resolve_address_result_qual_type_in(
          semantic_context, operand_type);
  if (resolution->result_qual_type.type_id ==
      PSX_TYPE_ID_INVALID)
    return;
  resolution->status = PSX_ADDRESS_OPERAND_OK;
}

void psx_resolve_incdec_operand_qual_type_in(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t operand_type,
    psx_incdec_operand_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_INCDEC_OPERAND_INVALID_TYPE;
  resolution->result_qual_type = invalid_qual_type();
  if (!semantic_context ||
      operand_type.type_id == PSX_TYPE_ID_INVALID)
    return;
  if ((operand_type.qualifiers & PSX_TYPE_QUALIFIER_CONST) != 0) {
    resolution->status = PSX_INCDEC_OPERAND_CONST;
    return;
  }
  const psx_type_t *type = ps_ctx_type_by_id_in(
      semantic_context, operand_type.type_id);
  if (!type ||
      (type->kind != PSX_TYPE_POINTER &&
       type->kind != PSX_TYPE_BOOL &&
       type->kind != PSX_TYPE_INTEGER &&
       type->kind != PSX_TYPE_FLOAT))
    return;
  resolution->status = PSX_INCDEC_OPERAND_OK;
  resolution->result_qual_type = (psx_qual_type_t){
      operand_type.type_id, PSX_TYPE_QUALIFIER_NONE};
}

psx_qual_type_t psx_resolve_incdec_result_qual_type_in(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t operand_type) {
  psx_incdec_operand_resolution_t resolution;
  psx_resolve_incdec_operand_qual_type_in(
      semantic_context, operand_type, &resolution);
  return resolution.result_qual_type;
}

psx_qual_type_t psx_resolve_value_decay_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t expression_type) {
  if (!semantic_context ||
      expression_type.type_id == PSX_TYPE_ID_INVALID)
    return invalid_qual_type();
  const psx_type_t *type = ps_ctx_type_by_id_in(
      semantic_context, expression_type.type_id);
  if (!type) return invalid_qual_type();
  if (type->kind == PSX_TYPE_ARRAY) {
    psx_qual_type_t element = psx_semantic_type_table_base(
        ps_ctx_semantic_type_table_in(semantic_context),
        expression_type.type_id);
    return element.type_id == PSX_TYPE_ID_INVALID
               ? invalid_qual_type()
               : ps_ctx_intern_pointer_to_qual_type_in(
                     semantic_context, element);
  }
  if (type->kind == PSX_TYPE_FUNCTION)
    return ps_ctx_intern_pointer_to_qual_type_in(
        semantic_context, expression_type);
  return expression_type;
}

static int qual_type_is_subscript_base(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t type) {
  const psx_type_t *canonical = semantic_context
                                    ? ps_ctx_type_by_id_in(
                                          semantic_context,
                                          type.type_id)
                                    : NULL;
  return canonical && ps_type_is_pointer_like(canonical);
}

static int qual_type_is_integer(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t type) {
  const psx_type_t *canonical = semantic_context
                                    ? ps_ctx_type_by_id_in(
                                          semantic_context,
                                          type.type_id)
                                    : NULL;
  return canonical &&
         (canonical->kind == PSX_TYPE_BOOL ||
          canonical->kind == PSX_TYPE_INTEGER);
}

void psx_resolve_subscript_qual_types_in(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t left_type,
    psx_qual_type_t right_type,
    psx_subscript_qual_types_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_SUBSCRIPT_OPERANDS_INVALID;
  resolution->base_qual_type = (psx_qual_type_t){
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  resolution->index_qual_type = resolution->base_qual_type;
  resolution->result_qual_type = resolution->base_qual_type;
  if (!semantic_context) return;

  int left_base = qual_type_is_subscript_base(
      semantic_context, left_type);
  int right_base = qual_type_is_subscript_base(
      semantic_context, right_type);
  int left_index = qual_type_is_integer(
      semantic_context, left_type);
  int right_index = qual_type_is_integer(
      semantic_context, right_type);
  if (left_base && right_index) {
    resolution->base_qual_type = left_type;
    resolution->index_qual_type = right_type;
  } else if (right_base && left_index) {
    resolution->base_qual_type = right_type;
    resolution->index_qual_type = left_type;
    resolution->swapped = 1;
  } else {
    return;
  }
  resolution->result_qual_type = psx_semantic_type_table_base(
      ps_ctx_semantic_type_table_in(semantic_context),
      resolution->base_qual_type.type_id);
  if (resolution->result_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return;
  resolution->status = PSX_SUBSCRIPT_OPERANDS_OK;
}
