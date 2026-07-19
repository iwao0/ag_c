#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../src/arch/wasm32/wasm32_machine_abi.h"
#include "../src/arch/wasm32/wasm32_machine_function.h"
#include "../src/arch/wasm32/wasm32_machine_ir.h"
#include "../src/arch/wasm32/wasm32_machine_module.h"

static const ir_func_t *fixture_function;
static const ir_abi_signature_t *fixture_function_abi;
static const ir_inst_t *fixture_call;
static const ir_abi_signature_t *fixture_call_abi;
static const ir_abi_argument_t *fixture_call_arguments;
static size_t fixture_call_argument_count;
static const ir_inst_t *fixture_reference;
static const ir_abi_signature_t *fixture_reference_abi;

const ir_abi_signature_t *ir_abi_function_signature(
    const ir_abi_module_t *module, const ir_func_t *function) {
  (void)module;
  return function == fixture_function ? fixture_function_abi : NULL;
}

const ir_abi_signature_t *ir_abi_call_signature(
    const ir_abi_module_t *module, const ir_inst_t *call) {
  (void)module;
  return call == fixture_call ? fixture_call_abi : NULL;
}

const ir_abi_argument_t *ir_abi_call_arguments(
    const ir_abi_module_t *module, const ir_inst_t *call,
    size_t *argument_count) {
  (void)module;
  if (call != fixture_call) {
    if (argument_count) *argument_count = 0;
    return NULL;
  }
  if (argument_count) *argument_count = fixture_call_argument_count;
  return fixture_call_arguments;
}

const ir_abi_signature_t *ir_abi_reference_signature(
    const ir_abi_module_t *module, const ir_inst_t *reference) {
  (void)module;
  return reference == fixture_reference ? fixture_reference_abi : NULL;
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
  wasm32_machine_copy_plan_t copy_plan;
  if (!wasm32_machine_copy_plan_build(15, &copy_plan) ||
      copy_plan.chunk_count != 4 ||
      copy_plan.chunks[0].offset != 0 ||
      copy_plan.chunks[0].load.opcode != WASM32_MI_I64_LOAD ||
      copy_plan.chunks[0].store.opcode != WASM32_MI_I64_STORE ||
      copy_plan.chunks[1].offset != 8 ||
      copy_plan.chunks[1].load.opcode != WASM32_MI_I32_LOAD ||
      copy_plan.chunks[2].offset != 12 ||
      copy_plan.chunks[2].load.opcode != WASM32_MI_I32_LOAD16_U ||
      copy_plan.chunks[3].offset != 14 ||
      copy_plan.chunks[3].load.opcode != WASM32_MI_I32_LOAD8_U) {
    fprintf(stderr, "FAIL: machine copy plan\n");
    return 1;
  }
  wasm32_machine_copy_plan_dispose(&copy_plan);
  if (copy_plan.chunks || copy_plan.chunk_count != 0 ||
      wasm32_machine_copy_plan_build(-1, &copy_plan)) {
    fprintf(stderr, "FAIL: machine copy plan boundary\n");
    return 1;
  }
  wasm32_machine_primitive_plan_t primitives;
  if (!wasm32_machine_primitive_plan_build(&primitives)) {
    fprintf(stderr, "FAIL: machine primitive plan build\n");
    return 1;
  }
  const wasm32_machine_conversion_t *planned_conversion =
      wasm32_machine_planned_conversion(
          &primitives, IR_TY_PTR, IR_TY_I64, 1);
  const wasm32_machine_memory_t *planned_load =
      wasm32_machine_planned_load(&primitives, IR_TY_I16, 1);
  const wasm32_machine_memory_t *planned_store =
      wasm32_machine_planned_store(&primitives, IR_TY_F64);
  if (!planned_conversion ||
      planned_conversion->opcode != WASM32_MI_I64_EXTEND_I32_U ||
      !planned_load || planned_load->opcode != WASM32_MI_I32_LOAD16_U ||
      !planned_store || planned_store->opcode != WASM32_MI_F64_STORE ||
      wasm32_machine_planned_conversion(
          &primitives, IR_TY_VOID, IR_TY_I32, 0) ||
      wasm32_machine_planned_load(&primitives, IR_TY_VOID, 0) ||
      wasm32_machine_planned_store(&primitives, IR_TY_VOID)) {
    fprintf(stderr, "FAIL: machine primitive plan lookup\n");
    return 1;
  }
  for (size_t i = 0; i < sizeof(binary_cases) / sizeof(binary_cases[0]); i++) {
    const binary_case_t *test = &binary_cases[i];
    wasm32_machine_binary_t selected;
    int is_comparison =
        test->source_op == IR_EQ || test->source_op == IR_NE ||
        test->source_op == IR_LT || test->source_op == IR_ULT ||
        test->source_op == IR_LE || test->source_op == IR_ULE ||
        test->source_op == IR_FEQ || test->source_op == IR_FNE ||
        test->source_op == IR_FLT || test->source_op == IR_FLE;
    int guards_zero =
        test->source_op == IR_MOD || test->source_op == IR_UMOD;
    if (!wasm32_machine_select_binary(
            test->source_op, test->operand_type, &selected) ||
        strcmp(wasm32_machine_opcode_wat(selected.opcode), test->wat) != 0 ||
        wasm32_machine_opcode_binary(selected.opcode) != test->binary ||
        selected.is_comparison != is_comparison ||
        selected.guard_zero_divisor != guards_zero ||
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

  ir_inst_t function_instructions[16] = {0};
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
  function_instructions[5].sym = "planned_ref";
  function_instructions[5].sym_len = 11;
  function_instructions[5].is_function_symbol = 1;
  function_instructions[6].op = IR_VLA_ALLOC;
  function_instructions[6].dst =
      (ir_val_t){.id = 5, .type = IR_TY_PTR};
  function_instructions[6].src1 =
      (ir_val_t){.id = 3, .type = IR_TY_I32};
  function_instructions[7].op = IR_ATOMIC;
  function_instructions[7].dst =
      (ir_val_t){.id = 10, .type = IR_TY_I32};
  function_instructions[7].src1 =
      (ir_val_t){.id = 4, .type = IR_TY_PTR};
  function_instructions[7].src2 =
      (ir_val_t){.id = 1, .type = IR_TY_PTR};
  function_instructions[7].atomic_kind = IR_ATOMIC_CAS;
  function_instructions[7].atomic_width = 8;
  function_instructions[8].op = IR_ZEXT;
  function_instructions[8].dst =
      (ir_val_t){.id = 6, .type = IR_TY_I64};
  function_instructions[8].src1 =
      (ir_val_t){.id = 3, .type = IR_TY_I32};
  function_instructions[9].op = IR_ADD;
  function_instructions[9].dst =
      (ir_val_t){.id = 7, .type = IR_TY_I64};
  function_instructions[9].src1 =
      (ir_val_t){.id = 6, .type = IR_TY_I64};
  function_instructions[9].src2 =
      (ir_val_t){.id = IR_VAL_IMM, .type = IR_TY_I64, .imm = 2};
  function_instructions[10].op = IR_NOT;
  function_instructions[10].dst =
      (ir_val_t){.id = 8, .type = IR_TY_I64};
  function_instructions[10].src1 =
      (ir_val_t){.id = 7, .type = IR_TY_I64};
  function_instructions[11].op = IR_ATOMIC;
  function_instructions[11].dst =
      (ir_val_t){.id = 9, .type = IR_TY_I64};
  function_instructions[11].src1 =
      (ir_val_t){.id = 4, .type = IR_TY_PTR};
  function_instructions[11].src2 =
      (ir_val_t){.id = IR_VAL_IMM, .type = IR_TY_I64, .imm = 3};
  function_instructions[11].atomic_kind = IR_ATOMIC_RMW;
  function_instructions[11].atomic_rmw_op = IR_ARMW_XOR;
  function_instructions[11].atomic_width = 8;
  function_instructions[12].op = IR_CALL;
  function_instructions[12].dst =
      (ir_val_t){.id = IR_VAL_NONE, .type = IR_TY_VOID};
  function_instructions[12].callee =
      (ir_val_t){.id = IR_VAL_NONE, .type = IR_TY_VOID};
  function_instructions[12].sym = "planned_call";
  function_instructions[12].sym_len = 12;
  function_instructions[13].op = IR_PARAM_BIND;
  function_instructions[13].src1 =
      (ir_val_t){.id = 0, .type = IR_TY_PTR};
  function_instructions[13].parameter_index = 0;
  function_instructions[14].op = IR_LOAD_SYM;
  function_instructions[14].dst =
      (ir_val_t){.id = 11, .type = IR_TY_PTR};
  function_instructions[14].sym = "global_slot";
  function_instructions[14].sym_len = 11;
  function_instructions[15].op = IR_BR;
  function_instructions[15].label_id = 1;
  for (size_t i = 0; i + 1 < 16; i++)
    function_instructions[i].next = &function_instructions[i + 1];
  ir_block_t function_block = {
      .id = 0,
      .head = &function_instructions[0],
      .tail = &function_instructions[15],
  };
  char function_name[] = "planned_function";
  char function_c_signature[] = "i32(i32)";
  char continuation_entry[] = "planned_entry";
  char continuation_condition[] = "planned_condition";
  char continuation_start[] = "planned_start";
  char continuation_resume[] = "planned_resume";
  char continuation_status[] = "planned_status";
  char continuation_result[] = "planned_result";
  ir_func_t function = {
      .name = function_name,
      .c_signature = function_c_signature,
      .continuation_entry_name = continuation_entry,
      .continuation_condition_name = continuation_condition,
      .continuation_start_export = continuation_start,
      .continuation_resume_export = continuation_resume,
      .continuation_status_export = continuation_status,
      .continuation_result_export = continuation_result,
      .entry = &function_block,
      .name_len = 16,
      .c_signature_len = 8,
      .next_vreg_id = 12,
      .is_static = 1,
      .continuation_condition_block_id = 7,
      .is_continuation_entry = 1,
      .continuation_has_suspend = 1,
  };
  ir_abi_piece_t function_result = {
      .type = IR_TY_PTR,
      .source_size = 24,
      .kind = IR_ABI_PIECE_INDIRECT,
  };
  ir_abi_piece_t physical_function_params[] = {
      {
          .type = IR_TY_I32,
          .source_index = 0,
          .source_size = 16,
          .byte_offset = 0,
          .kind = IR_ABI_PIECE_DIRECT,
      },
      {
          .type = IR_TY_I64,
          .source_index = 0,
          .source_size = 16,
          .byte_offset = 8,
          .kind = IR_ABI_PIECE_DIRECT,
      },
  };
  ir_abi_signature_t function_abi_with_parameter = {
      .result_pieces = &function_result,
      .result_count = 1,
      .param_pieces = physical_function_params,
      .param_count = 2,
      .fixed_param_count = 2,
  };
  ir_abi_piece_t planned_call_result = {
      .type = IR_TY_PTR,
      .kind = IR_ABI_PIECE_INDIRECT,
  };
  ir_abi_piece_t planned_call_params[] = {
      {.type = IR_TY_I32, .kind = IR_ABI_PIECE_DIRECT},
      {.type = IR_TY_I64, .kind = IR_ABI_PIECE_VARIADIC},
  };
  ir_abi_signature_t planned_call_abi = {
      .result_pieces = &planned_call_result,
      .result_count = 1,
      .param_pieces = planned_call_params,
      .param_count = 2,
      .fixed_param_count = 1,
      .result_area = {.id = 0, .type = IR_TY_PTR},
      .is_variadic = 1,
  };
  ir_abi_piece_t reference_result = {
      .type = IR_TY_I32,
      .kind = IR_ABI_PIECE_DIRECT,
  };
  ir_abi_piece_t reference_param = {
      .type = IR_TY_I64,
      .kind = IR_ABI_PIECE_DIRECT,
  };
  ir_abi_signature_t reference_abi = {
      .result_pieces = &reference_result,
      .result_count = 1,
      .param_pieces = &reference_param,
      .param_count = 1,
      .fixed_param_count = 1,
  };
  ir_abi_argument_t planned_call_arguments[] = {
      {
          .source = {.id = 4, .type = IR_TY_PTR},
          .type = IR_TY_I32,
          .byte_offset = 4,
          .access = IR_ABI_ARGUMENT_LOAD,
      },
      {
          .source = {.id = 6, .type = IR_TY_I64},
          .type = IR_TY_I64,
          .access = IR_ABI_ARGUMENT_DIRECT,
      },
  };
  ir_abi_module_t fake_abi_module = {0};
  fixture_function = &function;
  fixture_function_abi = &function_abi_with_parameter;
  fixture_call = &function_instructions[12];
  fixture_call_abi = &planned_call_abi;
  fixture_call_arguments = planned_call_arguments;
  fixture_call_argument_count = 2;
  fixture_reference = &function_instructions[5];
  fixture_reference_abi = &reference_abi;
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
  const wasm32_machine_inst_t *selected_store =
      wasm32_machine_function_instruction(
          &machine_function, &function_instructions[2]);
  const wasm32_machine_inst_t *selected_load =
      wasm32_machine_function_instruction(
          &machine_function, &function_instructions[3]);
  const wasm32_machine_inst_t *selected_conversion =
      wasm32_machine_function_instruction(
          &machine_function, &function_instructions[8]);
  const wasm32_machine_inst_t *selected_reference =
      wasm32_machine_function_instruction(
          &machine_function, &function_instructions[5]);
  const wasm32_machine_inst_t *selected_binary =
      wasm32_machine_function_instruction(
          &machine_function, &function_instructions[9]);
  const wasm32_machine_inst_t *selected_unary =
      wasm32_machine_function_instruction(
          &machine_function, &function_instructions[10]);
  const wasm32_machine_inst_t *selected_atomic =
      wasm32_machine_function_instruction(
          &machine_function, &function_instructions[11]);
  const wasm32_machine_inst_t *selected_cas =
      wasm32_machine_function_instruction(
          &machine_function, &function_instructions[7]);
  const wasm32_machine_inst_t *selected_call =
      wasm32_machine_function_instruction(
          &machine_function, &function_instructions[12]);
  const wasm32_machine_inst_t *selected_parameter =
      wasm32_machine_function_instruction(
          &machine_function, &function_instructions[13]);
  const wasm32_machine_inst_t *selected_control =
      wasm32_machine_function_instruction(
          &machine_function, &function_instructions[15]);
  const wasm32_machine_block_t *selected_block =
      wasm32_machine_function_block(&machine_function, 0);
  if (!machine_function.name ||
      machine_function.name == function.name ||
      machine_function.name_len != function.name_len ||
      strcmp(machine_function.name, function.name) != 0 ||
      !machine_function.c_signature ||
      machine_function.c_signature == function.c_signature ||
      machine_function.c_signature_len != function.c_signature_len ||
      strcmp(machine_function.c_signature, function.c_signature) != 0 ||
      machine_function.continuation_entry_name ==
          function.continuation_entry_name ||
      strcmp(machine_function.continuation_entry_name,
             function.continuation_entry_name) != 0 ||
      machine_function.continuation_condition_block_id != 7 ||
      !machine_function.is_static ||
      !machine_function.is_continuation_entry ||
      !machine_function.continuation_has_suspend ||
      machine_function.frame_size != 32 ||
      machine_function.alloca_count != 2 ||
      machine_function.instruction_count != 16 ||
      machine_function.block_count != 1 ||
      machine_function.instructions[0].kind !=
          WASM32_MACHINE_INST_ALLOCA ||
      machine_function.instructions[4].kind !=
          WASM32_MACHINE_INST_INTEGER_CONSTANT ||
      machine_function.instructions[5].kind !=
          WASM32_MACHINE_INST_SYMBOL_ADDRESS ||
      machine_function.instructions[6].kind !=
          WASM32_MACHINE_INST_DYNAMIC_ALLOCA ||
      machine_function.signature.nparams != 3 ||
      !machine_function.signature.has_hidden_result ||
      machine_function.direct_result_type != IR_TY_VOID ||
      machine_function.result_source_size != 24 ||
      machine_function.result_copy.chunk_count != 3 ||
      machine_function.result_copy.chunks[0].load.opcode !=
          WASM32_MI_I64_LOAD ||
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
      !machine_function.has_atomic_cas64 ||
      !selected_store ||
      selected_store->kind != WASM32_MACHINE_INST_STORE ||
      selected_store->store.opcode != WASM32_MI_I64_STORE ||
      !selected_load ||
      selected_load->kind != WASM32_MACHINE_INST_LOAD ||
      selected_load->load.opcode != WASM32_MI_I64_LOAD ||
      !selected_reference ||
      !selected_reference->has_reference_signature ||
      selected_reference->reference_signature.result != IR_TY_I32 ||
      selected_reference->reference_signature.nparams != 1 ||
      selected_reference->reference_signature.params[0] != IR_TY_I64 ||
      !selected_conversion ||
      selected_conversion->kind != WASM32_MACHINE_INST_CONVERSION ||
      selected_conversion->conversion.opcode !=
          WASM32_MI_I64_EXTEND_I32_U ||
      !selected_binary ||
      selected_binary->kind != WASM32_MACHINE_INST_BINARY ||
      selected_binary->binary.opcode != WASM32_MI_I64_ADD ||
      !selected_unary ||
      selected_unary->kind != WASM32_MACHINE_INST_UNARY ||
      selected_unary->unary.opcode != WASM32_MI_I64_XOR ||
      !selected_atomic ||
      selected_atomic->kind != WASM32_MACHINE_INST_ATOMIC ||
      selected_atomic->atomic.kind != WASM32_MACHINE_ATOMIC_RMW ||
      selected_atomic->atomic.load.opcode != WASM32_MI_I64_LOAD ||
      selected_atomic->atomic.store.opcode != WASM32_MI_I64_STORE ||
      selected_atomic->atomic.binary.opcode != WASM32_MI_I64_XOR ||
      !selected_cas ||
      selected_cas->atomic.kind !=
          WASM32_MACHINE_ATOMIC_COMPARE_EXCHANGE ||
      selected_cas->atomic.comparison.opcode != WASM32_MI_I64_EQ ||
      !selected_call ||
      selected_call->kind != WASM32_MACHINE_INST_CALL ||
      selected_call->call.signature.nparams != 2 ||
      selected_call->call.signature.params[0] != IR_TY_I32 ||
      selected_call->call.signature.params[1] != IR_TY_I32 ||
      selected_call->call.signature.result != IR_TY_VOID ||
      !selected_call->call.signature.has_hidden_result ||
      selected_call->call.argument_count != 2 ||
      selected_call->call.fixed_argument_count != 1 ||
      (const void *)selected_call->call.arguments ==
          (const void *)planned_call_arguments ||
      selected_call->call.arguments[0].access !=
          WASM32_MACHINE_ARGUMENT_LOAD ||
      selected_call->call.arguments[0].byte_offset != 4 ||
      selected_call->call.arguments[0].load.opcode !=
          WASM32_MI_I32_LOAD ||
      selected_call->call.arguments[1].source.id != 6 ||
      selected_call->call.arguments[1].value_type != IR_TY_I64 ||
      selected_call->call.arguments[1].access !=
          WASM32_MACHINE_ARGUMENT_DIRECT ||
      selected_call->call.result_area.id != 0 ||
      selected_call->call.direct_result_type != IR_TY_VOID ||
      selected_call->call.is_indirect ||
      !selected_call->call.is_variadic ||
      selected_call->call.variadic_argument_count != 1 ||
      selected_call->call.variadic_area_size != 16 ||
      !selected_call->call.variadic_arguments ||
      selected_call->call.variadic_arguments[0].argument_index != 1 ||
      selected_call->call.variadic_arguments[0].byte_offset != 0 ||
      selected_call->call.variadic_arguments[0].argument_type !=
          IR_TY_I64 ||
      selected_call->call.variadic_arguments[0].conversion.opcode !=
          WASM32_MI_COPY ||
      selected_call->call.variadic_arguments[0].store.opcode !=
          WASM32_MI_I64_STORE ||
      !selected_parameter ||
      selected_parameter->kind !=
          WASM32_MACHINE_INST_PARAMETER_BIND ||
      selected_parameter->parameter_bind.piece_count != 2 ||
      selected_parameter->parameter_bind.physical_index != 1 ||
      (const void *)selected_parameter->parameter_bind.pieces ==
          (const void *)physical_function_params ||
      selected_parameter->parameter_bind.pieces[1].value_type !=
          IR_TY_I64 ||
      selected_parameter->parameter_bind.pieces[1].byte_offset != 8 ||
      selected_parameter->parameter_bind.stores[0].opcode !=
          WASM32_MI_I32_STORE ||
      selected_parameter->parameter_bind.stores[1].opcode !=
          WASM32_MI_I64_STORE ||
      !selected_control ||
      selected_control->kind != WASM32_MACHINE_INST_CONTROL ||
      selected_control->control.kind != WASM32_MACHINE_CONTROL_BRANCH ||
      selected_control->control.target_block_id != 1 ||
      !selected_block || selected_block->id != 0 ||
      selected_block->first_instruction != 0 ||
      selected_block->instruction_count != 16 ||
      selected_block->next_block_id != -1 ||
      !selected_block->has_terminator ||
      wasm32_machine_function_block(&machine_function, 1) != NULL ||
      !wasm32_machine_opcode_is_comparison(
          WASM32_MI_I64_LT_U) ||
      !wasm32_machine_opcode_is_unsigned(
          WASM32_MI_I64_REM_U) ||
      !wasm32_machine_opcode_is_shift(
          WASM32_MI_I32_SHR_S) ||
      !wasm32_machine_opcode_is_remainder(
          WASM32_MI_I32_REM_S) ||
      !wasm32_machine_opcode_is_add(WASM32_MI_I64_ADD) ||
      !wasm32_machine_opcode_is_subtract(WASM32_MI_I32_SUB) ||
      wasm32_machine_opcode_is_comparison(WASM32_MI_I32_ADD) ||
      wasm32_machine_function_instruction(
          &machine_function, &call) != NULL) {
    fprintf(stderr, "FAIL: machine function plan invariants\n");
    return 1;
  }
  ir_symbol_func_ref_t source_func_ref = {
      .name = "initial_target",
      .name_len = 14,
      .offset = 8,
  };
  ir_symbol_t source_symbol = {
      .name = "global_slot",
      .name_len = 11,
      .byte_size = 16,
      .alignment = 8,
      .is_static = 1,
      .func_refs = &source_func_ref,
      .func_refs_tail = &source_func_ref,
  };
  ir_module_t source_module = {
      .funcs = &function,
      .symbols = &source_symbol,
  };
  wasm32_machine_module_t machine_module;
  if (!wasm32_machine_module_build(
          &source_module, &fake_abi_module, &machine_module)) {
    fprintf(stderr, "FAIL: machine module plan build\n");
    return 1;
  }
  const wasm32_machine_symbol_t *machine_symbol =
      wasm32_machine_module_symbol(
          &machine_module, "global_slot", 11);
  const wasm32_machine_symbol_func_ref_t *machine_func_ref =
      wasm32_machine_symbol_find_func_ref(machine_symbol, 8);
  const wasm32_machine_function_t *module_function =
      wasm32_machine_module_function(&machine_module, 0);
  if (machine_module.function_count != 1 ||
      machine_module.symbol_count != 1 ||
      !machine_symbol || machine_symbol->name == source_symbol.name ||
      machine_symbol->byte_size != 16 || machine_symbol->alignment != 8 ||
      !machine_symbol->is_static || !machine_func_ref ||
      machine_func_ref->name == source_func_ref.name ||
      strcmp(machine_func_ref->name, source_func_ref.name) != 0 ||
      !module_function ||
      module_function->source != &function ||
      module_function->instructions[14].resolved_symbol != machine_symbol ||
      wasm32_machine_module_function(&machine_module, 1) != NULL) {
    fprintf(stderr, "FAIL: machine module plan invariants\n");
    return 1;
  }
  wasm32_machine_module_dispose(&machine_module);
  if (machine_module.functions || machine_module.symbols ||
      machine_module.function_count != 0 ||
      machine_module.symbol_count != 0) {
    fprintf(stderr, "FAIL: machine module plan disposal\n");
    return 1;
  }
  wasm32_machine_function_dispose(&machine_function);
  puts("wasm32 Machine IR tests passed");
  return 0;
}
