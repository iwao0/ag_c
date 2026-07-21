#include "wasm32_obj_internal.h"

#include "../../diag/diag.h"
#include <stdlib.h>
#include <string.h>

#define g_obj (context->obj)

static void section_unsupported(
    wasm32_obj_context_t *context, const char *message) {
  diag_emit_internalf_in(
      context->diagnostic_context,
      DIAG_ERR_CODEGEN_UNSUPPORTED_IR_OP,
      diag_message_for_in(
          context->diagnostic_context,
          DIAG_ERR_CODEGEN_UNSUPPORTED_IR_OP),
      message);
}

static void *section_realloc(
    wasm32_obj_context_t *context, void *pointer, size_t size) {
  void *result = realloc(pointer, size);
  if (!result)
    diag_emit_internalf_in(
        context->diagnostic_context, DIAG_ERR_INTERNAL_OOM, "%s",
        diag_message_for_in(
            context->diagnostic_context, DIAG_ERR_INTERNAL_OOM));
  return result;
}

static unsigned wasm_value_type(
    wasm32_obj_context_t *context, ir_type_t type) {
  switch (type) {
    case IR_TY_I8:
    case IR_TY_I16:
    case IR_TY_I32:
    case IR_TY_PTR:
      return 0x7f;
    case IR_TY_I64:
      return 0x7e;
    case IR_TY_F32:
      return 0x7d;
    case IR_TY_F64:
      return 0x7c;
    default:
      section_unsupported(
          context, "unsupported Wasm object value type");
  }
  return 0;
}

static int defined_data_count(
    const wasm32_obj_context_t *context) {
  int count = 0;
  for (int index = 0; index < context->obj.data_count; index++) {
    if (!context->obj.data[index].is_undefined) count++;
  }
  return count;
}

static void add_global_reloc(
    wasm32_obj_context_t *context, obj_reloc_t **relocations,
    int *count, int *capacity, int type, uint32_t offset,
    int target_symbol, int addend) {
  if (*count == *capacity) {
    int next_capacity = *capacity ? *capacity * 2 : 16;
    *relocations = section_realloc(
        context, *relocations,
        (size_t)next_capacity * sizeof(**relocations));
    *capacity = next_capacity;
  }
  obj_reloc_t *relocation = &(*relocations)[(*count)++];
  *relocation = (obj_reloc_t){
      .body_off = offset,
      .type = type,
      .target_sym = target_symbol,
      .addend = addend,
  };
}

static void add_code_reloc(
    wasm32_obj_context_t *context, uint32_t offset,
    const obj_reloc_t *source) {
  add_global_reloc(
      context, &g_obj.code_relocs, &g_obj.code_reloc_count,
      &g_obj.code_reloc_cap, source->type, offset,
      source->target_sym, source->addend);
}

static void emit_type_section(
    wasm32_obj_context_t *context, wb_t *output) {
  wb_t payload = {
      .diagnostic_context = context->diagnostic_context};
  wb_uleb(&payload, (uint32_t)g_obj.type_count);
  for (int index = 0; index < g_obj.type_count; index++) {
    const obj_sig_t *signature = &g_obj.types[index];
    wb_u8(&payload, 0x60);
    wb_uleb(&payload, (uint32_t)signature->nparams);
    for (int argument = 0;
         argument < signature->nparams; argument++)
      wb_u8(
          &payload,
          wasm_value_type(context, signature->params[argument]));
    if (signature->result == IR_TY_VOID) {
      wb_uleb(&payload, 0);
    } else {
      wb_uleb(&payload, 1);
      wb_u8(
          &payload,
          wasm_value_type(context, signature->result));
    }
  }
  emit_section(output, WASM_SEC_TYPE, &payload);
  free(payload.data);
}

static void emit_import_section(
    wasm32_obj_context_t *context, wb_t *output) {
  int import_count = 1 + g_obj.global_count;
  for (int index = 0; index < g_obj.func_count; index++) {
    if (g_obj.funcs[index].imported) import_count++;
  }
  if (g_obj.has_indirect_call) import_count++;
  wb_t payload = {
      .diagnostic_context = context->diagnostic_context};
  wb_uleb(&payload, (uint32_t)import_count);
  for (int index = 0; index < g_obj.func_count; index++) {
    const obj_func_t *function = &g_obj.funcs[index];
    if (!function->imported) continue;
    wb_str(&payload, "env", 3);
    wb_str(&payload, function->name, function->name_len);
    wb_u8(&payload, 0x00);
    wb_uleb(&payload, (uint32_t)function->type_index);
  }
  if (g_obj.has_indirect_call) {
    wb_str(&payload, "env", 3);
    wb_str(&payload, "__indirect_function_table", 25);
    wb_u8(&payload, 0x01);
    wb_u8(&payload, 0x70);
    wb_u8(&payload, 0x00);
    wb_uleb(&payload, 0);
  }
  wb_str(&payload, "env", 3);
  wb_str(&payload, "__linear_memory", 15);
  wb_u8(&payload, 0x02);
  wb_u8(&payload, 0x00);
  wb_uleb(&payload, 0);
  for (int index = 0; index < g_obj.global_count; index++) {
    const obj_global_t *global = &g_obj.globals[index];
    wb_str(&payload, "env", 3);
    wb_str(&payload, global->name, global->name_len);
    wb_u8(&payload, 0x03);
    wb_u8(&payload, wasm_value_type(context, IR_TY_I32));
    wb_u8(&payload, 0x01);
  }
  emit_section(output, WASM_SEC_IMPORT, &payload);
  free(payload.data);
}

static int defined_function_count(
    const wasm32_obj_context_t *context) {
  int count = 0;
  for (int index = 0; index < context->obj.func_count; index++) {
    if (context->obj.funcs[index].defined) count++;
  }
  return count;
}

static void emit_function_section(
    wasm32_obj_context_t *context, wb_t *output) {
  int count = defined_function_count(context);
  if (count == 0) return;
  wb_t payload = {
      .diagnostic_context = context->diagnostic_context};
  wb_uleb(&payload, (uint32_t)count);
  for (int index = 0; index < g_obj.func_count; index++) {
    if (g_obj.funcs[index].defined)
      wb_uleb(
          &payload, (uint32_t)g_obj.funcs[index].type_index);
  }
  emit_section(output, WASM_SEC_FUNCTION, &payload);
  free(payload.data);
}

static void emit_datacount_section(
    wasm32_obj_context_t *context, wb_t *output) {
  int count = defined_data_count(context);
  if (count == 0) return;
  wb_t payload = {
      .diagnostic_context = context->diagnostic_context};
  wb_uleb(&payload, (uint32_t)count);
  emit_section(output, WASM_SEC_DATACOUNT, &payload);
  free(payload.data);
}

static void emit_code_section(
    wasm32_obj_context_t *context, wb_t *output) {
  int count = defined_function_count(context);
  if (count == 0) return;
  wb_t payload = {
      .diagnostic_context = context->diagnostic_context};
  wb_uleb(&payload, (uint32_t)count);
  for (int index = 0; index < g_obj.func_count; index++) {
    obj_func_t *function = &g_obj.funcs[index];
    if (!function->defined) continue;
    wb_t body_size = {
        .diagnostic_context = context->diagnostic_context};
    wb_uleb(&body_size, function->body.len);
    uint32_t body_start = payload.len + body_size.len;
    wb_bytes(&payload, body_size.data, body_size.len);
    wb_bytes(&payload, function->body.data, function->body.len);
    for (int relocation = 0;
         relocation < function->reloc_count; relocation++)
      add_code_reloc(
          context,
          body_start + function->relocs[relocation].body_off,
          &function->relocs[relocation]);
    free(body_size.data);
  }
  emit_section(output, WASM_SEC_CODE, &payload);
  free(payload.data);
}

static void emit_data_section(
    wasm32_obj_context_t *context, wb_t *output) {
  int count = defined_data_count(context);
  if (count == 0) return;
  wb_t payload = {
      .diagnostic_context = context->diagnostic_context};
  wb_uleb(&payload, (uint32_t)count);
  int segment_index = 0;
  for (int index = 0; index < g_obj.data_count; index++) {
    obj_data_t *data = &g_obj.data[index];
    if (data->is_undefined) continue;
    data->segment_index = segment_index++;
    wb_u8(&payload, 0x00);
    wb_u8(&payload, 0x41);
    wb_uleb(&payload, 0);
    wb_u8(&payload, 0x0b);
    wb_uleb(&payload, data->bytes.len);
    uint32_t data_start = payload.len;
    wb_bytes(&payload, data->bytes.data, data->bytes.len);
    for (int relocation = 0;
         relocation < data->reloc_count; relocation++) {
      const obj_reloc_t *source = &data->relocs[relocation];
      int symbol = source->target_is_data
          ? g_obj.data[source->target_sym].symbol_index
          : g_obj.funcs[source->target_sym].symbol_index;
      add_global_reloc(
          context, &g_obj.data_relocs, &g_obj.data_reloc_count,
          &g_obj.data_reloc_cap, source->type,
          data_start + source->body_off, symbol, source->addend);
    }
  }
  emit_section(output, WASM_SEC_DATA, &payload);
  free(payload.data);
}

static void emit_function_symbol(
    wb_t *payload, const obj_func_t *function) {
  uint32_t flags =
      (function->is_static ? WASM_SYMBOL_BINDING_LOCAL : 0) |
      WASM_SYMBOL_EXPLICIT_NAME;
  if (function->imported) flags |= WASM_SYMBOL_UNDEFINED;
  wb_u8(payload, WASM_SYM_FUNCTION);
  wb_uleb(payload, flags);
  wb_uleb(payload, (uint32_t)function->func_index);
  wb_str(payload, function->name, function->name_len);
}

static void emit_data_symbol(
    wb_t *payload, const obj_data_t *data) {
  uint32_t flags =
      data->is_static ? WASM_SYMBOL_BINDING_LOCAL : 0;
  if (data->is_undefined) flags |= WASM_SYMBOL_UNDEFINED;
  wb_u8(payload, WASM_SYM_DATA);
  wb_uleb(payload, flags);
  wb_str(payload, data->name, data->name_len);
  if (data->is_undefined) return;
  wb_uleb(payload, (uint32_t)data->segment_index);
  wb_uleb(payload, 0);
  wb_uleb(
      payload,
      (uint32_t)(data->alloc_size > data->bytes.len
                     ? data->alloc_size : data->bytes.len));
}

static void emit_global_symbol(
    wb_t *payload, const obj_global_t *global) {
  wb_u8(payload, WASM_SYM_GLOBAL);
  wb_uleb(
      payload,
      WASM_SYMBOL_UNDEFINED | WASM_SYMBOL_EXPLICIT_NAME);
  wb_uleb(payload, (uint32_t)global->global_index);
  wb_str(payload, global->name, global->name_len);
}

static void emit_linking_section(
    wasm32_obj_context_t *context, wb_t *output) {
  wb_t payload = {
      .diagnostic_context = context->diagnostic_context};
  wb_uleb(&payload, 2);
  wb_t symbols = {
      .diagnostic_context = context->diagnostic_context};
  wb_uleb(&symbols, (uint32_t)g_obj.symbol_count);
  for (int index = 0; index < g_obj.func_count; index++)
    emit_function_symbol(&symbols, &g_obj.funcs[index]);
  for (int index = 0; index < g_obj.data_count; index++)
    emit_data_symbol(&symbols, &g_obj.data[index]);
  for (int index = 0; index < g_obj.global_count; index++)
    emit_global_symbol(&symbols, &g_obj.globals[index]);
  wb_u8(&payload, WASM_SYMBOL_TABLE);
  wb_uleb(&payload, symbols.len);
  wb_bytes(&payload, symbols.data, symbols.len);

  int data_count = defined_data_count(context);
  if (data_count > 0) {
    wb_t segments = {
        .diagnostic_context = context->diagnostic_context};
    wb_uleb(&segments, (uint32_t)data_count);
    for (int index = 0; index < g_obj.data_count; index++) {
      if (g_obj.data[index].is_undefined) continue;
      wb_str(
          &segments, g_obj.data[index].name,
          g_obj.data[index].name_len);
      wb_uleb(&segments, (uint32_t)g_obj.data[index].align);
      wb_uleb(&segments, 0);
    }
    wb_u8(&payload, WASM_SEGMENT_INFO);
    wb_uleb(&payload, segments.len);
    wb_bytes(&payload, segments.data, segments.len);
    free(segments.data);
  }
  emit_custom_section(output, "linking", &payload);
  free(symbols.data);
  free(payload.data);
}

static void emit_c_signature_section(
    wasm32_obj_context_t *context, wb_t *output) {
  int count = 0;
  for (int index = 0; index < g_obj.func_count; index++) {
    if (g_obj.funcs[index].c_signature) count++;
  }
  if (count == 0) return;
  wb_t payload = {
      .diagnostic_context = context->diagnostic_context};
  wb_uleb(&payload, 1);
  wb_uleb(&payload, (uint32_t)count);
  for (int index = 0; index < g_obj.func_count; index++) {
    const obj_func_t *function = &g_obj.funcs[index];
    if (!function->c_signature) continue;
    wb_str(&payload, function->name, function->name_len);
    wb_str(
        &payload, function->c_signature,
        function->c_signature_len);
  }
  emit_custom_section(output, "agc.c_signature", &payload);
  free(payload.data);
}

static void emit_continuation_section(
    wasm32_obj_context_t *context, wb_t *output) {
  if (!g_obj.continuation_entry) return;
  wb_t payload = {
      .diagnostic_context = context->diagnostic_context};
  wb_uleb(&payload, 1);
  const char *values[] = {
      g_obj.continuation_entry,
      g_obj.continuation_condition,
      g_obj.continuation_step,
      g_obj.continuation_start,
      g_obj.continuation_resume,
      g_obj.continuation_status,
      g_obj.continuation_result,
  };
  for (size_t index = 0;
       index < sizeof(values) / sizeof(values[0]); index++)
    wb_str(&payload, values[index], (int)strlen(values[index]));
  emit_custom_section(output, "agc.continuation", &payload);
  free(payload.data);
}

static void emit_reloc_section(
    wasm32_obj_context_t *context, wb_t *output,
    const char *name, int target_section,
    const obj_reloc_t *relocations, int relocation_count) {
  if (relocation_count == 0) return;
  wb_t payload = {
      .diagnostic_context = context->diagnostic_context};
  wb_uleb(&payload, (uint32_t)target_section);
  wb_uleb(&payload, (uint32_t)relocation_count);
  for (int index = 0; index < relocation_count; index++) {
    wb_uleb(&payload, (uint32_t)relocations[index].type);
    wb_uleb(&payload, relocations[index].body_off);
    wb_uleb(&payload, (uint32_t)relocations[index].target_sym);
    if (relocations[index].type == R_WASM_MEMORY_ADDR_LEB ||
        relocations[index].type == R_WASM_MEMORY_ADDR_I32)
      wb_sleb(&payload, relocations[index].addend);
  }
  emit_custom_section(output, name, &payload);
  free(payload.data);
}

void wasm32_obj_serialize_sections(
    wasm32_obj_context_t *context, wb_t *output) {
  int has_definitions = defined_function_count(context) > 0;
  int data_count = defined_data_count(context);
  int section_index = 1;
  section_index++; /* Import: linear memory is always imported. */
  if (has_definitions) section_index++;
  if (data_count > 0) section_index++;
  int code_section_index =
      has_definitions ? section_index++ : -1;
  int data_section_index =
      data_count > 0 ? section_index++ : -1;

  emit_type_section(context, output);
  emit_import_section(context, output);
  emit_function_section(context, output);
  emit_datacount_section(context, output);
  emit_code_section(context, output);
  emit_data_section(context, output);
  emit_c_signature_section(context, output);
  emit_continuation_section(context, output);
  emit_linking_section(context, output);
  emit_reloc_section(
      context, output, "reloc.CODE", code_section_index,
      g_obj.code_relocs, g_obj.code_reloc_count);
  emit_reloc_section(
      context, output, "reloc.DATA", data_section_index,
      g_obj.data_relocs, g_obj.data_reloc_count);
}
