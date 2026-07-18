#include "abi_lowering.h"
#include "../parser/type.h"
#include "../target_info.h"
#include "../type_layout.h"

#include <stdlib.h>
#include <string.h>

typedef enum {
  IR_ABI_PARAM_UNKNOWN = 0,
  IR_ABI_PARAM_INTEGER,
  IR_ABI_PARAM_FLOAT,
  IR_ABI_PARAM_POINTER,
  IR_ABI_PARAM_AGGREGATE,
} ir_abi_param_class_t;

typedef struct {
  ir_type_t type;
  ir_abi_param_class_t param_class;
  int source_size;
  int is_unsigned;
} ir_abi_param_info_t;

static ir_abi_param_info_t abi_param_unknown(void) {
  return (ir_abi_param_info_t){
      .type = IR_TY_VOID,
      .param_class = IR_ABI_PARAM_UNKNOWN,
  };
}

static int aggregate_has_direct_integer_width(int size) {
  return size == 1 || size == 2 || size == 4 || size == 8;
}

static ir_type_t aggregate_direct_integer_type(int size) {
  switch (size) {
    case 1: return IR_TY_I8;
    case 2: return IR_TY_I16;
    case 4: return IR_TY_I32;
    case 8: return IR_TY_I64;
    default: return IR_TY_VOID;
  }
}

static ir_abi_param_info_t ir_abi_classify_type_id(
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
                      ? aggregate_direct_integer_type(info.source_size)
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

static int type_is_complex(
    const ir_abi_type_context_t *context, psx_type_id_t type_id) {
  const psx_type_t *type = context && context->semantic_types
                               ? psx_semantic_type_table_lookup(
                                     context->semantic_types, type_id)
                               : NULL;
  return type && type->kind == PSX_TYPE_COMPLEX;
}

static size_t type_piece_count(
    const ir_abi_type_context_t *context, psx_type_id_t type_id,
    ir_abi_param_info_t info) {
  if (type_is_complex(context, type_id)) return 2;
  (void)info;
  return 1;
}

static int lower_result_pieces(
    const ir_abi_type_context_t *context, psx_type_id_t type_id,
    ir_abi_signature_t *out) {
  const psx_type_t *type = psx_semantic_type_table_lookup(
      context->semantic_types, type_id);
  if (!type) return 0;
  if (type->kind == PSX_TYPE_VOID) return 1;

  ir_abi_param_info_t info = ir_abi_classify_type_id(context, type_id);
  if (info.param_class == IR_ABI_PARAM_UNKNOWN ||
      info.type == IR_TY_VOID || info.source_size <= 0)
    return 0;

  size_t piece_count = type->kind == PSX_TYPE_COMPLEX &&
                               ag_target_info_call_abi(context->target) ==
                                   AG_TARGET_CALL_ABI_AAPCS64
                           ? 2u
                           : 1u;
  out->result_pieces = calloc(
      piece_count, sizeof(*out->result_pieces));
  if (!out->result_pieces) return 0;
  out->result_count = piece_count;

  if (type->kind == PSX_TYPE_COMPLEX && piece_count == 2) {
    out->result_pieces[0] = (ir_abi_piece_t){
        .type = info.type,
        .source_index = SIZE_MAX,
        .source_size = info.source_size,
        .byte_offset = 0,
        .kind = IR_ABI_PIECE_COMPLEX_REAL,
    };
    out->result_pieces[1] = (ir_abi_piece_t){
        .type = info.type,
        .source_index = SIZE_MAX,
        .source_size = info.source_size,
        .byte_offset = ir_type_size(info.type),
        .kind = IR_ABI_PIECE_COMPLEX_IMAGINARY,
    };
    return 1;
  }

  ir_abi_piece_kind_t kind = IR_ABI_PIECE_DIRECT;
  ir_type_t piece_type = info.type;
  if (type->kind == PSX_TYPE_COMPLEX ||
      (info.param_class == IR_ABI_PARAM_AGGREGATE &&
       !aggregate_has_direct_integer_width(info.source_size))) {
    kind = IR_ABI_PIECE_INDIRECT;
    piece_type = IR_TY_PTR;
  } else if (info.param_class == IR_ABI_PARAM_AGGREGATE) {
    kind = IR_ABI_PIECE_DIRECT_AGGREGATE;
  }
  out->result_pieces[0] = (ir_abi_piece_t){
      .type = piece_type,
      .source_index = SIZE_MAX,
      .source_size = info.source_size,
      .byte_offset = 0,
      .kind = kind,
  };
  return 1;
}

static int lower_function_type_signature(
    const ir_abi_type_context_t *context,
    const ir_function_type_t *function_type,
    ir_abi_signature_t *out) {
  if (!context || !function_type || !out ||
      function_type->result.type_id == PSX_TYPE_ID_INVALID)
    return 0;
  memset(out, 0, sizeof(*out));
  out->result_area = ir_val_none();
  if (!lower_result_pieces(
          context, function_type->result.type_id, out))
    return 0;
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
    out->param_pieces = calloc(
        piece_count, sizeof(*out->param_pieces));
    if (!out->param_pieces) return 0;
  }
  for (size_t i = 0, piece = 0; i < function_type->param_count; i++) {
    psx_type_id_t type_id = function_type->params[i].type_id;
    ir_abi_param_info_t info = ir_abi_classify_type_id(context, type_id);
    if (type_is_complex(context, type_id)) {
      out->param_pieces[piece++] = (ir_abi_piece_t){
          .type = info.type,
          .source_index = i,
          .source_size = info.source_size,
          .byte_offset = 0,
          .kind = IR_ABI_PIECE_COMPLEX_REAL,
      };
      out->param_pieces[piece++] = (ir_abi_piece_t){
          .type = info.type,
          .source_index = i,
          .source_size = info.source_size,
          .byte_offset = ir_type_size(info.type),
          .kind = IR_ABI_PIECE_COMPLEX_IMAGINARY,
      };
    } else {
      out->param_pieces[piece++] = (ir_abi_piece_t){
          .type = info.type == IR_TY_VOID ? IR_TY_I32 : info.type,
          .source_index = i,
          .source_size = info.source_size,
          .byte_offset = 0,
          .kind = info.param_class == IR_ABI_PARAM_AGGREGATE &&
                          info.type == IR_TY_PTR
                      ? IR_ABI_PIECE_INDIRECT
                      : IR_ABI_PIECE_DIRECT,
      };
    }
  }
  out->param_count = piece_count;
  out->fixed_param_count = piece_count;
  return 1;
}

static void dispose_signature(ir_abi_signature_t *signature) {
  if (!signature) return;
  free(signature->result_pieces);
  free(signature->param_pieces);
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

static size_t count_module_references(const ir_module_t *module) {
  size_t count = 0;
  for (const ir_func_t *function = module ? module->funcs : NULL;
       function; function = function->next) {
    for (const ir_block_t *block = function->entry; block;
         block = block->next) {
      for (const ir_inst_t *instruction = block->head; instruction;
           instruction = instruction->next) {
        if (instruction->op != IR_CALL &&
            instruction->has_function_type)
          count++;
      }
    }
  }
  return count;
}

static size_t count_symbol_references(const ir_module_t *module) {
  size_t count = 0;
  for (const ir_symbol_t *symbol = module ? module->symbols : NULL;
       symbol; symbol = symbol->next) {
    for (const ir_symbol_func_ref_t *reference = symbol->func_refs;
         reference; reference = reference->next) {
      if (reference->has_function_type) count++;
    }
  }
  return count;
}

static size_t logical_variadic_piece_count(
    const ir_abi_type_context_t *context,
    const ir_call_argument_t *argument) {
  if (!context || !argument ||
      argument->type.type_id == PSX_TYPE_ID_INVALID)
    return 0;
  ir_abi_param_info_t info = ir_abi_classify_type_id(
      context, argument->type.type_id);
  if (info.param_class == IR_ABI_PARAM_UNKNOWN ||
      info.type == IR_TY_VOID)
    return 0;
  if (type_is_complex(context, argument->type.type_id)) return 2;
  if (info.param_class == IR_ABI_PARAM_AGGREGATE) {
    if (info.source_size <= 0) return 0;
    return (size_t)((info.source_size + 7) / 8);
  }
  return 1;
}

static int lower_logical_call_arguments(
    const ir_abi_type_context_t *context,
    const ir_inst_t *instruction,
    ir_abi_call_t *call) {
  size_t source_count = instruction->nargs > 0
                            ? (size_t)instruction->nargs : 0;
  size_t declared_source_count = instruction->function_type.param_count;
  size_t declared_piece_count = call->signature.param_count;
  int declared_variadic = call->signature.is_variadic;
  if ((source_count > 0 && !instruction->args) ||
      source_count < declared_source_count ||
      (instruction->function_type.has_prototype && !declared_variadic &&
       source_count != declared_source_count))
    return 0;

  size_t physical_count = declared_piece_count;
  for (size_t i = declared_source_count; i < source_count; i++) {
    size_t pieces = logical_variadic_piece_count(
        context, &instruction->args[i]);
    if (pieces == 0 || physical_count > SIZE_MAX - pieces) return 0;
    physical_count += pieces;
  }
  if (physical_count > 0) {
    if (physical_count > SIZE_MAX / sizeof(*call->arguments)) return 0;
    call->arguments = calloc(
        physical_count, sizeof(*call->arguments));
    if (!call->arguments) return 0;
  }
  if (physical_count != declared_piece_count) {
    ir_abi_piece_t *pieces = realloc(
        call->signature.param_pieces,
        physical_count * sizeof(*pieces));
    if (physical_count > 0 && !pieces) return 0;
    call->signature.param_pieces = pieces;
  }

  for (size_t i = 0; i < declared_piece_count; i++) {
    const ir_abi_piece_t *piece = &call->signature.param_pieces[i];
    if (piece->source_index >= declared_source_count) return 0;
    const ir_call_argument_t *logical_argument =
        &instruction->args[piece->source_index];
    ir_abi_argument_access_t access = IR_ABI_ARGUMENT_DIRECT;
    if (piece->kind == IR_ABI_PIECE_INDIRECT) {
      if (logical_argument->representation != IR_CALL_ARGUMENT_ADDRESS)
        return 0;
    } else if (logical_argument->representation ==
               IR_CALL_ARGUMENT_ADDRESS) {
      access = IR_ABI_ARGUMENT_LOAD;
    } else if (piece->kind == IR_ABI_PIECE_COMPLEX_REAL ||
               piece->kind == IR_ABI_PIECE_COMPLEX_IMAGINARY) {
      return 0;
    }
    call->arguments[i] = (ir_abi_argument_t){
        .source = logical_argument->value,
        .type = piece->type,
        .byte_offset = piece->byte_offset,
        .access = access,
    };
  }

  size_t physical_index = declared_piece_count;
  for (size_t i = declared_source_count; i < source_count; i++) {
    const ir_call_argument_t *logical_argument = &instruction->args[i];
    ir_abi_param_info_t info = ir_abi_classify_type_id(
        context, logical_argument->type.type_id);
    size_t piece_count = logical_variadic_piece_count(
        context, logical_argument);
    for (size_t piece_index = 0; piece_index < piece_count;
         piece_index++, physical_index++) {
      int offset = 0;
      ir_type_t type = logical_argument->value.type;
      ir_abi_argument_access_t access = IR_ABI_ARGUMENT_DIRECT;
      if (type_is_complex(context, logical_argument->type.type_id)) {
        if (logical_argument->representation != IR_CALL_ARGUMENT_ADDRESS)
          return 0;
        type = info.type;
        offset = (int)piece_index * ir_type_size(type);
        access = IR_ABI_ARGUMENT_LOAD;
      } else if (info.param_class == IR_ABI_PARAM_AGGREGATE) {
        if (logical_argument->representation != IR_CALL_ARGUMENT_ADDRESS)
          return 0;
        type = IR_TY_I64;
        offset = (int)piece_index * 8;
        access = IR_ABI_ARGUMENT_LOAD;
      }
      call->signature.param_pieces[physical_index] = (ir_abi_piece_t){
          .type = type,
          .source_index = i,
          .source_size = info.source_size,
          .byte_offset = offset,
          .kind = IR_ABI_PIECE_VARIADIC,
      };
      call->arguments[physical_index] = (ir_abi_argument_t){
          .source = logical_argument->value,
          .type = type,
          .byte_offset = offset,
          .access = access,
      };
    }
  }
  call->argument_count = physical_count;
  call->signature.param_count = physical_count;
  call->signature.fixed_param_count =
      declared_variadic ? declared_piece_count : physical_count;
  call->signature.is_variadic =
      declared_variadic && physical_count > declared_piece_count;
  return 1;
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
  abi->reference_count = count_module_references(module);
  abi->symbol_reference_count = count_symbol_references(module);
  if (abi->function_count > 0) {
    abi->functions = calloc(
        abi->function_count, sizeof(*abi->functions));
    if (!abi->functions) goto fail;
  }
  if (abi->call_count > 0) {
    abi->calls = calloc(abi->call_count, sizeof(*abi->calls));
    if (!abi->calls) goto fail;
  }
  if (abi->reference_count > 0) {
    abi->references = calloc(
        abi->reference_count, sizeof(*abi->references));
    if (!abi->references) goto fail;
  }
  if (abi->symbol_reference_count > 0) {
    abi->symbol_references = calloc(
        abi->symbol_reference_count, sizeof(*abi->symbol_references));
    if (!abi->symbol_references) goto fail;
  }

  size_t function_index = 0;
  size_t call_index = 0;
  size_t reference_index = 0;
  for (const ir_func_t *function = module->funcs; function;
       function = function->next) {
    ir_abi_function_t *lowered = &abi->functions[function_index++];
    lowered->function = function;
    if (!lower_function_type_signature(
            context, &function->function_type, &lowered->signature))
      goto fail;
    for (const ir_block_t *block = function->entry; block;
         block = block->next) {
      for (const ir_inst_t *instruction = block->head; instruction;
           instruction = instruction->next) {
        if (instruction->op == IR_PARAM_BIND) {
          size_t piece_count = 0;
          if (instruction->src1.type != IR_TY_PTR ||
              instruction->src1.id < 0 ||
              instruction->parameter_index >=
                  function->function_type.param_count ||
              !ir_abi_signature_parameter_pieces(
                  &lowered->signature,
                  instruction->parameter_index,
                  &piece_count, NULL) ||
              piece_count == 0)
            goto fail;
          continue;
        }
        if (instruction->op != IR_CALL) {
          if (instruction->has_function_type) {
            ir_abi_reference_t *reference =
                &abi->references[reference_index++];
            reference->reference = instruction;
            if (!lower_function_type_signature(
                    context, &instruction->function_type,
                    &reference->signature))
              goto fail;
          }
          continue;
        }
        ir_abi_call_t *call = &abi->calls[call_index++];
        call->call = instruction;
        if (!lower_function_type_signature(
                context, &instruction->function_type,
                &call->signature))
          goto fail;
        size_t declared_piece_count = call->signature.param_count;
        size_t actual_count = instruction->nargs > 0
                                  ? (size_t)instruction->nargs : 0;
        int has_logical_arguments = actual_count == 0;
        if (actual_count > 0) {
          if (!instruction->args) goto fail;
          has_logical_arguments =
              instruction->args[0].type.type_id != PSX_TYPE_ID_INVALID;
          for (size_t i = 1; i < actual_count; i++) {
            if ((instruction->args[i].type.type_id != PSX_TYPE_ID_INVALID) !=
                has_logical_arguments)
              goto fail;
          }
        }
        if (has_logical_arguments) {
          if (!lower_logical_call_arguments(
                  context, instruction, call))
            goto fail;
        } else {
          if (actual_count > 0) {
            if (actual_count > SIZE_MAX / sizeof(*call->arguments))
              goto fail;
            call->arguments = malloc(
                actual_count * sizeof(*call->arguments));
            if (!call->arguments) goto fail;
            for (size_t i = 0; i < actual_count; i++) {
              call->arguments[i] = (ir_abi_argument_t){
                  .source = instruction->args[i].value,
                  .type = instruction->args[i].value.type,
                  .byte_offset = 0,
                  .access = IR_ABI_ARGUMENT_DIRECT,
              };
            }
          }
          call->argument_count = actual_count;
          if (call->signature.is_variadic &&
              actual_count < declared_piece_count)
            goto fail;
          if (actual_count != declared_piece_count) {
            ir_abi_piece_t *pieces = realloc(
                call->signature.param_pieces,
                actual_count * sizeof(*pieces));
            if (actual_count > 0 && !pieces) goto fail;
            call->signature.param_pieces = pieces;
          }
          for (size_t i = declared_piece_count; i < actual_count; i++) {
            call->signature.param_pieces[i] = (ir_abi_piece_t){
                .type = instruction->args[i].value.type,
                .source_index = SIZE_MAX,
                .source_size = ir_type_size(
                    instruction->args[i].value.type),
                .byte_offset = 0,
                .kind = IR_ABI_PIECE_VARIADIC,
            };
          }
          call->signature.param_count = actual_count;
          call->signature.fixed_param_count =
              call->signature.is_variadic
                  ? declared_piece_count : actual_count;
          call->signature.is_variadic =
              call->signature.is_variadic &&
              actual_count > declared_piece_count;
        }
        call->signature.result_area = instruction->result_storage;
      }
    }
  }
  size_t symbol_reference_index = 0;
  for (const ir_symbol_t *symbol = module->symbols; symbol;
       symbol = symbol->next) {
    for (const ir_symbol_func_ref_t *reference = symbol->func_refs;
         reference; reference = reference->next) {
      if (!reference->has_function_type) continue;
      ir_abi_symbol_reference_t *lowered =
          &abi->symbol_references[symbol_reference_index++];
      lowered->reference = reference;
      if (!lower_function_type_signature(
              context, &reference->function_type,
              &lowered->signature))
        goto fail;
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
    free(module->calls[i].arguments);
  for (size_t i = 0; i < module->call_count; i++)
    dispose_signature(&module->calls[i].signature);
  for (size_t i = 0; i < module->reference_count; i++)
    dispose_signature(&module->references[i].signature);
  for (size_t i = 0; i < module->symbol_reference_count; i++)
    dispose_signature(&module->symbol_references[i].signature);
  free(module->functions);
  free(module->calls);
  free(module->references);
  free(module->symbol_references);
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

const ir_abi_piece_t *ir_abi_signature_parameter_pieces(
    const ir_abi_signature_t *signature, size_t source_index,
    size_t *piece_count, size_t *physical_index) {
  if (piece_count) *piece_count = 0;
  if (physical_index) *physical_index = 0;
  if (!signature || !signature->param_pieces) return NULL;
  size_t first = signature->param_count;
  size_t count = 0;
  for (size_t i = 0; i < signature->param_count; i++) {
    if (signature->param_pieces[i].source_index != source_index) {
      if (count > 0) break;
      continue;
    }
    if (first == signature->param_count) first = i;
    count++;
  }
  if (count == 0) return NULL;
  if (piece_count) *piece_count = count;
  if (physical_index) *physical_index = first;
  return &signature->param_pieces[first];
}

const ir_abi_piece_t *ir_abi_signature_result_pieces(
    const ir_abi_signature_t *signature, size_t *piece_count) {
  if (piece_count)
    *piece_count = signature ? signature->result_count : 0;
  return signature && signature->result_count > 0
             ? signature->result_pieces : NULL;
}

int ir_abi_signature_result_is_indirect(
    const ir_abi_signature_t *signature) {
  return signature && signature->result_count == 1 &&
         signature->result_pieces &&
         signature->result_pieces[0].kind == IR_ABI_PIECE_INDIRECT;
}

int ir_abi_signature_result_is_direct_aggregate(
    const ir_abi_signature_t *signature) {
  return signature && signature->result_count == 1 &&
         signature->result_pieces &&
         signature->result_pieces[0].kind ==
             IR_ABI_PIECE_DIRECT_AGGREGATE;
}

ir_type_t ir_abi_signature_direct_result_type(
    const ir_abi_signature_t *signature) {
  if (!signature || signature->result_count != 1 ||
      !signature->result_pieces ||
      signature->result_pieces[0].kind == IR_ABI_PIECE_INDIRECT)
    return IR_TY_VOID;
  return signature->result_pieces[0].type;
}

int ir_abi_signature_result_source_size(
    const ir_abi_signature_t *signature) {
  return signature && signature->result_count > 0 &&
                 signature->result_pieces
             ? signature->result_pieces[0].source_size
             : 0;
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

const ir_abi_argument_t *ir_abi_call_arguments(
    const ir_abi_module_t *module, const ir_inst_t *call,
    size_t *argument_count) {
  if (argument_count) *argument_count = 0;
  if (!module || !call) return NULL;
  for (size_t i = 0; i < module->call_count; i++) {
    if (module->calls[i].call != call) continue;
    if (argument_count)
      *argument_count = module->calls[i].argument_count;
    return module->calls[i].arguments;
  }
  return NULL;
}

const ir_abi_signature_t *ir_abi_reference_signature(
    const ir_abi_module_t *module, const ir_inst_t *reference) {
  if (!module || !reference) return NULL;
  for (size_t i = 0; i < module->reference_count; i++) {
    if (module->references[i].reference == reference)
      return &module->references[i].signature;
  }
  return NULL;
}

const ir_abi_signature_t *ir_abi_symbol_reference_signature(
    const ir_abi_module_t *module,
    const ir_symbol_func_ref_t *reference) {
  if (!module || !reference) return NULL;
  for (size_t i = 0; i < module->symbol_reference_count; i++) {
    if (module->symbol_references[i].reference == reference)
      return &module->symbol_references[i].signature;
  }
  return NULL;
}

static size_t count_data_function_relocations(
    const ir_data_module_t *module) {
  size_t count = 0;
  for (const ir_data_object_t *object = module ? module->objects : NULL;
       object; object = object->next) {
    for (const ir_data_reloc_t *relocation = object->relocs;
         relocation; relocation = relocation->next) {
      if (relocation->kind == IR_DATA_RELOC_FUNCTION &&
          relocation->has_function_type)
        count++;
    }
  }
  return count;
}

ir_abi_data_module_t *ir_abi_lower_data_module(
    const ir_abi_type_context_t *context,
    const ir_data_module_t *module) {
  if (!context || !module) return NULL;
  ir_abi_data_module_t *abi = calloc(1, sizeof(*abi));
  if (!abi) return NULL;
  abi->relocation_count = count_data_function_relocations(module);
  if (abi->relocation_count > 0) {
    abi->relocations = calloc(
        abi->relocation_count, sizeof(*abi->relocations));
    if (!abi->relocations) goto fail;
  }
  size_t index = 0;
  for (const ir_data_object_t *object = module->objects; object;
       object = object->next) {
    for (const ir_data_reloc_t *relocation = object->relocs;
         relocation; relocation = relocation->next) {
      if (relocation->kind != IR_DATA_RELOC_FUNCTION) continue;
      if (!relocation->has_function_type) goto fail;
      ir_abi_data_relocation_t *lowered = &abi->relocations[index++];
      lowered->relocation = relocation;
      if (!lower_function_type_signature(
              context, &relocation->function_type,
              &lowered->signature))
        goto fail;
    }
  }
  return abi;

fail:
  ir_abi_data_module_free(abi);
  return NULL;
}

void ir_abi_data_module_free(ir_abi_data_module_t *module) {
  if (!module) return;
  for (size_t i = 0; i < module->relocation_count; i++)
    dispose_signature(&module->relocations[i].signature);
  free(module->relocations);
  free(module);
}

const ir_abi_signature_t *ir_abi_data_relocation_signature(
    const ir_abi_data_module_t *module,
    const ir_data_reloc_t *relocation) {
  if (!module || !relocation) return NULL;
  for (size_t i = 0; i < module->relocation_count; i++) {
    if (module->relocations[i].relocation == relocation)
      return &module->relocations[i].signature;
  }
  return NULL;
}
