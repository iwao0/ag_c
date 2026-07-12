#include "abi_lowering.h"
#include "../parser/parser_public.h"

#include <string.h>

static ir_abi_param_info_t abi_param_unknown(void) {
  return (ir_abi_param_info_t){
      .type = IR_TY_VOID,
      .param_class = IR_ABI_PARAM_UNKNOWN,
  };
}

static ir_abi_param_info_t abi_compiler_builtin_param(
    const char *name, int name_len, int param_idx) {
  if (name_len == 6 && strncmp(name, "memset", 6) == 0) {
    if (param_idx == 0) {
      return (ir_abi_param_info_t){
          .type = IR_TY_PTR,
          .param_class = IR_ABI_PARAM_POINTER,
          .source_size = 4,
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

ir_abi_param_info_t ir_abi_classify_function_param(char *name, int name_len,
                                                    int param_idx) {
  ir_abi_param_info_t builtin =
      abi_compiler_builtin_param(name, name_len, param_idx);
  if (builtin.param_class != IR_ABI_PARAM_UNKNOWN) return builtin;
  const psx_type_t *type =
      ps_ctx_get_function_param_type(name, name_len, param_idx);
  if (!type) return abi_param_unknown();

  ir_abi_param_info_t info = {
      .type = IR_TY_VOID,
      .param_class = IR_ABI_PARAM_UNKNOWN,
      .source_size = ps_type_sizeof(type),
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
      info.type = info.source_size > 0 && info.source_size <= 4
                      ? IR_TY_I32
                  : info.source_size == 8 ? IR_TY_I64
                                          : IR_TY_PTR;
      info.param_class = IR_ABI_PARAM_AGGREGATE;
      return info;
    case PSX_TYPE_FLOAT:
    case PSX_TYPE_COMPLEX:
      info.type = type->fp_kind == TK_FLOAT_KIND_FLOAT ? IR_TY_F32 : IR_TY_F64;
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
