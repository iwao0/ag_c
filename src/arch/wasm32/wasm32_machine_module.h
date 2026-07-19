#ifndef ARCH_WASM32_MACHINE_MODULE_H
#define ARCH_WASM32_MACHINE_MODULE_H

#include "wasm32_machine_function.h"

typedef struct {
  const ir_module_t *source;
  wasm32_machine_function_t *functions;
  size_t function_count;
} wasm32_machine_module_t;

int wasm32_machine_module_build(
    const ir_module_t *source, const ir_abi_module_t *abi_module,
    wasm32_machine_module_t *module);
void wasm32_machine_module_dispose(wasm32_machine_module_t *module);
const wasm32_machine_function_t *wasm32_machine_module_function(
    const wasm32_machine_module_t *module, size_t index);

#endif
