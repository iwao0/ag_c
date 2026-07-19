#include "mir_type_lowering.h"

#include "../target_info.h"
#include "../type_layout.h"

static ir_mir_type_info_t unknown_type(void) {
  return (ir_mir_type_info_t){
      .type = IR_TY_VOID,
      .type_class = IR_MIR_TYPE_UNKNOWN,
  };
}

static int integer_rank(const psx_type_shape_t *type) {
  if (!type || type->kind != PSX_TYPE_INTEGER) return 0;
  if (type->is_plain_char) return 1;
  switch (type->integer_kind) {
    case PSX_INTEGER_KIND_CHAR: return 1;
    case PSX_INTEGER_KIND_SHORT: return 2;
    case PSX_INTEGER_KIND_INT:
    case PSX_INTEGER_KIND_ENUM: return 3;
    case PSX_INTEGER_KIND_LONG: return 4;
    case PSX_INTEGER_KIND_LONG_LONG: return 5;
    default: return 0;
  }
}

ir_mir_type_info_t ir_mir_classify_type_id(
    const ir_mir_type_context_t *context, psx_type_id_t type_id) {
  if (!context || !context->semantic_types || !context->record_layouts ||
      !context->target || type_id == PSX_TYPE_ID_INVALID)
    return unknown_type();
  psx_type_shape_t type = {0};
  if (!psx_semantic_type_table_describe(
          context->semantic_types, type_id, &type))
    return unknown_type();

  ir_mir_type_info_t info = {
      .type = IR_TY_VOID,
      .type_class = IR_MIR_TYPE_UNKNOWN,
      .source_size = ps_type_sizeof_id(
          context->semantic_types, context->record_layouts,
          type_id, context->target),
      .is_unsigned = type.is_unsigned,
  };
  switch (type.kind) {
    case PSX_TYPE_POINTER:
    case PSX_TYPE_ARRAY:
    case PSX_TYPE_FUNCTION:
      info.type = IR_TY_PTR;
      info.type_class = IR_MIR_TYPE_POINTER;
      return info;
    case PSX_TYPE_STRUCT:
    case PSX_TYPE_UNION:
      info.type = IR_TY_PTR;
      info.type_class = IR_MIR_TYPE_AGGREGATE;
      return info;
    case PSX_TYPE_FLOAT:
      info.type = type.floating_kind == PSX_FLOATING_KIND_FLOAT
                      ? IR_TY_F32 : IR_TY_F64;
      info.type_class = IR_MIR_TYPE_FLOAT;
      return info;
    case PSX_TYPE_COMPLEX:
      info.type = type.floating_kind == PSX_FLOATING_KIND_FLOAT
                      ? IR_TY_F32 : IR_TY_F64;
      info.type_class = IR_MIR_TYPE_COMPLEX;
      return info;
    case PSX_TYPE_BOOL:
      info.integer_rank = 0;
      info.type = info.source_size > 4 ? IR_TY_I64 : IR_TY_I32;
      info.type_class = IR_MIR_TYPE_INTEGER;
      return info;
    case PSX_TYPE_INTEGER:
      info.integer_rank = integer_rank(&type);
      info.type = info.source_size > 4 ? IR_TY_I64 : IR_TY_I32;
      info.type_class = IR_MIR_TYPE_INTEGER;
      return info;
    default:
      return info;
  }
}

static ag_target_scalar_kind_t scalar_kind_for_rank(int rank) {
  if (rank >= 5) return AG_TARGET_SCALAR_LONG_LONG;
  if (rank == 4) return AG_TARGET_SCALAR_LONG;
  if (rank == 2) return AG_TARGET_SCALAR_SHORT;
  if (rank == 1) return AG_TARGET_SCALAR_CHAR;
  return AG_TARGET_SCALAR_INT;
}

static int integer_size_for_rank(
    int rank, const ag_target_info_t *target) {
  return ag_target_info_scalar_size(target, scalar_kind_for_rank(rank));
}

int ir_mir_integer_promotion_is_unsigned(
    ir_mir_type_info_t type, const ag_target_info_t *target) {
  if (type.type_class != IR_MIR_TYPE_INTEGER || !target ||
      type.integer_rank <= 0)
    return 0;
  if (type.integer_rank >= 3) return type.is_unsigned;
  return type.is_unsigned &&
         integer_size_for_rank(type.integer_rank, target) >=
             integer_size_for_rank(3, target);
}

typedef struct {
  int rank;
  int is_unsigned;
} ir_mir_integer_conversion_t;

static ir_mir_integer_conversion_t promoted_integer(
    ir_mir_type_info_t type, const ag_target_info_t *target) {
  return (ir_mir_integer_conversion_t){
      .rank = type.integer_rank < 3 ? 3 : type.integer_rank,
      .is_unsigned = ir_mir_integer_promotion_is_unsigned(type, target),
  };
}

int ir_mir_usual_arithmetic_result_is_unsigned(
    ir_mir_type_info_t left, ir_mir_type_info_t right,
    const ag_target_info_t *target) {
  if (left.type_class != IR_MIR_TYPE_INTEGER ||
      right.type_class != IR_MIR_TYPE_INTEGER || !target)
    return 0;
  ir_mir_integer_conversion_t lhs = promoted_integer(left, target);
  ir_mir_integer_conversion_t rhs = promoted_integer(right, target);
  if (lhs.is_unsigned == rhs.is_unsigned) return lhs.is_unsigned;
  ir_mir_integer_conversion_t unsigned_type =
      lhs.is_unsigned ? lhs : rhs;
  ir_mir_integer_conversion_t signed_type =
      lhs.is_unsigned ? rhs : lhs;
  if (unsigned_type.rank >= signed_type.rank) return 1;
  return integer_size_for_rank(signed_type.rank, target) <=
         integer_size_for_rank(unsigned_type.rank, target);
}
