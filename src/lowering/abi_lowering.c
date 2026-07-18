#include "abi_lowering.h"
#include "../parser/type.h"
#include "../target_info.h"
#include "../type_layout.h"

#include <stdlib.h>
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
  ir_callable_sig_dispose(out);
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
  int count = function->param_count;
  if (count < 0) count = 0;
  ir_type_t *params = NULL;
  if (count > 0) {
    params = calloc((size_t)count, sizeof(*params));
    if (!params) return 0;
  }
  for (int i = 0; i < count; i++) {
    psx_type_id_t param_type_id = psx_semantic_type_table_parameter(
        context->semantic_types, type_id, i).type_id;
    ir_abi_param_info_t param = ir_abi_classify_type_id(
        context, param_type_id);
    params[i] = param.type == IR_TY_VOID ? IR_TY_I32 : param.type;
  }
  int ok = ir_callable_sig_set(
      out,
      function->base && function->base->kind == PSX_TYPE_VOID
          ? IR_TY_VOID : result.type,
      params, (size_t)count,
      function->is_variadic_function);
  free(params);
  return ok;
}

static int type_is_complex(
    const ir_abi_type_context_t *context, psx_type_id_t type_id) {
  const psx_type_t *type = context && context->semantic_types
                               ? psx_semantic_type_table_lookup(
                                     context->semantic_types, type_id)
                               : NULL;
  return type && type->kind == PSX_TYPE_COMPLEX;
}

static int type_is_aggregate(
    const ir_abi_type_context_t *context, psx_type_id_t type_id) {
  const psx_type_t *type = context && context->semantic_types
                               ? psx_semantic_type_table_lookup(
                                     context->semantic_types, type_id)
                               : NULL;
  return type && (type->kind == PSX_TYPE_STRUCT ||
                  type->kind == PSX_TYPE_UNION);
}

static size_t type_piece_count(
    const ir_abi_type_context_t *context, psx_type_id_t type_id,
    ir_abi_param_info_t info) {
  if (type_is_complex(context, type_id)) return 2;
  (void)info;
  return 1;
}

static int lower_function_type_signature(
    const ir_abi_type_context_t *context,
    const ir_function_type_t *function_type,
    ir_abi_signature_t *out) {
  if (!context || !function_type || !out ||
      function_type->type_id == PSX_TYPE_ID_INVALID)
    return 0;
  memset(out, 0, sizeof(*out));
  out->result_area = ir_val_none();
  out->result_area_vreg = -1;
  out->result = ir_abi_classify_type_id(
      context, function_type->result.type_id);
  out->result_size = out->result.source_size;
  out->result_is_indirect =
      type_is_aggregate(context, function_type->result.type_id) &&
      !aggregate_has_direct_integer_width(out->result.source_size);
  if (type_is_complex(context, function_type->result.type_id))
    out->result_complex_half = (unsigned char)ir_type_size(out->result.type);
  out->is_variadic = function_type->is_variadic;

  size_t piece_count = 0;
  for (size_t i = 0; i < function_type->param_count; i++) {
    ir_abi_param_info_t info = ir_abi_classify_type_id(
        context, function_type->params[i].type_id);
    size_t pieces = type_piece_count(
        context, function_type->params[i].type_id, info);
    if (pieces > SIZE_MAX - piece_count) return 0;
    piece_count += pieces;
  }
  if (piece_count > 0) {
    out->param_types = calloc(piece_count, sizeof(*out->param_types));
    if (!out->param_types) return 0;
  }
  for (size_t i = 0, piece = 0; i < function_type->param_count; i++) {
    psx_type_id_t type_id = function_type->params[i].type_id;
    ir_abi_param_info_t info = ir_abi_classify_type_id(context, type_id);
    if (type_is_complex(context, type_id)) {
      out->param_types[piece++] = info.type;
      out->param_types[piece++] = info.type;
    } else {
      out->param_types[piece++] = info.type == IR_TY_VOID
                                      ? IR_TY_I32 : info.type;
    }
  }
  out->param_count = piece_count;
  out->fixed_param_count = piece_count;
  return 1;
}

static void dispose_signature(ir_abi_signature_t *signature) {
  if (!signature) return;
  free(signature->param_types);
  memset(signature, 0, sizeof(*signature));
}

static size_t count_module_calls(const ir_module_t *module) {
  size_t count = 0;
  for (const ir_func_t *function = module ? module->funcs : NULL;
       function; function = function->next) {
    for (const ir_block_t *block = function->entry; block;
         block = block->next) {
      for (const ir_inst_t *instruction = block->head; instruction;
           instruction = instruction->next) {
        if (instruction->op == IR_CALL) count++;
      }
    }
  }
  return count;
}

ir_abi_module_t *ir_abi_lower_module(
    const ir_abi_type_context_t *context,
    const ir_module_t *module) {
  if (!context || !module) return NULL;
  ir_abi_module_t *abi = calloc(1, sizeof(*abi));
  if (!abi) return NULL;
  for (const ir_func_t *function = module->funcs; function;
       function = function->next)
    abi->function_count++;
  abi->call_count = count_module_calls(module);
  if (abi->function_count > 0) {
    abi->functions = calloc(
        abi->function_count, sizeof(*abi->functions));
    if (!abi->functions) goto fail;
  }
  if (abi->call_count > 0) {
    abi->calls = calloc(abi->call_count, sizeof(*abi->calls));
    if (!abi->calls) goto fail;
  }

  size_t function_index = 0;
  size_t call_index = 0;
  for (const ir_func_t *function = module->funcs; function;
       function = function->next) {
    ir_abi_function_t *lowered = &abi->functions[function_index++];
    lowered->function = function;
    if (!lower_function_type_signature(
            context, &function->function_type, &lowered->signature))
      goto fail;
    lowered->signature.result_area_vreg = function->result_area_vreg;
    for (const ir_block_t *block = function->entry; block;
         block = block->next) {
      for (const ir_inst_t *instruction = block->head; instruction;
           instruction = instruction->next) {
        if (instruction->op != IR_CALL) continue;
        ir_abi_call_t *call = &abi->calls[call_index++];
        call->call = instruction;
        if (!lower_function_type_signature(
                context, &instruction->function_type,
                &call->signature))
          goto fail;
        size_t declared_piece_count = call->signature.param_count;
        size_t actual_count = instruction->nargs > 0
                                  ? (size_t)instruction->nargs : 0;
        if (call->signature.is_variadic &&
            actual_count < declared_piece_count)
          goto fail;
        if (actual_count != declared_piece_count) {
          ir_type_t *types = realloc(
              call->signature.param_types,
              actual_count * sizeof(*types));
          if (actual_count > 0 && !types) goto fail;
          call->signature.param_types = types;
        }
        for (size_t i = declared_piece_count; i < actual_count; i++) {
          call->signature.param_types[i] = instruction->args[i].type;
        }
        call->signature.param_count = actual_count;
        call->signature.fixed_param_count =
            call->signature.is_variadic
                ? declared_piece_count : actual_count;
        call->signature.is_variadic =
            call->signature.is_variadic &&
            actual_count > declared_piece_count;
        call->signature.result_area = instruction->result_area;
      }
    }
  }
  return abi;

fail:
  ir_abi_module_free(abi);
  return NULL;
}

void ir_abi_module_free(ir_abi_module_t *module) {
  if (!module) return;
  for (size_t i = 0; i < module->function_count; i++)
    dispose_signature(&module->functions[i].signature);
  for (size_t i = 0; i < module->call_count; i++)
    dispose_signature(&module->calls[i].signature);
  free(module->functions);
  free(module->calls);
  free(module);
}

const ir_abi_signature_t *ir_abi_function_signature(
    const ir_abi_module_t *module, const ir_func_t *function) {
  if (!module || !function) return NULL;
  for (size_t i = 0; i < module->function_count; i++) {
    if (module->functions[i].function == function)
      return &module->functions[i].signature;
  }
  return NULL;
}

const ir_abi_signature_t *ir_abi_call_signature(
    const ir_abi_module_t *module, const ir_inst_t *call) {
  if (!module || !call) return NULL;
  for (size_t i = 0; i < module->call_count; i++) {
    if (module->calls[i].call == call)
      return &module->calls[i].signature;
  }
  return NULL;
}
