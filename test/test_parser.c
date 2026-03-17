#include "../src/parser/parser.h"
#include "../src/tokenizer/tokenizer.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>

#include "test_common.h"

// 文字列テーブル (parser.c で定義)
extern string_lit_t *string_literals;

static node_num_t *as_num(node_t *n) { return (node_num_t *)n; }
static node_lvar_t *as_lvar(node_t *n) { return (node_lvar_t *)n; }
static node_func_t *as_func(node_t *n) { return (node_func_t *)n; }
static node_block_t *as_block(node_t *n) { return (node_block_t *)n; }
static node_ctrl_t *as_ctrl(node_t *n) { return (node_ctrl_t *)n; }
static node_string_t *as_string(node_t *n) { return (node_string_t *)n; }
static node_case_t *as_case(node_t *n) { return (node_case_t *)n; }

static void expect_parse_fail(const char *input) {
  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    token = tk_tokenize((char *)input);
    program();
    _exit(0);
  }
  int status;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_TRUE(WEXITSTATUS(status) != 0);
}

static void test_expr_number() {
  printf("test_expr_number...\n");
  token = tk_tokenize("42");
  node_t *node = expr();
  ASSERT_EQ(ND_NUM, node->kind);
  ASSERT_EQ(42, as_num(node)->val);
  ASSERT_EQ(TK_EOF, token->kind);
}

static void test_expr_float() {
  printf("test_expr_float...\n");
  token = tk_tokenize("3.14 + 1.5f");
  node_t *node = expr();
  
  ASSERT_EQ(ND_ADD, node->kind);
  ASSERT_EQ(ND_NUM, node->lhs->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, node->lhs->fp_kind);
  ASSERT_TRUE(as_num(node->lhs)->fval > 3.13 && as_num(node->lhs)->fval < 3.15);
  
  ASSERT_EQ(ND_NUM, node->rhs->kind);
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT, node->rhs->fp_kind);
  ASSERT_TRUE(as_num(node->rhs)->fval > 1.49 && as_num(node->rhs)->fval < 1.51);
}

static void test_expr_long_double_suffix_metadata() {
  printf("test_expr_long_double_suffix_metadata...\n");
  token = tk_tokenize("4.0L");
  node_t *node = expr();

  ASSERT_EQ(ND_NUM, node->kind);
  ASSERT_EQ(TK_FLOAT_KIND_LONG_DOUBLE, node->fp_kind);
  ASSERT_EQ(TK_FLOAT_SUFFIX_L, as_num(node)->float_suffix_kind);
  ASSERT_TRUE(as_num(node)->fval > 3.9 && as_num(node)->fval < 4.1);

  bool found = false;
  for (float_lit_t *lit = float_literals; lit; lit = lit->next) {
    if (lit->float_suffix_kind == TK_FLOAT_SUFFIX_L) {
      found = true;
      break;
    }
  }
  ASSERT_TRUE(found);
}

static void test_expr_add_sub() {
  printf("test_expr_add_sub...\n");
  token = tk_tokenize("1 + 2 - 3");
  node_t *node = expr(); // (1+2)-3

  ASSERT_EQ(ND_SUB, node->kind);
  ASSERT_EQ(ND_ADD, node->lhs->kind);
  ASSERT_EQ(1, as_num(node->lhs->lhs)->val);
  ASSERT_EQ(2, as_num(node->lhs->rhs)->val);
  ASSERT_EQ(3, as_num(node->rhs)->val);
}

static void test_expr_mul_div() {
  printf("test_expr_mul_div...\n");
  token = tk_tokenize("1 * 2 / 3");
  node_t *node = expr(); // (1*2)/3

  ASSERT_EQ(ND_DIV, node->kind);
  ASSERT_EQ(ND_MUL, node->lhs->kind);
  ASSERT_EQ(1, as_num(node->lhs->lhs)->val);
  ASSERT_EQ(2, as_num(node->lhs->rhs)->val);
  ASSERT_EQ(3, as_num(node->rhs)->val);
}

static void test_expr_mod() {
  printf("test_expr_mod...\n");
  token = tk_tokenize("10 % 3");
  node_t *node = expr();

  ASSERT_EQ(ND_MOD, node->kind);
  ASSERT_EQ(10, as_num(node->lhs)->val);
  ASSERT_EQ(3, as_num(node->rhs)->val);
}

static void test_expr_precedence() {
  printf("test_expr_precedence...\n");
  token = tk_tokenize("1 + 2 * 3");
  node_t *node = expr(); // 1+(2*3)

  ASSERT_EQ(ND_ADD, node->kind);
  ASSERT_EQ(1, as_num(node->lhs)->val);
  ASSERT_EQ(ND_MUL, node->rhs->kind);
  ASSERT_EQ(2, as_num(node->rhs->lhs)->val);
  ASSERT_EQ(3, as_num(node->rhs->rhs)->val);
}

static void test_expr_parentheses() {
  printf("test_expr_parentheses...\n");
  token = tk_tokenize("(1 + 2) * 3");
  node_t *node = expr(); // (1+2)*3

  ASSERT_EQ(ND_MUL, node->kind);
  ASSERT_EQ(ND_ADD, node->lhs->kind);
  ASSERT_EQ(1, as_num(node->lhs->lhs)->val);
  ASSERT_EQ(2, as_num(node->lhs->rhs)->val);
  ASSERT_EQ(3, as_num(node->rhs)->val);
}

static void test_expr_eq_neq() {
  printf("test_expr_eq_neq...\n");
  token = tk_tokenize("1 == 2 != 3");
  node_t *node = expr(); // (1==2)!=3

  ASSERT_EQ(ND_NE, node->kind);
  ASSERT_EQ(ND_EQ, node->lhs->kind);
  ASSERT_EQ(1, as_num(node->lhs->lhs)->val);
  ASSERT_EQ(2, as_num(node->lhs->rhs)->val);
  ASSERT_EQ(3, as_num(node->rhs)->val);
}

static void test_expr_relational() {
  printf("test_expr_relational...\n");
  token = tk_tokenize("1 < 2 <= 3 > 4 >= 5");
  node_t *node = expr();

  // ルートは ND_LE (>= が反転)
  ASSERT_EQ(ND_LE, node->kind);
  ASSERT_EQ(5, as_num(node->lhs)->val); // 5が左辺
  // > が反転 → ND_LT
  ASSERT_EQ(ND_LT, node->rhs->kind);
  ASSERT_EQ(4, as_num(node->rhs->lhs)->val); // 4が左辺
  // <=
  ASSERT_EQ(ND_LE, node->rhs->rhs->kind);
  ASSERT_EQ(3, as_num(node->rhs->rhs->rhs)->val);
  // <
  ASSERT_EQ(ND_LT, node->rhs->rhs->lhs->kind);
  ASSERT_EQ(1, as_num(node->rhs->rhs->lhs->lhs)->val);
  ASSERT_EQ(2, as_num(node->rhs->rhs->lhs->rhs)->val);
}

static void test_expr_logical_and_or() {
  printf("test_expr_logical_and_or...\n");

  token = tk_tokenize("1 && 0 || 3");
  node_t *node = expr();
  ASSERT_EQ(ND_LOGOR, node->kind);
  ASSERT_EQ(ND_LOGAND, node->lhs->kind);
  ASSERT_EQ(3, as_num(node->rhs)->val);
}

static void test_expr_bitwise() {
  printf("test_expr_bitwise...\n");
  token = tk_tokenize("1 | 2 ^ 3 & 4");
  node_t *node = expr();

  ASSERT_EQ(ND_BITOR, node->kind);
  ASSERT_EQ(1, as_num(node->lhs)->val);
  ASSERT_EQ(ND_BITXOR, node->rhs->kind);
  ASSERT_EQ(2, as_num(node->rhs->lhs)->val);
  ASSERT_EQ(ND_BITAND, node->rhs->rhs->kind);
}

static void test_expr_shift() {
  printf("test_expr_shift...\n");
  token = tk_tokenize("1 + 2 << 3 >> 1");
  node_t *node = expr();

  ASSERT_EQ(ND_SHR, node->kind);
  ASSERT_EQ(ND_SHL, node->lhs->kind);
  ASSERT_EQ(ND_ADD, node->lhs->lhs->kind);
  ASSERT_EQ(1, as_num(node->rhs)->val);
}

static void test_expr_ternary() {
  printf("test_expr_ternary...\n");
  token = tk_tokenize("1 ? 2 : 3 ? 4 : 5");
  node_t *node = expr();

  ASSERT_EQ(ND_TERNARY, node->kind);
  ASSERT_EQ(1, as_num(node->lhs)->val);
  ASSERT_EQ(2, as_num(node->rhs)->val);
  ASSERT_EQ(ND_TERNARY, as_ctrl(node)->els->kind); // 右結合
}

static void test_expr_unary_ops() {
  printf("test_expr_unary_ops...\n");

  token = tk_tokenize("+42");
  node_t *pos = expr();
  ASSERT_EQ(ND_NUM, pos->kind);
  ASSERT_EQ(42, as_num(pos)->val);

  token = tk_tokenize("-42");
  node_t *neg = expr();
  ASSERT_EQ(ND_SUB, neg->kind);
  ASSERT_EQ(ND_NUM, neg->lhs->kind);
  ASSERT_EQ(0, as_num(neg->lhs)->val);
  ASSERT_EQ(ND_NUM, neg->rhs->kind);
  ASSERT_EQ(42, as_num(neg->rhs)->val);

  token = tk_tokenize("!0");
  node_t *not0 = expr();
  ASSERT_EQ(ND_EQ, not0->kind);
  ASSERT_EQ(ND_NUM, not0->lhs->kind);
  ASSERT_EQ(0, as_num(not0->lhs)->val);
  ASSERT_EQ(ND_NUM, not0->rhs->kind);
  ASSERT_EQ(0, as_num(not0->rhs)->val);

  token = tk_tokenize("~5");
  node_t *bitnot = expr();
  ASSERT_EQ(ND_SUB, bitnot->kind);               // (~5) == ((0-5)-1)
  ASSERT_EQ(ND_SUB, bitnot->lhs->kind);
  ASSERT_EQ(1, as_num(bitnot->rhs)->val);
}

static void test_expr_sizeof() {
  printf("test_expr_sizeof...\n");

  token = tk_tokenize("sizeof(int)");
  node_t *n1 = expr();
  ASSERT_EQ(ND_NUM, n1->kind);
  ASSERT_EQ(4, as_num(n1)->val);

  token = tk_tokenize("sizeof(int*)");
  node_t *n2 = expr();
  ASSERT_EQ(ND_NUM, n2->kind);
  ASSERT_EQ(8, as_num(n2)->val);

  token = tk_tokenize("main() { int x; return sizeof(x); }");
  program();
  node_t *ret = as_block(as_func(code[0])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_NUM, ret->lhs->kind);
  ASSERT_EQ(4, as_num(ret->lhs)->val);

  token = tk_tokenize("(char)300");
  node_t *c1 = expr();
  ASSERT_EQ(ND_BITAND, c1->kind);
  ASSERT_EQ(0xff, as_num(c1->rhs)->val);
}

static void test_expr_inc_dec() {
  printf("test_expr_inc_dec...\n");

  token = tk_tokenize("++a");
  node_t *prei = expr();
  ASSERT_EQ(ND_PRE_INC, prei->kind);
  ASSERT_EQ(ND_LVAR, prei->lhs->kind);

  token = tk_tokenize("--a");
  node_t *pred = expr();
  ASSERT_EQ(ND_PRE_DEC, pred->kind);
  ASSERT_EQ(ND_LVAR, pred->lhs->kind);

  token = tk_tokenize("a++");
  node_t *posti = expr();
  ASSERT_EQ(ND_POST_INC, posti->kind);
  ASSERT_EQ(ND_LVAR, posti->lhs->kind);

  token = tk_tokenize("a--");
  node_t *postd = expr();
  ASSERT_EQ(ND_POST_DEC, postd->kind);
  ASSERT_EQ(ND_LVAR, postd->lhs->kind);
}

static void test_expr_assign() {
  printf("test_expr_assign...\n");
  token = tk_tokenize("a = 3");
  node_t *node = expr();

  ASSERT_EQ(ND_ASSIGN, node->kind);
  ASSERT_EQ(ND_LVAR, node->lhs->kind);
  ASSERT_TRUE(as_lvar(node->lhs)->offset > 0);
  ASSERT_EQ(ND_NUM, node->rhs->kind);
  ASSERT_EQ(3, as_num(node->rhs)->val);
}

static void test_expr_compound_assign() {
  printf("test_expr_compound_assign...\n");

  token = tk_tokenize("a += 3");
  node_t *add = expr();
  ASSERT_EQ(ND_ASSIGN, add->kind);
  ASSERT_EQ(ND_ADD, add->rhs->kind);

  token = tk_tokenize("a -= 3");
  node_t *sub = expr();
  ASSERT_EQ(ND_ASSIGN, sub->kind);
  ASSERT_EQ(ND_SUB, sub->rhs->kind);

  token = tk_tokenize("a *= 3");
  node_t *mul = expr();
  ASSERT_EQ(ND_ASSIGN, mul->kind);
  ASSERT_EQ(ND_MUL, mul->rhs->kind);

  token = tk_tokenize("a /= 3");
  node_t *div = expr();
  ASSERT_EQ(ND_ASSIGN, div->kind);
  ASSERT_EQ(ND_DIV, div->rhs->kind);

  token = tk_tokenize("a %= 3");
  node_t *mod = expr();
  ASSERT_EQ(ND_ASSIGN, mod->kind);
  ASSERT_EQ(ND_MOD, mod->rhs->kind);

  token = tk_tokenize("a <<= 3");
  node_t *shl = expr();
  ASSERT_EQ(ND_ASSIGN, shl->kind);
  ASSERT_EQ(ND_SHL, shl->rhs->kind);

  token = tk_tokenize("a >>= 3");
  node_t *shr = expr();
  ASSERT_EQ(ND_ASSIGN, shr->kind);
  ASSERT_EQ(ND_SHR, shr->rhs->kind);

  token = tk_tokenize("a &= 3");
  node_t *band = expr();
  ASSERT_EQ(ND_ASSIGN, band->kind);
  ASSERT_EQ(ND_BITAND, band->rhs->kind);

  token = tk_tokenize("a ^= 3");
  node_t *bxor = expr();
  ASSERT_EQ(ND_ASSIGN, bxor->kind);
  ASSERT_EQ(ND_BITXOR, bxor->rhs->kind);

  token = tk_tokenize("a |= 3");
  node_t *bor = expr();
  ASSERT_EQ(ND_ASSIGN, bor->kind);
  ASSERT_EQ(ND_BITOR, bor->rhs->kind);
}

static void test_expr_comma() {
  printf("test_expr_comma...\n");

  token = tk_tokenize("a=1, b=2, a+b");
  node_t *node = expr();
  ASSERT_EQ(ND_COMMA, node->kind);
  ASSERT_EQ(ND_COMMA, node->lhs->kind);
  ASSERT_EQ(ND_ADD, node->rhs->kind);
}

static void test_program_funcdef() {
  printf("test_program_funcdef...\n");
  token = tk_tokenize("main() { a=1; b=2; a+b; }");
  program();

  ASSERT_TRUE(code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, code[0]->kind);
  ASSERT_EQ(0, as_func(code[0])->nargs);

  node_t *body = as_func(code[0])->base.rhs;
  ASSERT_EQ(ND_BLOCK, body->kind);
  ASSERT_EQ(ND_ASSIGN, as_block(body)->body[0]->kind);
  ASSERT_EQ(8, as_lvar(as_block(body)->body[0]->lhs)->offset);
  ASSERT_EQ(ND_ASSIGN, as_block(body)->body[1]->kind);
  ASSERT_EQ(16, as_lvar(as_block(body)->body[1]->lhs)->offset);
  ASSERT_EQ(ND_ADD, as_block(body)->body[2]->kind);
  ASSERT_TRUE(as_block(body)->body[3] == NULL);
  ASSERT_TRUE(code[1] == NULL);
}

static void test_funcall() {
  printf("test_funcall...\n");
  token = tk_tokenize("add(1, 2)");
  node_t *node = expr();

  ASSERT_EQ(ND_FUNCALL, node->kind);
  ASSERT_EQ(2, as_func(node)->nargs);
  ASSERT_EQ(1, as_num(as_func(node)->args[0])->val);
  ASSERT_EQ(2, as_num(as_func(node)->args[1])->val);

  token = tk_tokenize("foo((1,2), 3)");
  node = expr();
  ASSERT_EQ(ND_FUNCALL, node->kind);
  ASSERT_EQ(2, as_func(node)->nargs);
  ASSERT_EQ(ND_COMMA, as_func(node)->args[0]->kind);
  ASSERT_EQ(ND_NUM, as_func(node)->args[1]->kind);
  ASSERT_EQ(3, as_num(as_func(node)->args[1])->val);
}

// --- ここから追加テスト ---

static void test_funcdef_with_params() {
  printf("test_funcdef_with_params...\n");
  token = tk_tokenize("int add(int a, int b) { return a+b; }");
  program();

  ASSERT_EQ(ND_FUNCDEF, code[0]->kind);
  ASSERT_EQ(2, as_func(code[0])->nargs);
  ASSERT_EQ(ND_LVAR, as_func(code[0])->args[0]->kind);
  ASSERT_EQ(ND_LVAR, as_func(code[0])->args[1]->kind);
}

static void test_stmt_if() {
  printf("test_stmt_if...\n");
  token = tk_tokenize("main() { if (1) 2; }");
  program();
  node_t *body = as_func(code[0])->base.rhs;
  node_t *if_node = as_block(body)->body[0];

  ASSERT_EQ(ND_IF, if_node->kind);
  ASSERT_EQ(ND_NUM, if_node->lhs->kind);  // 条件: 1
  ASSERT_EQ(1, as_num(if_node->lhs)->val);
  ASSERT_EQ(ND_NUM, if_node->rhs->kind);  // then: 2
  ASSERT_EQ(2, as_num(if_node->rhs)->val);
  ASSERT_TRUE(as_ctrl(if_node)->els == NULL);       // else なし
}

static void test_stmt_if_else() {
  printf("test_stmt_if_else...\n");
  token = tk_tokenize("main() { if (1) 2; else 3; }");
  program();
  node_t *if_node = as_block(as_func(code[0])->base.rhs)->body[0];

  ASSERT_EQ(ND_IF, if_node->kind);
  ASSERT_EQ(1, as_num(if_node->lhs)->val);        // 条件
  ASSERT_EQ(2, as_num(if_node->rhs)->val);        // then
  ASSERT_EQ(ND_NUM, as_ctrl(if_node)->els->kind);  // else
  ASSERT_EQ(3, as_num(as_ctrl(if_node)->els)->val);
}

static void test_stmt_while() {
  printf("test_stmt_while...\n");
  token = tk_tokenize("main() { while (1) 2; }");
  program();
  node_t *wh = as_block(as_func(code[0])->base.rhs)->body[0];

  ASSERT_EQ(ND_WHILE, wh->kind);
  ASSERT_EQ(1, as_num(wh->lhs)->val);   // 条件
  ASSERT_EQ(2, as_num(wh->rhs)->val);   // ループ本体
}

static void test_stmt_do_while() {
  printf("test_stmt_do_while...\n");
  token = tk_tokenize("main() { do a=a+1; while (a<3); }");
  program();
  node_t *dw = as_block(as_func(code[0])->base.rhs)->body[0];

  ASSERT_EQ(ND_DO_WHILE, dw->kind);
  ASSERT_EQ(ND_ASSIGN, dw->rhs->kind);  // 本体: a=a+1
  ASSERT_EQ(ND_LT, dw->lhs->kind);      // 条件: a<3
}

static void test_stmt_break_continue() {
  printf("test_stmt_break_continue...\n");
  token = tk_tokenize("main() { while (1) { continue; break; } }");
  program();
  node_t *wh = as_block(as_func(code[0])->base.rhs)->body[0];
  node_t *body = wh->rhs;

  ASSERT_EQ(ND_WHILE, wh->kind);
  ASSERT_EQ(ND_BLOCK, body->kind);
  ASSERT_EQ(ND_CONTINUE, as_block(body)->body[0]->kind);
  ASSERT_EQ(ND_BREAK, as_block(body)->body[1]->kind);
}

static void test_stmt_switch_case_default() {
  printf("test_stmt_switch_case_default...\n");
  token = tk_tokenize("main() { switch (a) { case 1: a=2; break; default: a=3; } }");
  program();
  node_t *sw = as_block(as_func(code[0])->base.rhs)->body[0];

  ASSERT_EQ(ND_SWITCH, sw->kind);
  ASSERT_EQ(ND_LVAR, sw->lhs->kind);
  ASSERT_EQ(ND_BLOCK, sw->rhs->kind);
  ASSERT_EQ(ND_CASE, as_block(sw->rhs)->body[0]->kind);
  ASSERT_EQ(1, as_case(as_block(sw->rhs)->body[0])->val);
  ASSERT_EQ(ND_BREAK, as_block(sw->rhs)->body[1]->kind);
  ASSERT_EQ(ND_DEFAULT, as_block(sw->rhs)->body[2]->kind);
}

static void test_stmt_for() {
  printf("test_stmt_for...\n");
  token = tk_tokenize("main() { for (a=0; a<10; a=a+1) a; }");
  program();
  node_t *fr = as_block(as_func(code[0])->base.rhs)->body[0];

  ASSERT_EQ(ND_FOR, fr->kind);
  ASSERT_EQ(ND_ASSIGN, as_ctrl(fr)->init->kind);  // init: a=0
  ASSERT_EQ(ND_LT, fr->lhs->kind);      // 条件: a<10
  ASSERT_EQ(ND_ASSIGN, as_ctrl(fr)->inc->kind);   // inc: a=a+1
  ASSERT_EQ(ND_LVAR, fr->rhs->kind);     // 本体: a
}

static void test_stmt_for_with_decl_init() {
  printf("test_stmt_for_with_decl_init...\n");
  token = tk_tokenize("main() { for (int i=0; i<3; i=i+1) i; }");
  program();
  node_t *fr = as_block(as_func(code[0])->base.rhs)->body[0];

  ASSERT_EQ(ND_FOR, fr->kind);
  ASSERT_EQ(ND_ASSIGN, as_ctrl(fr)->init->kind);  // init: int i=0
  ASSERT_EQ(ND_LT, fr->lhs->kind);                // 条件: i<3
  ASSERT_EQ(ND_ASSIGN, as_ctrl(fr)->inc->kind);   // inc: i=i+1
  ASSERT_EQ(ND_LVAR, fr->rhs->kind);              // 本体: i

  token = tk_tokenize("main() { for (int i=0, j=2; i<j; i=i+1) i; }");
  program();
  fr = as_block(as_func(code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_FOR, fr->kind);
  ASSERT_EQ(ND_COMMA, as_ctrl(fr)->init->kind);   // init: int i=0, j=2
}

static void test_stmt_return() {
  printf("test_stmt_return...\n");
  token = tk_tokenize("main() { return 42; }");
  program();
  node_t *ret = as_block(as_func(code[0])->base.rhs)->body[0];

  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_NUM, ret->lhs->kind);
  ASSERT_EQ(42, as_num(ret->lhs)->val);

  token = tk_tokenize("void noop() { return; }");
  program();
  ret = as_block(as_func(code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_TRUE(ret->lhs == NULL);
}

static void test_stmt_block() {
  printf("test_stmt_block...\n");
  token = tk_tokenize("main() { { 1; 2; } }");
  program();
  node_t *blk = as_block(as_func(code[0])->base.rhs)->body[0];

  ASSERT_EQ(ND_BLOCK, blk->kind);
  ASSERT_EQ(ND_NUM, as_block(blk)->body[0]->kind);
  ASSERT_EQ(1, as_num(as_block(blk)->body[0])->val);
  ASSERT_EQ(ND_NUM, as_block(blk)->body[1]->kind);
  ASSERT_EQ(2, as_num(as_block(blk)->body[1])->val);
  ASSERT_TRUE(as_block(blk)->body[2] == NULL);
}

static void test_stmt_goto_label() {
  printf("test_stmt_goto_label...\n");
  token = tk_tokenize("main() { goto L1; L1: return 42; }");
  program();
  node_block_t *body = as_block(as_func(code[0])->base.rhs);
  ASSERT_EQ(ND_GOTO, body->body[0]->kind);
  ASSERT_EQ(ND_LABEL, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->rhs->kind);
}

static void test_expr_deref_addr() {
  printf("test_expr_deref_addr...\n");
  // &a
  token = tk_tokenize("&a");
  node_t *addr = expr();
  ASSERT_EQ(ND_ADDR, addr->kind);
  ASSERT_EQ(ND_LVAR, addr->lhs->kind);

  // *p (p が変数として存在する前提)
  token = tk_tokenize("*a");
  node_t *deref = expr();
  ASSERT_EQ(ND_DEREF, deref->kind);
  ASSERT_EQ(ND_LVAR, deref->lhs->kind);
}

static void test_expr_string() {
  printf("test_expr_string...\n");
  string_literals = NULL; // リセット
  token = tk_tokenize("\"hello\"");
  node_t *node = expr();

  ASSERT_EQ(ND_STRING, node->kind);
  ASSERT_TRUE(as_string(node)->string_label != NULL);
  // 文字列テーブルに登録されている
  ASSERT_TRUE(string_literals != NULL);
  ASSERT_EQ(5, string_literals->len);
  ASSERT_TRUE(strncmp(string_literals->str, "hello", 5) == 0);
}

static void test_expr_concat_string() {
  printf("test_expr_concat_string...\n");
  string_literals = NULL;
  token = tk_tokenize("\"he\" \"llo\"");
  node_t *node = expr();

  ASSERT_EQ(ND_STRING, node->kind);
  ASSERT_TRUE(string_literals != NULL);
  ASSERT_EQ(5, string_literals->len);
  ASSERT_TRUE(strncmp(string_literals->str, "hello", 5) == 0);
}

static void test_type_decl() {
  printf("test_type_decl...\n");
  // int x = 5; → ND_ASSIGN
  token = tk_tokenize("main() { int x = 5; }");
  program();
  node_t *stmt = as_block(as_func(code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_ASSIGN, stmt->kind);
  ASSERT_EQ(ND_LVAR, stmt->lhs->kind);
  ASSERT_EQ(5, as_num(stmt->rhs)->val);

  // int x; → ND_NUM(0) ダミー
  token = tk_tokenize("main() { int x; }");
  program();
  stmt = as_block(as_func(code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_NUM, stmt->kind);
  ASSERT_EQ(0, as_num(stmt)->val);

  // int a, b=1; → 初期化のある宣言子のみ式木に残る
  token = tk_tokenize("main() { int a, b=1; }");
  program();
  stmt = as_block(as_func(code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_ASSIGN, stmt->kind);
  ASSERT_EQ(ND_NUM, stmt->rhs->kind);
  ASSERT_EQ(1, as_num(stmt->rhs)->val);

  token = tk_tokenize("main() { int a=1, b=2; }");
  program();
  stmt = as_block(as_func(code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_COMMA, stmt->kind);
}

static void test_multiple_funcdefs() {
  printf("test_multiple_funcdefs...\n");
  token = tk_tokenize("foo() { 1; } bar() { 2; }");
  program();

  ASSERT_TRUE(code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, code[0]->kind);
  ASSERT_TRUE(strncmp(as_func(code[0])->funcname, "foo", 3) == 0);

  ASSERT_TRUE(code[1] != NULL);
  ASSERT_EQ(ND_FUNCDEF, code[1]->kind);
  ASSERT_TRUE(strncmp(as_func(code[1])->funcname, "bar", 3) == 0);

  ASSERT_TRUE(code[2] == NULL);

  token = tk_tokenize("int add(int a, int b); int add(int a, int b) { return a+b; }");
  program();
  ASSERT_TRUE(code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, code[0]->kind);
  ASSERT_TRUE(strncmp(as_func(code[0])->funcname, "add", 3) == 0);
  ASSERT_TRUE(code[1] == NULL);
}

static void test_parse_invalid() {
  printf("test_parse_invalid...\n");
  expect_parse_fail("main( { return 0; }");     // ')' がない
  expect_parse_fail("main() { return 0; ");     // '}' がない
  expect_parse_fail("main() { return 0 }");     // ';' がない
  expect_parse_fail("main() { if (1) return 1; else }"); // else ブロック不正
  expect_parse_fail("main() { int ; return 0; }");       // 変数名なし
  expect_parse_fail("main() { int a[; return 0; }");     // 配列サイズ不正
  expect_parse_fail("main() { int a[1 return 0; }");     // ']' がない
  expect_parse_fail("main() { return (1+2; }");          // ')' がない
  expect_parse_fail("main() { if 1) return 0; }");       // '(' がない
  expect_parse_fail("main() { for (i=0 i<3; i=i+1) return 0; }"); // ';' 不足
  expect_parse_fail("main() { ++1; }");                  // lvalueでない
  expect_parse_fail("main() { 1++; }");                  // lvalueでない
  expect_parse_fail("main() { float f=1.0; ++f; }");     // 非整数スカラー
  expect_parse_fail("main() { double d=1.0; d--; }");    // 非整数スカラー
  expect_parse_fail("main() { 1 += 2; }");               // lvalueでない
  expect_parse_fail("main() { return; }");               // 非void関数で式なしreturn
  expect_parse_fail("void f() { return 1; }");           // void関数で値return
  expect_parse_fail("main() { return sizeof(void); }");  // sizeof(void) 未対応
  expect_parse_fail("main() { return (void)1; }");       // void cast 未対応
  expect_parse_fail("main() { goto MISSING; return 0; }"); // 未定義ラベル
  expect_parse_fail("main() { break; }");                // ループ/switch外
  expect_parse_fail("main() { continue; }");             // ループ外
  expect_parse_fail("main() { switch (1) { case 1: 0; case 1: 0; } }"); // case 重複
  expect_parse_fail("main() { switch (1) { default: 0; default: 1; } }"); // default 重複
}

int main() {
  printf("Running tests for Parser...\n");

  test_expr_number();
  test_expr_add_sub();
  test_expr_mul_div();
  test_expr_mod();
  test_expr_precedence();
  test_expr_parentheses();
  test_expr_eq_neq();
  test_expr_relational();
  test_expr_bitwise();
  test_expr_shift();
  test_expr_logical_and_or();
  test_expr_ternary();
  test_expr_unary_ops();
  test_expr_sizeof();
  test_expr_inc_dec();
  test_expr_assign();
  test_expr_compound_assign();
  test_expr_comma();
  test_program_funcdef();
  test_funcall();
  test_funcdef_with_params();
  test_stmt_if();
  test_stmt_if_else();
  test_stmt_while();
  test_stmt_do_while();
  test_stmt_break_continue();
  test_stmt_switch_case_default();
  test_stmt_for();
  test_stmt_for_with_decl_init();
  test_stmt_return();
  test_stmt_block();
  test_stmt_goto_label();
  test_expr_deref_addr();
  test_expr_string();
  test_expr_concat_string();
  test_expr_float();
  test_expr_long_double_suffix_metadata();
  test_type_decl();
  test_multiple_funcdefs();
  test_parse_invalid();

  printf("OK: All unit tests passed!\n");
  return 0;
}
