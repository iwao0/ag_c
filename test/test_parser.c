#include "../src/parser/parser.h"
#include "../src/tokenizer/tokenizer.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "test_common.h"

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
  node_t *node = expr(); // (1+2)-3 のはず

  ASSERT_EQ(ND_SUB, node->kind);

  node_t *lhs = node->lhs; // 1+2
  node_t *rhs = node->rhs; // 3

  ASSERT_EQ(ND_ADD, lhs->kind);
  ASSERT_EQ(ND_NUM, lhs->lhs->kind);
  ASSERT_EQ(1, lhs->lhs->val);
  ASSERT_EQ(ND_NUM, lhs->rhs->kind);
  ASSERT_EQ(2, lhs->rhs->val);

  ASSERT_EQ(ND_NUM, rhs->kind);
  ASSERT_EQ(3, rhs->val);
}

static void test_expr_mul_div() {
  printf("test_expr_mul_div...\n");
  token = tokenize("1 * 2 / 3");
  node_t *node = expr(); // (1*2)/3 のはず

  ASSERT_EQ(ND_DIV, node->kind);

  node_t *lhs = node->lhs; // 1*2
  node_t *rhs = node->rhs; // 3

  ASSERT_EQ(ND_MUL, lhs->kind);
  ASSERT_EQ(ND_NUM, lhs->lhs->kind);
  ASSERT_EQ(1, lhs->lhs->val);
  ASSERT_EQ(ND_NUM, lhs->rhs->kind);
  ASSERT_EQ(2, lhs->rhs->val);

  ASSERT_EQ(ND_NUM, rhs->kind);
  ASSERT_EQ(3, rhs->val);
}

static void test_expr_precedence() {
  printf("test_expr_precedence...\n");
  token = tokenize("1 + 2 * 3");
  node_t *node = expr(); // 1+(2*3) のはず

  ASSERT_EQ(ND_ADD, node->kind);

  node_t *lhs = node->lhs; // 1
  node_t *rhs = node->rhs; // 2*3

  ASSERT_EQ(ND_NUM, lhs->kind);
  ASSERT_EQ(1, lhs->val);

  ASSERT_EQ(ND_MUL, rhs->kind);
  ASSERT_EQ(ND_NUM, rhs->lhs->kind);
  ASSERT_EQ(2, rhs->lhs->val);
  ASSERT_EQ(ND_NUM, rhs->rhs->kind);
  ASSERT_EQ(3, rhs->rhs->val);
}

static void test_expr_parentheses() {
  printf("test_expr_parentheses...\n");
  token = tokenize("(1 + 2) * 3");
  node_t *node = expr(); // (1+2)*3 のはず

  ASSERT_EQ(ND_MUL, node->kind);

  node_t *lhs = node->lhs; // 1+2
  node_t *rhs = node->rhs; // 3

  ASSERT_EQ(ND_ADD, lhs->kind);
  ASSERT_EQ(ND_NUM, lhs->lhs->kind);
  ASSERT_EQ(1, lhs->lhs->val);
  ASSERT_EQ(ND_NUM, lhs->rhs->kind);
  ASSERT_EQ(2, lhs->rhs->val);

  ASSERT_EQ(ND_NUM, rhs->kind);
  ASSERT_EQ(3, rhs->val);
}

static void test_expr_eq_neq() {
  printf("test_expr_eq_neq...\n");
  token = tokenize("1 == 2 != 3");
  node_t *node = expr(); // (1==2)!=3 のはず

  ASSERT_EQ(ND_NE, node->kind);

  node_t *lhs = node->lhs; // 1==2
  node_t *rhs = node->rhs; // 3

  ASSERT_EQ(ND_EQ, lhs->kind);
  ASSERT_EQ(ND_NUM, lhs->lhs->kind);
  ASSERT_EQ(1, lhs->lhs->val);
  ASSERT_EQ(ND_NUM, lhs->rhs->kind);
  ASSERT_EQ(2, lhs->rhs->val);

  ASSERT_EQ(ND_NUM, rhs->kind);
  ASSERT_EQ(3, rhs->val);
}

static void test_expr_relational() {
  printf("test_expr_relational...\n");
  token = tokenize("1 < 2 <= 3 > 4 >= 5");
  // (((1<2)<=3)>4)>=5 は
  // (((1<2)<=3)>4) => 5 となり、 > と >= は左右反転して < と <=
  // に置き換えられるため： 5 <= ( 4 < ((1<2)<=3) )
  // のようにパースされる（実装予定の仕様に基づく）
  node_t *node = expr();

  // 最終的なルートは `<=` (>=が反転したもの)
  ASSERT_EQ(ND_LE, node->kind);
  ASSERT_EQ(ND_NUM, node->lhs->kind);
  ASSERT_EQ(5, node->lhs->val); // 5が左辺にくる

  node_t *node_gt = node->rhs; // 4 < ((1<2)<=3) (元の>が反転したもの)
  ASSERT_EQ(ND_LT, node_gt->kind);
  ASSERT_EQ(ND_NUM, node_gt->lhs->kind);
  ASSERT_EQ(4, node_gt->lhs->val); // 4が左辺にくる

  node_t *node_le = node_gt->rhs; // (1<2)<=3
  ASSERT_EQ(ND_LE, node_le->kind);
  ASSERT_EQ(ND_NUM, node_le->rhs->kind);
  ASSERT_EQ(3, node_le->rhs->val);

  node_t *node_lt = node_le->lhs; // 1<2
  ASSERT_EQ(ND_LT, node_lt->kind);
  ASSERT_EQ(ND_NUM, node_lt->lhs->kind);
  ASSERT_EQ(1, node_lt->lhs->val);
  ASSERT_EQ(ND_NUM, node_lt->rhs->kind);
  ASSERT_EQ(2, node_lt->rhs->val);
}

static void test_expr_assign() {
  printf("test_expr_assign...\n");
  token = tokenize("a = 3");
  node_t *node = expr(); // a=3 のはず

  ASSERT_EQ(ND_ASSIGN, node->kind);
  ASSERT_EQ(ND_LVAR, node->lhs->kind);
  ASSERT_EQ(8, node->lhs->offset); // 'a' = offset 8
  ASSERT_EQ(ND_NUM, node->rhs->kind);
  ASSERT_EQ(3, node->rhs->val);
}

static void test_program_multi_stmt() {
  printf("test_program_multi_stmt...\n");
  token = tokenize("a=1; b=2; a+b;");
  program();

  // 1文目: a=1
  ASSERT_TRUE(code[0] != NULL);
  ASSERT_EQ(ND_ASSIGN, code[0]->kind);
  ASSERT_EQ(ND_LVAR, code[0]->lhs->kind);
  ASSERT_EQ(8, code[0]->lhs->offset); // a
  ASSERT_EQ(ND_NUM, code[0]->rhs->kind);
  ASSERT_EQ(1, code[0]->rhs->val);

  // 2文目: b=2
  ASSERT_TRUE(code[1] != NULL);
  ASSERT_EQ(ND_ASSIGN, code[1]->kind);
  ASSERT_EQ(ND_LVAR, code[1]->lhs->kind);
  ASSERT_EQ(16, code[1]->lhs->offset); // b
  ASSERT_EQ(ND_NUM, code[1]->rhs->kind);
  ASSERT_EQ(2, code[1]->rhs->val);

  // 3文目: a+b
  ASSERT_TRUE(code[2] != NULL);
  ASSERT_EQ(ND_ADD, code[2]->kind);
  ASSERT_EQ(ND_LVAR, code[2]->lhs->kind);
  ASSERT_EQ(8, code[2]->lhs->offset); // a
  ASSERT_EQ(ND_LVAR, code[2]->rhs->kind);
  ASSERT_EQ(16, code[2]->rhs->offset); // b

  // 終端
  ASSERT_TRUE(code[3] == NULL);
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
  test_program_multi_stmt();

  printf("OK: All unit tests passed!\n");
  return 0;
}
