#include "wasm32_machine_module.h"
#include "../../lowering/abi_lowering.h"

#include <stdlib.h>
#include <string.h>

static int copy_name(char **destination, const char *source, int length) {
  if (!destination || !source || length <= 0) return 0;
  char *copy = malloc((size_t)length + 1);
  if (!copy) return 0;
  memcpy(copy, source, (size_t)length);
  copy[length] = '\0';
  *destination = copy;
  return 1;
}

static void dispose_symbol(wasm32_machine_symbol_t *symbol) {
  if (!symbol) return;
  free(symbol->name);
  for (int i = 0; i < symbol->func_ref_count; i++)
    free(symbol->func_refs[i].name);
  free(symbol->func_refs);
  memset(symbol, 0, sizeof(*symbol));
}

static int copy_symbol(
    wasm32_machine_symbol_t *destination,
    const ir_symbol_t *source) {
  memset(destination, 0, sizeof(*destination));
  destination->name_len = source->name_len;
  destination->byte_size = source->byte_size;
  destination->alignment = source->alignment;
  destination->is_extern = source->is_extern;
  destination->is_static = source->is_static;
  destination->is_thread_local = source->is_thread_local;
  if (!copy_name(
          &destination->name, source->name, source->name_len))
    return 0;
  for (const ir_symbol_func_ref_t *ref = source->func_refs; ref;
       ref = ref->next)
    destination->func_ref_count++;
  if (destination->func_ref_count > 0) {
    destination->func_refs = calloc(
        (size_t)destination->func_ref_count,
        sizeof(*destination->func_refs));
    if (!destination->func_refs) return 0;
  }
  int index = 0;
  for (const ir_symbol_func_ref_t *ref = source->func_refs; ref;
       ref = ref->next, index++) {
    destination->func_refs[index].name_len = ref->name_len;
    destination->func_refs[index].offset = ref->offset;
    if (!copy_name(
            &destination->func_refs[index].name,
            ref->name, ref->name_len))
      return 0;
  }
  return 1;
}

static void dispose_data_object(
    wasm32_machine_data_object_t *object) {
  if (!object) return;
  free(object->name);
  free(object->bytes);
  for (int i = 0; i < object->relocation_count; i++) {
    free(object->relocations[i].target);
    wasm32_machine_signature_dispose(
        &object->relocations[i].function_signature);
  }
  free(object->relocations);
  memset(object, 0, sizeof(*object));
}

static void clear_data_objects(wasm32_machine_module_t *module) {
  if (!module) return;
  for (size_t i = 0; i < module->data_object_count; i++)
    dispose_data_object(&module->data_objects[i]);
  free(module->data_objects);
  module->data_objects = NULL;
  module->data_object_count = 0;
}

static wasm32_machine_data_object_kind_t machine_data_kind(
    ir_data_object_kind_t kind) {
  switch (kind) {
    case IR_DATA_OBJECT: return WASM32_MACHINE_DATA_OBJECT;
    case IR_DATA_STRING: return WASM32_MACHINE_DATA_STRING;
    case IR_DATA_FLOAT: return WASM32_MACHINE_DATA_FLOAT;
  }
  return WASM32_MACHINE_DATA_FLOAT;
}

static int copy_data_object(
    wasm32_machine_data_object_t *destination,
    const ir_data_object_t *source,
    const ir_abi_data_module_t *data_abi) {
  memset(destination, 0, sizeof(*destination));
  destination->name_len = source->name_len;
  destination->byte_size = source->byte_size;
  destination->alignment = source->alignment;
  destination->element_size = source->element_size;
  destination->kind = machine_data_kind(source->kind);
  destination->is_extern = source->is_extern;
  destination->is_static = source->is_static;
  destination->is_thread_local = source->is_thread_local;
  destination->is_read_only = source->is_read_only;
  destination->has_explicit_initializer =
      source->has_explicit_initializer;
  if (!copy_name(
          &destination->name, source->name, source->name_len))
    return 0;
  if (source->bytes && source->byte_size > 0) {
    destination->bytes = malloc((size_t)source->byte_size);
    if (!destination->bytes) return 0;
    memcpy(
        destination->bytes, source->bytes,
        (size_t)source->byte_size);
  }
  for (const ir_data_reloc_t *relocation = source->relocs; relocation;
       relocation = relocation->next)
    destination->relocation_count++;
  if (destination->relocation_count > 0) {
    destination->relocations = calloc(
        (size_t)destination->relocation_count,
        sizeof(*destination->relocations));
    if (!destination->relocations) return 0;
  }
  int index = 0;
  for (const ir_data_reloc_t *relocation = source->relocs; relocation;
       relocation = relocation->next, index++) {
    wasm32_machine_data_reloc_t *machine =
        &destination->relocations[index];
    machine->target_len = relocation->target_len;
    machine->offset = relocation->offset;
    machine->width = relocation->width;
    machine->addend = relocation->addend;
    machine->kind = relocation->kind == IR_DATA_RELOC_FUNCTION
                        ? WASM32_MACHINE_DATA_RELOC_FUNCTION
                        : WASM32_MACHINE_DATA_RELOC_DATA;
    if (!copy_name(
            &machine->target,
            relocation->target, relocation->target_len))
      return 0;
    if (machine->kind == WASM32_MACHINE_DATA_RELOC_FUNCTION) {
      const ir_abi_signature_t *abi =
          ir_abi_data_relocation_signature(data_abi, relocation);
      if (!wasm32_machine_signature_from_abi(
              abi, 1, &machine->function_signature))
        return 0;
      machine->has_function_signature = 1;
    }
  }
  return 1;
}

void wasm32_machine_module_dispose(wasm32_machine_module_t *module) {
  if (!module) return;
  for (size_t i = 0; i < module->function_count; i++)
    wasm32_machine_function_dispose(&module->functions[i]);
  for (size_t i = 0; i < module->symbol_count; i++)
    dispose_symbol(&module->symbols[i]);
  clear_data_objects(module);
  free(module->functions);
  free(module->symbols);
  memset(module, 0, sizeof(*module));
}

int wasm32_machine_module_build_data(
    wasm32_machine_module_t *module,
    const ir_data_module_t *source,
    const ir_abi_data_module_t *data_abi) {
  if (!module) return 0;
  clear_data_objects(module);
  if (!source) return 1;
  for (const ir_data_object_t *object = source->objects; object;
       object = object->next)
    module->data_object_count++;
  if (module->data_object_count > 0) {
    module->data_objects = calloc(
        module->data_object_count, sizeof(*module->data_objects));
    if (!module->data_objects) goto fail;
  }
  size_t index = 0;
  for (const ir_data_object_t *object = source->objects; object;
       object = object->next, index++) {
    if (!copy_data_object(
            &module->data_objects[index], object, data_abi))
      goto fail;
  }
  for (size_t object_index = 0;
       object_index < module->data_object_count; object_index++) {
    wasm32_machine_data_object_t *object =
        &module->data_objects[object_index];
    for (int relocation_index = 0;
         relocation_index < object->relocation_count;
         relocation_index++) {
      wasm32_machine_data_reloc_t *relocation =
          &object->relocations[relocation_index];
      if (relocation->kind != WASM32_MACHINE_DATA_RELOC_DATA)
        continue;
      relocation->resolved_target = wasm32_machine_module_data_object(
          module, relocation->target, relocation->target_len);
      if (!relocation->resolved_target) goto fail;
    }
  }
  return 1;

fail:
  clear_data_objects(module);
  return 0;
}

int wasm32_machine_module_build(
    const ir_module_t *source, const ir_abi_module_t *abi_module,
    wasm32_machine_module_t *module) {
  if (!source || !abi_module || !module) return 0;
  memset(module, 0, sizeof(*module));
  for (const ir_func_t *function = source->funcs; function;
       function = function->next)
    module->function_count++;
  for (const ir_symbol_t *symbol = source->symbols; symbol;
       symbol = symbol->next)
    module->symbol_count++;
  if (module->function_count > 0) {
    module->functions = calloc(
        module->function_count, sizeof(*module->functions));
    if (!module->functions) goto fail;
  }
  if (module->symbol_count > 0) {
    module->symbols = calloc(
        module->symbol_count, sizeof(*module->symbols));
    if (!module->symbols) goto fail;
  }
  size_t symbol_index = 0;
  for (const ir_symbol_t *symbol = source->symbols; symbol;
       symbol = symbol->next, symbol_index++) {
    if (!copy_symbol(&module->symbols[symbol_index], symbol))
      goto fail;
  }
  size_t index = 0;
  for (const ir_func_t *function = source->funcs; function;
       function = function->next, index++) {
    if (!wasm32_machine_function_build(
            function, abi_module, &module->functions[index]))
      goto fail;
    wasm32_machine_function_t *machine = &module->functions[index];
    for (int instruction_index = 0;
         instruction_index < machine->instruction_count;
         instruction_index++) {
      wasm32_machine_inst_t *instruction =
          &machine->instructions[instruction_index];
      if ((instruction->kind == WASM32_MACHINE_INST_SYMBOL_ADDRESS &&
           !instruction->is_function_symbol) ||
          instruction->kind == WASM32_MACHINE_INST_TLS_ADDRESS) {
        instruction->resolved_symbol = wasm32_machine_module_symbol(
            module, instruction->sym, instruction->sym_len);
        if (!instruction->resolved_symbol) goto fail;
      }
    }
  }
  return 1;

fail:
  wasm32_machine_module_dispose(module);
  return 0;
}

const wasm32_machine_function_t *wasm32_machine_module_function(
    const wasm32_machine_module_t *module, size_t index) {
  return module && index < module->function_count
             ? &module->functions[index] : NULL;
}

const wasm32_machine_symbol_t *wasm32_machine_module_symbol(
    const wasm32_machine_module_t *module,
    const char *name, int name_len) {
  if (!module || !name || name_len <= 0) return NULL;
  for (size_t i = 0; i < module->symbol_count; i++) {
    const wasm32_machine_symbol_t *symbol = &module->symbols[i];
    if (symbol->name_len == name_len &&
        memcmp(symbol->name, name, (size_t)name_len) == 0)
      return symbol;
  }
  return NULL;
}

const wasm32_machine_symbol_func_ref_t *
wasm32_machine_symbol_find_func_ref(
    const wasm32_machine_symbol_t *symbol, int offset) {
  if (!symbol) return NULL;
  for (int i = 0; i < symbol->func_ref_count; i++) {
    if (symbol->func_refs[i].offset == offset)
      return &symbol->func_refs[i];
  }
  return NULL;
}

const wasm32_machine_data_object_t *wasm32_machine_module_data_object(
    const wasm32_machine_module_t *module,
    const char *name, int name_len) {
  if (!module || !name || name_len <= 0) return NULL;
  for (size_t i = 0; i < module->data_object_count; i++) {
    const wasm32_machine_data_object_t *object =
        &module->data_objects[i];
    if (object->name_len == name_len &&
        memcmp(object->name, name, (size_t)name_len) == 0)
      return object;
  }
  return NULL;
}
