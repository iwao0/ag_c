#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../src/arch/wasm32/wasm32_machine_abi.h"
#include "../src/arch/wasm32/wasm32_machine_function.h"
#include "../src/arch/wasm32/wasm32_machine_ir.h"

static const ir_func_t *fixture_function;
static const ir_abi_signature_t *fixture_function_abi;

const ir_abi_signature_t *ir_abi_function_signature(
    const ir_abi_module_t *module, const ir_func_t *function) {
  (void)module;
  return function == fixture_function ? fixture_function_abi : NULL;
}

const ir_abi_signature_t *ir_abi_call_signature(
    const ir_abi_module_t *module, const ir_inst_t *call) {
  (void)module;
  (void)call;
  return NULL;
}

const ir_abi_argument_t *ir_abi_call_arguments(
    const ir_abi_module_t *module, const ir_inst_t *call,
    size_t *argument_count) {
  (void)module;
  (void)call;
  if (argument_count) *argument_count = 0;
  return NULL;
}

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

typedef struct {
  ir_type_t memory_type;
  int is_unsigned;
  ir_type_t selected_memory_type;
  ir_type_t value_type;
  unsigned alignment_log2;
  const char *wat;
  unsigned binary;
} load_case_t;

static const load_case_t load_cases[] = {
    {IR_TY_I8, 0, IR_TY_I8, IR_TY_I32, 0, "i32.load8_s", 0x2c},
    {IR_TY_I8, 1, IR_TY_I8, IR_TY_I32, 0, "i32.load8_u", 0x2d},
    {IR_TY_I16, 0, IR_TY_I16, IR_TY_I32, 1, "i32.load16_s", 0x2e},
    {IR_TY_I16, 1, IR_TY_I16, IR_TY_I32, 1, "i32.load16_u", 0x2f},
    {IR_TY_I32, 0, IR_TY_I32, IR_TY_I32, 2, "i32.load", 0x28},
    {IR_TY_PTR, 0, IR_TY_I32, IR_TY_I32, 2, "i32.load", 0x28},
    {IR_TY_I64, 0, IR_TY_I64, IR_TY_I64, 3, "i64.load", 0x29},
    {IR_TY_F32, 0, IR_TY_F32, IR_TY_F32, 2, "f32.load", 0x2a},
    {IR_TY_F64, 0, IR_TY_F64, IR_TY_F64, 3, "f64.load", 0x2b},
};

typedef struct {
  ir_type_t memory_type;
  ir_type_t selected_memory_type;
  ir_type_t value_type;
  unsigned alignment_log2;
  const char *wat;
  unsigned binary;
} store_case_t;

static const store_case_t store_cases[] = {
    {IR_TY_I8, IR_TY_I8, IR_TY_I32, 0, "i32.store8", 0x3a},
    {IR_TY_I16, IR_TY_I16, IR_TY_I32, 1, "i32.store16", 0x3b},
    {IR_TY_I32, IR_TY_I32, IR_TY_I32, 2, "i32.store", 0x36},
    {IR_TY_PTR, IR_TY_I32, IR_TY_I32, 2, "i32.store", 0x36},
    {IR_TY_I64, IR_TY_I64, IR_TY_I64, 3, "i64.store", 0x37},
    {IR_TY_F32, IR_TY_F32, IR_TY_F32, 2, "f32.store", 0x38},
    {IR_TY_F64, IR_TY_F64, IR_TY_F64, 3, "f64.store", 0x39},
};

typedef struct {
  ir_op_t source_op;
  ir_type_t operand_type;
  wasm32_machine_unary_form_t form;
  const char *wat;
  unsigned binary;
} unary_case_t;

static const unary_case_t unary_cases[] = {
    {IR_NEG, IR_TY_I32, WASM32_MI_UNARY_ZERO_THEN_OPERAND,
     "i32.sub", 0x6b},
    {IR_NEG, IR_TY_I64, WASM32_MI_UNARY_ZERO_THEN_OPERAND,
     "i64.sub", 0x7d},
    {IR_NOT, IR_TY_I32, WASM32_MI_UNARY_OPERAND_THEN_NEG_ONE,
     "i32.xor", 0x73},
    {IR_NOT, IR_TY_I64, WASM32_MI_UNARY_OPERAND_THEN_NEG_ONE,
     "i64.xor", 0x85},
    {IR_FNEG, IR_TY_F32, WASM32_MI_UNARY_DIRECT,
     "f32.neg", 0x8c},
    {IR_FNEG, IR_TY_F64, WASM32_MI_UNARY_DIRECT,
     "f64.neg", 0x9a},
    {IR_NEG, IR_TY_F32, WASM32_MI_UNARY_DIRECT,
     "f32.neg", 0x8c},
    {IR_NEG, IR_TY_F64, WASM32_MI_UNARY_DIRECT,
     "f64.neg", 0x9a},
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
  for (size_t i = 0; i < sizeof(load_cases) / sizeof(load_cases[0]); i++) {
    const load_case_t *test = &load_cases[i];
    wasm32_machine_memory_t selected;
    if (!wasm32_machine_select_load(
            test->memory_type, test->is_unsigned, &selected) ||
        selected.memory_type != test->selected_memory_type ||
        selected.value_type != test->value_type ||
        selected.alignment_log2 != test->alignment_log2 ||
        strcmp(wasm32_machine_opcode_wat(selected.opcode), test->wat) != 0 ||
        wasm32_machine_opcode_binary(selected.opcode) != test->binary) {
      fprintf(stderr, "FAIL: machine load case %zu (%s)\n", i, test->wat);
      return 1;
    }
  }
  for (size_t i = 0; i < sizeof(store_cases) / sizeof(store_cases[0]); i++) {
    const store_case_t *test = &store_cases[i];
    wasm32_machine_memory_t selected;
    if (!wasm32_machine_select_store(test->memory_type, &selected) ||
        selected.memory_type != test->selected_memory_type ||
        selected.value_type != test->value_type ||
        selected.alignment_log2 != test->alignment_log2 ||
        strcmp(wasm32_machine_opcode_wat(selected.opcode), test->wat) != 0 ||
        wasm32_machine_opcode_binary(selected.opcode) != test->binary) {
      fprintf(stderr, "FAIL: machine store case %zu (%s)\n", i, test->wat);
      return 1;
    }
  }
  wasm32_machine_memory_t invalid_memory;
  if (wasm32_machine_select_load(
          IR_TY_VOID, 0, &invalid_memory) ||
      wasm32_machine_select_store(IR_TY_VOID, &invalid_memory)) {
    fprintf(stderr, "FAIL: machine memory invalid-type boundary\n");
    return 1;
  }
  for (size_t i = 0; i < sizeof(unary_cases) / sizeof(unary_cases[0]); i++) {
    const unary_case_t *test = &unary_cases[i];
    wasm32_machine_unary_t selected;
    if (!wasm32_machine_select_unary(
            test->source_op, test->operand_type, &selected) ||
        selected.operand_type != test->operand_type ||
        selected.result_type != test->operand_type ||
        selected.form != test->form ||
        strcmp(wasm32_machine_opcode_wat(selected.opcode), test->wat) != 0 ||
        wasm32_machine_opcode_binary(selected.opcode) != test->binary) {
      fprintf(stderr, "FAIL: machine unary case %zu (%s)\n", i, test->wat);
      return 1;
    }
  }
  wasm32_machine_unary_t invalid_unary;
  if (wasm32_machine_select_unary(
          IR_NOT, IR_TY_F32, &invalid_unary) ||
      wasm32_machine_select_unary(
          IR_FNEG, IR_TY_I32, &invalid_unary)) {
    fprintf(stderr, "FAIL: machine unary invalid-type boundary\n");
    return 1;
  }
  const ir_atomic_rmw_op_t atomic_ops[] = {
      IR_ARMW_ADD, IR_ARMW_SUB, IR_ARMW_OR, IR_ARMW_AND, IR_ARMW_XOR,
  };
  const ir_op_t expected_binary_ops[] = {
      IR_ADD, IR_SUB, IR_OR, IR_AND, IR_XOR,
  };
  for (size_t i = 0; i < sizeof(atomic_ops) / sizeof(atomic_ops[0]); i++) {
    for (size_t type_index = 0; type_index < 2; type_index++) {
      ir_type_t type = type_index == 0 ? IR_TY_I32 : IR_TY_I64;
      wasm32_machine_binary_t atomic_selected;
      wasm32_machine_binary_t binary_selected;
      if (!wasm32_machine_select_atomic_rmw(
              atomic_ops[i], type, &atomic_selected) ||
          !wasm32_machine_select_binary(
              expected_binary_ops[i], type, &binary_selected) ||
          atomic_selected.opcode != binary_selected.opcode) {
        fprintf(stderr, "FAIL: machine atomic RMW case %zu/%zu\n",
                i, type_index);
        return 1;
      }
    }
  }
  wasm32_machine_binary_t invalid_atomic;
  if (wasm32_machine_select_atomic_rmw(
          IR_ARMW_XCHG, IR_TY_I32, &invalid_atomic)) {
    fprintf(stderr, "FAIL: machine atomic RMW exchange boundary\n");
    return 1;
  }
  ir_abi_piece_t function_params[] = {
      {.type = IR_TY_I8, .kind = IR_ABI_PIECE_DIRECT},
      {.type = IR_TY_PTR, .kind = IR_ABI_PIECE_DIRECT},
      {.type = IR_TY_F64, .kind = IR_ABI_PIECE_DIRECT},
  };
  ir_abi_piece_t direct_result = {
      .type = IR_TY_I16,
      .kind = IR_ABI_PIECE_DIRECT,
  };
  ir_abi_signature_t function_abi = {
      .result_pieces = &direct_result,
      .result_count = 1,
      .param_pieces = function_params,
      .param_count = 3,
      .fixed_param_count = 3,
  };
  wasm32_machine_signature_t function_signature;
  if (!wasm32_machine_signature_from_abi(
          &function_abi, 1, &function_signature) ||
      function_signature.nparams != 3 ||
      function_signature.params[0] != IR_TY_I32 ||
      function_signature.params[1] != IR_TY_I32 ||
      function_signature.params[2] != IR_TY_F64 ||
      function_signature.result != IR_TY_I32 ||
      function_signature.has_hidden_result ||
      function_signature.has_direct_aggregate_result) {
    fprintf(stderr, "FAIL: machine function ABI signature\n");
    return 1;
  }
  wasm32_machine_signature_dispose(&function_signature);

  ir_abi_piece_t indirect_result = {
      .type = IR_TY_PTR,
      .kind = IR_ABI_PIECE_INDIRECT,
  };
  function_abi.result_pieces = &indirect_result;
  wasm32_machine_signature_t indirect_signature;
  if (!wasm32_machine_signature_from_abi(
          &function_abi, 1, &indirect_signature) ||
      indirect_signature.nparams != 4 ||
      indirect_signature.params[0] != IR_TY_I32 ||
      indirect_signature.result != IR_TY_VOID ||
      !indirect_signature.has_hidden_result ||
      indirect_signature.has_direct_aggregate_result) {
    fprintf(stderr, "FAIL: machine hidden-result ABI signature\n");
    return 1;
  }
  wasm32_machine_signature_dispose(&indirect_signature);

  ir_abi_piece_t call_result = {
      .type = IR_TY_I64,
      .kind = IR_ABI_PIECE_DIRECT_AGGREGATE,
  };
  ir_abi_signature_t call_abi = {
      .result_pieces = &call_result,
      .result_count = 1,
      .param_pieces = function_params,
      .param_count = 3,
      .fixed_param_count = 2,
      .is_variadic = 1,
  };
  ir_inst_t call = {0};
  call.op = IR_CALL;
  call.dst = (ir_val_t){.id = 1, .type = IR_TY_I64};
  wasm32_machine_signature_t call_signature;
  if (!wasm32_machine_call_signature(
          &call, &call_abi, &call_signature) ||
      call_signature.nparams != 2 ||
      call_signature.params[0] != IR_TY_I32 ||
      call_signature.params[1] != IR_TY_I32 ||
      call_signature.result != IR_TY_I64 ||
      call_signature.has_hidden_result ||
      !call_signature.has_direct_aggregate_result) {
    fprintf(stderr, "FAIL: machine variadic call ABI signature\n");
    return 1;
  }
  wasm32_machine_signature_dispose(&call_signature);

  call_abi.result_pieces = &indirect_result;
  call_abi.fixed_param_count = 3;
  wasm32_machine_signature_t hidden_call_signature;
  if (!wasm32_machine_call_signature(
          &call, &call_abi, &hidden_call_signature) ||
      hidden_call_signature.nparams != 4 ||
      hidden_call_signature.params[0] != IR_TY_I32 ||
      hidden_call_signature.result != IR_TY_VOID ||
      !hidden_call_signature.has_hidden_result) {
    fprintf(stderr, "FAIL: machine hidden-result call ABI signature\n");
    return 1;
  }
  wasm32_machine_signature_dispose(&hidden_call_signature);

  call_abi.fixed_param_count = 4;
  if (wasm32_machine_call_signature(
          &call, &call_abi, &hidden_call_signature)) {
    fprintf(stderr, "FAIL: machine invalid call ABI boundary\n");
    return 1;
  }

  ir_inst_t function_instructions[9] = {0};
  function_instructions[0].op = IR_ALLOCA;
  function_instructions[0].dst =
      (ir_val_t){.id = 0, .type = IR_TY_PTR};
  function_instructions[0].alloca_size = 12;
  function_instructions[0].alloca_align = 8;
  function_instructions[1].op = IR_ALLOCA;
  function_instructions[1].dst =
      (ir_val_t){.id = 1, .type = IR_TY_PTR};
  function_instructions[1].alloca_size = 4;
  function_instructions[1].alloca_align = 16;
  function_instructions[2].op = IR_STORE;
  function_instructions[2].src1 =
      (ir_val_t){.id = 0, .type = IR_TY_PTR};
  function_instructions[2].src2 =
      (ir_val_t){.id = IR_VAL_IMM, .type = IR_TY_I64, .imm = 7};
  function_instructions[3].op = IR_LOAD;
  function_instructions[3].dst =
      (ir_val_t){.id = 2, .type = IR_TY_PTR};
  function_instructions[3].src1 =
      (ir_val_t){.id = 0, .type = IR_TY_PTR};
  function_instructions[4].op = IR_LOAD_IMM;
  function_instructions[4].dst =
      (ir_val_t){.id = 3, .type = IR_TY_I32};
  function_instructions[4].src1 =
      (ir_val_t){.id = IR_VAL_IMM, .type = IR_TY_I32,
                 .imm = (long long)INT32_MAX + 1};
  function_instructions[5].op = IR_LOAD_SYM;
  function_instructions[5].dst =
      (ir_val_t){.id = 4, .type = IR_TY_PTR};
  function_instructions[6].op = IR_VLA_ALLOC;
  function_instructions[6].dst =
      (ir_val_t){.id = 5, .type = IR_TY_PTR};
  function_instructions[6].src1 =
      (ir_val_t){.id = 3, .type = IR_TY_I32};
  function_instructions[7].op = IR_ATOMIC;
  function_instructions[7].src1 =
      (ir_val_t){.id = 4, .type = IR_TY_PTR};
  function_instructions[7].src2 =
      (ir_val_t){.id = 1, .type = IR_TY_PTR};
  function_instructions[7].atomic_kind = IR_ATOMIC_CAS;
  function_instructions[7].atomic_width = 8;
  function_instructions[8].op = IR_BR;
  function_instructions[8].label_id = 1;
  for (size_t i = 0; i + 1 < 9; i++)
    function_instructions[i].next = &function_instructions[i + 1];
  ir_block_t function_block = {
      .id = 0,
      .head = &function_instructions[0],
      .tail = &function_instructions[8],
  };
  ir_func_t function = {
      .entry = &function_block,
      .next_vreg_id = 6,
  };
  ir_abi_signature_t empty_function_abi = {0};
  ir_abi_module_t fake_abi_module = {0};
  fixture_function = &function;
  fixture_function_abi = &empty_function_abi;
  wasm32_machine_function_t machine_function;
  if (!wasm32_machine_function_build(
          &function, &fake_abi_module, &machine_function)) {
    fprintf(stderr, "FAIL: machine function plan build\n");
    return 1;
  }
  const wasm32_machine_alloca_t *first_alloca =
      wasm32_machine_function_alloca(&machine_function, 0);
  const wasm32_machine_alloca_t *second_alloca =
      wasm32_machine_function_alloca(&machine_function, 1);
  if (machine_function.frame_size != 32 ||
      machine_function.alloca_count != 2 ||
      !first_alloca || first_alloca->offset != 0 ||
      first_alloca->value_type != IR_TY_I64 ||
      !second_alloca || second_alloca->offset != 16 ||
      wasm32_machine_function_vreg_type(
          &machine_function, function_instructions[3].dst) != IR_TY_I64 ||
      !wasm32_machine_function_vreg_is_unsigned(
          &machine_function, function_instructions[4].dst) ||
      !machine_function.has_vla_alloc ||
      !machine_function.has_control_flow ||
      machine_function.has_atomic_cas32 ||
      !machine_function.has_atomic_cas64) {
    fprintf(stderr, "FAIL: machine function plan invariants\n");
    return 1;
  }
  wasm32_machine_function_dispose(&machine_function);
  puts("wasm32 Machine IR tests passed");
  return 0;
}
