#include "../src/ag_c.h"
#include "../src/parser/parser.h"
#include "test_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// stdout を一時的にバッファへリダイレクトして gen 系関数の出力をキャプチャする
static char *capture_buf;
static size_t capture_size;
static FILE *original_stdout;

static void capture_start(void) {
  fflush(stdout);
  original_stdout = stdout;
  stdout = open_memstream(&capture_buf, &capture_size);
}

static char *capture_end(void) {
  fflush(stdout);
  fclose(stdout);
  stdout = original_stdout;
  return capture_buf;
}

// ヘルパー: ASTリーフ（整数ノード）を作成
static node_t *make_num(int val) {
  node_t *n = calloc(1, sizeof(node_t));
  n->kind = ND_NUM;
  n->val = val;
  return n;
}

// ヘルパー: 二項演算ノードを作成
static node_t *make_binop(node_kind_t kind, node_t *lhs, node_t *rhs) {
  node_t *n = calloc(1, sizeof(node_t));
  n->kind = kind;
  n->lhs = lhs;
  n->rhs = rhs;
  return n;
}

// ヘルパー: ローカル変数ノードを作成
static node_t *make_lvar(int offset) {
  node_t *n = calloc(1, sizeof(node_t));
  n->kind = ND_LVAR;
  n->offset = offset;
  return n;
}

// --- テストケース ---

// gen_main_prologue: アセンブリのプロローグが正しく出力されるか
static void test_gen_main_prologue(void) {
  printf("test_gen_main_prologue...\n");
  capture_start();
  gen_main_prologue();
  char *out = capture_end();

  ASSERT_TRUE(strstr(out, ".global _main") != NULL);
  ASSERT_TRUE(strstr(out, ".align 2") != NULL);
  ASSERT_TRUE(strstr(out, "_main:") != NULL);
  ASSERT_TRUE(strstr(out, "stp x29, x30") != NULL);
  ASSERT_TRUE(strstr(out, "mov x29, sp") != NULL);
  free(out);
}

// gen_main_epilogue: アセンブリのエピローグが正しく出力されるか
static void test_gen_main_epilogue(void) {
  printf("test_gen_main_epilogue...\n");
  capture_start();
  gen_main_epilogue();
  char *out = capture_end();

  ASSERT_TRUE(strstr(out, "ldp x29, x30") != NULL);
  ASSERT_TRUE(strstr(out, "ret") != NULL);
  free(out);
}

// gen(ND_NUM): 整数リテラルの出力
static void test_gen_num(void) {
  printf("test_gen_num...\n");
  node_t *n = make_num(42);

  capture_start();
  gen(n);
  char *out = capture_end();

  ASSERT_TRUE(strstr(out, "mov x0, #42") != NULL);
  ASSERT_TRUE(strstr(out, "str x0, [sp, #-16]!") != NULL);
  free(out);
  free(n);
}

// gen(ND_ADD): 加算の出力
static void test_gen_add(void) {
  printf("test_gen_add...\n");
  node_t *n = make_binop(ND_ADD, make_num(1), make_num(2));

  capture_start();
  gen(n);
  char *out = capture_end();

  // 左辺と右辺がプッシュされ、ポップされて加算される
  ASSERT_TRUE(strstr(out, "mov x0, #1") != NULL);
  ASSERT_TRUE(strstr(out, "mov x0, #2") != NULL);
  ASSERT_TRUE(strstr(out, "add x0, x0, x1") != NULL);
  free(out);
}

// gen(ND_SUB): 減算の出力
static void test_gen_sub(void) {
  printf("test_gen_sub...\n");
  node_t *n = make_binop(ND_SUB, make_num(5), make_num(3));

  capture_start();
  gen(n);
  char *out = capture_end();

  ASSERT_TRUE(strstr(out, "sub x0, x0, x1") != NULL);
  free(out);
}

// gen(ND_MUL): 乗算の出力
static void test_gen_mul(void) {
  printf("test_gen_mul...\n");
  node_t *n = make_binop(ND_MUL, make_num(3), make_num(4));

  capture_start();
  gen(n);
  char *out = capture_end();

  ASSERT_TRUE(strstr(out, "mul x0, x0, x1") != NULL);
  free(out);
}

// gen(ND_DIV): 除算の出力
static void test_gen_div(void) {
  printf("test_gen_div...\n");
  node_t *n = make_binop(ND_DIV, make_num(10), make_num(2));

  capture_start();
  gen(n);
  char *out = capture_end();

  ASSERT_TRUE(strstr(out, "sdiv x0, x0, x1") != NULL);
  free(out);
}

// gen(ND_EQ): 等値比較の出力
static void test_gen_eq(void) {
  printf("test_gen_eq...\n");
  node_t *n = make_binop(ND_EQ, make_num(1), make_num(1));

  capture_start();
  gen(n);
  char *out = capture_end();

  ASSERT_TRUE(strstr(out, "cmp x0, x1") != NULL);
  ASSERT_TRUE(strstr(out, "cset x0, eq") != NULL);
  free(out);
}

// gen(ND_NE): 非等値比較の出力
static void test_gen_ne(void) {
  printf("test_gen_ne...\n");
  node_t *n = make_binop(ND_NE, make_num(1), make_num(2));

  capture_start();
  gen(n);
  char *out = capture_end();

  ASSERT_TRUE(strstr(out, "cset x0, ne") != NULL);
  free(out);
}

// gen(ND_LT): 小なり比較の出力
static void test_gen_lt(void) {
  printf("test_gen_lt...\n");
  node_t *n = make_binop(ND_LT, make_num(1), make_num(2));

  capture_start();
  gen(n);
  char *out = capture_end();

  ASSERT_TRUE(strstr(out, "cset x0, lt") != NULL);
  free(out);
}

// gen(ND_LE): 以下比較の出力
static void test_gen_le(void) {
  printf("test_gen_le...\n");
  node_t *n = make_binop(ND_LE, make_num(1), make_num(2));

  capture_start();
  gen(n);
  char *out = capture_end();

  ASSERT_TRUE(strstr(out, "cset x0, le") != NULL);
  free(out);
}

// gen(ND_LVAR): ローカル変数の読み出し
static void test_gen_lvar(void) {
  printf("test_gen_lvar...\n");
  node_t *n = make_lvar(8); // 変数 'a' (offset=8)

  capture_start();
  gen(n);
  char *out = capture_end();

  // フレームポインタ + 16 + 8 = 24 のアドレスを算出
  ASSERT_TRUE(strstr(out, "add x0, x29, #24") != NULL);
  ASSERT_TRUE(strstr(out, "ldr x0, [x0]") != NULL);
  free(out);
}

// gen(ND_ASSIGN): 代入の出力
static void test_gen_assign(void) {
  printf("test_gen_assign...\n");
  node_t *n = make_binop(ND_ASSIGN, make_lvar(8), make_num(42));

  capture_start();
  gen(n);
  char *out = capture_end();

  ASSERT_TRUE(strstr(out, "add x0, x29, #24") != NULL); // 変数アドレス
  ASSERT_TRUE(strstr(out, "mov x0, #42") != NULL);       // 右辺値
  ASSERT_TRUE(strstr(out, "str x1, [x0]") != NULL);      // メモリ書き込み
  free(out);
}

// gen(ND_IF): if 文の分岐ラベル出力
static void test_gen_if(void) {
  printf("test_gen_if...\n");
  node_t *n = calloc(1, sizeof(node_t));
  n->kind = ND_IF;
  n->lhs = make_num(1);      // 条件
  n->rhs = make_num(42);     // then
  n->els = make_num(0);      // else

  capture_start();
  gen(n);
  char *out = capture_end();

  ASSERT_TRUE(strstr(out, "cbz x0") != NULL);
  ASSERT_TRUE(strstr(out, ".Lelse") != NULL);
  ASSERT_TRUE(strstr(out, ".Lend") != NULL);
  free(out);
}

// gen(ND_WHILE): while 文のループラベル出力
static void test_gen_while(void) {
  printf("test_gen_while...\n");
  node_t *n = calloc(1, sizeof(node_t));
  n->kind = ND_WHILE;
  n->lhs = make_num(0);      // 条件（偽→即終了）
  n->rhs = make_num(1);      // 本体

  capture_start();
  gen(n);
  char *out = capture_end();

  ASSERT_TRUE(strstr(out, ".Lbegin") != NULL);
  ASSERT_TRUE(strstr(out, "cbz x0") != NULL);
  ASSERT_TRUE(strstr(out, ".Lend") != NULL);
  free(out);
}

// gen(ND_FOR): for 文のラベル出力
static void test_gen_for(void) {
  printf("test_gen_for...\n");
  node_t *n = calloc(1, sizeof(node_t));
  n->kind = ND_FOR;
  n->init = make_num(0);     // 初期化
  n->lhs = make_num(1);      // 条件
  n->inc = make_num(1);      // インクリメント
  n->rhs = make_num(99);     // 本体

  capture_start();
  gen(n);
  char *out = capture_end();

  ASSERT_TRUE(strstr(out, ".Lbegin") != NULL);
  ASSERT_TRUE(strstr(out, "cbz x0") != NULL);
  ASSERT_TRUE(strstr(out, ".Lend") != NULL);
  free(out);
}

// gen(ND_RETURN): return 文の出力
static void test_gen_return(void) {
  printf("test_gen_return...\n");
  node_t *n = calloc(1, sizeof(node_t));
  n->kind = ND_RETURN;
  n->lhs = make_num(42);

  capture_start();
  gen(n);
  char *out = capture_end();

  ASSERT_TRUE(strstr(out, "mov x0, #42") != NULL);
  ASSERT_TRUE(strstr(out, "ldp x29, x30") != NULL);
  ASSERT_TRUE(strstr(out, "ret") != NULL);
  free(out);
}

int main(void) {
  printf("Running tests for ARM64 Code Generator...\n");

  test_gen_main_prologue();
  test_gen_main_epilogue();
  test_gen_num();
  test_gen_add();
  test_gen_sub();
  test_gen_mul();
  test_gen_div();
  test_gen_eq();
  test_gen_ne();
  test_gen_lt();
  test_gen_le();
  test_gen_lvar();
  test_gen_assign();
  test_gen_if();
  test_gen_while();
  test_gen_for();
  test_gen_return();

  printf("OK: All unit tests passed!\n");
  return 0;
}
