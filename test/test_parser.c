#include "../src/parser/parser.h"
#include "../src/tokenizer/tokenizer.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_common.h"

// 文字列テーブル (parser.c で定義)
extern string_lit_t *string_literals;

static void test_expr_number() {
  printf("test_expr_number...\n");
  token = tokenize("42");
  node_t *node = expr();
  ASSERT_EQ(ND_NUM, node->kind);
  ASSERT_EQ(42, node->val);
}

static void test_expr_add_sub() {
  printf("test_expr_add_sub...\n");
  token = tokenize("1 + 2 - 3");
  node_t *node = expr(); // (1+2)-3

  ASSERT_EQ(ND_SUB, node->kind);
  ASSERT_EQ(ND_ADD, node->lhs->kind);
  ASSERT_EQ(1, node->lhs->lhs->val);
  ASSERT_EQ(2, node->lhs->rhs->val);
  ASSERT_EQ(3, node->rhs->val);
}

static void test_expr_mul_div() {
  printf("test_expr_mul_div...\n");
  token = tokenize("1 * 2 / 3");
  node_t *node = expr(); // (1*2)/3

  ASSERT_EQ(ND_DIV, node->kind);
  ASSERT_EQ(ND_MUL, node->lhs->kind);
  ASSERT_EQ(1, node->lhs->lhs->val);
  ASSERT_EQ(2, node->lhs->rhs->val);
  ASSERT_EQ(3, node->rhs->val);
}

static void test_expr_precedence() {
  printf("test_expr_precedence...\n");
  token = tokenize("1 + 2 * 3");
  node_t *node = expr(); // 1+(2*3)

  ASSERT_EQ(ND_ADD, node->kind);
  ASSERT_EQ(1, node->lhs->val);
  ASSERT_EQ(ND_MUL, node->rhs->kind);
  ASSERT_EQ(2, node->rhs->lhs->val);
  ASSERT_EQ(3, node->rhs->rhs->val);
}

static void test_expr_parentheses() {
  printf("test_expr_parentheses...\n");
  token = tokenize("(1 + 2) * 3");
  node_t *node = expr(); // (1+2)*3

  ASSERT_EQ(ND_MUL, node->kind);
  ASSERT_EQ(ND_ADD, node->lhs->kind);
  ASSERT_EQ(1, node->lhs->lhs->val);
  ASSERT_EQ(2, node->lhs->rhs->val);
  ASSERT_EQ(3, node->rhs->val);
}

static void test_expr_eq_neq() {
  printf("test_expr_eq_neq...\n");
  token = tokenize("1 == 2 != 3");
  node_t *node = expr(); // (1==2)!=3

  ASSERT_EQ(ND_NE, node->kind);
  ASSERT_EQ(ND_EQ, node->lhs->kind);
  ASSERT_EQ(1, node->lhs->lhs->val);
  ASSERT_EQ(2, node->lhs->rhs->val);
  ASSERT_EQ(3, node->rhs->val);
}

static void test_expr_relational() {
  printf("test_expr_relational...\n");
  token = tokenize("1 < 2 <= 3 > 4 >= 5");
  node_t *node = expr();

  // ルートは ND_LE (>= が反転)
  ASSERT_EQ(ND_LE, node->kind);
  ASSERT_EQ(5, node->lhs->val); // 5が左辺
  // > が反転 → ND_LT
  ASSERT_EQ(ND_LT, node->rhs->kind);
  ASSERT_EQ(4, node->rhs->lhs->val); // 4が左辺
  // <=
  ASSERT_EQ(ND_LE, node->rhs->rhs->kind);
  ASSERT_EQ(3, node->rhs->rhs->rhs->val);
  // <
  ASSERT_EQ(ND_LT, node->rhs->rhs->lhs->kind);
  ASSERT_EQ(1, node->rhs->rhs->lhs->lhs->val);
  ASSERT_EQ(2, node->rhs->rhs->lhs->rhs->val);
}

static void test_expr_assign() {
  printf("test_expr_assign...\n");
  token = tokenize("a = 3");
  node_t *node = expr();

  ASSERT_EQ(ND_ASSIGN, node->kind);
  ASSERT_EQ(ND_LVAR, node->lhs->kind);
  ASSERT_EQ(8, node->lhs->offset);
  ASSERT_EQ(ND_NUM, node->rhs->kind);
  ASSERT_EQ(3, node->rhs->val);
}

static void test_program_funcdef() {
  printf("test_program_funcdef...\n");
  token = tokenize("main() { a=1; b=2; a+b; }");
  program();

  ASSERT_TRUE(code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, code[0]->kind);
  ASSERT_EQ(0, code[0]->nargs);

  node_t *body = code[0]->rhs;
  ASSERT_EQ(ND_BLOCK, body->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(8, body->body[0]->lhs->offset);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(16, body->body[1]->lhs->offset);
  ASSERT_EQ(ND_ADD, body->body[2]->kind);
  ASSERT_TRUE(body->body[3] == NULL);
  ASSERT_TRUE(code[1] == NULL);
}

static void test_funcall() {
  printf("test_funcall...\n");
  token = tokenize("add(1, 2)");
  node_t *node = expr();

  ASSERT_EQ(ND_FUNCALL, node->kind);
  ASSERT_EQ(2, node->nargs);
  ASSERT_EQ(1, node->args[0]->val);
  ASSERT_EQ(2, node->args[1]->val);
}

// --- ここから追加テスト ---

static void test_funcdef_with_params() {
  printf("test_funcdef_with_params...\n");
  token = tokenize("int add(int a, int b) { return a+b; }");
  program();

  ASSERT_EQ(ND_FUNCDEF, code[0]->kind);
  ASSERT_EQ(2, code[0]->nargs);
  ASSERT_EQ(ND_LVAR, code[0]->args[0]->kind);
  ASSERT_EQ(ND_LVAR, code[0]->args[1]->kind);
}

static void test_stmt_if() {
  printf("test_stmt_if...\n");
  token = tokenize("main() { if (1) 2; }");
  program();
  node_t *body = code[0]->rhs;
  node_t *if_node = body->body[0];

  ASSERT_EQ(ND_IF, if_node->kind);
  ASSERT_EQ(ND_NUM, if_node->lhs->kind);  // 条件: 1
  ASSERT_EQ(1, if_node->lhs->val);
  ASSERT_EQ(ND_NUM, if_node->rhs->kind);  // then: 2
  ASSERT_EQ(2, if_node->rhs->val);
  ASSERT_TRUE(if_node->els == NULL);       // else なし
}

static void test_stmt_if_else() {
  printf("test_stmt_if_else...\n");
  token = tokenize("main() { if (1) 2; else 3; }");
  program();
  node_t *if_node = code[0]->rhs->body[0];

  ASSERT_EQ(ND_IF, if_node->kind);
  ASSERT_EQ(1, if_node->lhs->val);        // 条件
  ASSERT_EQ(2, if_node->rhs->val);        // then
  ASSERT_EQ(ND_NUM, if_node->els->kind);  // else
  ASSERT_EQ(3, if_node->els->val);
}

static void test_stmt_while() {
  printf("test_stmt_while...\n");
  token = tokenize("main() { while (1) 2; }");
  program();
  node_t *wh = code[0]->rhs->body[0];

  ASSERT_EQ(ND_WHILE, wh->kind);
  ASSERT_EQ(1, wh->lhs->val);   // 条件
  ASSERT_EQ(2, wh->rhs->val);   // ループ本体
}

static void test_stmt_for() {
  printf("test_stmt_for...\n");
  token = tokenize("main() { for (a=0; a<10; a=a+1) a; }");
  program();
  node_t *fr = code[0]->rhs->body[0];

  ASSERT_EQ(ND_FOR, fr->kind);
  ASSERT_EQ(ND_ASSIGN, fr->init->kind);  // init: a=0
  ASSERT_EQ(ND_LT, fr->lhs->kind);      // 条件: a<10
  ASSERT_EQ(ND_ASSIGN, fr->inc->kind);   // inc: a=a+1
  ASSERT_EQ(ND_LVAR, fr->rhs->kind);     // 本体: a
}

static void test_stmt_return() {
  printf("test_stmt_return...\n");
  token = tokenize("main() { return 42; }");
  program();
  node_t *ret = code[0]->rhs->body[0];

  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_NUM, ret->lhs->kind);
  ASSERT_EQ(42, ret->lhs->val);
}

static void test_stmt_block() {
  printf("test_stmt_block...\n");
  token = tokenize("main() { { 1; 2; } }");
  program();
  node_t *blk = code[0]->rhs->body[0];

  ASSERT_EQ(ND_BLOCK, blk->kind);
  ASSERT_EQ(ND_NUM, blk->body[0]->kind);
  ASSERT_EQ(1, blk->body[0]->val);
  ASSERT_EQ(ND_NUM, blk->body[1]->kind);
  ASSERT_EQ(2, blk->body[1]->val);
  ASSERT_TRUE(blk->body[2] == NULL);
}

static void test_expr_deref_addr() {
  printf("test_expr_deref_addr...\n");
  // &a
  token = tokenize("&a");
  node_t *addr = expr();
  ASSERT_EQ(ND_ADDR, addr->kind);
  ASSERT_EQ(ND_LVAR, addr->lhs->kind);

  // *p (p が変数として存在する前提)
  token = tokenize("*a");
  node_t *deref = expr();
  ASSERT_EQ(ND_DEREF, deref->kind);
  ASSERT_EQ(ND_LVAR, deref->lhs->kind);
}

static void test_expr_string() {
  printf("test_expr_string...\n");
  string_literals = NULL; // リセット
  token = tokenize("\"hello\"");
  node_t *node = expr();

  ASSERT_EQ(ND_STRING, node->kind);
  ASSERT_TRUE(node->string_label != NULL);
  // 文字列テーブルに登録されている
  ASSERT_TRUE(string_literals != NULL);
  ASSERT_EQ(5, string_literals->len);
  ASSERT_TRUE(strncmp(string_literals->str, "hello", 5) == 0);
}

static void test_type_decl() {
  printf("test_type_decl...\n");
  // int x = 5; → ND_ASSIGN
  token = tokenize("main() { int x = 5; }");
  program();
  node_t *stmt = code[0]->rhs->body[0];
  ASSERT_EQ(ND_ASSIGN, stmt->kind);
  ASSERT_EQ(ND_LVAR, stmt->lhs->kind);
  ASSERT_EQ(5, stmt->rhs->val);

  // int x; → ND_NUM(0) ダミー
  token = tokenize("main() { int x; }");
  program();
  stmt = code[0]->rhs->body[0];
  ASSERT_EQ(ND_NUM, stmt->kind);
  ASSERT_EQ(0, stmt->val);
}

static void test_multiple_funcdefs() {
  printf("test_multiple_funcdefs...\n");
  token = tokenize("foo() { 1; } bar() { 2; }");
  program();

  ASSERT_TRUE(code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, code[0]->kind);
  ASSERT_TRUE(strncmp(code[0]->funcname, "foo", 3) == 0);

  ASSERT_TRUE(code[1] != NULL);
  ASSERT_EQ(ND_FUNCDEF, code[1]->kind);
  ASSERT_TRUE(strncmp(code[1]->funcname, "bar", 3) == 0);

  ASSERT_TRUE(code[2] == NULL);
}

int main() {
  printf("Running tests for Parser...\n");

  test_expr_number();
  test_expr_add_sub();
  test_expr_mul_div();
  test_expr_precedence();
  test_expr_parentheses();
  test_expr_eq_neq();
  test_expr_relational();
  test_expr_assign();
  test_program_funcdef();
  test_funcall();
  test_funcdef_with_params();
  test_stmt_if();
  test_stmt_if_else();
  test_stmt_while();
  test_stmt_for();
  test_stmt_return();
  test_stmt_block();
  test_expr_deref_addr();
  test_expr_string();
  test_type_decl();
  test_multiple_funcdefs();

  printf("OK: All unit tests passed!\n");
  return 0;
}
