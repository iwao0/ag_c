#include "mir_type_lowering.h"

#include "../parser/type.h"
#include "../type_layout.h"

static ir_mir_type_info_t unknown_type(void) {
  return (ir_mir_type_info_t){
      .type = IR_TY_VOID,
      .type_class = IR_MIR_TYPE_UNKNOWN,
  };
}

ir_mir_type_info_t ir_mir_classify_type_id(
    const ir_mir_type_context_t *context, psx_type_id_t type_id) {
  if (!context || !context->semantic_types || !context->record_layouts ||
      !context->target || type_id == PSX_TYPE_ID_INVALID)
    return unknown_type();
  const psx_type_t *type = psx_semantic_type_table_lookup(
      context->semantic_types, type_id);
  if (!type) return unknown_type();

  ir_mir_type_info_t info = {
      .type = IR_TY_VOID,
      .type_class = IR_MIR_TYPE_UNKNOWN,
      .source_size = ps_type_sizeof_id_with_records(
          context->semantic_types, context->record_layouts,
          type_id, context->target),
      .is_unsigned = ps_type_is_unsigned(type),
  };
  switch (type->kind) {
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
      info.type = type->floating_kind == PSX_FLOATING_KIND_FLOAT
                      ? IR_TY_F32 : IR_TY_F64;
      info.type_class = IR_MIR_TYPE_FLOAT;
      return info;
    case PSX_TYPE_COMPLEX:
      info.type = type->floating_kind == PSX_FLOATING_KIND_FLOAT
                      ? IR_TY_F32 : IR_TY_F64;
      info.type_class = IR_MIR_TYPE_COMPLEX;
      return info;
    case PSX_TYPE_BOOL:
    case PSX_TYPE_INTEGER:
      info.type = info.source_size > 4 ? IR_TY_I64 : IR_TY_I32;
      info.type_class = IR_MIR_TYPE_INTEGER;
      return info;
    default:
      return info;
  }
}
