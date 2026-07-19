#ifndef ARCH_WASM32_MACHINE_MODULE_H
#define ARCH_WASM32_MACHINE_MODULE_H

#include "wasm32_machine_function.h"

typedef struct {
  char *name;
  int name_len;
  int offset;
} wasm32_machine_symbol_func_ref_t;

struct wasm32_machine_symbol_t {
  char *name;
  wasm32_machine_symbol_func_ref_t *func_refs;
  int name_len;
  int byte_size;
  int alignment;
  int func_ref_count;
  unsigned char is_extern;
  unsigned char is_static;
  unsigned char is_thread_local;
};

typedef struct {
  wasm32_machine_function_t *functions;
  wasm32_machine_symbol_t *symbols;
  size_t function_count;
  size_t symbol_count;
} wasm32_machine_module_t;

int wasm32_machine_module_build(
    const ir_module_t *source, const ir_abi_module_t *abi_module,
    wasm32_machine_module_t *module);
void wasm32_machine_module_dispose(wasm32_machine_module_t *module);
const wasm32_machine_function_t *wasm32_machine_module_function(
    const wasm32_machine_module_t *module, size_t index);
const wasm32_machine_symbol_t *wasm32_machine_module_symbol(
    const wasm32_machine_module_t *module,
    const char *name, int name_len);
const wasm32_machine_symbol_func_ref_t *
wasm32_machine_symbol_find_func_ref(
    const wasm32_machine_symbol_t *symbol, int offset);

#endif
