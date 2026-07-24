#ifndef ARCH_WASM32_WAT_RUNTIME_H
#define ARCH_WASM32_WAT_RUNTIME_H

#include "../../codegen_emit.h"
#include "../../ir/ir.h"
#include "wasm32_machine_function.h"

typedef struct {
  char *name;
  int name_len;
  int addr;
  int size;
} wasm_data_symbol_t;

typedef struct {
  char *name;
  int name_len;
  ir_type_t result_type;
  ir_type_t *param_types;
  int param_count;
  unsigned char referenced;
  unsigned char defined;
  unsigned char has_signature;
} wasm_function_symbol_t;

typedef struct wasm32_ir_context_t wasm32_ir_context_t;

typedef enum {
  WASM32_WAT_RUNTIME_ARGUMENT_SOURCE = 0,
  WASM32_WAT_RUNTIME_ARGUMENT_ZERO_I32,
  WASM32_WAT_RUNTIME_ARGUMENT_ZERO_I64,
} wasm32_wat_runtime_argument_kind_t;

typedef struct {
  wasm32_wat_runtime_argument_kind_t kind;
  int source_index;
  ir_type_t value_type;
} wasm32_wat_runtime_argument_t;

typedef struct {
  wasm32_wat_runtime_argument_t *arguments;
  int argument_count;
} wasm32_wat_runtime_call_plan_t;

typedef struct {
  const wasm32_machine_inst_t *instruction;
  wasm32_wat_runtime_call_plan_t call;
} wasm32_wat_runtime_call_plan_entry_t;

typedef struct {
  wasm32_wat_runtime_call_plan_entry_t *entries;
  size_t entry_count;
} wasm32_wat_runtime_module_plan_t;

int wasm32_wat_runtime_plan_call(
    const char *name, int name_len, int is_undefined,
    const wasm32_machine_call_t *call,
    wasm32_wat_runtime_call_plan_t *plan);
void wasm32_wat_runtime_call_plan_dispose(
    wasm32_wat_runtime_call_plan_t *plan);
int wasm32_wat_runtime_module_plan_build(
    const wasm32_machine_function_t *functions,
    size_t function_count,
    wasm32_wat_runtime_module_plan_t *plan);
const wasm32_wat_runtime_call_plan_t *
wasm32_wat_runtime_module_plan_call(
    const wasm32_wat_runtime_module_plan_t *plan,
    const wasm32_machine_inst_t *instruction);
void wasm32_wat_runtime_module_plan_dispose(
    wasm32_wat_runtime_module_plan_t *plan);

ag_codegen_emit_context_t *wasm32_ir_emit_context(
    wasm32_ir_context_t *context);
wasm_function_symbol_t *function_symbol_state(
    wasm32_ir_context_t *context,
    const char *name, int name_len, int create);
wasm_data_symbol_t *intern_data_symbol(
    wasm32_ir_context_t *context,
    const char *name, int name_len, int size, int align);
const char *wasm_any_type_or_unsupported(
    wasm32_ir_context_t *context, ir_type_t type);
const char *wasm_type(ir_type_t type);
void wasm_unsupported_msg(
    wasm32_ir_context_t *context, const char *message);
void emit_i32_data_bytes(
    wasm32_ir_context_t *context,
    int address, long long value, int size);
int has_undefined_function(
    wasm32_ir_context_t *context, const char *name, int length);
int has_defined_function(
    wasm32_ir_context_t *context, const char *name, int length);
int function_table_has_ref(
    wasm32_ir_context_t *context, const char *name, int name_length);
void wasm32_wat_require_function_table(wasm32_ir_context_t *context);

void wasm32_wat_emit_minimal_libc_stubs(
    wasm32_ir_context_t *context);

#endif
