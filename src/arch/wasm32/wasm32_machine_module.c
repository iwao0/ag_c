#include "wasm32_machine_module.h"

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

void wasm32_machine_module_dispose(wasm32_machine_module_t *module) {
  if (!module) return;
  for (size_t i = 0; i < module->function_count; i++)
    wasm32_machine_function_dispose(&module->functions[i]);
  for (size_t i = 0; i < module->symbol_count; i++)
    dispose_symbol(&module->symbols[i]);
  free(module->functions);
  free(module->symbols);
  memset(module, 0, sizeof(*module));
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
