#include "../src/ag_c.h"
#include "../src/parser/parser.h"
#include "test_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// test_codegen は parser.o をリンクしないため、string_literals のダミー定義が必要
string_lit_t *string_literals = NULL;

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

static void test_gen_funcdef(void) {
  printf("test_gen_funcdef...\n");
  node_t *n = calloc(1, sizeof(node_t));
  n->kind = ND_FUNCDEF;
  n->funcname = "foo";
  n->funcname_len = 3;
  n->nargs = 0;
  node_t *body = calloc(1, sizeof(node_t));
  body->kind = ND_BLOCK;
  body->body[0] = make_num(42);
  body->body[1] = NULL;
  n->rhs = body;

  capture_start();
  gen(n);
  char *out = capture_end();

  ASSERT_TRUE(strstr(out, ".global _foo") != NULL);
  ASSERT_TRUE(strstr(out, "_foo:") != NULL);
  ASSERT_TRUE(strstr(out, "stp x29, x30") != NULL);
  ASSERT_TRUE(strstr(out, "mov x29, sp") != NULL);
  ASSERT_TRUE(strstr(out, "mov x0, #42") != NULL);
  ASSERT_TRUE(strstr(out, "ret") != NULL);
  free(out);
}

static void test_gen_funcall(void) {
  printf("test_gen_funcall...\n");
  node_t *n = calloc(1, sizeof(node_t));
  n->kind = ND_FUNCALL;
  n->funcname = "bar";
  n->funcname_len = 3;
  n->nargs = 2;
  n->args[0] = make_num(1);
  n->args[1] = make_num(2);

  capture_start();
  gen(n);
  char *out = capture_end();

  ASSERT_TRUE(strstr(out, "bl _bar") != NULL);
  free(out);
}

static void test_gen_num(void) {
  printf("test_gen_num...\n");
  node_t *n = make_num(42);
  capture_start();
  gen(n);
  char *out = capture_end();
  ASSERT_TRUE(strstr(out, "mov x0, #42") != NULL);
  free(out);
}

static void test_gen_add(void) {
  printf("test_gen_add...\n");
  node_t *n = make_binop(ND_ADD, make_num(1), make_num(2));
  capture_start();
  gen(n);
  char *out = capture_end();
  ASSERT_TRUE(strstr(out, "add x0, x0, x1") != NULL);
  free(out);
}

static void test_gen_sub(void) {
  printf("test_gen_sub...\n");
  node_t *n = make_binop(ND_SUB, make_num(5), make_num(3));
  capture_start();
  gen(n);
  char *out = capture_end();
  ASSERT_TRUE(strstr(out, "sub x0, x0, x1") != NULL);
  free(out);
}

static void test_gen_mul(void) {
  printf("test_gen_mul...\n");
  node_t *n = make_binop(ND_MUL, make_num(3), make_num(4));
  capture_start();
  gen(n);
  char *out = capture_end();
  ASSERT_TRUE(strstr(out, "mul x0, x0, x1") != NULL);
  free(out);
}

static void test_gen_div(void) {
  printf("test_gen_div...\n");
  node_t *n = make_binop(ND_DIV, make_num(10), make_num(2));
  capture_start();
  gen(n);
  char *out = capture_end();
  ASSERT_TRUE(strstr(out, "sdiv x0, x0, x1") != NULL);
  free(out);
}

static void test_gen_eq(void) {
  printf("test_gen_eq...\n");
  node_t *n = make_binop(ND_EQ, make_num(1), make_num(1));
  capture_start();
  gen(n);
  char *out = capture_end();
  ASSERT_TRUE(strstr(out, "cset x0, eq") != NULL);
  free(out);
}

static void test_gen_ne(void) {
  printf("test_gen_ne...\n");
  node_t *n = make_binop(ND_NE, make_num(1), make_num(2));
  capture_start();
  gen(n);
  char *out = capture_end();
  ASSERT_TRUE(strstr(out, "cset x0, ne") != NULL);
  free(out);
}

static void test_gen_lt(void) {
  printf("test_gen_lt...\n");
  node_t *n = make_binop(ND_LT, make_num(1), make_num(2));
  capture_start();
  gen(n);
  char *out = capture_end();
  ASSERT_TRUE(strstr(out, "cset x0, lt") != NULL);
  free(out);
}

static void test_gen_le(void) {
  printf("test_gen_le...\n");
  node_t *n = make_binop(ND_LE, make_num(1), make_num(2));
  capture_start();
  gen(n);
  char *out = capture_end();
  ASSERT_TRUE(strstr(out, "cset x0, le") != NULL);
  free(out);
}

static void test_gen_lvar(void) {
  printf("test_gen_lvar...\n");
  node_t *n = make_lvar(8);
  capture_start();
  gen(n);
  char *out = capture_end();
  ASSERT_TRUE(strstr(out, "add x0, x29, #24") != NULL);
  ASSERT_TRUE(strstr(out, "ldr x0, [x0]") != NULL);
  free(out);
}

static void test_gen_assign(void) {
  printf("test_gen_assign...\n");
  node_t *n = make_binop(ND_ASSIGN, make_lvar(8), make_num(42));
  capture_start();
  gen(n);
  char *out = capture_end();
  ASSERT_TRUE(strstr(out, "str x1, [x0]") != NULL);
  free(out);
}

static void test_gen_if(void) {
  printf("test_gen_if...\n");
  node_t *n = calloc(1, sizeof(node_t));
  n->kind = ND_IF;
  n->lhs = make_num(1);
  n->rhs = make_num(42);
  n->els = make_num(0);
  capture_start();
  gen(n);
  char *out = capture_end();
  ASSERT_TRUE(strstr(out, "cbz x0") != NULL);
  ASSERT_TRUE(strstr(out, ".Lelse") != NULL);
  free(out);
}

static void test_gen_while(void) {
  printf("test_gen_while...\n");
  node_t *n = calloc(1, sizeof(node_t));
  n->kind = ND_WHILE;
  n->lhs = make_num(0);
  n->rhs = make_num(1);
  capture_start();
  gen(n);
  char *out = capture_end();
  ASSERT_TRUE(strstr(out, ".Lbegin") != NULL);
  ASSERT_TRUE(strstr(out, "cbz x0") != NULL);
  free(out);
}

static void test_gen_for(void) {
  printf("test_gen_for...\n");
  node_t *n = calloc(1, sizeof(node_t));
  n->kind = ND_FOR;
  n->init = make_num(0);
  n->lhs = make_num(1);
  n->inc = make_num(1);
  n->rhs = make_num(99);
  capture_start();
  gen(n);
  char *out = capture_end();
  ASSERT_TRUE(strstr(out, ".Lbegin") != NULL);
  ASSERT_TRUE(strstr(out, "cbz x0") != NULL);
  free(out);
}

static void test_gen_return(void) {
  printf("test_gen_return...\n");
  node_t *n = calloc(1, sizeof(node_t));
  n->kind = ND_RETURN;
  n->lhs = make_num(42);
  capture_start();
  gen(n);
  char *out = capture_end();
  ASSERT_TRUE(strstr(out, "ret") != NULL);
  free(out);
}

static void test_gen_block(void) {
  printf("test_gen_block...\n");
  node_t *n = calloc(1, sizeof(node_t));
  n->kind = ND_BLOCK;
  n->body[0] = make_num(1);
  n->body[1] = make_num(2);
  n->body[2] = NULL;
  capture_start();
  gen(n);
  char *out = capture_end();
  ASSERT_TRUE(strstr(out, "mov x0, #1") != NULL);
  ASSERT_TRUE(strstr(out, "mov x0, #2") != NULL);
  free(out);
}

int main(void) {
  printf("Running tests for ARM64 Code Generator...\n");

  test_gen_funcdef();
  test_gen_funcall();
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
  test_gen_block();

  printf("OK: All unit tests passed!\n");
  return 0;
}
