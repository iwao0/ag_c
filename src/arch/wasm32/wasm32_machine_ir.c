#include "wasm32_machine_ir.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const char *wat;
  unsigned binary;
} wasm32_opcode_encoding_t;

static const wasm32_opcode_encoding_t opcode_encodings[WASM32_MI_COUNT] = {
    [WASM32_MI_I32_ADD] = {"i32.add", 0x6a},
    [WASM32_MI_I64_ADD] = {"i64.add", 0x7c},
    [WASM32_MI_I32_SUB] = {"i32.sub", 0x6b},
    [WASM32_MI_I64_SUB] = {"i64.sub", 0x7d},
    [WASM32_MI_I32_MUL] = {"i32.mul", 0x6c},
    [WASM32_MI_I64_MUL] = {"i64.mul", 0x7e},
    [WASM32_MI_I32_DIV_S] = {"i32.div_s", 0x6d},
    [WASM32_MI_I64_DIV_S] = {"i64.div_s", 0x7f},
    [WASM32_MI_I32_DIV_U] = {"i32.div_u", 0x6e},
    [WASM32_MI_I64_DIV_U] = {"i64.div_u", 0x80},
    [WASM32_MI_I32_REM_S] = {"i32.rem_s", 0x6f},
    [WASM32_MI_I64_REM_S] = {"i64.rem_s", 0x81},
    [WASM32_MI_I32_REM_U] = {"i32.rem_u", 0x70},
    [WASM32_MI_I64_REM_U] = {"i64.rem_u", 0x82},
    [WASM32_MI_I32_AND] = {"i32.and", 0x71},
    [WASM32_MI_I64_AND] = {"i64.and", 0x83},
    [WASM32_MI_I32_OR] = {"i32.or", 0x72},
    [WASM32_MI_I64_OR] = {"i64.or", 0x84},
    [WASM32_MI_I32_XOR] = {"i32.xor", 0x73},
    [WASM32_MI_I64_XOR] = {"i64.xor", 0x85},
    [WASM32_MI_I32_SHL] = {"i32.shl", 0x74},
    [WASM32_MI_I64_SHL] = {"i64.shl", 0x86},
    [WASM32_MI_I32_SHR_S] = {"i32.shr_s", 0x75},
    [WASM32_MI_I64_SHR_S] = {"i64.shr_s", 0x87},
    [WASM32_MI_I32_SHR_U] = {"i32.shr_u", 0x76},
    [WASM32_MI_I64_SHR_U] = {"i64.shr_u", 0x88},
    [WASM32_MI_I32_EQ] = {"i32.eq", 0x46},
    [WASM32_MI_I64_EQ] = {"i64.eq", 0x51},
    [WASM32_MI_I32_NE] = {"i32.ne", 0x47},
    [WASM32_MI_I64_NE] = {"i64.ne", 0x52},
    [WASM32_MI_I32_LT_S] = {"i32.lt_s", 0x48},
    [WASM32_MI_I64_LT_S] = {"i64.lt_s", 0x53},
    [WASM32_MI_I32_LT_U] = {"i32.lt_u", 0x49},
    [WASM32_MI_I64_LT_U] = {"i64.lt_u", 0x54},
    [WASM32_MI_I32_LE_S] = {"i32.le_s", 0x4c},
    [WASM32_MI_I64_LE_S] = {"i64.le_s", 0x57},
    [WASM32_MI_I32_LE_U] = {"i32.le_u", 0x4d},
    [WASM32_MI_I64_LE_U] = {"i64.le_u", 0x58},
    [WASM32_MI_I32_EQZ] = {"i32.eqz", 0x45},
    [WASM32_MI_I64_EQZ] = {"i64.eqz", 0x50},
    [WASM32_MI_F32_ADD] = {"f32.add", 0x92},
    [WASM32_MI_F64_ADD] = {"f64.add", 0xa0},
    [WASM32_MI_F32_SUB] = {"f32.sub", 0x93},
    [WASM32_MI_F64_SUB] = {"f64.sub", 0xa1},
    [WASM32_MI_F32_MUL] = {"f32.mul", 0x94},
    [WASM32_MI_F64_MUL] = {"f64.mul", 0xa2},
    [WASM32_MI_F32_DIV] = {"f32.div", 0x95},
    [WASM32_MI_F64_DIV] = {"f64.div", 0xa3},
    [WASM32_MI_F32_EQ] = {"f32.eq", 0x5b},
    [WASM32_MI_F64_EQ] = {"f64.eq", 0x61},
    [WASM32_MI_F32_NE] = {"f32.ne", 0x5c},
    [WASM32_MI_F64_NE] = {"f64.ne", 0x62},
    [WASM32_MI_F32_LT] = {"f32.lt", 0x5d},
    [WASM32_MI_F64_LT] = {"f64.lt", 0x63},
    [WASM32_MI_F32_LE] = {"f32.le", 0x5f},
    [WASM32_MI_F64_LE] = {"f64.le", 0x65},
    [WASM32_MI_COPY] = {NULL, 0},
    [WASM32_MI_I32_WRAP_I64] = {"i32.wrap_i64", 0xa7},
    [WASM32_MI_I64_EXTEND_I32_S] = {"i64.extend_i32_s", 0xac},
    [WASM32_MI_I64_EXTEND_I32_U] = {"i64.extend_i32_u", 0xad},
    [WASM32_MI_F32_CONVERT_I32_S] = {"f32.convert_i32_s", 0xb2},
    [WASM32_MI_F32_CONVERT_I32_U] = {"f32.convert_i32_u", 0xb3},
    [WASM32_MI_F32_CONVERT_I64_S] = {"f32.convert_i64_s", 0xb4},
    [WASM32_MI_F32_CONVERT_I64_U] = {"f32.convert_i64_u", 0xb5},
    [WASM32_MI_F64_CONVERT_I32_S] = {"f64.convert_i32_s", 0xb7},
    [WASM32_MI_F64_CONVERT_I32_U] = {"f64.convert_i32_u", 0xb8},
    [WASM32_MI_F64_CONVERT_I64_S] = {"f64.convert_i64_s", 0xb9},
    [WASM32_MI_F64_CONVERT_I64_U] = {"f64.convert_i64_u", 0xba},
    [WASM32_MI_I32_TRUNC_F32_S] = {"i32.trunc_f32_s", 0xa8},
    [WASM32_MI_I32_TRUNC_F32_U] = {"i32.trunc_f32_u", 0xa9},
    [WASM32_MI_I32_TRUNC_F64_S] = {"i32.trunc_f64_s", 0xaa},
    [WASM32_MI_I32_TRUNC_F64_U] = {"i32.trunc_f64_u", 0xab},
    [WASM32_MI_I64_TRUNC_F32_S] = {"i64.trunc_f32_s", 0xae},
    [WASM32_MI_I64_TRUNC_F32_U] = {"i64.trunc_f32_u", 0xaf},
    [WASM32_MI_I64_TRUNC_F64_S] = {"i64.trunc_f64_s", 0xb0},
    [WASM32_MI_I64_TRUNC_F64_U] = {"i64.trunc_f64_u", 0xb1},
    [WASM32_MI_F32_DEMOTE_F64] = {"f32.demote_f64", 0xb6},
    [WASM32_MI_F64_PROMOTE_F32] = {"f64.promote_f32", 0xbb},
    [WASM32_MI_F32_NEG] = {"f32.neg", 0x8c},
    [WASM32_MI_F64_NEG] = {"f64.neg", 0x9a},
    [WASM32_MI_I32_LOAD8_S] = {"i32.load8_s", 0x2c},
    [WASM32_MI_I32_LOAD8_U] = {"i32.load8_u", 0x2d},
    [WASM32_MI_I32_LOAD16_S] = {"i32.load16_s", 0x2e},
    [WASM32_MI_I32_LOAD16_U] = {"i32.load16_u", 0x2f},
    [WASM32_MI_I32_LOAD] = {"i32.load", 0x28},
    [WASM32_MI_I64_LOAD] = {"i64.load", 0x29},
    [WASM32_MI_F32_LOAD] = {"f32.load", 0x2a},
    [WASM32_MI_F64_LOAD] = {"f64.load", 0x2b},
    [WASM32_MI_I32_STORE8] = {"i32.store8", 0x3a},
    [WASM32_MI_I32_STORE16] = {"i32.store16", 0x3b},
    [WASM32_MI_I32_STORE] = {"i32.store", 0x36},
    [WASM32_MI_I64_STORE] = {"i64.store", 0x37},
    [WASM32_MI_F32_STORE] = {"f32.store", 0x38},
    [WASM32_MI_F64_STORE] = {"f64.store", 0x39},
};

ir_type_t wasm32_machine_value_type(ir_type_t type) {
  if (type == IR_TY_I8 || type == IR_TY_I16 || type == IR_TY_PTR)
    return IR_TY_I32;
  return type;
}

static wasm32_machine_opcode_t select_integer(
    ir_op_t op, int is64) {
  switch (op) {
    case IR_ADD: return is64 ? WASM32_MI_I64_ADD : WASM32_MI_I32_ADD;
    case IR_SUB: return is64 ? WASM32_MI_I64_SUB : WASM32_MI_I32_SUB;
    case IR_MUL: return is64 ? WASM32_MI_I64_MUL : WASM32_MI_I32_MUL;
    case IR_DIV: return is64 ? WASM32_MI_I64_DIV_S : WASM32_MI_I32_DIV_S;
    case IR_UDIV: return is64 ? WASM32_MI_I64_DIV_U : WASM32_MI_I32_DIV_U;
    case IR_MOD: return is64 ? WASM32_MI_I64_REM_S : WASM32_MI_I32_REM_S;
    case IR_UMOD: return is64 ? WASM32_MI_I64_REM_U : WASM32_MI_I32_REM_U;
    case IR_AND: return is64 ? WASM32_MI_I64_AND : WASM32_MI_I32_AND;
    case IR_OR: return is64 ? WASM32_MI_I64_OR : WASM32_MI_I32_OR;
    case IR_XOR: return is64 ? WASM32_MI_I64_XOR : WASM32_MI_I32_XOR;
    case IR_SHL: return is64 ? WASM32_MI_I64_SHL : WASM32_MI_I32_SHL;
    case IR_SHR: return is64 ? WASM32_MI_I64_SHR_S : WASM32_MI_I32_SHR_S;
    case IR_LSR: return is64 ? WASM32_MI_I64_SHR_U : WASM32_MI_I32_SHR_U;
    case IR_EQ: return is64 ? WASM32_MI_I64_EQ : WASM32_MI_I32_EQ;
    case IR_NE: return is64 ? WASM32_MI_I64_NE : WASM32_MI_I32_NE;
    case IR_LT: return is64 ? WASM32_MI_I64_LT_S : WASM32_MI_I32_LT_S;
    case IR_ULT: return is64 ? WASM32_MI_I64_LT_U : WASM32_MI_I32_LT_U;
    case IR_LE: return is64 ? WASM32_MI_I64_LE_S : WASM32_MI_I32_LE_S;
    case IR_ULE: return is64 ? WASM32_MI_I64_LE_U : WASM32_MI_I32_LE_U;
    default: return WASM32_MI_INVALID;
  }
}

static wasm32_machine_opcode_t select_float(
    ir_op_t op, int is64) {
  switch (op) {
    case IR_FADD: return is64 ? WASM32_MI_F64_ADD : WASM32_MI_F32_ADD;
    case IR_FSUB: return is64 ? WASM32_MI_F64_SUB : WASM32_MI_F32_SUB;
    case IR_FMUL: return is64 ? WASM32_MI_F64_MUL : WASM32_MI_F32_MUL;
    case IR_FDIV: return is64 ? WASM32_MI_F64_DIV : WASM32_MI_F32_DIV;
    case IR_FEQ: return is64 ? WASM32_MI_F64_EQ : WASM32_MI_F32_EQ;
    case IR_FNE: return is64 ? WASM32_MI_F64_NE : WASM32_MI_F32_NE;
    case IR_FLT: return is64 ? WASM32_MI_F64_LT : WASM32_MI_F32_LT;
    case IR_FLE: return is64 ? WASM32_MI_F64_LE : WASM32_MI_F32_LE;
    default: return WASM32_MI_INVALID;
  }
}

int wasm32_machine_select_binary(
    ir_op_t source_op, ir_type_t operand_type,
    wasm32_machine_binary_t *selected) {
  if (!selected) return 0;
  operand_type = wasm32_machine_value_type(operand_type);
  wasm32_machine_opcode_t opcode = WASM32_MI_INVALID;
  if (operand_type == IR_TY_I32 || operand_type == IR_TY_I64)
    opcode = select_integer(source_op, operand_type == IR_TY_I64);
  else if (operand_type == IR_TY_F32 || operand_type == IR_TY_F64)
    opcode = select_float(source_op, operand_type == IR_TY_F64);
  if (opcode == WASM32_MI_INVALID) return 0;
  selected->opcode = opcode;
  selected->operand_type = operand_type;
  selected->result_type =
      (source_op == IR_EQ || source_op == IR_NE ||
       source_op == IR_LT || source_op == IR_LE ||
       source_op == IR_ULT || source_op == IR_ULE ||
       source_op == IR_FEQ || source_op == IR_FNE ||
       source_op == IR_FLT || source_op == IR_FLE)
          ? IR_TY_I32
          : operand_type;
  selected->is_comparison =
      wasm32_machine_opcode_is_comparison(opcode) ? 1 : 0;
  selected->is_unsigned =
      wasm32_machine_opcode_is_unsigned(opcode) ? 1 : 0;
  selected->is_shift =
      wasm32_machine_opcode_is_shift(opcode) ? 1 : 0;
  selected->guard_zero_divisor =
      wasm32_machine_opcode_is_remainder(opcode) ? 1 : 0;
  if (selected->guard_zero_divisor &&
      !wasm32_machine_select_zero_test(
          operand_type, &selected->zero_test))
    return 0;
  selected->tracks_address =
      ((wasm32_machine_opcode_is_add(opcode) ||
        wasm32_machine_opcode_is_subtract(opcode)) &&
       !selected->is_comparison)
          ? 1
          : 0;
  selected->subtracts_address =
      wasm32_machine_opcode_is_subtract(opcode) ? 1 : 0;
  return 1;
}

int wasm32_machine_select_conversion(
    ir_type_t source_type, ir_type_t result_type, int is_unsigned,
    wasm32_machine_conversion_t *selected) {
  if (!selected) return 0;
  source_type = wasm32_machine_value_type(source_type);
  result_type = wasm32_machine_value_type(result_type);
  wasm32_machine_opcode_t opcode = WASM32_MI_INVALID;
  if (source_type == result_type) {
    opcode = WASM32_MI_COPY;
  } else if (source_type == IR_TY_I64 && result_type == IR_TY_I32) {
    opcode = WASM32_MI_I32_WRAP_I64;
  } else if (source_type == IR_TY_I32 && result_type == IR_TY_I64) {
    opcode = is_unsigned ? WASM32_MI_I64_EXTEND_I32_U
                         : WASM32_MI_I64_EXTEND_I32_S;
  } else if (source_type == IR_TY_I32 && result_type == IR_TY_F32) {
    opcode = is_unsigned ? WASM32_MI_F32_CONVERT_I32_U
                         : WASM32_MI_F32_CONVERT_I32_S;
  } else if (source_type == IR_TY_I64 && result_type == IR_TY_F32) {
    opcode = is_unsigned ? WASM32_MI_F32_CONVERT_I64_U
                         : WASM32_MI_F32_CONVERT_I64_S;
  } else if (source_type == IR_TY_I32 && result_type == IR_TY_F64) {
    opcode = is_unsigned ? WASM32_MI_F64_CONVERT_I32_U
                         : WASM32_MI_F64_CONVERT_I32_S;
  } else if (source_type == IR_TY_I64 && result_type == IR_TY_F64) {
    opcode = is_unsigned ? WASM32_MI_F64_CONVERT_I64_U
                         : WASM32_MI_F64_CONVERT_I64_S;
  } else if (source_type == IR_TY_F32 && result_type == IR_TY_I32) {
    opcode = is_unsigned ? WASM32_MI_I32_TRUNC_F32_U
                         : WASM32_MI_I32_TRUNC_F32_S;
  } else if (source_type == IR_TY_F64 && result_type == IR_TY_I32) {
    opcode = is_unsigned ? WASM32_MI_I32_TRUNC_F64_U
                         : WASM32_MI_I32_TRUNC_F64_S;
  } else if (source_type == IR_TY_F32 && result_type == IR_TY_I64) {
    opcode = is_unsigned ? WASM32_MI_I64_TRUNC_F32_U
                         : WASM32_MI_I64_TRUNC_F32_S;
  } else if (source_type == IR_TY_F64 && result_type == IR_TY_I64) {
    opcode = is_unsigned ? WASM32_MI_I64_TRUNC_F64_U
                         : WASM32_MI_I64_TRUNC_F64_S;
  } else if (source_type == IR_TY_F64 && result_type == IR_TY_F32) {
    opcode = WASM32_MI_F32_DEMOTE_F64;
  } else if (source_type == IR_TY_F32 && result_type == IR_TY_F64) {
    opcode = WASM32_MI_F64_PROMOTE_F32;
  }
  if (opcode == WASM32_MI_INVALID) return 0;
  selected->opcode = opcode;
  selected->source_type = source_type;
  selected->result_type = result_type;
  return 1;
}

int wasm32_machine_select_unary(
    ir_op_t source_op, ir_type_t operand_type,
    wasm32_machine_unary_t *selected) {
  if (!selected) return 0;
  operand_type = wasm32_machine_value_type(operand_type);
  wasm32_machine_opcode_t opcode = WASM32_MI_INVALID;
  wasm32_machine_unary_form_t form = WASM32_MI_UNARY_DIRECT;
  if (source_op == IR_NEG &&
      (operand_type == IR_TY_I32 || operand_type == IR_TY_I64)) {
    opcode = operand_type == IR_TY_I64
                 ? WASM32_MI_I64_SUB
                 : WASM32_MI_I32_SUB;
    form = WASM32_MI_UNARY_ZERO_THEN_OPERAND;
  } else if (source_op == IR_NOT &&
             (operand_type == IR_TY_I32 || operand_type == IR_TY_I64)) {
    opcode = operand_type == IR_TY_I64
                 ? WASM32_MI_I64_XOR
                 : WASM32_MI_I32_XOR;
    form = WASM32_MI_UNARY_OPERAND_THEN_NEG_ONE;
  } else if ((source_op == IR_FNEG || source_op == IR_NEG) &&
             (operand_type == IR_TY_F32 || operand_type == IR_TY_F64)) {
    opcode = operand_type == IR_TY_F64
                 ? WASM32_MI_F64_NEG
                 : WASM32_MI_F32_NEG;
  }
  if (opcode == WASM32_MI_INVALID) return 0;
  *selected = (wasm32_machine_unary_t){
      .opcode = opcode,
      .operand_type = operand_type,
      .result_type = operand_type,
      .form = form,
  };
  return 1;
}

int wasm32_machine_select_zero_test(
    ir_type_t operand_type,
    wasm32_machine_zero_test_t *selected) {
  if (!selected) return 0;
  operand_type = wasm32_machine_value_type(operand_type);
  if (operand_type != IR_TY_I32 && operand_type != IR_TY_I64)
    return 0;
  *selected = (wasm32_machine_zero_test_t){
      .opcode = operand_type == IR_TY_I64
                    ? WASM32_MI_I64_EQZ
                    : WASM32_MI_I32_EQZ,
      .operand_type = operand_type,
  };
  return 1;
}

int wasm32_machine_select_atomic_rmw(
    ir_atomic_rmw_op_t source_op, ir_type_t operand_type,
    wasm32_machine_binary_t *selected) {
  ir_op_t binary_op;
  switch (source_op) {
    case IR_ARMW_ADD: binary_op = IR_ADD; break;
    case IR_ARMW_SUB: binary_op = IR_SUB; break;
    case IR_ARMW_OR: binary_op = IR_OR; break;
    case IR_ARMW_AND: binary_op = IR_AND; break;
    case IR_ARMW_XOR: binary_op = IR_XOR; break;
    default: return 0;
  }
  return wasm32_machine_select_binary(binary_op, operand_type, selected);
}

int wasm32_machine_select_load(
    ir_type_t memory_type, int is_unsigned,
    wasm32_machine_memory_t *selected) {
  if (!selected) return 0;
  wasm32_machine_opcode_t opcode;
  ir_type_t value_type;
  unsigned alignment_log2;
  switch (memory_type) {
    case IR_TY_I8:
      opcode = is_unsigned ? WASM32_MI_I32_LOAD8_U
                           : WASM32_MI_I32_LOAD8_S;
      value_type = IR_TY_I32;
      alignment_log2 = 0;
      break;
    case IR_TY_I16:
      opcode = is_unsigned ? WASM32_MI_I32_LOAD16_U
                           : WASM32_MI_I32_LOAD16_S;
      value_type = IR_TY_I32;
      alignment_log2 = 1;
      break;
    case IR_TY_I32:
    case IR_TY_PTR:
      opcode = WASM32_MI_I32_LOAD;
      memory_type = IR_TY_I32;
      value_type = IR_TY_I32;
      alignment_log2 = 2;
      break;
    case IR_TY_I64:
      opcode = WASM32_MI_I64_LOAD;
      value_type = IR_TY_I64;
      alignment_log2 = 3;
      break;
    case IR_TY_F32:
      opcode = WASM32_MI_F32_LOAD;
      value_type = IR_TY_F32;
      alignment_log2 = 2;
      break;
    case IR_TY_F64:
      opcode = WASM32_MI_F64_LOAD;
      value_type = IR_TY_F64;
      alignment_log2 = 3;
      break;
    default:
      return 0;
  }
  *selected = (wasm32_machine_memory_t){
      .opcode = opcode,
      .memory_type = memory_type,
      .value_type = value_type,
      .alignment_log2 = alignment_log2,
  };
  return 1;
}

int wasm32_machine_select_store(
    ir_type_t memory_type, wasm32_machine_memory_t *selected) {
  if (!selected) return 0;
  wasm32_machine_opcode_t opcode;
  ir_type_t value_type;
  unsigned alignment_log2;
  switch (memory_type) {
    case IR_TY_I8:
      opcode = WASM32_MI_I32_STORE8;
      value_type = IR_TY_I32;
      alignment_log2 = 0;
      break;
    case IR_TY_I16:
      opcode = WASM32_MI_I32_STORE16;
      value_type = IR_TY_I32;
      alignment_log2 = 1;
      break;
    case IR_TY_I32:
    case IR_TY_PTR:
      opcode = WASM32_MI_I32_STORE;
      memory_type = IR_TY_I32;
      value_type = IR_TY_I32;
      alignment_log2 = 2;
      break;
    case IR_TY_I64:
      opcode = WASM32_MI_I64_STORE;
      value_type = IR_TY_I64;
      alignment_log2 = 3;
      break;
    case IR_TY_F32:
      opcode = WASM32_MI_F32_STORE;
      value_type = IR_TY_F32;
      alignment_log2 = 2;
      break;
    case IR_TY_F64:
      opcode = WASM32_MI_F64_STORE;
      value_type = IR_TY_F64;
      alignment_log2 = 3;
      break;
    default:
      return 0;
  }
  *selected = (wasm32_machine_memory_t){
      .opcode = opcode,
      .memory_type = memory_type,
      .value_type = value_type,
      .alignment_log2 = alignment_log2,
  };
  return 1;
}

int wasm32_machine_copy_plan_build(
    int size, wasm32_machine_copy_plan_t *plan) {
  static const struct {
    int width;
    ir_type_t type;
  } copy_kinds[] = {
      {8, IR_TY_I64},
      {4, IR_TY_I32},
      {2, IR_TY_I16},
      {1, IR_TY_I8},
  };
  if (!plan || size < 0) return 0;
  memset(plan, 0, sizeof(*plan));
  int count = 0;
  int offset = 0;
  for (size_t i = 0; i < sizeof(copy_kinds) / sizeof(copy_kinds[0]); i++) {
    while (offset + copy_kinds[i].width <= size) {
      count++;
      offset += copy_kinds[i].width;
    }
  }
  if (count == 0) return 1;
  plan->chunks = calloc((size_t)count, sizeof(*plan->chunks));
  if (!plan->chunks) return 0;
  plan->chunk_count = count;
  int output = 0;
  offset = 0;
  for (size_t i = 0; i < sizeof(copy_kinds) / sizeof(copy_kinds[0]); i++) {
    while (offset + copy_kinds[i].width <= size) {
      wasm32_machine_copy_chunk_t *chunk = &plan->chunks[output++];
      chunk->offset = offset;
      if (!wasm32_machine_select_load(
              copy_kinds[i].type, 1, &chunk->load) ||
          !wasm32_machine_select_store(
              copy_kinds[i].type, &chunk->store)) {
        wasm32_machine_copy_plan_dispose(plan);
        return 0;
      }
      offset += copy_kinds[i].width;
    }
  }
  return 1;
}

void wasm32_machine_copy_plan_dispose(
    wasm32_machine_copy_plan_t *plan) {
  if (!plan) return;
  free(plan->chunks);
  memset(plan, 0, sizeof(*plan));
}

int wasm32_machine_alignment_plan_build(
    int requested_alignment, int default_alignment,
    wasm32_machine_alignment_t *plan) {
  if (plan) memset(plan, 0, sizeof(*plan));
  if (!plan || default_alignment <= 0) return 0;
  int alignment = requested_alignment > 0
                      ? requested_alignment
                      : default_alignment;
  if ((alignment & (alignment - 1)) != 0) return 0;
  plan->alignment = alignment;
  plan->addend = alignment - 1;
  plan->mask = -alignment;
  return 1;
}

int wasm32_machine_stack_plan_build(
    int fixed_frame_size, int has_dynamic_allocation,
    int has_variadic_call_area, int has_persistent_frame,
    wasm32_machine_stack_plan_t *plan) {
  if (plan) memset(plan, 0, sizeof(*plan));
  if (!plan || fixed_frame_size < 0) return 0;
  plan->fixed_frame_size = fixed_frame_size;
  plan->has_dynamic_allocation = has_dynamic_allocation ? 1 : 0;
  plan->has_variadic_call_area = has_variadic_call_area ? 1 : 0;
  plan->has_persistent_frame = has_persistent_frame ? 1 : 0;
  int transient_stack = fixed_frame_size > 0 ||
                        plan->has_dynamic_allocation ||
                        plan->has_variadic_call_area;
  plan->saves_stack_pointer =
      transient_stack && !plan->has_persistent_frame ? 1 : 0;
  plan->restores_stack_pointer = plan->saves_stack_pointer;
  plan->uses_stack_pointer =
      plan->saves_stack_pointer || plan->has_dynamic_allocation ||
      plan->has_variadic_call_area;
  return 1;
}

int wasm32_machine_primitive_plan_build(
    wasm32_machine_primitive_plan_t *plan) {
  if (!plan) return 0;
  memset(plan, 0, sizeof(*plan));
  for (int source = IR_TY_VOID; source <= IR_TY_PTR; source++) {
    for (int result = IR_TY_VOID; result <= IR_TY_PTR; result++) {
      for (int is_unsigned = 0; is_unsigned <= 1; is_unsigned++) {
        plan->conversion_valid[source][result][is_unsigned] =
            wasm32_machine_select_conversion(
                (ir_type_t)source, (ir_type_t)result, is_unsigned,
                &plan->conversions[source][result][is_unsigned])
                ? 1
                : 0;
      }
    }
    for (int is_unsigned = 0; is_unsigned <= 1; is_unsigned++) {
      plan->load_valid[source][is_unsigned] =
          wasm32_machine_select_load(
              (ir_type_t)source, is_unsigned,
              &plan->loads[source][is_unsigned])
              ? 1
              : 0;
    }
    plan->store_valid[source] =
        wasm32_machine_select_store(
            (ir_type_t)source, &plan->stores[source])
            ? 1
            : 0;
  }
  return wasm32_machine_select_binary(
             IR_ADD, IR_TY_I32, &plan->i32_add) &&
         wasm32_machine_select_binary(
             IR_SUB, IR_TY_I32, &plan->i32_subtract) &&
         wasm32_machine_select_binary(
             IR_AND, IR_TY_I32, &plan->i32_and) &&
         wasm32_machine_select_binary(
             IR_EQ, IR_TY_I32, &plan->i32_equal) &&
         wasm32_machine_select_binary(
             IR_NE, IR_TY_I32, &plan->i32_not_equal) &&
         wasm32_machine_select_zero_test(
             IR_TY_I32, &plan->i32_zero_test) &&
         wasm32_machine_select_zero_test(
             IR_TY_I64, &plan->i64_zero_test);
}

static int valid_plan_type(ir_type_t type) {
  return type >= IR_TY_VOID && type <= IR_TY_PTR;
}

const wasm32_machine_conversion_t *wasm32_machine_planned_conversion(
    const wasm32_machine_primitive_plan_t *plan,
    ir_type_t source_type, ir_type_t result_type, int is_unsigned) {
  source_type = wasm32_machine_value_type(source_type);
  result_type = wasm32_machine_value_type(result_type);
  is_unsigned = is_unsigned ? 1 : 0;
  if (!plan || !valid_plan_type(source_type) ||
      !valid_plan_type(result_type) ||
      !plan->conversion_valid[source_type][result_type][is_unsigned])
    return NULL;
  return &plan->conversions[source_type][result_type][is_unsigned];
}

const wasm32_machine_memory_t *wasm32_machine_planned_load(
    const wasm32_machine_primitive_plan_t *plan,
    ir_type_t memory_type, int is_unsigned) {
  is_unsigned = is_unsigned ? 1 : 0;
  if (!plan || !valid_plan_type(memory_type) ||
      !plan->load_valid[memory_type][is_unsigned])
    return NULL;
  return &plan->loads[memory_type][is_unsigned];
}

const wasm32_machine_memory_t *wasm32_machine_planned_store(
    const wasm32_machine_primitive_plan_t *plan,
    ir_type_t memory_type) {
  if (!plan || !valid_plan_type(memory_type) ||
      !plan->store_valid[memory_type])
    return NULL;
  return &plan->stores[memory_type];
}

const char *wasm32_machine_opcode_wat(wasm32_machine_opcode_t opcode) {
  if (opcode <= WASM32_MI_INVALID || opcode >= WASM32_MI_COUNT)
    return NULL;
  return opcode_encodings[opcode].wat;
}

unsigned wasm32_machine_opcode_binary(wasm32_machine_opcode_t opcode) {
  if (opcode <= WASM32_MI_INVALID || opcode >= WASM32_MI_COUNT)
    return 0;
  return opcode_encodings[opcode].binary;
}

int wasm32_machine_opcode_is_comparison(wasm32_machine_opcode_t opcode) {
  switch (opcode) {
    case WASM32_MI_I32_EQ: case WASM32_MI_I64_EQ:
    case WASM32_MI_I32_NE: case WASM32_MI_I64_NE:
    case WASM32_MI_I32_LT_S: case WASM32_MI_I64_LT_S:
    case WASM32_MI_I32_LT_U: case WASM32_MI_I64_LT_U:
    case WASM32_MI_I32_LE_S: case WASM32_MI_I64_LE_S:
    case WASM32_MI_I32_LE_U: case WASM32_MI_I64_LE_U:
    case WASM32_MI_F32_EQ: case WASM32_MI_F64_EQ:
    case WASM32_MI_F32_NE: case WASM32_MI_F64_NE:
    case WASM32_MI_F32_LT: case WASM32_MI_F64_LT:
    case WASM32_MI_F32_LE: case WASM32_MI_F64_LE:
      return 1;
    default:
      return 0;
  }
}

int wasm32_machine_opcode_is_unsigned(wasm32_machine_opcode_t opcode) {
  switch (opcode) {
    case WASM32_MI_I32_DIV_U: case WASM32_MI_I64_DIV_U:
    case WASM32_MI_I32_REM_U: case WASM32_MI_I64_REM_U:
    case WASM32_MI_I32_SHR_U: case WASM32_MI_I64_SHR_U:
    case WASM32_MI_I32_LT_U: case WASM32_MI_I64_LT_U:
    case WASM32_MI_I32_LE_U: case WASM32_MI_I64_LE_U:
      return 1;
    default:
      return 0;
  }
}

int wasm32_machine_opcode_is_shift(wasm32_machine_opcode_t opcode) {
  switch (opcode) {
    case WASM32_MI_I32_SHL: case WASM32_MI_I64_SHL:
    case WASM32_MI_I32_SHR_S: case WASM32_MI_I64_SHR_S:
    case WASM32_MI_I32_SHR_U: case WASM32_MI_I64_SHR_U:
      return 1;
    default:
      return 0;
  }
}

int wasm32_machine_opcode_is_remainder(wasm32_machine_opcode_t opcode) {
  return opcode == WASM32_MI_I32_REM_S ||
         opcode == WASM32_MI_I64_REM_S ||
         opcode == WASM32_MI_I32_REM_U ||
         opcode == WASM32_MI_I64_REM_U;
}

int wasm32_machine_opcode_is_add(wasm32_machine_opcode_t opcode) {
  return opcode == WASM32_MI_I32_ADD ||
         opcode == WASM32_MI_I64_ADD;
}

int wasm32_machine_opcode_is_subtract(wasm32_machine_opcode_t opcode) {
  return opcode == WASM32_MI_I32_SUB ||
         opcode == WASM32_MI_I64_SUB;
}
