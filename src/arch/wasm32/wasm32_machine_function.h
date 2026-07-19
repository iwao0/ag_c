#ifndef ARCH_WASM32_MACHINE_FUNCTION_H
#define ARCH_WASM32_MACHINE_FUNCTION_H

#include "wasm32_machine_abi.h"
#include "wasm32_machine_ir.h"

typedef struct wasm32_machine_symbol_t wasm32_machine_symbol_t;

typedef struct {
  int vreg;
  int offset;
  int size;
  ir_type_t value_type;
} wasm32_machine_alloca_t;

typedef enum {
  WASM32_MACHINE_INST_NONE = 0,
  WASM32_MACHINE_INST_NOP,
  WASM32_MACHINE_INST_ALLOCA,
  WASM32_MACHINE_INST_INTEGER_CONSTANT,
  WASM32_MACHINE_INST_FLOAT_CONSTANT,
  WASM32_MACHINE_INST_STRING_ADDRESS,
  WASM32_MACHINE_INST_SYMBOL_ADDRESS,
  WASM32_MACHINE_INST_TLS_ADDRESS,
  WASM32_MACHINE_INST_BINARY,
  WASM32_MACHINE_INST_UNARY,
  WASM32_MACHINE_INST_CONVERSION,
  WASM32_MACHINE_INST_LOAD,
  WASM32_MACHINE_INST_STORE,
  WASM32_MACHINE_INST_ATOMIC,
  WASM32_MACHINE_INST_MEMORY_COPY,
  WASM32_MACHINE_INST_ALIGN_POINTER,
  WASM32_MACHINE_INST_DYNAMIC_ALLOCA,
  WASM32_MACHINE_INST_VARARG_AREA,
  WASM32_MACHINE_INST_ADDRESS_ADD,
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

typedef enum {
  WASM32_MACHINE_ARGUMENT_DIRECT = 0,
  WASM32_MACHINE_ARGUMENT_LOAD,
} wasm32_machine_argument_access_t;

typedef struct {
  ir_val_t source;
  ir_type_t value_type;
  int byte_offset;
  wasm32_machine_argument_access_t access;
  wasm32_machine_memory_t load;
} wasm32_machine_argument_t;

typedef struct {
  int argument_index;
  int byte_offset;
  ir_type_t argument_type;
  wasm32_machine_conversion_t conversion;
  wasm32_machine_memory_t store;
} wasm32_machine_variadic_argument_t;

typedef struct {
  wasm32_machine_signature_t signature;
  wasm32_machine_argument_t *arguments;
  int argument_count;
  int fixed_argument_count;
  ir_val_t result_area;
  ir_type_t direct_result_type;
  wasm32_machine_memory_t direct_result_store;
  wasm32_machine_variadic_argument_t *variadic_arguments;
  int variadic_argument_count;
  int variadic_area_size;
  unsigned char is_indirect;
  unsigned char is_variadic;
} wasm32_machine_call_t;

typedef enum {
  WASM32_MACHINE_PARAMETER_DIRECT = 0,
  WASM32_MACHINE_PARAMETER_INDIRECT,
} wasm32_machine_parameter_kind_t;

typedef struct {
  ir_type_t value_type;
  int source_size;
  int byte_offset;
  wasm32_machine_parameter_kind_t kind;
} wasm32_machine_parameter_piece_t;

typedef struct {
  wasm32_machine_parameter_piece_t *pieces;
  wasm32_machine_memory_t *stores;
  wasm32_machine_copy_plan_t *copy_plans;
  int piece_count;
  int physical_index;
} wasm32_machine_parameter_bind_t;

typedef enum {
  WASM32_MACHINE_ATOMIC_FENCE = 0,
  WASM32_MACHINE_ATOMIC_LOAD,
  WASM32_MACHINE_ATOMIC_STORE,
  WASM32_MACHINE_ATOMIC_EXCHANGE,
  WASM32_MACHINE_ATOMIC_RMW,
  WASM32_MACHINE_ATOMIC_COMPARE_EXCHANGE,
} wasm32_machine_atomic_kind_t;

typedef struct {
  wasm32_machine_atomic_kind_t kind;
  wasm32_machine_memory_t load;
  wasm32_machine_memory_t store;
  wasm32_machine_binary_t binary;
  wasm32_machine_binary_t comparison;
} wasm32_machine_atomic_t;

typedef struct {
  const wasm32_machine_symbol_t *resolved_symbol;
  ir_op_t op;
  ir_val_t dst;
  ir_val_t src1;
  ir_val_t src2;
  ir_val_t src3;
  ir_val_t callee;
  ir_val_t result_storage;
  char *sym;
  int sym_len;
  int object_size;
  int alloca_size;
  int alloca_align;
  size_t parameter_index;
  unsigned char is_unsigned;
  unsigned char is_function_symbol;
  unsigned char is_implicit_call;
  unsigned char has_reference_signature;
  wasm32_machine_inst_kind_t kind;
  wasm32_machine_binary_t binary;
  wasm32_machine_unary_t unary;
  wasm32_machine_conversion_t conversion;
  wasm32_machine_memory_t load;
  wasm32_machine_memory_t store;
  wasm32_machine_atomic_t atomic;
  wasm32_machine_copy_plan_t copy;
  wasm32_machine_call_t call;
  wasm32_machine_parameter_bind_t parameter_bind;
  wasm32_machine_control_t control;
  wasm32_machine_signature_t reference_signature;
} wasm32_machine_inst_t;

typedef struct {
  int id;
  int first_instruction;
  int instruction_count;
  int next_block_id;
  unsigned char has_terminator;
} wasm32_machine_block_t;

typedef struct {
  char *name;
  char *c_signature;
  char *continuation_entry_name;
  char *continuation_condition_name;
  char *continuation_start_export;
  char *continuation_resume_export;
  char *continuation_status_export;
  char *continuation_result_export;
  wasm32_machine_signature_t signature;
  ir_type_t direct_result_type;
  int result_source_size;
  wasm32_machine_memory_t direct_result_load;
  wasm32_machine_copy_plan_t result_copy;
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
  int name_len;
  int c_signature_len;
  int continuation_condition_block_id;
  int is_static;
  unsigned char has_control_flow;
  unsigned char has_vla_alloc;
  unsigned char has_variadic_varargs;
  unsigned char has_atomic_cas32;
  unsigned char has_atomic_cas64;
  unsigned char is_continuation_entry;
  unsigned char continuation_has_suspend;
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
const wasm32_machine_block_t *wasm32_machine_function_block(
    const wasm32_machine_function_t *machine_function,
    int block_id);

#endif
