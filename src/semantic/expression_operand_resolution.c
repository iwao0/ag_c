#include "expression_operand_resolution.h"

#include "../parser/semantic_ctx.h"
#include "../type_system/integer_conversion.h"
#include "type_completeness.h"

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

static int kind_is_real(psx_type_kind_t kind) {
  return kind_is_integer(kind) || kind == PSX_TYPE_FLOAT;
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

static int unqualified_types_are_compatible(
    const psx_semantic_type_table_t *types,
    psx_qual_type_t left, psx_qual_type_t right) {
  left.qualifiers = PSX_TYPE_QUALIFIER_NONE;
  right.qualifiers = PSX_TYPE_QUALIFIER_NONE;
  return psx_semantic_type_table_types_compatible(
      types, left, right);
}

static int array_types_are_compatible(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t left, psx_qual_type_t right,
    int allow_element_qualifier_difference) {
  if (((left.qualifiers ^ right.qualifiers) &
       PSX_TYPE_QUALIFIER_ATOMIC) != 0 ||
      (!allow_element_qualifier_difference &&
       left.qualifiers != right.qualifiers))
    return 0;
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_type_shape_t left_shape = {0};
  psx_type_shape_t right_shape = {0};
  if (!describe_type(semantic_context, left, &left_shape) ||
      !describe_type(semantic_context, right, &right_shape) ||
      left_shape.kind != PSX_TYPE_ARRAY ||
      right_shape.kind != PSX_TYPE_ARRAY)
    return 0;
  int left_has_constant_bound =
      !left_shape.is_vla && left_shape.array_len > 0;
  int right_has_constant_bound =
      !right_shape.is_vla && right_shape.array_len > 0;
  if (left_has_constant_bound && right_has_constant_bound &&
      left_shape.array_len != right_shape.array_len)
    return 0;
  psx_qual_type_t left_element =
      psx_semantic_type_table_base(types, left.type_id);
  psx_qual_type_t right_element =
      psx_semantic_type_table_base(types, right.type_id);
  psx_type_shape_t left_element_shape = {0};
  psx_type_shape_t right_element_shape = {0};
  if (!describe_type(
          semantic_context, left_element, &left_element_shape) ||
      !describe_type(
          semantic_context, right_element, &right_element_shape) ||
      left_element_shape.kind != right_element_shape.kind)
    return 0;
  if (left_element_shape.kind == PSX_TYPE_ARRAY)
    return array_types_are_compatible(
        semantic_context, left_element, right_element,
        allow_element_qualifier_difference);
  if (((left_element.qualifiers ^ right_element.qualifiers) &
       PSX_TYPE_QUALIFIER_ATOMIC) != 0 ||
      (!allow_element_qualifier_difference &&
       left_element.qualifiers != right_element.qualifiers))
    return 0;
  return unqualified_types_are_compatible(
      types, left_element, right_element);
}

static int nested_pointed_types_are_compatible(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t left, psx_qual_type_t right,
    int require_identical_qualifiers) {
  if (require_identical_qualifiers &&
      left.qualifiers != right.qualifiers)
    return 0;
  psx_type_shape_t left_shape = {0};
  psx_type_shape_t right_shape = {0};
  if (!describe_type(semantic_context, left, &left_shape) ||
      !describe_type(semantic_context, right, &right_shape) ||
      left_shape.kind != right_shape.kind)
    return 0;
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  if (left_shape.kind == PSX_TYPE_POINTER)
    return nested_pointed_types_are_compatible(
        semantic_context,
        psx_semantic_type_table_base(types, left.type_id),
        psx_semantic_type_table_base(types, right.type_id), 1);
  if (left_shape.kind == PSX_TYPE_ARRAY)
    return array_types_are_compatible(
        semantic_context, left, right, 0);
  if (left_shape.kind == PSX_TYPE_FUNCTION)
    return psx_semantic_type_table_function_types_compatible(
        types, left, right);
  return unqualified_types_are_compatible(
      types, left, right);
}

static int pointer_types_are_compatible(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t left, psx_qual_type_t right,
    int allow_void_object_conversion,
    int allow_function_comparison,
    int require_complete_object,
    int allow_array_element_qualifier_difference) {
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_qual_type_t left_base = psx_semantic_type_table_base(
      types, left.type_id);
  psx_qual_type_t right_base = psx_semantic_type_table_base(
      types, right.type_id);
  psx_type_shape_t left_shape = {0};
  psx_type_shape_t right_shape = {0};
  if (!describe_type(semantic_context, left_base, &left_shape) ||
      !describe_type(semantic_context, right_base, &right_shape))
    return 0;
  int left_function = left_shape.kind == PSX_TYPE_FUNCTION;
  int right_function = right_shape.kind == PSX_TYPE_FUNCTION;
  if (left_function || right_function)
    return allow_function_comparison && !require_complete_object &&
           left_function && right_function &&
           psx_semantic_type_table_function_types_compatible(
               types, left_base, right_base);
  int left_atomic =
      (left_base.qualifiers & PSX_TYPE_QUALIFIER_ATOMIC) != 0;
  int right_atomic =
      (right_base.qualifiers & PSX_TYPE_QUALIFIER_ATOMIC) != 0;
  if (left_atomic != right_atomic &&
      !(allow_void_object_conversion &&
        (left_shape.kind == PSX_TYPE_VOID ||
         right_shape.kind == PSX_TYPE_VOID)))
    return 0;
  if (allow_void_object_conversion &&
      (left_shape.kind == PSX_TYPE_VOID ||
       right_shape.kind == PSX_TYPE_VOID))
    return 1;
  if (left_shape.kind == PSX_TYPE_VOID ||
      right_shape.kind == PSX_TYPE_VOID ||
      (left_shape.kind == PSX_TYPE_ARRAY
           ? !array_types_are_compatible(
                 semantic_context,
                 (psx_qual_type_t){
                     left_base.type_id,
                     PSX_TYPE_QUALIFIER_NONE},
                 (psx_qual_type_t){
                     right_base.type_id,
                     PSX_TYPE_QUALIFIER_NONE},
                 allow_array_element_qualifier_difference)
           : left_shape.kind == PSX_TYPE_POINTER
                 ? !nested_pointed_types_are_compatible(
                       semantic_context, left_base, right_base, 0)
           : !unqualified_types_are_compatible(
                 types, left_base, right_base)))
    return 0;
  return !require_complete_object ||
         (psx_semantic_type_is_complete_object_in(
              semantic_context, left_base.type_id) &&
          psx_semantic_type_is_complete_object_in(
              semantic_context, right_base.type_id));
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
  if (operator == PSX_TYPE_BINARY_EQUALITY ||
      operator == PSX_TYPE_BINARY_RELATIONAL ||
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

void psx_resolve_binary_qual_types_in(
    psx_semantic_context_t *semantic_context,
    psx_type_binary_op_t operator,
    psx_qual_type_t lhs_type, psx_qual_type_t rhs_type,
    int lhs_is_null_pointer_constant,
    int rhs_is_null_pointer_constant,
    psx_binary_types_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_BINARY_TYPES_INVALID;
  resolution->result_qual_type = invalid_qual_type();
  psx_type_shape_t lhs = {0};
  psx_type_shape_t rhs = {0};
  if (!semantic_context ||
      !describe_type(semantic_context, lhs_type, &lhs) ||
      !describe_type(semantic_context, rhs_type, &rhs))
    return;

  int compatible = 0;
  int lhs_pointer = kind_is_pointer_like(lhs.kind);
  int rhs_pointer = kind_is_pointer_like(rhs.kind);
  psx_qual_type_t effective_lhs = lhs_pointer
      ? decay_pointer_like(semantic_context, lhs_type, &lhs)
      : lhs_type;
  psx_qual_type_t effective_rhs = rhs_pointer
      ? decay_pointer_like(semantic_context, rhs_type, &rhs)
      : rhs_type;
  switch (operator) {
    case PSX_TYPE_BINARY_COMMA:
      compatible = 1;
      break;
    case PSX_TYPE_BINARY_LOGICAL:
      compatible = psx_type_kind_is_scalar(lhs.kind) &&
                   psx_type_kind_is_scalar(rhs.kind);
      break;
    case PSX_TYPE_BINARY_EQUALITY:
      if (kind_is_arithmetic(lhs.kind) &&
          kind_is_arithmetic(rhs.kind)) {
        compatible = 1;
      } else if (lhs_pointer && rhs_pointer) {
        compatible = pointer_types_are_compatible(
            semantic_context, effective_lhs, effective_rhs,
            1, 1, 0, 1);
      } else if (lhs_pointer && kind_is_integer(rhs.kind)) {
        compatible = rhs_is_null_pointer_constant;
      } else if (rhs_pointer && kind_is_integer(lhs.kind)) {
        compatible = lhs_is_null_pointer_constant;
      }
      break;
    case PSX_TYPE_BINARY_RELATIONAL:
      if (kind_is_real(lhs.kind) && kind_is_real(rhs.kind)) {
        compatible = 1;
      } else if (lhs_pointer && rhs_pointer) {
        compatible = pointer_types_are_compatible(
            semantic_context, effective_lhs, effective_rhs,
            0, 0, 0, 1);
      }
      break;
    case PSX_TYPE_BINARY_ADD:
      compatible =
          (kind_is_arithmetic(lhs.kind) &&
           kind_is_arithmetic(rhs.kind)) ||
          (lhs_pointer && kind_is_integer(rhs.kind) &&
           psx_semantic_pointer_points_to_complete_object_in(
               semantic_context, effective_lhs)) ||
          (rhs_pointer && kind_is_integer(lhs.kind) &&
           psx_semantic_pointer_points_to_complete_object_in(
               semantic_context, effective_rhs));
      break;
    case PSX_TYPE_BINARY_SUB:
      compatible =
          (kind_is_arithmetic(lhs.kind) &&
           kind_is_arithmetic(rhs.kind)) ||
          (lhs_pointer && kind_is_integer(rhs.kind) &&
           psx_semantic_pointer_points_to_complete_object_in(
               semantic_context, effective_lhs)) ||
          (lhs_pointer && rhs_pointer &&
           pointer_types_are_compatible(
               semantic_context, effective_lhs, effective_rhs,
               0, 0, 1, 1));
      break;
    case PSX_TYPE_BINARY_MUL:
    case PSX_TYPE_BINARY_DIV:
      compatible = kind_is_arithmetic(lhs.kind) &&
                   kind_is_arithmetic(rhs.kind);
      break;
    case PSX_TYPE_BINARY_MOD:
    case PSX_TYPE_BINARY_BITAND:
    case PSX_TYPE_BINARY_BITXOR:
    case PSX_TYPE_BINARY_BITOR:
    case PSX_TYPE_BINARY_SHL:
    case PSX_TYPE_BINARY_SHR:
      compatible = kind_is_integer(lhs.kind) &&
                   kind_is_integer(rhs.kind);
      break;
  }
  if (!compatible) {
    resolution->status = PSX_BINARY_OPERANDS_INCOMPATIBLE;
    return;
  }
  resolution->result_qual_type =
      psx_resolve_binary_result_qual_type_in(
          semantic_context, operator, lhs_type, rhs_type);
  if (resolution->result_qual_type.type_id != PSX_TYPE_ID_INVALID)
    resolution->status = PSX_BINARY_TYPES_OK;
}

psx_qual_type_t psx_resolve_conditional_result_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t then_type, psx_qual_type_t else_type,
    int then_is_null_pointer_constant,
    int else_is_null_pointer_constant) {
  if (!semantic_context) return invalid_qual_type();
  then_type = psx_resolve_value_decay_qual_type_in(
      semantic_context, then_type);
  else_type = psx_resolve_value_decay_qual_type_in(
      semantic_context, else_type);
  psx_type_shape_t then_shape = {0};
  psx_type_shape_t else_shape = {0};
  if (!describe_type(semantic_context, then_type, &then_shape) ||
      !describe_type(semantic_context, else_type, &else_shape))
    return invalid_qual_type();
  if (kind_is_arithmetic(then_shape.kind) &&
      kind_is_arithmetic(else_shape.kind))
    return usual_arithmetic_result(
        semantic_context, &then_shape, &else_shape);
  if (then_shape.kind == PSX_TYPE_VOID ||
      else_shape.kind == PSX_TYPE_VOID)
    return then_shape.kind == PSX_TYPE_VOID &&
                   else_shape.kind == PSX_TYPE_VOID
               ? unqualified(then_type)
               : invalid_qual_type();

  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  if (psx_type_kind_is_aggregate(then_shape.kind) ||
      psx_type_kind_is_aggregate(else_shape.kind)) {
    if (then_shape.kind != else_shape.kind ||
        !psx_semantic_type_table_unqualified_types_match(
            types, then_type, else_type))
      return invalid_qual_type();
    return unqualified(then_type);
  }

  int then_pointer = then_shape.kind == PSX_TYPE_POINTER;
  int else_pointer = else_shape.kind == PSX_TYPE_POINTER;
  if (then_pointer && kind_is_integer(else_shape.kind))
    return else_is_null_pointer_constant
               ? unqualified(then_type)
               : invalid_qual_type();
  if (else_pointer && kind_is_integer(then_shape.kind))
    return then_is_null_pointer_constant
               ? unqualified(else_type)
               : invalid_qual_type();
  if (!then_pointer || !else_pointer ||
      !pointer_types_are_compatible(
          semantic_context, then_type, else_type,
          1, 1, 0, 0))
    return invalid_qual_type();

  psx_qual_type_t then_base =
      psx_semantic_type_table_base(types, then_type.type_id);
  psx_qual_type_t else_base =
      psx_semantic_type_table_base(types, else_type.type_id);
  psx_type_shape_t then_base_shape = {0};
  psx_type_shape_t else_base_shape = {0};
  if (!describe_type(
          semantic_context, then_base, &then_base_shape) ||
      !describe_type(
          semantic_context, else_base, &else_base_shape))
    return invalid_qual_type();

  psx_qual_type_t common_base = invalid_qual_type();
  if (then_base_shape.kind == PSX_TYPE_VOID) {
    common_base = then_base;
  } else if (else_base_shape.kind == PSX_TYPE_VOID) {
    common_base = else_base;
  } else {
    common_base = ps_ctx_composite_qual_type_in(
        semantic_context, unqualified(then_base),
        unqualified(else_base));
    if (common_base.type_id == PSX_TYPE_ID_INVALID)
      return invalid_qual_type();
  }
  common_base.qualifiers =
      then_base.qualifiers | else_base.qualifiers;
  if (then_base_shape.kind == PSX_TYPE_VOID ||
      else_base_shape.kind == PSX_TYPE_VOID)
    common_base.qualifiers &= ~PSX_TYPE_QUALIFIER_ATOMIC;
  return ps_ctx_intern_pointer_to_qual_type_in(
      semantic_context, common_base);
}

void psx_resolve_conditional_qual_types_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t condition_type,
    psx_qual_type_t then_type, psx_qual_type_t else_type,
    int then_is_null_pointer_constant,
    int else_is_null_pointer_constant,
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
          semantic_context, then_type, else_type,
          then_is_null_pointer_constant,
          else_is_null_pointer_constant);
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
    psx_semantic_context_t *semantic_context,
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
  if (shape.kind == PSX_TYPE_POINTER &&
      !psx_semantic_pointer_points_to_complete_object_in(
          semantic_context, operand_type)) {
    resolution->status =
        PSX_INCDEC_OPERAND_POINTER_NOT_COMPLETE_OBJECT;
    return;
  }
  resolution->status = PSX_INCDEC_OPERAND_OK;
  resolution->result_qual_type = unqualified(operand_type);
}

psx_qual_type_t psx_resolve_incdec_result_qual_type_in(
    psx_semantic_context_t *semantic_context,
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
    psx_semantic_context_t *semantic_context,
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
  resolution->result_qual_type =
      psx_semantic_type_table_base(
          ps_ctx_semantic_type_table_in(semantic_context),
          resolution->base_qual_type.type_id);
  if (resolution->result_qual_type.type_id ==
      PSX_TYPE_ID_INVALID)
    return;
  if (!psx_semantic_type_is_complete_object_in(
          semantic_context,
          resolution->result_qual_type.type_id)) {
    resolution->status =
        PSX_SUBSCRIPT_BASE_NOT_COMPLETE_OBJECT;
    resolution->result_qual_type = invalid_qual_type();
    return;
  }
  resolution->status = PSX_SUBSCRIPT_OPERANDS_OK;
}
