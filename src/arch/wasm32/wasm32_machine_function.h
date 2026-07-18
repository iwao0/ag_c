#ifndef ARCH_WASM32_MACHINE_FUNCTION_H
#define ARCH_WASM32_MACHINE_FUNCTION_H

#include "wasm32_machine_abi.h"
#include "wasm32_machine_ir.h"

typedef struct {
  int vreg;
  int offset;
  int size;
  ir_type_t value_type;
} wasm32_machine_alloca_t;

typedef enum {
  WASM32_MACHINE_INST_NONE = 0,
  WASM32_MACHINE_INST_BINARY,
  WASM32_MACHINE_INST_UNARY,
  WASM32_MACHINE_INST_CONVERSION,
  WASM32_MACHINE_INST_LOAD,
  WASM32_MACHINE_INST_STORE,
  WASM32_MACHINE_INST_ATOMIC,
  WASM32_MACHINE_INST_CALL,
  WASM32_MACHINE_INST_PARAMETER_BIND,
  WASM32_MACHINE_INST_CONTROL,
} wasm32_machine_inst_kind_t;

typedef enum {
  WASM32_MACHINE_CONTROL_LABEL = 0,
  WASM32_MACHINE_CONTROL_BRANCH,
  WASM32_MACHINE_CONTROL_BRANCH_CONDITIONAL,
  WASM32_MACHINE_CONTROL_RETURN,
  WASM32_MACHINE_CONTROL_SUSPEND,
} wasm32_machine_control_kind_t;

typedef struct {
  wasm32_machine_control_kind_t kind;
  int target_block_id;
  int else_block_id;
  ir_val_t value;
} wasm32_machine_control_t;

typedef struct {
  wasm32_machine_signature_t signature;
  ir_abi_argument_t *arguments;
  int argument_count;
  int fixed_argument_count;
  ir_val_t result_area;
  ir_type_t direct_result_type;
  unsigned char is_indirect;
  unsigned char is_variadic;
} wasm32_machine_call_t;

typedef struct {
  ir_abi_piece_t *pieces;
  int piece_count;
  int physical_index;
} wasm32_machine_parameter_bind_t;

typedef struct {
  ir_inst_t *source;
  wasm32_machine_inst_kind_t kind;
  wasm32_machine_binary_t binary;
  wasm32_machine_unary_t unary;
  wasm32_machine_conversion_t conversion;
  wasm32_machine_memory_t load;
  wasm32_machine_memory_t store;
  wasm32_machine_call_t call;
  wasm32_machine_parameter_bind_t parameter_bind;
  wasm32_machine_control_t control;
} wasm32_machine_inst_t;

typedef struct {
  ir_block_t *source;
  int id;
  int first_instruction;
  int instruction_count;
  int next_block_id;
  unsigned char has_terminator;
} wasm32_machine_block_t;

typedef struct {
  const ir_func_t *source;
  wasm32_machine_signature_t signature;
  ir_type_t direct_result_type;
  int result_source_size;
  ir_type_t *vreg_types;
  unsigned char *vreg_unsigned;
  unsigned char *vreg_used;
  int vreg_count;
  wasm32_machine_inst_t *instructions;
  int instruction_count;
  wasm32_machine_block_t *blocks;
  int block_count;
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
const wasm32_machine_inst_t *wasm32_machine_function_instruction(
    const wasm32_machine_function_t *machine_function,
    const ir_inst_t *source_instruction);
const wasm32_machine_block_t *wasm32_machine_function_block(
    const wasm32_machine_function_t *machine_function,
    int block_id);

#endif
