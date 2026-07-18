#ifndef ARCH_WASM32_MACHINE_FUNCTION_H
#define ARCH_WASM32_MACHINE_FUNCTION_H

#include "wasm32_machine_abi.h"

typedef struct {
  int vreg;
  int offset;
  int size;
  ir_type_t value_type;
} wasm32_machine_alloca_t;

typedef struct {
  const ir_func_t *source;
  wasm32_machine_signature_t signature;
  ir_type_t *vreg_types;
  unsigned char *vreg_unsigned;
  unsigned char *vreg_used;
  int vreg_count;
  wasm32_machine_alloca_t *allocas;
  int alloca_count;
  int frame_size;
  unsigned char has_control_flow;
  unsigned char has_vla_alloc;
  unsigned char has_variadic_varargs;
  unsigned char has_atomic_cas32;
  unsigned char has_atomic_cas64;
} wasm32_machine_function_t;

int wasm32_machine_function_build(
    const ir_func_t *function,
    const ir_abi_module_t *abi_module,
    wasm32_machine_function_t *machine_function);
void wasm32_machine_function_dispose(
    wasm32_machine_function_t *machine_function);
ir_type_t wasm32_machine_function_vreg_type(
    const wasm32_machine_function_t *machine_function,
    ir_val_t value);
int wasm32_machine_function_vreg_is_unsigned(
    const wasm32_machine_function_t *machine_function,
    ir_val_t value);
const wasm32_machine_alloca_t *wasm32_machine_function_alloca(
    const wasm32_machine_function_t *machine_function,
    int vreg);

#endif
