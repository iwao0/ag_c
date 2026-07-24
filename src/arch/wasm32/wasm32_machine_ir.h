#ifndef ARCH_WASM32_MACHINE_IR_H
#define ARCH_WASM32_MACHINE_IR_H

#include "../../ir/ir.h"

typedef enum {
  WASM32_MI_INVALID = 0,
  WASM32_MI_I32_ADD,
  WASM32_MI_I64_ADD,
  WASM32_MI_I32_SUB,
  WASM32_MI_I64_SUB,
  WASM32_MI_I32_MUL,
  WASM32_MI_I64_MUL,
  WASM32_MI_I32_DIV_S,
  WASM32_MI_I64_DIV_S,
  WASM32_MI_I32_DIV_U,
  WASM32_MI_I64_DIV_U,
  WASM32_MI_I32_REM_S,
  WASM32_MI_I64_REM_S,
  WASM32_MI_I32_REM_U,
  WASM32_MI_I64_REM_U,
  WASM32_MI_I32_AND,
  WASM32_MI_I64_AND,
  WASM32_MI_I32_OR,
  WASM32_MI_I64_OR,
  WASM32_MI_I32_XOR,
  WASM32_MI_I64_XOR,
  WASM32_MI_I32_SHL,
  WASM32_MI_I64_SHL,
  WASM32_MI_I32_SHR_S,
  WASM32_MI_I64_SHR_S,
  WASM32_MI_I32_SHR_U,
  WASM32_MI_I64_SHR_U,
  WASM32_MI_I32_EQ,
  WASM32_MI_I64_EQ,
  WASM32_MI_I32_NE,
  WASM32_MI_I64_NE,
  WASM32_MI_I32_LT_S,
  WASM32_MI_I64_LT_S,
  WASM32_MI_I32_LT_U,
  WASM32_MI_I64_LT_U,
  WASM32_MI_I32_LE_S,
  WASM32_MI_I64_LE_S,
  WASM32_MI_I32_LE_U,
  WASM32_MI_I64_LE_U,
  WASM32_MI_I32_EQZ,
  WASM32_MI_I64_EQZ,
  WASM32_MI_F32_ADD,
  WASM32_MI_F64_ADD,
  WASM32_MI_F32_SUB,
  WASM32_MI_F64_SUB,
  WASM32_MI_F32_MUL,
  WASM32_MI_F64_MUL,
  WASM32_MI_F32_DIV,
  WASM32_MI_F64_DIV,
  WASM32_MI_F32_EQ,
  WASM32_MI_F64_EQ,
  WASM32_MI_F32_NE,
  WASM32_MI_F64_NE,
  WASM32_MI_F32_LT,
  WASM32_MI_F64_LT,
  WASM32_MI_F32_LE,
  WASM32_MI_F64_LE,
  WASM32_MI_COPY,
  WASM32_MI_I32_WRAP_I64,
  WASM32_MI_I32_EXTEND8_S,
  WASM32_MI_I32_EXTEND16_S,
  WASM32_MI_I64_EXTEND_I32_S,
  WASM32_MI_I64_EXTEND_I32_U,
  WASM32_MI_F32_CONVERT_I32_S,
  WASM32_MI_F32_CONVERT_I32_U,
  WASM32_MI_F32_CONVERT_I64_S,
  WASM32_MI_F32_CONVERT_I64_U,
  WASM32_MI_F64_CONVERT_I32_S,
  WASM32_MI_F64_CONVERT_I32_U,
  WASM32_MI_F64_CONVERT_I64_S,
  WASM32_MI_F64_CONVERT_I64_U,
  WASM32_MI_I32_TRUNC_F32_S,
  WASM32_MI_I32_TRUNC_F32_U,
  WASM32_MI_I32_TRUNC_F64_S,
  WASM32_MI_I32_TRUNC_F64_U,
  WASM32_MI_I64_TRUNC_F32_S,
  WASM32_MI_I64_TRUNC_F32_U,
  WASM32_MI_I64_TRUNC_F64_S,
  WASM32_MI_I64_TRUNC_F64_U,
  WASM32_MI_F32_DEMOTE_F64,
  WASM32_MI_F64_PROMOTE_F32,
  WASM32_MI_F32_NEG,
  WASM32_MI_F64_NEG,
  WASM32_MI_I32_LOAD8_S,
  WASM32_MI_I32_LOAD8_U,
  WASM32_MI_I32_LOAD16_S,
  WASM32_MI_I32_LOAD16_U,
  WASM32_MI_I32_LOAD,
  WASM32_MI_I64_LOAD,
  WASM32_MI_F32_LOAD,
  WASM32_MI_F64_LOAD,
  WASM32_MI_I32_STORE8,
  WASM32_MI_I32_STORE16,
  WASM32_MI_I32_STORE,
  WASM32_MI_I64_STORE,
  WASM32_MI_F32_STORE,
  WASM32_MI_F64_STORE,
  WASM32_MI_COUNT,
} wasm32_machine_opcode_t;

typedef struct {
  wasm32_machine_opcode_t opcode;
  ir_type_t operand_type;
} wasm32_machine_zero_test_t;

typedef struct {
  wasm32_machine_opcode_t opcode;
  ir_type_t operand_type;
  ir_type_t result_type;
  wasm32_machine_zero_test_t zero_test;
  unsigned char is_comparison;
  unsigned char is_unsigned;
  unsigned char is_shift;
  unsigned char guard_zero_divisor;
  unsigned char tracks_address;
  unsigned char subtracts_address;
} wasm32_machine_binary_t;

typedef struct {
  wasm32_machine_opcode_t opcode;
  ir_type_t source_type;
  ir_type_t result_type;
  int immediate;
  unsigned char has_immediate;
} wasm32_machine_conversion_t;

typedef enum {
  WASM32_MI_UNARY_DIRECT = 0,
  WASM32_MI_UNARY_ZERO_THEN_OPERAND,
  WASM32_MI_UNARY_OPERAND_THEN_NEG_ONE,
} wasm32_machine_unary_form_t;

typedef struct {
  wasm32_machine_opcode_t opcode;
  ir_type_t operand_type;
  ir_type_t result_type;
  wasm32_machine_unary_form_t form;
} wasm32_machine_unary_t;

typedef struct {
  wasm32_machine_opcode_t opcode;
  ir_type_t memory_type;
  ir_type_t value_type;
  unsigned alignment_log2;
} wasm32_machine_memory_t;

typedef struct {
  int offset;
  wasm32_machine_memory_t load;
  wasm32_machine_memory_t store;
} wasm32_machine_copy_chunk_t;

typedef struct {
  int alignment;
  int addend;
  int mask;
} wasm32_machine_alignment_t;

typedef struct {
  int fixed_frame_size;
  unsigned char has_dynamic_allocation;
  unsigned char has_variadic_call_area;
  unsigned char has_persistent_frame;
  unsigned char saves_stack_pointer;
  unsigned char restores_stack_pointer;
  unsigned char uses_stack_pointer;
} wasm32_machine_stack_plan_t;

typedef struct {
  wasm32_machine_copy_chunk_t *chunks;
  int chunk_count;
} wasm32_machine_copy_plan_t;

#define WASM32_MACHINE_IR_TYPE_COUNT ((int)IR_TY_PTR + 1)

typedef struct {
  wasm32_machine_conversion_t
      conversions[WASM32_MACHINE_IR_TYPE_COUNT]
                 [WASM32_MACHINE_IR_TYPE_COUNT][2];
  wasm32_machine_memory_t
      loads[WASM32_MACHINE_IR_TYPE_COUNT][2];
  wasm32_machine_memory_t stores[WASM32_MACHINE_IR_TYPE_COUNT];
  unsigned char
      conversion_valid[WASM32_MACHINE_IR_TYPE_COUNT]
                      [WASM32_MACHINE_IR_TYPE_COUNT][2];
  unsigned char load_valid[WASM32_MACHINE_IR_TYPE_COUNT][2];
  unsigned char store_valid[WASM32_MACHINE_IR_TYPE_COUNT];
  wasm32_machine_binary_t i32_add;
  wasm32_machine_binary_t i32_subtract;
  wasm32_machine_binary_t i32_and;
  wasm32_machine_binary_t i32_equal;
  wasm32_machine_binary_t i32_not_equal;
  wasm32_machine_zero_test_t i32_zero_test;
  wasm32_machine_zero_test_t i64_zero_test;
} wasm32_machine_primitive_plan_t;

ir_type_t wasm32_machine_value_type(ir_type_t type);

int wasm32_machine_select_binary(
    ir_op_t source_op, ir_type_t operand_type,
    wasm32_machine_binary_t *selected);
int wasm32_machine_select_conversion(
    ir_type_t source_type, ir_type_t result_type, int is_unsigned,
    wasm32_machine_conversion_t *selected);
int wasm32_machine_select_ir_conversion(
    ir_op_t source_op, ir_type_t source_type, ir_type_t result_type,
    int is_unsigned, wasm32_machine_conversion_t *selected);
int wasm32_machine_select_unary(
    ir_op_t source_op, ir_type_t operand_type,
    wasm32_machine_unary_t *selected);
int wasm32_machine_select_zero_test(
    ir_type_t operand_type,
    wasm32_machine_zero_test_t *selected);
int wasm32_machine_select_atomic_rmw(
    ir_atomic_rmw_op_t source_op, ir_type_t operand_type,
    wasm32_machine_binary_t *selected);
int wasm32_machine_select_load(
    ir_type_t memory_type, int is_unsigned,
    wasm32_machine_memory_t *selected);
int wasm32_machine_select_store(
    ir_type_t memory_type, wasm32_machine_memory_t *selected);
int wasm32_machine_copy_plan_build(
    int size, wasm32_machine_copy_plan_t *plan);
void wasm32_machine_copy_plan_dispose(
    wasm32_machine_copy_plan_t *plan);
int wasm32_machine_alignment_plan_build(
    int requested_alignment, int default_alignment,
    wasm32_machine_alignment_t *plan);
int wasm32_machine_stack_plan_build(
    int fixed_frame_size, int has_dynamic_allocation,
    int has_variadic_call_area, int has_persistent_frame,
    wasm32_machine_stack_plan_t *plan);
int wasm32_machine_primitive_plan_build(
    wasm32_machine_primitive_plan_t *plan);
const wasm32_machine_conversion_t *wasm32_machine_planned_conversion(
    const wasm32_machine_primitive_plan_t *plan,
    ir_type_t source_type, ir_type_t result_type, int is_unsigned);
const wasm32_machine_memory_t *wasm32_machine_planned_load(
    const wasm32_machine_primitive_plan_t *plan,
    ir_type_t memory_type, int is_unsigned);
const wasm32_machine_memory_t *wasm32_machine_planned_store(
    const wasm32_machine_primitive_plan_t *plan,
    ir_type_t memory_type);
const char *wasm32_machine_opcode_wat(wasm32_machine_opcode_t opcode);
unsigned wasm32_machine_opcode_binary(wasm32_machine_opcode_t opcode);
int wasm32_machine_opcode_is_comparison(wasm32_machine_opcode_t opcode);
int wasm32_machine_opcode_is_unsigned(wasm32_machine_opcode_t opcode);
int wasm32_machine_opcode_is_shift(wasm32_machine_opcode_t opcode);
int wasm32_machine_opcode_is_remainder(wasm32_machine_opcode_t opcode);
int wasm32_machine_opcode_is_add(wasm32_machine_opcode_t opcode);
int wasm32_machine_opcode_is_subtract(wasm32_machine_opcode_t opcode);

#endif
