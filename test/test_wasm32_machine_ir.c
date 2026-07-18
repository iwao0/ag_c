#include <stdio.h>
#include <string.h>

#include "../src/arch/wasm32/wasm32_machine_ir.h"

typedef struct {
  ir_op_t source_op;
  ir_type_t operand_type;
  const char *wat;
  unsigned binary;
} binary_case_t;

#define INT_CASE(op, suffix, i32_code, i64_code) \
  {IR_##op, IR_TY_I32, "i32." suffix, i32_code}, \
  {IR_##op, IR_TY_I64, "i64." suffix, i64_code}
#define FP_CASE(op, suffix, f32_code, f64_code) \
  {IR_##op, IR_TY_F32, "f32." suffix, f32_code}, \
  {IR_##op, IR_TY_F64, "f64." suffix, f64_code}

static const binary_case_t binary_cases[] = {
    INT_CASE(ADD, "add", 0x6a, 0x7c),
    INT_CASE(SUB, "sub", 0x6b, 0x7d),
    INT_CASE(MUL, "mul", 0x6c, 0x7e),
    INT_CASE(DIV, "div_s", 0x6d, 0x7f),
    INT_CASE(UDIV, "div_u", 0x6e, 0x80),
    INT_CASE(MOD, "rem_s", 0x6f, 0x81),
    INT_CASE(UMOD, "rem_u", 0x70, 0x82),
    INT_CASE(AND, "and", 0x71, 0x83),
    INT_CASE(OR, "or", 0x72, 0x84),
    INT_CASE(XOR, "xor", 0x73, 0x85),
    INT_CASE(SHL, "shl", 0x74, 0x86),
    INT_CASE(SHR, "shr_s", 0x75, 0x87),
    INT_CASE(LSR, "shr_u", 0x76, 0x88),
    INT_CASE(EQ, "eq", 0x46, 0x51),
    INT_CASE(NE, "ne", 0x47, 0x52),
    INT_CASE(LT, "lt_s", 0x48, 0x53),
    INT_CASE(ULT, "lt_u", 0x49, 0x54),
    INT_CASE(LE, "le_s", 0x4c, 0x57),
    INT_CASE(ULE, "le_u", 0x4d, 0x58),
    FP_CASE(FADD, "add", 0x92, 0xa0),
    FP_CASE(FSUB, "sub", 0x93, 0xa1),
    FP_CASE(FMUL, "mul", 0x94, 0xa2),
    FP_CASE(FDIV, "div", 0x95, 0xa3),
    FP_CASE(FEQ, "eq", 0x5b, 0x61),
    FP_CASE(FNE, "ne", 0x5c, 0x62),
    FP_CASE(FLT, "lt", 0x5d, 0x63),
    FP_CASE(FLE, "le", 0x5f, 0x65),
};

typedef struct {
  ir_type_t source_type;
  ir_type_t result_type;
  int is_unsigned;
  const char *wat;
  unsigned binary;
} conversion_case_t;

static const conversion_case_t conversion_cases[] = {
    {IR_TY_I32, IR_TY_I64, 0, "i64.extend_i32_s", 0xac},
    {IR_TY_I32, IR_TY_I64, 1, "i64.extend_i32_u", 0xad},
    {IR_TY_I64, IR_TY_I32, 0, "i32.wrap_i64", 0xa7},
    {IR_TY_I32, IR_TY_F32, 0, "f32.convert_i32_s", 0xb2},
    {IR_TY_I32, IR_TY_F32, 1, "f32.convert_i32_u", 0xb3},
    {IR_TY_I64, IR_TY_F32, 0, "f32.convert_i64_s", 0xb4},
    {IR_TY_I64, IR_TY_F32, 1, "f32.convert_i64_u", 0xb5},
    {IR_TY_I32, IR_TY_F64, 0, "f64.convert_i32_s", 0xb7},
    {IR_TY_I32, IR_TY_F64, 1, "f64.convert_i32_u", 0xb8},
    {IR_TY_I64, IR_TY_F64, 0, "f64.convert_i64_s", 0xb9},
    {IR_TY_I64, IR_TY_F64, 1, "f64.convert_i64_u", 0xba},
    {IR_TY_F32, IR_TY_I32, 0, "i32.trunc_f32_s", 0xa8},
    {IR_TY_F32, IR_TY_I32, 1, "i32.trunc_f32_u", 0xa9},
    {IR_TY_F64, IR_TY_I32, 0, "i32.trunc_f64_s", 0xaa},
    {IR_TY_F64, IR_TY_I32, 1, "i32.trunc_f64_u", 0xab},
    {IR_TY_F32, IR_TY_I64, 0, "i64.trunc_f32_s", 0xae},
    {IR_TY_F32, IR_TY_I64, 1, "i64.trunc_f32_u", 0xaf},
    {IR_TY_F64, IR_TY_I64, 0, "i64.trunc_f64_s", 0xb0},
    {IR_TY_F64, IR_TY_I64, 1, "i64.trunc_f64_u", 0xb1},
    {IR_TY_F64, IR_TY_F32, 0, "f32.demote_f64", 0xb6},
    {IR_TY_F32, IR_TY_F64, 0, "f64.promote_f32", 0xbb},
};

int main(void) {
  for (size_t i = 0; i < sizeof(binary_cases) / sizeof(binary_cases[0]); i++) {
    const binary_case_t *test = &binary_cases[i];
    wasm32_machine_binary_t selected;
    int is_comparison =
        test->source_op == IR_EQ || test->source_op == IR_NE ||
        test->source_op == IR_LT || test->source_op == IR_ULT ||
        test->source_op == IR_LE || test->source_op == IR_ULE ||
        test->source_op == IR_FEQ || test->source_op == IR_FNE ||
        test->source_op == IR_FLT || test->source_op == IR_FLE;
    if (!wasm32_machine_select_binary(
            test->source_op, test->operand_type, &selected) ||
        strcmp(wasm32_machine_opcode_wat(selected.opcode), test->wat) != 0 ||
        wasm32_machine_opcode_binary(selected.opcode) != test->binary ||
        selected.result_type !=
            (is_comparison ? IR_TY_I32 : test->operand_type)) {
      fprintf(stderr, "FAIL: machine binary case %zu (%s)\n", i, test->wat);
      return 1;
    }
  }
  wasm32_machine_binary_t normalized;
  if (!wasm32_machine_select_binary(IR_ADD, IR_TY_I8, &normalized) ||
      normalized.operand_type != IR_TY_I32 ||
      strcmp(wasm32_machine_opcode_wat(normalized.opcode), "i32.add") != 0 ||
      wasm32_machine_select_binary(IR_CALL, IR_TY_I32, &normalized)) {
    fprintf(stderr, "FAIL: machine binary normalization boundary\n");
    return 1;
  }
  for (size_t i = 0;
       i < sizeof(conversion_cases) / sizeof(conversion_cases[0]); i++) {
    const conversion_case_t *test = &conversion_cases[i];
    wasm32_machine_conversion_t selected;
    if (!wasm32_machine_select_conversion(
            test->source_type, test->result_type,
            test->is_unsigned, &selected) ||
        strcmp(wasm32_machine_opcode_wat(selected.opcode), test->wat) != 0 ||
        wasm32_machine_opcode_binary(selected.opcode) != test->binary ||
        selected.source_type != test->source_type ||
        selected.result_type != test->result_type) {
      fprintf(stderr, "FAIL: machine conversion case %zu (%s)\n",
              i, test->wat);
      return 1;
    }
  }
  wasm32_machine_conversion_t copy;
  if (!wasm32_machine_select_conversion(
          IR_TY_I16, IR_TY_I32, 0, &copy) ||
      copy.opcode != WASM32_MI_COPY ||
      copy.source_type != IR_TY_I32 || copy.result_type != IR_TY_I32 ||
      wasm32_machine_opcode_wat(copy.opcode) != NULL ||
      wasm32_machine_opcode_binary(copy.opcode) != 0 ||
      wasm32_machine_select_conversion(
          IR_TY_F32, IR_TY_VOID, 0, &copy)) {
    fprintf(stderr, "FAIL: machine conversion normalization boundary\n");
    return 1;
  }
  puts("wasm32 Machine IR tests passed");
  return 0;
}
