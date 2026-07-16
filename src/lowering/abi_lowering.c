#include "abi_lowering.h"
#include "../parser/type.h"
#include "../target_info.h"
#include "../type_layout.h"

#include <string.h>

static ir_abi_param_info_t abi_param_unknown(void) {
  return (ir_abi_param_info_t){
      .type = IR_TY_VOID,
      .param_class = IR_ABI_PARAM_UNKNOWN,
  };
}

static int aggregate_has_direct_integer_width(int size) {
  return size == 1 || size == 2 || size == 4 || size == 8;
}

ir_abi_param_info_t ir_abi_classify_builtin_param(
    const ir_abi_type_context_t *context,
    const char *name, int name_len, int param_idx) {
  if (name_len == 6 && strncmp(name, "memset", 6) == 0) {
    if (param_idx == 0) {
      return (ir_abi_param_info_t){
          .type = IR_TY_PTR,
          .param_class = IR_ABI_PARAM_POINTER,
          .source_size = context && context->target
                             ? ag_target_info_pointer_size(context->target)
                             : 0,
      };
    }
    if (param_idx == 1) {
      return (ir_abi_param_info_t){
          .type = IR_TY_I32,
          .param_class = IR_ABI_PARAM_INTEGER,
          .source_size = 4,
      };
    }
    if (param_idx == 2) {
      return (ir_abi_param_info_t){
          .type = IR_TY_I64,
          .param_class = IR_ABI_PARAM_INTEGER,
          .source_size = 8,
          .is_unsigned = 1,
      };
    }
  }
  return abi_param_unknown();
}

ir_abi_param_info_t ir_abi_classify_type_id(
    const ir_abi_type_context_t *context, psx_type_id_t type_id) {
  if (!context || !context->semantic_types || !context->record_layouts ||
      !context->target || type_id == PSX_TYPE_ID_INVALID)
    return abi_param_unknown();
  const psx_type_t *type = psx_semantic_type_table_lookup(
      context->semantic_types, type_id);
  if (!type) return abi_param_unknown();

  ir_abi_param_info_t info = {
      .type = IR_TY_VOID,
      .param_class = IR_ABI_PARAM_UNKNOWN,
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
      info.param_class = IR_ABI_PARAM_POINTER;
      return info;
    case PSX_TYPE_STRUCT:
    case PSX_TYPE_UNION:
      info.type = aggregate_has_direct_integer_width(info.source_size)
                      ? (info.source_size == 8 ? IR_TY_I64 : IR_TY_I32)
                      : IR_TY_PTR;
      info.param_class = IR_ABI_PARAM_AGGREGATE;
      return info;
    case PSX_TYPE_FLOAT:
    case PSX_TYPE_COMPLEX:
      info.type = type->floating_kind == PSX_FLOATING_KIND_FLOAT
                      ? IR_TY_F32 : IR_TY_F64;
      info.param_class = IR_ABI_PARAM_FLOAT;
      return info;
    case PSX_TYPE_BOOL:
    case PSX_TYPE_INTEGER:
      info.type = info.source_size > 4 ? IR_TY_I64 : IR_TY_I32;
      info.param_class = IR_ABI_PARAM_INTEGER;
      return info;
    default:
      return info;
  }
}

int ir_abi_callable_sig_from_type_id(
    const ir_abi_type_context_t *context, psx_type_id_t type_id,
    ir_callable_sig_t *out) {
  if (!context || !context->semantic_types || !out) return 0;
  memset(out, 0, sizeof(*out));
  const psx_type_t *function = psx_semantic_type_table_lookup(
      context->semantic_types, type_id);
  while (function && (function->kind == PSX_TYPE_POINTER ||
                      function->kind == PSX_TYPE_ARRAY)) {
    type_id = psx_semantic_type_table_base(
        context->semantic_types, type_id).type_id;
    function = psx_semantic_type_table_lookup(
        context->semantic_types, type_id);
  }
  if (!function || function->kind != PSX_TYPE_FUNCTION) return 0;

  psx_type_id_t result_type_id = psx_semantic_type_table_base(
      context->semantic_types, type_id).type_id;
  ir_abi_param_info_t result = ir_abi_classify_type_id(
      context, result_type_id);
  out->result = function->base && function->base->kind == PSX_TYPE_VOID
                    ? IR_TY_VOID
                    : result.type;
  int count = function->param_count;
  if (count < 0) count = 0;
  if (count > IR_CALLABLE_MAX_PARAMS) return 0;
  out->param_count = (unsigned char)count;
  out->is_variadic = function->is_variadic_function ? 1 : 0;
  for (int i = 0; i < count; i++) {
    psx_type_id_t param_type_id = psx_semantic_type_table_parameter(
        context->semantic_types, type_id, i).type_id;
    ir_abi_param_info_t param = ir_abi_classify_type_id(
        context, param_type_id);
    out->params[i] = param.type == IR_TY_VOID ? IR_TY_I32 : param.type;
  }
  return 1;
}
