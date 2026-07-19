#include "wasm32_machine_module.h"

#include <stdlib.h>
#include <string.h>

void wasm32_machine_module_dispose(wasm32_machine_module_t *module) {
  if (!module) return;
  for (size_t i = 0; i < module->function_count; i++)
    wasm32_machine_function_dispose(&module->functions[i]);
  free(module->functions);
  memset(module, 0, sizeof(*module));
}

int wasm32_machine_module_build(
    const ir_module_t *source, const ir_abi_module_t *abi_module,
    wasm32_machine_module_t *module) {
  if (!source || !abi_module || !module) return 0;
  memset(module, 0, sizeof(*module));
  module->source = source;
  for (const ir_func_t *function = source->funcs; function;
       function = function->next)
    module->function_count++;
  if (module->function_count > 0) {
    module->functions = calloc(
        module->function_count, sizeof(*module->functions));
    if (!module->functions) goto fail;
  }
  size_t index = 0;
  for (const ir_func_t *function = source->funcs; function;
       function = function->next, index++) {
    if (!wasm32_machine_function_build(
            function, abi_module, &module->functions[index]))
      goto fail;
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
