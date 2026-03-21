#include "../src/codegen_backend.h"
#include "../src/parser/parser.h"
#include "test_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// test_codegen は parser.o をリンクしないため、string_literals のダミー定義が必要
string_lit_t *string_literals = NULL;
float_lit_t *float_literals = NULL;
global_var_t *global_vars = NULL;

static node_num_t *as_num(node_t *n) { return (node_num_t *)n; }

typedef struct {
  char *buf;
  size_t len;
  size_t cap;
} capture_buf_t;

static capture_buf_t g_capture;

static void capture_line(const char *line, size_t len, void *user_data) {
  capture_buf_t *cap = (capture_buf_t *)user_data;
  size_t required = cap->len + len + 1;
  if (required > cap->cap) {
    size_t new_cap = cap->cap ? cap->cap : 1024;
    while (new_cap < required) {
      new_cap *= 2;
    }
    char *new_buf = realloc(cap->buf, new_cap);
    if (!new_buf) {
      fprintf(stderr, "capture realloc failed\n");
      exit(1);
    }
    cap->buf = new_buf;
    cap->cap = new_cap;
  }
  memcpy(cap->buf + cap->len, line, len);
  cap->len += len;
  cap->buf[cap->len] = '\0';
}

static void capture_start(void) {
  g_capture.len = 0;
  if (!g_capture.buf) {
    g_capture.cap = 1024;
    g_capture.buf = calloc(g_capture.cap, 1);
    if (!g_capture.buf) {
      fprintf(stderr, "capture calloc failed\n");
      exit(1);
    }
  }
  g_capture.buf[0] = '\0';
  gen_set_output_callback(capture_line, &g_capture);
}

static char *capture_end(void) {
  gen_set_output_callback(NULL, NULL);
  return strdup(g_capture.buf ? g_capture.buf : "");
}

// ヘルパー: ASTリーフ（整数ノード）を作成
static node_t *make_num(int val) {
  node_num_t *n = calloc(1, sizeof(node_num_t));
  n->base.kind = ND_NUM;
  n->val = val;
  return (node_t *)n;
}

// ヘルパー: 二項演算ノードを作成
static node_t *make_binop(node_kind_t kind, node_t *lhs, node_t *rhs) {
  if (kind == ND_ASSIGN) {
    node_mem_t *n = calloc(1, sizeof(node_mem_t));
    n->base.kind = kind;
    n->base.lhs = lhs;
    n->base.rhs = rhs;
    n->type_size = 8;
    return (node_t *)n;
  }
  node_t *n = calloc(1, sizeof(node_t));
  n->kind = kind;
  n->lhs = lhs;
  n->rhs = rhs;
  return n;
}

// ヘルパー: ローカル変数ノードを作成
static node_t *make_lvar(int offset) {
  node_lvar_t *n = calloc(1, sizeof(node_lvar_t));
  n->mem.base.kind = ND_LVAR;
  n->offset = offset;
  n->mem.type_size = 8;
  return (node_t *)n;
}

// --- テストケース ---

static void test_gen_funcdef(void) {
  printf("test_gen_funcdef...\n");
  node_func_t *n = calloc(1, sizeof(node_func_t));
  n->base.kind = ND_FUNCDEF;
  n->funcname = "foo";
  n->funcname_len = 3;
  n->nargs = 0;
  node_block_t *body = calloc(1, sizeof(node_block_t));
  body->base.kind = ND_BLOCK;
  body->body = calloc(2, sizeof(node_t*));
  body->body[0] = make_num(42);
  body->body[1] = NULL;
  n->base.rhs = (node_t *)body;

  capture_start();
  gen((node_t *)n);
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
  node_func_t *n = calloc(1, sizeof(node_func_t));
  n->base.kind = ND_FUNCALL;
  n->funcname = "bar";
  n->funcname_len = 3;
  n->nargs = 2;
  n->args = calloc(2, sizeof(node_t*));
  n->args[0] = make_num(1);
  n->args[1] = make_num(2);

  capture_start();
  gen((node_t *)n);
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

static void test_gen_fadd(void) {
  printf("test_gen_fadd...\n");
  node_t *lhs = make_num(3);
  lhs->fp_kind = TK_FLOAT_KIND_FLOAT;
  as_num(lhs)->fval_id = 1;
  node_t *rhs = make_num(4);
  rhs->fp_kind = TK_FLOAT_KIND_FLOAT;
  as_num(rhs)->fval_id = 2;
  node_t *n = make_binop(ND_ADD, lhs, rhs);
  n->fp_kind = TK_FLOAT_KIND_FLOAT;
  
  capture_start();
  gen(n);
  char *out = capture_end();
  ASSERT_TRUE(strstr(out, "fadd s0, s0, s1") != NULL);
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
  node_ctrl_t *n = calloc(1, sizeof(node_ctrl_t));
  n->base.kind = ND_IF;
  n->base.lhs = make_num(1);
  n->base.rhs = make_num(42);
  n->els = make_num(0);
  capture_start();
  gen((node_t *)n);
  char *out = capture_end();
  ASSERT_TRUE(strstr(out, "cbz x0") != NULL);
  ASSERT_TRUE(strstr(out, ".Lelse") != NULL);
  free(out);
}

static void test_gen_while(void) {
  printf("test_gen_while...\n");
  node_ctrl_t *n = calloc(1, sizeof(node_ctrl_t));
  n->base.kind = ND_WHILE;
  n->base.lhs = make_num(0);
  n->base.rhs = make_num(1);
  capture_start();
  gen((node_t *)n);
  char *out = capture_end();
  ASSERT_TRUE(strstr(out, ".Lbegin") != NULL);
  ASSERT_TRUE(strstr(out, "cbz x0") != NULL);
  free(out);
}

static void test_gen_for(void) {
  printf("test_gen_for...\n");
  node_ctrl_t *n = calloc(1, sizeof(node_ctrl_t));
  n->base.kind = ND_FOR;
  n->init = make_num(0);
  n->base.lhs = make_num(1);
  n->inc = make_num(1);
  n->base.rhs = make_num(99);
  capture_start();
  gen((node_t *)n);
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
  node_block_t *n = calloc(1, sizeof(node_block_t));
  n->base.kind = ND_BLOCK;
  n->body = calloc(3, sizeof(node_t*));
  n->body[0] = make_num(1);
  n->body[1] = make_num(2);
  n->body[2] = NULL;
  capture_start();
  gen((node_t *)n);
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
  test_gen_fadd();
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
