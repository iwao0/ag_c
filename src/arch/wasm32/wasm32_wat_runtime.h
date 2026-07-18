#ifndef ARCH_WASM32_WAT_RUNTIME_H
#define ARCH_WASM32_WAT_RUNTIME_H

#include "../../codegen_emit.h"
#include "../../ir/ir.h"

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

ag_codegen_emit_context_t *wasm32_ir_emit_context(void);
wasm_function_symbol_t *function_symbol_state(
    const char *name, int name_len, int create);
wasm_data_symbol_t *intern_data_symbol(
    const char *name, int name_len, int size, int align);
const char *wasm_any_type_or_unsupported(ir_type_t type);
const char *wasm_type(ir_type_t type);
void wasm_unsupported_msg(const char *message);
void emit_i32_data_bytes(int address, long long value, int size);
int has_undefined_function(const char *name, int length);
int has_defined_function(const char *name, int length);
int function_table_has_ref(const char *name, int name_length);
void wasm32_wat_require_function_table(void);

void wasm32_wat_emit_minimal_libc_stubs(void);

#endif
