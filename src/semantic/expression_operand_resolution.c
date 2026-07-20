#include "expression_operand_resolution.h"

#include "../parser/semantic_ctx.h"
#include "../type_system/integer_conversion.h"

#include <string.h>

static psx_qual_type_t invalid_qual_type(void) {
  return (psx_qual_type_t){
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
}

static psx_qual_type_t unqualified(psx_qual_type_t type) {
  return (psx_qual_type_t){type.type_id, PSX_TYPE_QUALIFIER_NONE};
}

static int describe_type(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t type, psx_type_shape_t *shape) {
  return semantic_context && shape &&
         type.type_id != PSX_TYPE_ID_INVALID &&
         psx_semantic_type_table_describe(
             ps_ctx_semantic_type_table_in(semantic_context),
             type.type_id, shape);
}

static int kind_is_integer(psx_type_kind_t kind) {
  return kind == PSX_TYPE_BOOL || kind == PSX_TYPE_INTEGER;
}

static int kind_is_arithmetic(psx_type_kind_t kind) {
  return kind_is_integer(kind) || kind == PSX_TYPE_FLOAT ||
         kind == PSX_TYPE_COMPLEX;
}

static int kind_is_pointer_like(psx_type_kind_t kind) {
  return kind == PSX_TYPE_POINTER || kind == PSX_TYPE_ARRAY;
}

static psx_qual_type_t integer_result(
    psx_semantic_context_t *semantic_context,
    psx_integer_kind_t kind, int is_unsigned) {
  return ps_ctx_intern_integer_qual_type_in(
      semantic_context, kind, is_unsigned, 0);
}

static psx_integer_kind_t integer_kind_for_rank(int rank) {
  return rank >= 5 ? PSX_INTEGER_KIND_LONG_LONG
       : rank >= 4 ? PSX_INTEGER_KIND_LONG
                   : PSX_INTEGER_KIND_INT;
}

static psx_qual_type_t promoted_integer_result(
    psx_semantic_context_t *semantic_context,
    const psx_type_shape_t *shape) {
  if (!shape || !kind_is_integer(shape->kind))
    return invalid_qual_type();
  psx_integer_conversion_t conversion =
      psx_integer_promotion_for_data_layout(
          psx_integer_conversion_from_shape(shape),
          ps_ctx_data_layout(semantic_context));
  return integer_result(
      semantic_context, integer_kind_for_rank(conversion.rank),
      conversion.is_unsigned);
}

static psx_qual_type_t decay_pointer_like(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t type, const psx_type_shape_t *shape) {
  if (!shape || !kind_is_pointer_like(shape->kind))
    return invalid_qual_type();
  if (shape->kind == PSX_TYPE_POINTER) return unqualified(type);
  psx_qual_type_t element = psx_semantic_type_table_base(
      ps_ctx_semantic_type_table_in(semantic_context), type.type_id);
  return element.type_id == PSX_TYPE_ID_INVALID
             ? invalid_qual_type()
             : ps_ctx_intern_pointer_to_qual_type_in(
                   semantic_context, element);
}

static psx_qual_type_t usual_arithmetic_result(
    psx_semantic_context_t *semantic_context,
    const psx_type_shape_t *lhs, const psx_type_shape_t *rhs) {
  if (!lhs || !rhs || !kind_is_arithmetic(lhs->kind) ||
      !kind_is_arithmetic(rhs->kind))
    return invalid_qual_type();
  int is_complex = lhs->kind == PSX_TYPE_COMPLEX ||
                   rhs->kind == PSX_TYPE_COMPLEX;
  if (is_complex || lhs->kind == PSX_TYPE_FLOAT ||
      rhs->kind == PSX_TYPE_FLOAT) {
    psx_floating_kind_t floating_kind = PSX_FLOATING_KIND_NONE;
    if ((lhs->kind == PSX_TYPE_FLOAT ||
         lhs->kind == PSX_TYPE_COMPLEX) &&
        lhs->floating_kind > floating_kind)
      floating_kind = lhs->floating_kind;
    if ((rhs->kind == PSX_TYPE_FLOAT ||
         rhs->kind == PSX_TYPE_COMPLEX) &&
        rhs->floating_kind > floating_kind)
      floating_kind = rhs->floating_kind;
    if (floating_kind == PSX_FLOATING_KIND_NONE)
      floating_kind = PSX_FLOATING_KIND_DOUBLE;
    return ps_ctx_intern_floating_qual_type_in(
        semantic_context, floating_kind, is_complex);
  }
  psx_integer_conversion_t conversion =
      psx_usual_integer_conversion_for_data_layout(
          psx_integer_conversion_from_shape(lhs),
          psx_integer_conversion_from_shape(rhs),
          ps_ctx_data_layout(semantic_context));
  return integer_result(
      semantic_context, integer_kind_for_rank(conversion.rank),
      conversion.is_unsigned);
}

psx_qual_type_t psx_resolve_arithmetic_unary_result_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_type_arithmetic_unary_op_t operator,
    psx_qual_type_t operand_type) {
  psx_type_shape_t shape = {0};
  if (!describe_type(semantic_context, operand_type, &shape))
    return invalid_qual_type();
  if (operator == PSX_TYPE_UNARY_PLUS ||
      operator == PSX_TYPE_UNARY_NEGATE) {
    if (kind_is_integer(shape.kind))
      return promoted_integer_result(
          semantic_context, &shape);
    return shape.kind == PSX_TYPE_FLOAT ||
                   shape.kind == PSX_TYPE_COMPLEX
               ? unqualified(operand_type)
               : invalid_qual_type();
  }
  if (operator != PSX_TYPE_UNARY_REAL &&
      operator != PSX_TYPE_UNARY_IMAGINARY)
    return invalid_qual_type();
  if (shape.kind == PSX_TYPE_COMPLEX) {
    psx_floating_kind_t floating_kind =
        shape.floating_kind != PSX_FLOATING_KIND_NONE
            ? shape.floating_kind
            : PSX_FLOATING_KIND_DOUBLE;
    return ps_ctx_intern_floating_qual_type_in(
        semantic_context, floating_kind, 0);
  }
  return shape.kind == PSX_TYPE_FLOAT || kind_is_integer(shape.kind)
             ? unqualified(operand_type)
             : invalid_qual_type();
}

psx_qual_type_t psx_resolve_logical_not_result_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t operand_type) {
  if (!psx_qual_type_is_scalar_in(semantic_context, operand_type))
    return invalid_qual_type();
  return integer_result(
      semantic_context, PSX_INTEGER_KIND_INT, 0);
}

psx_qual_type_t psx_resolve_bitwise_not_result_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t operand_type) {
  psx_type_shape_t shape = {0};
  return describe_type(semantic_context, operand_type, &shape)
             ? promoted_integer_result(
                   semantic_context, &shape)
             : invalid_qual_type();
}

psx_qual_type_t psx_resolve_binary_result_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_type_binary_op_t operator,
    psx_qual_type_t lhs_type, psx_qual_type_t rhs_type) {
  psx_type_shape_t lhs = {0};
  psx_type_shape_t rhs = {0};
  if (!semantic_context ||
      !describe_type(semantic_context, rhs_type, &rhs) ||
      (operator != PSX_TYPE_BINARY_COMMA &&
       !describe_type(semantic_context, lhs_type, &lhs)))
    return invalid_qual_type();
  if (operator == PSX_TYPE_BINARY_COMMA) return unqualified(rhs_type);
  if (operator == PSX_TYPE_BINARY_COMPARE ||
      operator == PSX_TYPE_BINARY_LOGICAL)
    return integer_result(
        semantic_context, PSX_INTEGER_KIND_INT, 0);
  if (operator == PSX_TYPE_BINARY_SHL ||
      operator == PSX_TYPE_BINARY_SHR)
    return promoted_integer_result(
        semantic_context, &lhs);

  int lhs_pointer = kind_is_pointer_like(lhs.kind);
  int rhs_pointer = kind_is_pointer_like(rhs.kind);
  if (operator == PSX_TYPE_BINARY_ADD && lhs_pointer != rhs_pointer)
    return decay_pointer_like(
        semantic_context, lhs_pointer ? lhs_type : rhs_type,
        lhs_pointer ? &lhs : &rhs);
  if (operator == PSX_TYPE_BINARY_SUB) {
    if (lhs_pointer && rhs_pointer)
      return integer_result(
          semantic_context, PSX_INTEGER_KIND_LONG, 0);
    if (lhs_pointer)
      return decay_pointer_like(semantic_context, lhs_type, &lhs);
  }
  return usual_arithmetic_result(
      semantic_context, &lhs, &rhs);
}

psx_qual_type_t psx_resolve_conditional_result_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t then_type, psx_qual_type_t else_type) {
  psx_type_shape_t then_shape = {0};
  psx_type_shape_t else_shape = {0};
  if (!describe_type(semantic_context, then_type, &then_shape) ||
      !describe_type(semantic_context, else_type, &else_shape))
    return invalid_qual_type();
  if (then_shape.kind == PSX_TYPE_VOID ||
      else_shape.kind == PSX_TYPE_VOID)
    return then_shape.kind == PSX_TYPE_VOID &&
                   else_shape.kind == PSX_TYPE_VOID
               ? unqualified(then_type)
               : invalid_qual_type();
  if (kind_is_pointer_like(then_shape.kind))
    return decay_pointer_like(
        semantic_context, then_type, &then_shape);
  if (kind_is_pointer_like(else_shape.kind))
    return decay_pointer_like(
        semantic_context, else_type, &else_shape);
  if (then_shape.kind == else_shape.kind &&
      psx_type_kind_is_aggregate(then_shape.kind))
    return unqualified(then_type);
  return usual_arithmetic_result(
      semantic_context, &then_shape, &else_shape);
}

void psx_resolve_conditional_qual_types_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t condition_type,
    psx_qual_type_t then_type, psx_qual_type_t else_type,
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
  if (!psx_qual_type_is_scalar_in(
          semantic_context, condition_type)) {
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
  psx_type_shape_t shape = {0};
  return describe_type(semantic_context, type, &shape) &&
         psx_type_kind_is_scalar(shape.kind);
}

void psx_resolve_control_expression_qual_type_in(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t type,
    psx_control_expression_requirement_t requirement,
    psx_control_expression_status_t *status) {
  if (!status) return;
  *status = PSX_CONTROL_EXPRESSION_INVALID;
  psx_type_shape_t shape = {0};
  if (!describe_type(semantic_context, type, &shape)) return;
  if (requirement == PSX_CONTROL_EXPRESSION_REQUIRES_INTEGER) {
    *status = kind_is_integer(shape.kind)
                  ? PSX_CONTROL_EXPRESSION_OK
                  : PSX_CONTROL_EXPRESSION_NOT_INTEGER;
    return;
  }
  *status = psx_type_kind_is_scalar(shape.kind)
                ? PSX_CONTROL_EXPRESSION_OK
                : PSX_CONTROL_EXPRESSION_NOT_SCALAR;
}

psx_deref_operand_status_t psx_resolve_deref_operand_qual_type_in(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t operand_type) {
  psx_type_shape_t shape = {0};
  if (!describe_type(semantic_context, operand_type, &shape) ||
      !kind_is_pointer_like(shape.kind))
    return PSX_DEREF_OPERAND_NOT_POINTER;
  psx_qual_type_t pointee = psx_semantic_type_table_base(
      ps_ctx_semantic_type_table_in(semantic_context),
      operand_type.type_id);
  psx_type_shape_t pointee_shape = {0};
  return describe_type(semantic_context, pointee, &pointee_shape) &&
                 pointee_shape.kind == PSX_TYPE_VOID
             ? PSX_DEREF_OPERAND_VOID_POINTER
             : PSX_DEREF_OPERAND_OK;
}

psx_qual_type_t psx_resolve_indirection_result_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t operand_type) {
  psx_type_shape_t shape = {0};
  if (!describe_type(semantic_context, operand_type, &shape) ||
      !kind_is_pointer_like(shape.kind))
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
  if (resolution->result_qual_type.type_id !=
      PSX_TYPE_ID_INVALID)
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
  psx_type_shape_t shape = {0};
  if (!describe_type(semantic_context, operand_type, &shape) ||
      (shape.kind != PSX_TYPE_POINTER &&
       !kind_is_integer(shape.kind) &&
       shape.kind != PSX_TYPE_FLOAT))
    return;
  resolution->status = PSX_INCDEC_OPERAND_OK;
  resolution->result_qual_type = unqualified(operand_type);
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
  psx_type_shape_t shape = {0};
  if (!describe_type(semantic_context, expression_type, &shape))
    return invalid_qual_type();
  if (shape.kind == PSX_TYPE_ARRAY)
    return decay_pointer_like(
        semantic_context, expression_type, &shape);
  if (shape.kind == PSX_TYPE_FUNCTION)
    return ps_ctx_intern_pointer_to_qual_type_in(
        semantic_context, expression_type);
  return expression_type;
}

void psx_resolve_subscript_qual_types_in(
    const psx_semantic_context_t *semantic_context,
    psx_qual_type_t left_type, psx_qual_type_t right_type,
    psx_subscript_qual_types_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_SUBSCRIPT_OPERANDS_INVALID;
  resolution->base_qual_type = invalid_qual_type();
  resolution->index_qual_type = invalid_qual_type();
  resolution->result_qual_type = invalid_qual_type();
  psx_type_shape_t left = {0};
  psx_type_shape_t right = {0};
  if (!describe_type(semantic_context, left_type, &left) ||
      !describe_type(semantic_context, right_type, &right))
    return;
  if (kind_is_pointer_like(left.kind) &&
      kind_is_integer(right.kind)) {
    resolution->base_qual_type = left_type;
    resolution->index_qual_type = right_type;
  } else if (kind_is_pointer_like(right.kind) &&
             kind_is_integer(left.kind)) {
    resolution->base_qual_type = right_type;
    resolution->index_qual_type = left_type;
    resolution->swapped = 1;
  } else {
    return;
  }
  resolution->result_qual_type = psx_semantic_type_table_base(
      ps_ctx_semantic_type_table_in(semantic_context),
      resolution->base_qual_type.type_id);
  if (resolution->result_qual_type.type_id != PSX_TYPE_ID_INVALID)
    resolution->status = PSX_SUBSCRIPT_OPERANDS_OK;
}
