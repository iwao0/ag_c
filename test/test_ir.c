/*
 * IR Phase 1 単体テスト。
 *
 * 手組みの IR モジュールを構築し、プリンタの出力が期待文字列と一致することを確認する。
 * AST→IR 変換 (Phase 2 以降) や IR→ASM 変換は対象外。
 */

#include "../src/ir/ir.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures = 0;

static void check_str_eq(const char *label, const char *got, const char *expected) {
  if (strcmp(got, expected) != 0) {
    failures++;
    fprintf(stderr, "FAIL [%s]\n--- got ---\n%s\n--- expected ---\n%s\n", label, got, expected);
  }
}

/* ---- test 1: 算術式 (1 + 2) を return する 1 関数だけのモジュール ---- */
static void test_simple_add(void) {
  ir_module_t *m = ir_module_new();
  ir_func_t *f = ir_func_new(m, "main", 4, IR_TY_I32);

  int v0 = ir_func_new_vreg(f);
  int v1 = ir_func_new_vreg(f);
  int v2 = ir_func_new_vreg(f);

  ir_inst_t *i0 = ir_inst_new(IR_LOAD_IMM);
  i0->dst = ir_val_vreg(v0, IR_TY_I32);
  i0->src1 = ir_val_imm(IR_TY_I32, 1);
  ir_func_append_inst(f, i0);

  ir_inst_t *i1 = ir_inst_new(IR_LOAD_IMM);
  i1->dst = ir_val_vreg(v1, IR_TY_I32);
  i1->src1 = ir_val_imm(IR_TY_I32, 2);
  ir_func_append_inst(f, i1);

  ir_inst_t *i2 = ir_inst_new(IR_ADD);
  i2->dst = ir_val_vreg(v2, IR_TY_I32);
  i2->src1 = ir_val_vreg(v0, IR_TY_I32);
  i2->src2 = ir_val_vreg(v1, IR_TY_I32);
  ir_func_append_inst(f, i2);

  ir_inst_t *iret = ir_inst_new(IR_RET);
  iret->src1 = ir_val_vreg(v2, IR_TY_I32);
  ir_func_append_inst(f, iret);

  char buf[1024];
  ir_print_module_to_buf(m, buf, sizeof(buf));

  const char *expected =
    "func @main -> i32 {\n"
    ".L0:\n"
    "  v0 = load_imm i32 1\n"
    "  v1 = load_imm i32 2\n"
    "  v2 = add i32 v0, v1\n"
    "  ret v2\n"
    "}\n";
  check_str_eq("simple_add", buf, expected);
}

/* ---- test 2: グローバル変数 (スカラ + 配列) のダンプ ---- */
static void test_globals(void) {
  ir_module_t *m = ir_module_new();

  ir_global_t *g1 = calloc(1, sizeof(ir_global_t));
  g1->name = "x"; g1->name_len = 1;
  g1->byte_size = 4; g1->elem_size = 4; g1->is_array = 0;
  g1->init_val = 42;
  m->globals = g1; m->globals_tail = g1;

  ir_global_t *g2 = calloc(1, sizeof(ir_global_t));
  g2->name = "arr"; g2->name_len = 3;
  g2->byte_size = 12; g2->elem_size = 4; g2->is_array = 1;
  g2->init_count = 3;
  g2->init_values = calloc(3, sizeof(long long));
  g2->init_values[0] = 10;
  g2->init_values[1] = 20;
  g2->init_values[2] = 30;
  g1->next = g2;
  m->globals_tail = g2;

  ir_global_t *g3 = calloc(1, sizeof(ir_global_t));
  g3->name = "zeroes"; g3->name_len = 6;
  g3->byte_size = 8; g3->elem_size = 4; g3->is_array = 1;
  g3->init_count = 2;
  g2->next = g3;
  m->globals_tail = g3;

  char buf[1024];
  ir_print_module_to_buf(m, buf, sizeof(buf));

  const char *expected =
    "global @x = 42\n"
    "global @arr [3] = {10, 20, 30}\n"
    "global @zeroes [2] = {0, 0}\n"
    "\n";
  check_str_eq("globals", buf, expected);
}

/* ---- test 3: 制御フロー (if 風) ---- */
static void test_control_flow(void) {
  ir_module_t *m = ir_module_new();
  ir_func_t *f = ir_func_new(m, "cmp", 3, IR_TY_I32);

  int v0 = ir_func_new_vreg(f); /* x */
  int v1 = ir_func_new_vreg(f); /* 0 */
  int v2 = ir_func_new_vreg(f); /* x > 0 ? */

  ir_inst_t *iparam = ir_inst_new(IR_PARAM);
  iparam->dst = ir_val_vreg(v0, IR_TY_I32);
  iparam->src1 = ir_val_imm(IR_TY_I32, 0);
  ir_func_append_inst(f, iparam);

  ir_inst_t *izero = ir_inst_new(IR_LOAD_IMM);
  izero->dst = ir_val_vreg(v1, IR_TY_I32);
  izero->src1 = ir_val_imm(IR_TY_I32, 0);
  ir_func_append_inst(f, izero);

  ir_inst_t *ilt = ir_inst_new(IR_LT);
  ilt->dst = ir_val_vreg(v2, IR_TY_I32);
  ilt->src1 = ir_val_vreg(v1, IR_TY_I32);
  ilt->src2 = ir_val_vreg(v0, IR_TY_I32);
  ir_func_append_inst(f, ilt);

  /* br_cond v2, then=block1, else=block2 */
  ir_block_t *then_b = ir_block_new(f);
  ir_block_t *else_b = ir_block_new(f);

  ir_inst_t *ibr = ir_inst_new(IR_BR_COND);
  ibr->src1 = ir_val_vreg(v2, IR_TY_I32);
  ibr->label_id = then_b->id;
  ibr->else_label_id = else_b->id;
  ir_func_append_inst(f, ibr);

  ir_func_switch_block(f, then_b);
  ir_inst_t *iret_one = ir_inst_new(IR_RET);
  iret_one->src1 = ir_val_imm(IR_TY_I32, 1);
  ir_func_append_inst(f, iret_one);

  ir_func_switch_block(f, else_b);
  ir_inst_t *iret_zero = ir_inst_new(IR_RET);
  iret_zero->src1 = ir_val_imm(IR_TY_I32, 0);
  ir_func_append_inst(f, iret_zero);

  char buf[2048];
  ir_print_module_to_buf(m, buf, sizeof(buf));

  const char *expected =
    "func @cmp -> i32 {\n"
    ".L0:\n"
    "  v0 = param i32 #0\n"
    "  v1 = load_imm i32 0\n"
    "  v2 = lt i32 v1, v0\n"
    "  br_cond v2, .L1, .L2\n"
    ".L1:\n"
    "  ret 1\n"
    ".L2:\n"
    "  ret 0\n"
    "}\n";
  check_str_eq("control_flow", buf, expected);
}

/* ---- test 4: 関数呼び出し ---- */
static void test_call(void) {
  ir_module_t *m = ir_module_new();
  ir_func_t *f = ir_func_new(m, "main", 4, IR_TY_I32);

  int v0 = ir_func_new_vreg(f);
  int v1 = ir_func_new_vreg(f);
  int vret = ir_func_new_vreg(f);

  ir_inst_t *i0 = ir_inst_new(IR_LOAD_IMM);
  i0->dst = ir_val_vreg(v0, IR_TY_I32);
  i0->src1 = ir_val_imm(IR_TY_I32, 3);
  ir_func_append_inst(f, i0);

  ir_inst_t *i1 = ir_inst_new(IR_LOAD_IMM);
  i1->dst = ir_val_vreg(v1, IR_TY_I32);
  i1->src1 = ir_val_imm(IR_TY_I32, 4);
  ir_func_append_inst(f, i1);

  ir_inst_t *icall = ir_inst_new(IR_CALL);
  icall->dst = ir_val_vreg(vret, IR_TY_I32);
  icall->sym = "add"; icall->sym_len = 3;
  icall->args = calloc(2, sizeof(ir_val_t));
  icall->args[0] = ir_val_vreg(v0, IR_TY_I32);
  icall->args[1] = ir_val_vreg(v1, IR_TY_I32);
  icall->nargs = 2;
  ir_func_append_inst(f, icall);

  ir_inst_t *iret = ir_inst_new(IR_RET);
  iret->src1 = ir_val_vreg(vret, IR_TY_I32);
  ir_func_append_inst(f, iret);

  char buf[1024];
  ir_print_module_to_buf(m, buf, sizeof(buf));

  const char *expected =
    "func @main -> i32 {\n"
    ".L0:\n"
    "  v0 = load_imm i32 3\n"
    "  v1 = load_imm i32 4\n"
    "  v2 = call i32 @add(v0, v1)\n"
    "  ret v2\n"
    "}\n";
  check_str_eq("call", buf, expected);
}

/* ---- test 5: 各種ヘルパの値検査 ---- */
static void test_helpers(void) {
  if (ir_type_size(IR_TY_I32) != 4) { failures++; fprintf(stderr, "FAIL: ir_type_size(I32)\n"); }
  if (ir_type_size(IR_TY_I64) != 8) { failures++; fprintf(stderr, "FAIL: ir_type_size(I64)\n"); }
  if (ir_type_size(IR_TY_F64) != 8) { failures++; fprintf(stderr, "FAIL: ir_type_size(F64)\n"); }
  if (strcmp(ir_type_name(IR_TY_PTR), "ptr") != 0) { failures++; fprintf(stderr, "FAIL: ir_type_name(PTR)\n"); }
  if (strcmp(ir_op_name(IR_ADD), "add") != 0) { failures++; fprintf(stderr, "FAIL: ir_op_name(ADD)\n"); }

  ir_val_t imm = ir_val_imm(IR_TY_I32, 42);
  if (imm.id != IR_VAL_IMM || imm.imm != 42) { failures++; fprintf(stderr, "FAIL: ir_val_imm\n"); }

  ir_val_t v = ir_val_vreg(7, IR_TY_F64);
  if (v.id != 7 || v.type != IR_TY_F64) { failures++; fprintf(stderr, "FAIL: ir_val_vreg\n"); }

  ir_val_t none = ir_val_none();
  if (none.id != IR_VAL_NONE) { failures++; fprintf(stderr, "FAIL: ir_val_none\n"); }
}

int main(void) {
  test_helpers();
  test_simple_add();
  test_globals();
  test_control_flow();
  test_call();
  if (failures > 0) {
    fprintf(stderr, "IR Phase 1: %d test(s) failed\n", failures);
    return 1;
  }
  printf("OK: All IR Phase 1 tests passed!\n");
  return 0;
}
