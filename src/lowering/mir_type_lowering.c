#include "mir_type_lowering.h"

#include "../target_info.h"
#include "../type_layout.h"
#include "../type_system/integer_conversion.h"

static ir_mir_type_info_t unknown_type(void) {
  return (ir_mir_type_info_t){
      .type = IR_TY_VOID,
      .type_class = IR_MIR_TYPE_UNKNOWN,
  };
}

ir_mir_type_info_t ir_mir_classify_type_id(
    const ir_mir_type_context_t *context, psx_type_id_t type_id) {
  if (!context || !context->semantic_types || !context->record_layouts ||
      !ag_data_layout_is_valid(context->data_layout) ||
      type_id == PSX_TYPE_ID_INVALID)
    return unknown_type();
  psx_type_shape_t type = {0};
  if (!psx_semantic_type_table_describe(
          context->semantic_types, type_id, &type))
    return unknown_type();

  ir_mir_type_info_t info = {
      .type = IR_TY_VOID,
      .type_class = IR_MIR_TYPE_UNKNOWN,
      .source_size =
          ps_type_sizeof_id(context->semantic_types, context->record_layouts,
                            type_id, context->data_layout),
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
      info.integer_rank = psx_integer_conversion_from_shape(&type).rank;
      info.type = info.source_size > 4 ? IR_TY_I64 : IR_TY_I32;
      info.type_class = IR_MIR_TYPE_INTEGER;
      return info;
    default:
      return info;
  }
}

static psx_integer_conversion_t integer_conversion_type(
    ir_mir_type_info_t type) {
  return (psx_integer_conversion_t){
      .rank = type.integer_rank,
      .is_unsigned = type.is_unsigned ? 1 : 0,
      .is_integer = type.type_class == IR_MIR_TYPE_INTEGER ? 1 : 0,
  };
}

int ir_mir_integer_promotion_is_unsigned(ir_mir_type_info_t type,
                                         const ag_data_layout_t *data_layout) {
  return psx_integer_promotion_for_data_layout(
             integer_conversion_type(type), data_layout)
      .is_unsigned;
}

int ir_mir_usual_arithmetic_result_is_unsigned(
    ir_mir_type_info_t left, ir_mir_type_info_t right,
    const ag_data_layout_t *data_layout) {
  return psx_usual_integer_conversion_for_data_layout(
             integer_conversion_type(left), integer_conversion_type(right),
             data_layout)
      .is_unsigned;
}
