#include "../src/parser/parser.h"
#include "../src/parser/decl.h"
#include "../src/parser/expr.h"
#include "../src/parser/node_utils.h"
#include "../src/parser/config_runtime.h"
#include "../src/parser/semantic_ctx.h"
#include "../src/pragma_pack.h"
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

static node_t **parsed_code;

/* parse_expr_input は単体式パースのため、main 関数の宣言ブロックを通らない。
 * ag_c は未宣言識別子をエラー扱いするので、テストで多用する短い名前を
 * あらかじめローカル変数として登録しておく。 */
static void preregister_test_locals(void) {
  static char names[] = "abcdefghijklmnopqrstuvwxyz";
  for (int i = 0; i < 26; i++) {
    psx_decl_register_lvar(&names[i], 1);
  }
}

static node_t *parse_expr_input(const char *input) {
  psx_decl_reset_locals();
  preregister_test_locals();
  /* 単体式パースは関数本体内のコードを模す (ローカルを登録するのと同様)。
   * 複合リテラル `(int){3}` をローカル実体化経路 (ND_COMMA) で扱わせるため、
   * 現在関数名を非 NULL にしておく (本物のパースでは関数定義時に設定される)。 */
  psx_decl_set_current_funcname((char *)"__test__", 8);
  token_t *head = tk_tokenize((char *)input);
  return ps_expr_from(head);
}

static node_t **parse_program_input(const char *input) {
  token_t *head = tk_tokenize((char *)input);
  return ps_program_from(head);
}

static node_num_t *as_num(node_t *n) { return (node_num_t *)n; }
static node_lvar_t *as_lvar(node_t *n) { return (node_lvar_t *)n; }
static node_func_t *as_func(node_t *n) { return (node_func_t *)n; }
static node_block_t *as_block(node_t *n) { return (node_block_t *)n; }
static node_ctrl_t *as_ctrl(node_t *n) { return (node_ctrl_t *)n; }
static node_string_t *as_string(node_t *n) { return (node_string_t *)n; }
static node_case_t *as_case(node_t *n) { return (node_case_t *)n; }
static node_mem_t *as_mem(node_t *n) { return (node_mem_t *)n; }

static lvar_t *find_func_lvar(node_func_t *fn, const char *name) {
  int len = (int)strlen(name);
  for (lvar_t *v = fn ? fn->lvars : NULL; v; v = v->next_all) {
    if (v->len == len && strncmp(v->name, name, (size_t)len) == 0) return v;
  }
  return NULL;
}

static void expect_parse_fail(const char *input) {
  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    token_t *head = tk_tokenize((char *)input);
    parsed_code = ps_program_from(head);
    _exit(0);
  }
  int status;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_TRUE(WEXITSTATUS(status) != 0);
}

static void expect_parse_ok(const char *input) {
  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    token_t *head = tk_tokenize((char *)input);
    parsed_code = ps_program_from(head);
    _exit(0);
  }
  int status;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(0, WEXITSTATUS(status));
}

static void expect_const_assign_ok_for_node(node_t *node) {
  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    psx_node_reject_const_assign(node, "=");
    _exit(0);
  }
  int status;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(0, WEXITSTATUS(status));
}

static void expect_const_qual_discard_fail_for_nodes(node_t *lhs, node_t *rhs) {
  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    psx_node_reject_const_qual_discard(lhs, rhs);
    _exit(0);
  }
  int status;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_TRUE(WEXITSTATUS(status) != 0);
}

static void expect_parse_fail_with_message(const char *input, const char *needle) {
  int fds[2];
  ASSERT_TRUE(pipe(fds) == 0);

  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    close(fds[0]);
    dup2(fds[1], STDERR_FILENO);
    close(fds[1]);
    freopen("/dev/null", "w", stdout);
    token_t *head = tk_tokenize((char *)input);
    parsed_code = ps_program_from(head);
    _exit(0);
  }

  close(fds[1]);
  char buf[4096];
  size_t used = 0;
  for (;;) {
    ssize_t nread = read(fds[0], buf + used, sizeof(buf) - 1 - used);
    if (nread <= 0) break;
    used += (size_t)nread;
    if (used >= sizeof(buf) - 1) break;
  }
  buf[used] = '\0';
  close(fds[0]);

  int status;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_TRUE(WEXITSTATUS(status) != 0);
  ASSERT_TRUE(strstr(buf, needle) != NULL);
}

static void expect_parse_fail_without_message(const char *input, const char *needle) {
  int fds[2];
  ASSERT_TRUE(pipe(fds) == 0);

  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    close(fds[0]);
    dup2(fds[1], STDERR_FILENO);
    close(fds[1]);
    freopen("/dev/null", "w", stdout);
    token_t *head = tk_tokenize((char *)input);
    parsed_code = ps_program_from(head);
    _exit(0);
  }

  close(fds[1]);
  char buf[4096];
  size_t used = 0;
  for (;;) {
    ssize_t nread = read(fds[0], buf + used, sizeof(buf) - 1 - used);
    if (nread <= 0) break;
    used += (size_t)nread;
    if (used >= sizeof(buf) - 1) break;
  }
  buf[used] = '\0';
  close(fds[0]);

  int status;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_TRUE(WEXITSTATUS(status) != 0);
  ASSERT_TRUE(strstr(buf, needle) == NULL);
}

static void expect_parse_ok_without_message(const char *input, const char *needle) {
  int fds[2];
  ASSERT_TRUE(pipe(fds) == 0);

  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    close(fds[0]);
    dup2(fds[1], STDERR_FILENO);
    close(fds[1]);
    freopen("/dev/null", "w", stdout);
    token_t *head = tk_tokenize((char *)input);
    parsed_code = ps_program_from(head);
    _exit(0);
  }

  close(fds[1]);
  char buf[4096];
  size_t used = 0;
  for (;;) {
    ssize_t nread = read(fds[0], buf + used, sizeof(buf) - 1 - used);
    if (nread <= 0) break;
    used += (size_t)nread;
    if (used >= sizeof(buf) - 1) break;
  }
  buf[used] = '\0';
  close(fds[0]);

  int status;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(0, WEXITSTATUS(status));
  ASSERT_TRUE(strstr(buf, needle) == NULL);
}

static void expect_parse_ok_with_message(const char *input, const char *needle) {
  int fds[2];
  ASSERT_TRUE(pipe(fds) == 0);

  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    close(fds[0]);
    dup2(fds[1], STDERR_FILENO);
    close(fds[1]);
    freopen("/dev/null", "w", stdout);
    token_t *head = tk_tokenize((char *)input);
    parsed_code = ps_program_from(head);
    _exit(0);
  }

  close(fds[1]);
  char buf[4096];
  size_t used = 0;
  for (;;) {
    ssize_t nread = read(fds[0], buf + used, sizeof(buf) - 1 - used);
    if (nread <= 0) break;
    used += (size_t)nread;
    if (used >= sizeof(buf) - 1) break;
  }
  buf[used] = '\0';
  close(fds[0]);

  int status;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(0, WEXITSTATUS(status));
  ASSERT_TRUE(strstr(buf, needle) != NULL);
}

static void test_expr_number() {
  printf("test_expr_number...\n");
    node_t *node = parse_expr_input("42");
  ASSERT_EQ(ND_NUM, node->kind);
  ASSERT_EQ(42, as_num(node)->val);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);
}

static void test_expr_float() {
  printf("test_expr_float...\n");
    node_t *node = parse_expr_input("3.14 + 1.5f");
  
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
    node_t *node = parse_expr_input("4.0L");

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

static void test_expr_compound_literal() {
  printf("test_expr_compound_literal...\n");
    node_t *node = parse_expr_input("(int){3}");
  ASSERT_EQ(ND_COMMA, node->kind);
}

static void test_expr_compound_literal_array_subscript() {
  printf("test_expr_compound_literal_array_subscript...\n");
  // 配列型複合リテラルへの添字アクセス: ((int[2]){1,2})[1]
    node_t *node = parse_expr_input("((int[2]){1,2})[1]");
  // 外側 primary() の括弧グループ: ND_COMMA(init, ND_DEREF(...))
  ASSERT_EQ(ND_COMMA, node->kind);
  // rhs が添字アクセス結果 (ND_DEREF)
  ASSERT_EQ(ND_DEREF, node->rhs->kind);
}

static void test_expr_add_sub() {
  printf("test_expr_add_sub...\n");
    node_t *node = parse_expr_input("1 + 2 - 3"); // (1+2)-3

  ASSERT_EQ(ND_SUB, node->kind);
  ASSERT_EQ(ND_ADD, node->lhs->kind);
  ASSERT_EQ(1, as_num(node->lhs->lhs)->val);
  ASSERT_EQ(2, as_num(node->lhs->rhs)->val);
  ASSERT_EQ(3, as_num(node->rhs)->val);
}

static void test_expr_mul_div() {
  printf("test_expr_mul_div...\n");
    node_t *node = parse_expr_input("1 * 2 / 3"); // (1*2)/3

  ASSERT_EQ(ND_DIV, node->kind);
  ASSERT_EQ(ND_MUL, node->lhs->kind);
  ASSERT_EQ(1, as_num(node->lhs->lhs)->val);
  ASSERT_EQ(2, as_num(node->lhs->rhs)->val);
  ASSERT_EQ(3, as_num(node->rhs)->val);
}

static void test_expr_mod() {
  printf("test_expr_mod...\n");
    node_t *node = parse_expr_input("10 % 3");

  ASSERT_EQ(ND_MOD, node->kind);
  ASSERT_EQ(10, as_num(node->lhs)->val);
  ASSERT_EQ(3, as_num(node->rhs)->val);
}

static void test_expr_precedence() {
  printf("test_expr_precedence...\n");
    node_t *node = parse_expr_input("1 + 2 * 3"); // 1+(2*3)

  ASSERT_EQ(ND_ADD, node->kind);
  ASSERT_EQ(1, as_num(node->lhs)->val);
  ASSERT_EQ(ND_MUL, node->rhs->kind);
  ASSERT_EQ(2, as_num(node->rhs->lhs)->val);
  ASSERT_EQ(3, as_num(node->rhs->rhs)->val);
}

static void test_expr_parentheses() {
  printf("test_expr_parentheses...\n");
    node_t *node = parse_expr_input("(1 + 2) * 3"); // (1+2)*3

  ASSERT_EQ(ND_MUL, node->kind);
  ASSERT_EQ(ND_ADD, node->lhs->kind);
  ASSERT_EQ(1, as_num(node->lhs->lhs)->val);
  ASSERT_EQ(2, as_num(node->lhs->rhs)->val);
  ASSERT_EQ(3, as_num(node->rhs)->val);
}

static void test_expr_eq_neq() {
  printf("test_expr_eq_neq...\n");
    node_t *node = parse_expr_input("1 == 2 != 3"); // (1==2)!=3

  ASSERT_EQ(ND_NE, node->kind);
  ASSERT_EQ(ND_EQ, node->lhs->kind);
  ASSERT_EQ(1, as_num(node->lhs->lhs)->val);
  ASSERT_EQ(2, as_num(node->lhs->rhs)->val);
  ASSERT_EQ(3, as_num(node->rhs)->val);
}

static void test_expr_relational() {
  printf("test_expr_relational...\n");
    node_t *node = parse_expr_input("1 < 2 <= 3 > 4 >= 5");

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

    node_t *node = parse_expr_input("1 && 0 || 3");
  ASSERT_EQ(ND_LOGOR, node->kind);
  ASSERT_EQ(ND_LOGAND, node->lhs->kind);
  ASSERT_EQ(3, as_num(node->rhs)->val);
}

static void test_expr_bitwise() {
  printf("test_expr_bitwise...\n");
    node_t *node = parse_expr_input("1 | 2 ^ 3 & 4");

  ASSERT_EQ(ND_BITOR, node->kind);
  ASSERT_EQ(1, as_num(node->lhs)->val);
  ASSERT_EQ(ND_BITXOR, node->rhs->kind);
  ASSERT_EQ(2, as_num(node->rhs->lhs)->val);
  ASSERT_EQ(ND_BITAND, node->rhs->rhs->kind);
}

static void test_expr_shift() {
  printf("test_expr_shift...\n");
    node_t *node = parse_expr_input("1 + 2 << 3 >> 1");

  ASSERT_EQ(ND_SHR, node->kind);
  ASSERT_EQ(ND_SHL, node->lhs->kind);
  ASSERT_EQ(ND_ADD, node->lhs->lhs->kind);
  ASSERT_EQ(1, as_num(node->rhs)->val);

    node_t *promoted_signed_shift = parse_expr_input("(unsigned char)a >> 1");
  ASSERT_EQ(ND_SHR, promoted_signed_shift->kind);
  ASSERT_TRUE(!psx_node_integer_promotion_is_unsigned(promoted_signed_shift->lhs));
  ASSERT_TRUE(!psx_node_shift_operation_is_unsigned(promoted_signed_shift));

    node_t *promoted_unsigned_shift = parse_expr_input("(unsigned int)a >> 1");
  ASSERT_EQ(ND_SHR, promoted_unsigned_shift->kind);
  ASSERT_TRUE(psx_node_integer_promotion_is_unsigned(promoted_unsigned_shift->lhs));
  ASSERT_TRUE(psx_node_shift_operation_is_unsigned(promoted_unsigned_shift));

    node_t *forced_signed_shift = parse_expr_input("(int)(unsigned long)a");
  ASSERT_EQ(ND_CAST, forced_signed_shift->kind);
  ASSERT_EQ(ND_SHR, forced_signed_shift->lhs->kind);
  ASSERT_EQ(ND_SHL, forced_signed_shift->lhs->lhs->kind);
  ASSERT_TRUE(!psx_node_shift_operation_is_unsigned(forced_signed_shift->lhs->lhs));
  ASSERT_TRUE(!psx_node_shift_operation_is_unsigned(forced_signed_shift->lhs));
  ASSERT_TRUE(!psx_node_conversion_value_is_unsigned(forced_signed_shift));

    node_t *forced_signed_keyword_shift = parse_expr_input("(signed)(unsigned long)a");
  ASSERT_EQ(ND_CAST, forced_signed_keyword_shift->kind);
  ASSERT_EQ(ND_SHR, forced_signed_keyword_shift->lhs->kind);
  ASSERT_EQ(ND_SHL, forced_signed_keyword_shift->lhs->lhs->kind);
  ASSERT_TRUE(!psx_node_shift_operation_is_unsigned(forced_signed_keyword_shift->lhs->lhs));
  ASSERT_TRUE(!psx_node_shift_operation_is_unsigned(forced_signed_keyword_shift->lhs));
  ASSERT_TRUE(!psx_node_conversion_value_is_unsigned(forced_signed_keyword_shift));

    node_t *forced_unsigned_shift = parse_expr_input("(unsigned)(long)a");
  ASSERT_EQ(ND_CAST, forced_unsigned_shift->kind);
  ASSERT_EQ(ND_SHR, forced_unsigned_shift->lhs->kind);
  ASSERT_EQ(ND_SHL, forced_unsigned_shift->lhs->lhs->kind);
  ASSERT_TRUE(psx_node_shift_operation_is_unsigned(forced_unsigned_shift->lhs->lhs));
  ASSERT_TRUE(psx_node_shift_operation_is_unsigned(forced_unsigned_shift->lhs));
  ASSERT_TRUE(psx_node_conversion_value_is_unsigned(forced_unsigned_shift));
}

static void test_expr_ternary() {
  printf("test_expr_ternary...\n");
    node_t *node = parse_expr_input("1 ? 2 : 3 ? 4 : 5");

  ASSERT_EQ(ND_TERNARY, node->kind);
  ASSERT_EQ(1, as_num(node->lhs)->val);
  ASSERT_EQ(2, as_num(node->rhs)->val);
  ASSERT_EQ(ND_TERNARY, as_ctrl(node)->els->kind); // 右結合
}

static void test_expr_unary_ops() {
  printf("test_expr_unary_ops...\n");

    node_t *pos = parse_expr_input("+42");
  ASSERT_EQ(ND_NUM, pos->kind);
  ASSERT_EQ(42, as_num(pos)->val);

    node_t *neg = parse_expr_input("-42");
  ASSERT_EQ(ND_SUB, neg->kind);
  ASSERT_EQ(ND_NUM, neg->lhs->kind);
  ASSERT_EQ(0, as_num(neg->lhs)->val);
  ASSERT_EQ(ND_NUM, neg->rhs->kind);
  ASSERT_EQ(42, as_num(neg->rhs)->val);

    node_t *not0 = parse_expr_input("!0");
  ASSERT_EQ(ND_EQ, not0->kind);
  ASSERT_EQ(ND_NUM, not0->lhs->kind);
  ASSERT_EQ(0, as_num(not0->lhs)->val);
  ASSERT_EQ(ND_NUM, not0->rhs->kind);
  ASSERT_EQ(0, as_num(not0->rhs)->val);

    node_t *bitnot = parse_expr_input("~5");
  ASSERT_EQ(ND_SUB, bitnot->kind);               // (~5) == ((0-5)-1)
  ASSERT_EQ(ND_SUB, bitnot->lhs->kind);
  ASSERT_EQ(1, as_num(bitnot->rhs)->val);

    node_t *voidcast = parse_expr_input("(void)1");
  ASSERT_EQ(ND_CAST, voidcast->kind);
  ASSERT_TRUE(psx_node_get_type(voidcast)->kind == PSX_TYPE_VOID);
  ASSERT_EQ(ND_NUM, voidcast->lhs->kind);
  ASSERT_EQ(1, as_num(voidcast->lhs)->val);

    node_t *ptr_const_cast = parse_expr_input("(int *)0x1000");
  ASSERT_EQ(ND_CAST, ptr_const_cast->kind);
  ASSERT_TRUE(ps_node_is_pointer(ptr_const_cast));
  ASSERT_EQ(ND_NUM, ptr_const_cast->lhs->kind);
  ASSERT_EQ(0x1000, as_num(ptr_const_cast->lhs)->val);

    node_t *boolcast = parse_expr_input("(_Bool)3");
  ASSERT_EQ(ND_NE, boolcast->kind);
  ASSERT_EQ(ND_NUM, boolcast->rhs->kind);
  ASSERT_EQ(0, as_num(boolcast->rhs)->val);

    node_t *const_cast = parse_expr_input("(const int)7");
  ASSERT_EQ(ND_NUM, const_cast->kind);
  ASSERT_EQ(7, as_num(const_cast)->val);

    node_t *volatile_cast = parse_expr_input("(volatile int)8");
  ASSERT_EQ(ND_NUM, volatile_cast->kind);
  ASSERT_EQ(8, as_num(volatile_cast)->val);

    node_t *post_const_cast = parse_expr_input("(int const)12");
  ASSERT_EQ(ND_NUM, post_const_cast->kind);
  ASSERT_EQ(12, as_num(post_const_cast)->val);

    node_t *post_dup_const_cast = parse_expr_input("(int const const)21");
  ASSERT_EQ(ND_NUM, post_dup_const_cast->kind);
  ASSERT_EQ(21, as_num(post_dup_const_cast)->val);

    node_t *multi_ptr_qual_cast = parse_expr_input("(int const * volatile * restrict)0");
  ASSERT_EQ(ND_CAST, multi_ptr_qual_cast->kind);
  ASSERT_TRUE(ps_node_is_pointer(multi_ptr_qual_cast));
  ASSERT_EQ(ND_NUM, multi_ptr_qual_cast->lhs->kind);
  ASSERT_EQ(0, as_num(multi_ptr_qual_cast->lhs)->val);

    node_t *unsigned_int_const_cast = parse_expr_input("(unsigned int const)13");
  ASSERT_EQ(ND_NUM, unsigned_int_const_cast->kind);
  ASSERT_EQ(13, as_num(unsigned_int_const_cast)->val);

    node_t *funcptr_const_cast = parse_expr_input("(int (*const)(int))0");
  ASSERT_EQ(ND_CAST, funcptr_const_cast->kind);
  ASSERT_TRUE(ps_node_is_pointer(funcptr_const_cast));
  ASSERT_EQ(ND_NUM, funcptr_const_cast->lhs->kind);
  ASSERT_EQ(0, as_num(funcptr_const_cast->lhs)->val);

    node_t *long_long_cast = parse_expr_input("(long long)14");
  ASSERT_EQ(ND_NUM, long_long_cast->kind);
  ASSERT_EQ(14, as_num(long_long_cast)->val);

    node_t *unsigned_long_cast = parse_expr_input("(unsigned long)15");
  ASSERT_EQ(ND_NUM, unsigned_long_cast->kind);
  ASSERT_EQ(15, as_num(unsigned_long_cast)->val);

    node_t *long_unsigned_int_cast = parse_expr_input("(long)(unsigned int)a");
  ASSERT_EQ(ND_CAST, long_unsigned_int_cast->kind);
  ASSERT_TRUE(as_mem(long_unsigned_int_cast)->widen_zext_i64);
  ASSERT_TRUE(psx_node_i64_widen_source_is_unsigned(long_unsigned_int_cast->lhs));

    node_t *long_signed_int_cast = parse_expr_input("(long)(int)a");
  ASSERT_EQ(ND_CAST, long_signed_int_cast->kind);
  ASSERT_TRUE(!as_mem(long_signed_int_cast)->widen_zext_i64);
  ASSERT_TRUE(!psx_node_i64_widen_source_is_unsigned(long_signed_int_cast->lhs));

    // 定数の short/char キャストは目的幅へ切り詰めて ND_NUM へ定数畳み込みする
    // (16/17/18 は範囲内なので値は不変)。
    node_t *unsigned_short_int_cast = parse_expr_input("(unsigned short int)16");
  ASSERT_EQ(ND_NUM, unsigned_short_int_cast->kind);
  ASSERT_EQ(16, as_num(unsigned_short_int_cast)->val);

    node_t *signed_char_cast = parse_expr_input("(signed char)17");
  ASSERT_EQ(ND_NUM, signed_char_cast->kind);
  ASSERT_EQ(17, as_num(signed_char_cast)->val);

    node_t *unsigned_char_cast = parse_expr_input("(unsigned char)18");
  ASSERT_EQ(ND_NUM, unsigned_char_cast->kind);
  ASSERT_EQ(18, as_num(unsigned_char_cast)->val);

    node_t *long_unsigned_char_cast = parse_expr_input("(long)(unsigned char)a");
  ASSERT_EQ(ND_CAST, long_unsigned_char_cast->kind);
  ASSERT_TRUE(as_mem(long_unsigned_char_cast)->widen_zext_i64);
  ASSERT_TRUE(psx_node_integer_value_is_unsigned(long_unsigned_char_cast->lhs));

    node_t *long_unsigned_short_cast = parse_expr_input("(long)(unsigned short)a");
  ASSERT_EQ(ND_CAST, long_unsigned_short_cast->kind);
  ASSERT_TRUE(as_mem(long_unsigned_short_cast)->widen_zext_i64);
  ASSERT_TRUE(psx_node_integer_value_is_unsigned(long_unsigned_short_cast->lhs));

    node_t *long_signed_short_cast = parse_expr_input("(long)(short)a");
  ASSERT_EQ(ND_CAST, long_signed_short_cast->kind);
  ASSERT_TRUE(!as_mem(long_signed_short_cast)->widen_zext_i64);
  ASSERT_TRUE(!psx_node_integer_value_is_unsigned(long_signed_short_cast->lhs));

    node_t *restrict_ptr_cast = parse_expr_input("(restrict int*)0");
  ASSERT_EQ(ND_CAST, restrict_ptr_cast->kind);
  ASSERT_TRUE(ps_node_is_pointer(restrict_ptr_cast));
  ASSERT_EQ(ND_NUM, restrict_ptr_cast->lhs->kind);
  ASSERT_EQ(0, as_num(restrict_ptr_cast->lhs)->val);

    node_t *dup_restrict_ptr_cast = parse_expr_input("(restrict restrict int*)0");
  ASSERT_EQ(ND_CAST, dup_restrict_ptr_cast->kind);
  ASSERT_TRUE(ps_node_is_pointer(dup_restrict_ptr_cast));
  ASSERT_EQ(ND_NUM, dup_restrict_ptr_cast->lhs->kind);
  ASSERT_EQ(0, as_num(dup_restrict_ptr_cast->lhs)->val);

    node_t *atomic_cast = parse_expr_input("(_Atomic int)9");
  ASSERT_EQ(ND_NUM, atomic_cast->kind);
  ASSERT_EQ(9, as_num(atomic_cast)->val);

    node_t *atomic_const_cast = parse_expr_input("(_Atomic const int)10");
  ASSERT_EQ(ND_NUM, atomic_const_cast->kind);
  ASSERT_EQ(10, as_num(atomic_const_cast)->val);

    node_t *nested_atomic_cast = parse_expr_input("(_Atomic(_Atomic(int)))11");
  ASSERT_EQ(ND_NUM, nested_atomic_cast->kind);
  ASSERT_EQ(11, as_num(nested_atomic_cast)->val);

  parsed_code = parse_program_input("main() { struct S { int x; }; struct S a={1}, b={2}; int c=1; struct S s=c?a:(struct S){3}; return s.x; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);

  parsed_code = parse_program_input("main() { struct S { int x; }; struct S a={1}, b={2}; int c=1; struct S s=(c?(struct S){3}:b); return s.x; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);

  parsed_code = parse_program_input("main() { struct S { int x; }; struct S a={1}; struct S s=(a,(struct S){9}); return s.x; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);

  parsed_code = parse_program_input("main() { struct S { int x; }; struct S a={1}, b={2}; int c=1; struct S t=(struct S)(c?a:b); return t.x; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);

  parsed_code = parse_program_input("main() { union U { int x; char y; }; union U a={.x=1}, b={.x=2}; int c=1; union U t=(union U)(c?a:b); return t.x; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
}

static void test_expr_generic() {
  printf("test_expr_generic...\n");

    node_t *g1 = parse_expr_input("_Generic(1, int: 11, default: 22)");
  ASSERT_EQ(ND_NUM, g1->kind);
  ASSERT_EQ(11, as_num(g1)->val);

    node_t *g2 = parse_expr_input("_Generic(1.0, float: 11, double: 33, default: 22)");
  ASSERT_EQ(ND_NUM, g2->kind);
  ASSERT_EQ(33, as_num(g2)->val);

  parsed_code = parse_program_input("main() { int *p=0; return _Generic(p, int*: 3, default: 7); }");
  node_t *ret = as_block(as_func(parsed_code[0])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_NUM, ret->lhs->kind);
  ASSERT_EQ(3, as_num(ret->lhs)->val);

  parsed_code = parse_program_input(
      "typedef int (*fp_t)(int); "
      "int f(int x){ return x; } "
      "main(){ fp_t p=f; return _Generic(p, int (*)(int): 13, default: 7); }");
  node_t *ret_fp = as_block(as_func(parsed_code[1])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret_fp->kind);
  ASSERT_EQ(ND_NUM, ret_fp->lhs->kind);
  ASSERT_EQ(13, as_num(ret_fp->lhs)->val);

  expect_parse_ok(
      "main(){ struct S{int x;}; return _Generic((struct S){1}, struct S: 1, default: 2); }");
  expect_parse_ok(
      "main(){ union U{int x;}; return _Generic((union U){.x=1}, union U: 1, default: 2); }");
  parsed_code = parse_program_input(
      "main(){ struct S{int x;}; return _Generic((struct S){1}, struct S: 1, default: 2); }");
  node_t *ret_struct = as_block(as_func(parsed_code[0])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret_struct->kind);
  ASSERT_EQ(ND_NUM, ret_struct->lhs->kind);
  ASSERT_EQ(1, as_num(ret_struct->lhs)->val);

  parsed_code = parse_program_input(
      "main(){ struct S{int x;}; struct T{int x;}; return _Generic((struct S){1}, struct T: 1, default: 2); }");
  node_t *ret_struct_nomatch = as_block(as_func(parsed_code[0])->base.rhs)->body[2];
  ASSERT_EQ(ND_RETURN, ret_struct_nomatch->kind);
  ASSERT_EQ(ND_NUM, ret_struct_nomatch->lhs->kind);
  ASSERT_EQ(2, as_num(ret_struct_nomatch->lhs)->val);
  expect_parse_ok(
      "main(){ int *p=0; return _Generic(p, int[3]: 1, default: 2); }");
  expect_parse_ok(
      "main(){ double d=1.0; double *p=&d; return _Generic(*p, double:42, default:99); }");
  expect_parse_ok(
      "main(){ float f=1.0f; float *p=&f; return _Generic(*p, float:11, default:99); }");
  expect_parse_ok(
      "main(){ double a[1]={1.0}; double *p=a; return _Generic(p[0], double:42, default:99); }");
  parsed_code = parse_program_input(
      "main(){ int x=0; char c=0; int *pi=&x; char *pc=&c; return _Generic(pc, int*:1, char*:2, default:3); }");
  node_t *ret_ptr_kind = as_block(as_func(parsed_code[0])->base.rhs)->body[4];
  ASSERT_EQ(ND_RETURN, ret_ptr_kind->kind);
  ASSERT_EQ(ND_NUM, ret_ptr_kind->lhs->kind);
  ASSERT_EQ(2, as_num(ret_ptr_kind->lhs)->val);

  parsed_code = parse_program_input(
      "main(){ double d=1.0; double *pd=&d; return _Generic(pd, int*:1, double*:2, default:3); }");
  node_t *ret_ptr_fp = as_block(as_func(parsed_code[0])->base.rhs)->body[2];
  ASSERT_EQ(ND_RETURN, ret_ptr_fp->kind);
  ASSERT_EQ(ND_NUM, ret_ptr_fp->lhs->kind);
  ASSERT_EQ(2, as_num(ret_ptr_fp->lhs)->val);

  parsed_code = parse_program_input(
      "main(){ struct S{int x;}; struct T{int x;}; struct S s={1}; struct S *ps=&s; return _Generic(ps, struct T*:1, struct S*:2, default:3); }");
  node_t *ret_ptr_tag = as_block(as_func(parsed_code[0])->base.rhs)->body[4];
  ASSERT_EQ(ND_RETURN, ret_ptr_tag->kind);
  ASSERT_EQ(ND_NUM, ret_ptr_tag->lhs->kind);
  ASSERT_EQ(2, as_num(ret_ptr_tag->lhs)->val);

  parsed_code = parse_program_input(
      "main(){ int x=0; const int *p=&x; return _Generic(p, int*:1, const int*:2, default:3); }");
  node_t *ret_ptr_const = as_block(as_func(parsed_code[0])->base.rhs)->body[2];
  ASSERT_EQ(ND_RETURN, ret_ptr_const->kind);
  ASSERT_EQ(ND_NUM, ret_ptr_const->lhs->kind);
  ASSERT_EQ(2, as_num(ret_ptr_const->lhs)->val);

  parsed_code = parse_program_input(
      "typedef const int *cip_t; main(){ int x=0; cip_t p=&x; return _Generic(p, int*:1, const int*:2, default:3); }");
  node_t *ret_typedef_const_ptr = as_block(as_func(parsed_code[0])->base.rhs)->body[2];
  ASSERT_EQ(ND_RETURN, ret_typedef_const_ptr->kind);
  ASSERT_EQ(ND_NUM, ret_typedef_const_ptr->lhs->kind);
  ASSERT_EQ(2, as_num(ret_typedef_const_ptr->lhs)->val);

  parsed_code = parse_program_input(
      "typedef volatile int *vip_t; main(){ int x=0; vip_t p=&x; return _Generic(p, volatile int*:2, int*:1, default:3); }");
  node_t *ret_typedef_volatile_ptr = as_block(as_func(parsed_code[0])->base.rhs)->body[2];
  ASSERT_EQ(ND_RETURN, ret_typedef_volatile_ptr->kind);
  ASSERT_EQ(ND_NUM, ret_typedef_volatile_ptr->lhs->kind);
  ASSERT_EQ(2, as_num(ret_typedef_volatile_ptr->lhs)->val);

  parsed_code = parse_program_input(
      "main(){ int x=0; char c=0; int *pi=&x; char *pc=&c; int **ppi=&pi; return _Generic(ppi, char**:1, int**:2, default:3); }");
  node_t *ret_ptr_ptr_kind = as_block(as_func(parsed_code[0])->base.rhs)->body[5];
  ASSERT_EQ(ND_RETURN, ret_ptr_ptr_kind->kind);
  ASSERT_EQ(ND_NUM, ret_ptr_ptr_kind->lhs->kind);
  ASSERT_EQ(2, as_num(ret_ptr_ptr_kind->lhs)->val);

  parsed_code = parse_program_input(
      "main(){ int x=0; unsigned int u=0; unsigned int *pu=&u; return _Generic(pu, int*:1, unsigned int*:2, default:3); }");
  node_t *ret_ptr_unsigned = as_block(as_func(parsed_code[0])->base.rhs)->body[3];
  ASSERT_EQ(ND_RETURN, ret_ptr_unsigned->kind);
  ASSERT_EQ(ND_NUM, ret_ptr_unsigned->lhs->kind);
  ASSERT_EQ(2, as_num(ret_ptr_unsigned->lhs)->val);

  parsed_code = parse_program_input(
      "typedef unsigned int *uip_t; main(){ unsigned int u=0; uip_t pu=&u; return _Generic(pu, int*:1, unsigned int*:2, default:3); }");
  node_t *ret_ptr_unsigned_typedef = as_block(as_func(parsed_code[0])->base.rhs)->body[2];
  ASSERT_EQ(ND_RETURN, ret_ptr_unsigned_typedef->kind);
  ASSERT_EQ(ND_NUM, ret_ptr_unsigned_typedef->lhs->kind);
  ASSERT_EQ(2, as_num(ret_ptr_unsigned_typedef->lhs)->val);

  parsed_code = parse_program_input(
      "main(){ int x=0; int *p=&x; int * const *pp=&p; return _Generic(pp, int**:1, int * const *:2, default:3); }");
  node_t *ret_ptr_level_const = as_block(as_func(parsed_code[0])->base.rhs)->body[3];
  ASSERT_EQ(ND_RETURN, ret_ptr_level_const->kind);
  ASSERT_EQ(ND_NUM, ret_ptr_level_const->lhs->kind);
  ASSERT_EQ(2, as_num(ret_ptr_level_const->lhs)->val);

  parsed_code = parse_program_input(
      "main(){ int x=0; int *p=&x; int * volatile *pp=&p; return _Generic(pp, int**:1, int * volatile *:2, default:3); }");
  node_t *ret_ptr_level_volatile = as_block(as_func(parsed_code[0])->base.rhs)->body[3];
  ASSERT_EQ(ND_RETURN, ret_ptr_level_volatile->kind);
  ASSERT_EQ(ND_NUM, ret_ptr_level_volatile->lhs->kind);
  ASSERT_EQ(2, as_num(ret_ptr_level_volatile->lhs)->val);

  parsed_code = parse_program_input(
      "main(){ unsigned long ul=1; return _Generic(ul, unsigned long:2, unsigned int:1, default:3); }");
  node_t *ret_unsigned_long = as_block(as_func(parsed_code[0])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret_unsigned_long->kind);
  ASSERT_EQ(ND_NUM, ret_unsigned_long->lhs->kind);
  ASSERT_EQ(2, as_num(ret_unsigned_long->lhs)->val);

  parsed_code = parse_program_input(
      "main(){ long l=1; return _Generic(l, unsigned long:1, long:2, default:3); }");
  node_t *ret_long_signed = as_block(as_func(parsed_code[0])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret_long_signed->kind);
  ASSERT_EQ(ND_NUM, ret_long_signed->lhs->kind);
  ASSERT_EQ(2, as_num(ret_long_signed->lhs)->val);

  parsed_code = parse_program_input(
      "main(){ return _Generic(1, int const:2, default:3); }");
  node_t *ret_int_post_const = as_block(as_func(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret_int_post_const->kind);
  ASSERT_EQ(ND_NUM, ret_int_post_const->lhs->kind);
  ASSERT_EQ(2, as_num(ret_int_post_const->lhs)->val);

  parsed_code = parse_program_input(
      "main(){ int x=0; int const *p=&x; return _Generic(p, int const *:2, int *:1, default:3); }");
  node_t *ret_ptr_post_const = as_block(as_func(parsed_code[0])->base.rhs)->body[2];
  ASSERT_EQ(ND_RETURN, ret_ptr_post_const->kind);
  ASSERT_EQ(ND_NUM, ret_ptr_post_const->lhs->kind);
  ASSERT_EQ(2, as_num(ret_ptr_post_const->lhs)->val);
}

static void test_expr_sizeof() {
  printf("test_expr_sizeof...\n");

    node_t *n1 = parse_expr_input("sizeof(int)");
  ASSERT_EQ(ND_NUM, n1->kind);
  ASSERT_EQ(4, as_num(n1)->val);

    node_t *n0 = parse_expr_input("sizeof(void)");
  ASSERT_EQ(ND_NUM, n0->kind);
  ASSERT_EQ(1, as_num(n0)->val);

    node_t *n2 = parse_expr_input("sizeof(int*)");
  ASSERT_EQ(ND_NUM, n2->kind);
  ASSERT_EQ(8, as_num(n2)->val);

    node_t *n2q1 = parse_expr_input("sizeof(int * const)");
  ASSERT_EQ(ND_NUM, n2q1->kind);
  ASSERT_EQ(8, as_num(n2q1)->val);

    node_t *n2q2 = parse_expr_input("sizeof(int * volatile)");
  ASSERT_EQ(ND_NUM, n2q2->kind);
  ASSERT_EQ(8, as_num(n2q2)->val);

    node_t *n2q3 = parse_expr_input("sizeof(int * restrict)");
  ASSERT_EQ(ND_NUM, n2q3->kind);
  ASSERT_EQ(8, as_num(n2q3)->val);

    node_t *n2a = parse_expr_input("sizeof(int[10])");
  ASSERT_EQ(ND_NUM, n2a->kind);
  ASSERT_EQ(40, as_num(n2a)->val);

    node_t *n2b = parse_expr_input("sizeof(int (*)[3])");
  ASSERT_EQ(ND_NUM, n2b->kind);
  ASSERT_EQ(8, as_num(n2b)->val);

    node_t *n2c = parse_expr_input("sizeof((int[3]))");
  ASSERT_EQ(ND_NUM, n2c->kind);
  ASSERT_EQ(12, as_num(n2c)->val);

    node_t *n3 = parse_expr_input("sizeof(int (*)(int))");
  ASSERT_EQ(ND_NUM, n3->kind);
  ASSERT_EQ(8, as_num(n3)->val);

    node_t *n4 = parse_expr_input("sizeof(_Complex double)");
  ASSERT_EQ(ND_NUM, n4->kind);
  ASSERT_EQ(16, as_num(n4)->val);

    node_t *n5 = parse_expr_input("sizeof(float _Imaginary)");
  ASSERT_EQ(ND_NUM, n5->kind);
  ASSERT_EQ(8, as_num(n5)->val);

    node_t *a1 = parse_expr_input("_Alignof(int)");
  ASSERT_EQ(ND_NUM, a1->kind);
  ASSERT_EQ(4, as_num(a1)->val);

    node_t *a2 = parse_expr_input("_Alignof(int*)");
  ASSERT_EQ(ND_NUM, a2->kind);
  ASSERT_EQ(8, as_num(a2)->val);

    node_t *a2q1 = parse_expr_input("_Alignof(int * const)");
  ASSERT_EQ(ND_NUM, a2q1->kind);
  ASSERT_EQ(8, as_num(a2q1)->val);

    node_t *a2q2 = parse_expr_input("_Alignof(int * volatile)");
  ASSERT_EQ(ND_NUM, a2q2->kind);
  ASSERT_EQ(8, as_num(a2q2)->val);

    node_t *a2q3 = parse_expr_input("_Alignof(int * restrict)");
  ASSERT_EQ(ND_NUM, a2q3->kind);
  ASSERT_EQ(8, as_num(a2q3)->val);

    node_t *a2a = parse_expr_input("_Alignof(int[10])");
  ASSERT_EQ(ND_NUM, a2a->kind);
  /* 配列のアラインメントは要素のアラインメント (= 4)。sizeof (40) ではない。 */
  ASSERT_EQ(4, as_num(a2a)->val);

    node_t *a2b = parse_expr_input("_Alignof(int (*)[3])");
  ASSERT_EQ(ND_NUM, a2b->kind);
  ASSERT_EQ(8, as_num(a2b)->val);

    node_t *a2c = parse_expr_input("_Alignof((int[3]))");
  ASSERT_EQ(ND_NUM, a2c->kind);
  /* 配列のアラインメントは要素のアラインメント (= 4)、sizeof (12) ではない。 */
  ASSERT_EQ(4, as_num(a2c)->val);

    node_t *a3 = parse_expr_input("_Alignof(_Imaginary double)");
  ASSERT_EQ(ND_NUM, a3->kind);
  ASSERT_EQ(16, as_num(a3)->val);

  parsed_code = parse_program_input("main() { int x; return sizeof(x); }");
  node_t *ret = as_block(as_func(parsed_code[0])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_NUM, ret->lhs->kind);
  ASSERT_EQ(4, as_num(ret->lhs)->val);
  expect_parse_ok_without_message("int main(void){ int x; return sizeof(x); }", "W3004");
  expect_parse_ok_without_message("int main(void){ int a[3]; return sizeof(a); }", "W3003");
  expect_parse_ok_without_message("int main(void){ int n=3; int a[n]; return sizeof(a); }", "W3003");
  expect_parse_ok_without_message("int main(void){ int n=2,m=4; int v[n][m]; int idx=1; return sizeof(v[idx]); }", "W3003");
  expect_parse_ok_without_message("int main(void){ static int a[3]; return sizeof(a); }", "W3003");
  expect_parse_ok_without_message("int main(void){ int (*p)[3][4]; return sizeof(*p); }", "W3004");

  parsed_code = parse_program_input("main() { struct S { int x; }; return sizeof(struct S); }");
  ret = as_block(as_func(parsed_code[0])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_NUM, ret->lhs->kind);
  ASSERT_EQ(4, as_num(ret->lhs)->val);

  parsed_code = parse_program_input("main() { struct S { int x; }; return _Alignof(struct S); }");
  ret = as_block(as_func(parsed_code[0])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_NUM, ret->lhs->kind);
  ASSERT_EQ(4, as_num(ret->lhs)->val);

  parsed_code = parse_program_input("main() { struct S { int x; }; return sizeof(struct S (*)[3]); }");
  ret = as_block(as_func(parsed_code[0])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_NUM, ret->lhs->kind);
  ASSERT_EQ(8, as_num(ret->lhs)->val);

  parsed_code = parse_program_input("typedef int A3[3]; main() { return sizeof(A3 (*)[2]); }");
  ret = as_block(as_func(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_NUM, ret->lhs->kind);
  ASSERT_EQ(8, as_num(ret->lhs)->val);

  parsed_code = parse_program_input("main() { struct S { int x; }; return sizeof(struct S[3]); }");
  ret = as_block(as_func(parsed_code[0])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_NUM, ret->lhs->kind);
  ASSERT_EQ(12, as_num(ret->lhs)->val);

  parsed_code = parse_program_input("typedef int A3[3]; main() { return sizeof(A3[2]); }");
  ret = as_block(as_func(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_NUM, ret->lhs->kind);
  ASSERT_EQ(24, as_num(ret->lhs)->val);

    // (char)300: signed char へ切り詰めて ND_NUM へ畳み込む (300 → 44)。
    node_t *c1 = parse_expr_input("(char)300");
  ASSERT_EQ(ND_NUM, c1->kind);
  ASSERT_EQ(44, as_num(c1)->val);

    // 整数リテラルの fp キャストは ND_INT_TO_FP でラップされ、codegen が I2F を発行する。
    node_t *c2 = parse_expr_input("(_Complex double)1");
  ASSERT_EQ(ND_INT_TO_FP, c2->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, c2->fp_kind);
  ASSERT_EQ(ND_NUM, c2->lhs->kind);
  psx_type_t *c2_ty = psx_node_get_type(c2);
  ASSERT_TRUE(c2_ty != NULL);
  ASSERT_EQ(PSX_TYPE_COMPLEX, c2_ty->kind);
  ASSERT_EQ(16, psx_type_sizeof(c2_ty));
  ASSERT_EQ(16, ps_node_type_size(c2));

    node_t *c3 = parse_expr_input("(float _Imaginary)1");
  ASSERT_EQ(ND_INT_TO_FP, c3->kind);
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT, c3->fp_kind);
  ASSERT_EQ(ND_NUM, c3->lhs->kind);
  psx_type_t *c3_ty = psx_node_get_type(c3);
  ASSERT_TRUE(c3_ty != NULL);
  ASSERT_EQ(PSX_TYPE_COMPLEX, c3_ty->kind);
  ASSERT_EQ(8, psx_type_sizeof(c3_ty));
  ASSERT_EQ(8, ps_node_type_size(c3));

    node_t *c4 = parse_expr_input("(long double)1");
  ASSERT_EQ(ND_INT_TO_FP, c4->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, c4->fp_kind);
  ASSERT_EQ(ND_NUM, c4->lhs->kind);

    node_t *c5 = parse_expr_input("(_Atomic(int))1");
  ASSERT_EQ(ND_NUM, c5->kind);

    node_t *c6 = parse_expr_input("(_Atomic(int*))0");
  ASSERT_EQ(ND_CAST, c6->kind);
  ASSERT_TRUE(ps_node_is_pointer(c6));
  ASSERT_EQ(ND_NUM, c6->lhs->kind);

    node_t *ci = parse_expr_input("(int)a");
  psx_type_t *ci_ty = psx_node_get_type(ci);
  ASSERT_TRUE(ci_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, ci_ty->kind);
  ASSERT_EQ(4, psx_type_sizeof(ci_ty));
  ASSERT_TRUE(!psx_type_is_unsigned(ci_ty));
  ASSERT_TRUE(!psx_node_integer_value_is_unsigned(ci));

    node_t *cus = parse_expr_input("(unsigned short)a");
  psx_type_t *cus_ty = psx_node_get_type(cus);
  ASSERT_TRUE(cus_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, cus_ty->kind);
  ASSERT_EQ(2, psx_type_sizeof(cus_ty));
  ASSERT_EQ(2, ps_node_type_size(cus));
  ASSERT_TRUE(psx_type_is_unsigned(cus_ty));
  ASSERT_TRUE(psx_node_integer_value_is_unsigned(cus));

    node_t *cul = parse_expr_input("(unsigned long)a");
  psx_type_t *cul_ty = psx_node_get_type(cul);
  ASSERT_TRUE(cul_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, cul_ty->kind);
  ASSERT_EQ(8, psx_type_sizeof(cul_ty));
  ASSERT_EQ(8, ps_node_type_size(cul));
  ASSERT_TRUE(psx_type_is_unsigned(cul_ty));
  ASSERT_TRUE(psx_node_integer_value_is_unsigned(cul));

    node_t *cf = parse_expr_input("(float)a");
  psx_type_t *cf_ty = psx_node_get_type(cf);
  ASSERT_TRUE(cf_ty != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, cf_ty->kind);
  ASSERT_EQ(4, psx_type_sizeof(cf_ty));

    node_t *cp = parse_expr_input("(double*)a");
  psx_type_t *cp_ty = psx_node_get_type(cp);
  ASSERT_TRUE(cp_ty != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, cp_ty->kind);
  ASSERT_EQ(8, ps_node_type_size(cp));
  ASSERT_EQ(8, psx_type_deref_size(cp_ty));
  ASSERT_EQ(8, ps_node_deref_size(cp));
  ASSERT_EQ(1, psx_node_pointer_qual_levels(cp));
  ASSERT_EQ(8, psx_node_base_deref_size(cp));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, psx_node_pointee_fp_kind(cp));
  ASSERT_TRUE(ps_node_is_pointer(cp));

    node_t *uac_promote = parse_expr_input("(unsigned char)1 + (short)2");
  psx_type_t *uac_promote_ty = psx_node_get_type(uac_promote);
  ASSERT_TRUE(uac_promote_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, uac_promote_ty->kind);
  ASSERT_EQ(4, psx_type_sizeof(uac_promote_ty));
  ASSERT_TRUE(!psx_type_is_unsigned(uac_promote_ty));
  ASSERT_EQ(4, ps_node_type_size(uac_promote));
  ASSERT_TRUE(!psx_node_integer_value_is_unsigned(uac_promote));
  ASSERT_TRUE(!psx_node_usual_arith_is_unsigned(uac_promote));

    node_t *uac_signed_wider = parse_expr_input("(unsigned int)1 + (long)-1");
  psx_type_t *uac_signed_wider_ty = psx_node_get_type(uac_signed_wider);
  ASSERT_TRUE(uac_signed_wider_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, uac_signed_wider_ty->kind);
  ASSERT_EQ(8, psx_type_sizeof(uac_signed_wider_ty));
  ASSERT_TRUE(!psx_type_is_unsigned(uac_signed_wider_ty));
  ASSERT_EQ(8, ps_node_type_size(uac_signed_wider));
  ASSERT_TRUE(!psx_node_integer_value_is_unsigned(uac_signed_wider));
  ASSERT_TRUE(!psx_node_usual_arith_is_unsigned(uac_signed_wider));

    node_t *uac_unsigned_same_width = parse_expr_input("(unsigned long)1 + (long)-1");
  psx_type_t *uac_unsigned_same_width_ty = psx_node_get_type(uac_unsigned_same_width);
  ASSERT_TRUE(uac_unsigned_same_width_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, uac_unsigned_same_width_ty->kind);
  ASSERT_EQ(8, psx_type_sizeof(uac_unsigned_same_width_ty));
  ASSERT_TRUE(psx_type_is_unsigned(uac_unsigned_same_width_ty));
  ASSERT_EQ(8, ps_node_type_size(uac_unsigned_same_width));
  ASSERT_TRUE(psx_node_integer_value_is_unsigned(uac_unsigned_same_width));
  ASSERT_TRUE(psx_node_usual_arith_is_unsigned(uac_unsigned_same_width));

    node_t *uac_long_long = parse_expr_input("((unsigned long long)9ULL) ^ ((unsigned short)3)");
  psx_type_t *uac_long_long_ty = psx_node_get_type(uac_long_long);
  ASSERT_TRUE(uac_long_long_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, uac_long_long_ty->kind);
  ASSERT_EQ(8, psx_type_sizeof(uac_long_long_ty));
  ASSERT_TRUE(psx_type_is_unsigned(uac_long_long_ty));
  ASSERT_TRUE(uac_long_long_ty->is_long_long);
  ASSERT_EQ(8, ps_node_type_size(uac_long_long));
  ASSERT_TRUE(psx_node_integer_value_is_unsigned(uac_long_long));
  ASSERT_TRUE(psx_node_usual_arith_is_unsigned(uac_long_long));

    node_t *ternary_uac = parse_expr_input("1 ? (unsigned int)1 : (long)-1");
  psx_type_t *ternary_uac_ty = psx_node_get_type(ternary_uac);
  ASSERT_TRUE(ternary_uac_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, ternary_uac_ty->kind);
  ASSERT_EQ(8, psx_type_sizeof(ternary_uac_ty));
  ASSERT_TRUE(!psx_type_is_unsigned(ternary_uac_ty));
  ASSERT_EQ(8, ps_node_type_size(ternary_uac));
  ASSERT_TRUE(!psx_node_integer_value_is_unsigned(ternary_uac));
  ASSERT_TRUE(!psx_node_usual_arith_is_unsigned(ternary_uac));

    node_t *cmp_uac = parse_expr_input("(unsigned int)1 < (long)-1");
  ASSERT_TRUE(!psx_node_usual_arith_is_unsigned(cmp_uac));
}

static void test_expr_inc_dec() {
  printf("test_expr_inc_dec...\n");

    node_t *prei = parse_expr_input("++a");
  ASSERT_EQ(ND_PRE_INC, prei->kind);
  ASSERT_EQ(ND_LVAR, prei->lhs->kind);

    node_t *pred = parse_expr_input("--a");
  ASSERT_EQ(ND_PRE_DEC, pred->kind);
  ASSERT_EQ(ND_LVAR, pred->lhs->kind);

    node_t *posti = parse_expr_input("a++");
  ASSERT_EQ(ND_POST_INC, posti->kind);
  ASSERT_EQ(ND_LVAR, posti->lhs->kind);

    node_t *postd = parse_expr_input("a--");
  ASSERT_EQ(ND_POST_DEC, postd->kind);
  ASSERT_EQ(ND_LVAR, postd->lhs->kind);
}

static void test_expr_assign() {
  printf("test_expr_assign...\n");
    node_t *node = parse_expr_input("a = 3");

  ASSERT_EQ(ND_ASSIGN, node->kind);
  ASSERT_EQ(ND_LVAR, node->lhs->kind);
  ASSERT_TRUE(as_lvar(node->lhs)->offset >= 0);
  ASSERT_EQ(ND_NUM, node->rhs->kind);
  ASSERT_EQ(3, as_num(node->rhs)->val);
}

static void test_expr_compound_assign() {
  printf("test_expr_compound_assign...\n");

    node_t *add = parse_expr_input("a += 3");
  ASSERT_EQ(ND_ASSIGN, add->kind);
  ASSERT_EQ(ND_ADD, add->rhs->kind);

    node_t *sub = parse_expr_input("a -= 3");
  ASSERT_EQ(ND_ASSIGN, sub->kind);
  ASSERT_EQ(ND_SUB, sub->rhs->kind);

    node_t *mul = parse_expr_input("a *= 3");
  ASSERT_EQ(ND_ASSIGN, mul->kind);
  ASSERT_EQ(ND_MUL, mul->rhs->kind);

    node_t *div = parse_expr_input("a /= 3");
  ASSERT_EQ(ND_ASSIGN, div->kind);
  ASSERT_EQ(ND_DIV, div->rhs->kind);

    node_t *mod = parse_expr_input("a %= 3");
  ASSERT_EQ(ND_ASSIGN, mod->kind);
  ASSERT_EQ(ND_MOD, mod->rhs->kind);

    node_t *shl = parse_expr_input("a <<= 3");
  ASSERT_EQ(ND_ASSIGN, shl->kind);
  ASSERT_EQ(ND_SHL, shl->rhs->kind);

    node_t *shr = parse_expr_input("a >>= 3");
  ASSERT_EQ(ND_ASSIGN, shr->kind);
  ASSERT_EQ(ND_SHR, shr->rhs->kind);

    node_t *band = parse_expr_input("a &= 3");
  ASSERT_EQ(ND_ASSIGN, band->kind);
  ASSERT_EQ(ND_BITAND, band->rhs->kind);

    node_t *bxor = parse_expr_input("a ^= 3");
  ASSERT_EQ(ND_ASSIGN, bxor->kind);
  ASSERT_EQ(ND_BITXOR, bxor->rhs->kind);

    node_t *bor = parse_expr_input("a |= 3");
  ASSERT_EQ(ND_ASSIGN, bor->kind);
  ASSERT_EQ(ND_BITOR, bor->rhs->kind);
}

static void test_expr_comma() {
  printf("test_expr_comma...\n");

    node_t *node = parse_expr_input("a=1, b=2, a+b");
  ASSERT_EQ(ND_COMMA, node->kind);
  ASSERT_EQ(ND_COMMA, node->lhs->kind);
  ASSERT_EQ(ND_ADD, node->rhs->kind);
}

static void test_program_funcdef() {
  printf("test_program_funcdef...\n");
  parsed_code = parse_program_input("int main(void) { int a=1; int b=2; a+b; }");

  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_EQ(0, as_func(parsed_code[0])->nargs);

  node_t *body = as_func(parsed_code[0])->base.rhs;
  ASSERT_EQ(ND_BLOCK, body->kind);
  ASSERT_EQ(ND_ASSIGN, as_block(body)->body[0]->kind);
  ASSERT_EQ(0, as_lvar(as_block(body)->body[0]->lhs)->offset);
  ASSERT_EQ(ND_ASSIGN, as_block(body)->body[1]->kind);
  ASSERT_EQ(4, as_lvar(as_block(body)->body[1]->lhs)->offset);
  ASSERT_EQ(ND_ADD, as_block(body)->body[2]->kind);
  ASSERT_TRUE(as_block(body)->body[3] == NULL);
  ASSERT_TRUE(parsed_code[1] == NULL);
}

static void test_funcall() {
  printf("test_funcall...\n");
    node_t *node = parse_expr_input("add(1, 2)");

  ASSERT_EQ(ND_FUNCALL, node->kind);
  ASSERT_EQ(2, as_func(node)->nargs);
  ASSERT_EQ(1, as_num(as_func(node)->args[0])->val);
  ASSERT_EQ(2, as_num(as_func(node)->args[1])->val);

  node= parse_expr_input("foo((1,2), 3)");
  ASSERT_EQ(ND_FUNCALL, node->kind);
  ASSERT_EQ(2, as_func(node)->nargs);
  ASSERT_EQ(ND_COMMA, as_func(node)->args[0]->kind);
  ASSERT_EQ(ND_NUM, as_func(node)->args[1]->kind);
  ASSERT_EQ(3, as_num(as_func(node)->args[1])->val);

  parsed_code = parse_program_input("main() { int fp; fp(1); }");
  node_t *stmt = as_block(as_func(parsed_code[0])->base.rhs)->body[1];
  ASSERT_EQ(ND_FUNCALL, stmt->kind);
  ASSERT_EQ(ND_LVAR, as_func(stmt)->callee->kind);
  ASSERT_EQ(1, as_func(stmt)->nargs);
}

// --- ここから追加テスト ---

static void test_funcdef_with_params() {
  printf("test_funcdef_with_params...\n");
  parsed_code = parse_program_input("int add(int a, int b) { return a+b; }");

  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_EQ(2, as_func(parsed_code[0])->nargs);
  ASSERT_EQ(ND_LVAR, as_func(parsed_code[0])->args[0]->kind);
  ASSERT_EQ(ND_LVAR, as_func(parsed_code[0])->args[1]->kind);

  parsed_code = parse_program_input("int apply(int (*fp)(int), int x) { return x; }");
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_EQ(2, as_func(parsed_code[0])->nargs);

  parsed_code = parse_program_input("int sum(int a[], int n) { return n; }");
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_EQ(2, as_func(parsed_code[0])->nargs);

  // プロトタイプ宣言では名前なし仮引数を許容
  parsed_code = parse_program_input("int proto(int); int main() { return 0; }");
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_EQ(0, as_func(parsed_code[0])->nargs);
}

static void test_stmt_if() {
  printf("test_stmt_if...\n");
  parsed_code = parse_program_input("main() { if (1) 2; }");
  node_t *body = as_func(parsed_code[0])->base.rhs;
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
  parsed_code = parse_program_input("main() { if (1) 2; else 3; }");
  node_t *if_node = as_block(as_func(parsed_code[0])->base.rhs)->body[0];

  ASSERT_EQ(ND_IF, if_node->kind);
  ASSERT_EQ(1, as_num(if_node->lhs)->val);        // 条件
  ASSERT_EQ(2, as_num(if_node->rhs)->val);        // then
  ASSERT_EQ(ND_NUM, as_ctrl(if_node)->els->kind);  // else
  ASSERT_EQ(3, as_num(as_ctrl(if_node)->els)->val);
}

static void test_stmt_while() {
  printf("test_stmt_while...\n");
  parsed_code = parse_program_input("main() { while (1) 2; }");
  node_t *wh = as_block(as_func(parsed_code[0])->base.rhs)->body[0];

  ASSERT_EQ(ND_WHILE, wh->kind);
  ASSERT_EQ(1, as_num(wh->lhs)->val);   // 条件
  ASSERT_EQ(2, as_num(wh->rhs)->val);   // ループ本体
}

static void test_stmt_do_while() {
  printf("test_stmt_do_while...\n");
  parsed_code = parse_program_input("int main(void) { int a = 0; do a=a+1; while (a<3); }");
  /* body[0] は int a=0 の初期化代入、body[1] が do-while */
  node_t *dw = as_block(as_func(parsed_code[0])->base.rhs)->body[1];

  ASSERT_EQ(ND_DO_WHILE, dw->kind);
  ASSERT_EQ(ND_ASSIGN, dw->rhs->kind);  // 本体: a=a+1
  ASSERT_EQ(ND_LT, dw->lhs->kind);      // 条件: a<3
}

static void test_stmt_break_continue() {
  printf("test_stmt_break_continue...\n");
  parsed_code = parse_program_input("main() { while (1) { continue; break; } }");
  node_t *wh = as_block(as_func(parsed_code[0])->base.rhs)->body[0];
  node_t *body = wh->rhs;

  ASSERT_EQ(ND_WHILE, wh->kind);
  ASSERT_EQ(ND_BLOCK, body->kind);
  ASSERT_EQ(ND_CONTINUE, as_block(body)->body[0]->kind);
  ASSERT_EQ(ND_BREAK, as_block(body)->body[1]->kind);
}

static void test_stmt_switch_case_default() {
  printf("test_stmt_switch_case_default...\n");
  parsed_code = parse_program_input("int main(void) { int a = 0; switch (a) { case 1: a=2; break; default: a=3; } }");
  /* body[0] は int a = 0、body[1] が switch */
  node_t *sw = as_block(as_func(parsed_code[0])->base.rhs)->body[1];

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
  parsed_code = parse_program_input("int main(void) { int a; for (a=0; a<10; a=a+1) a; }");
  /* body[0] は int a; (宣言のみで初期化なし → ND_NUM ダミー)、body[1] が for */
  node_t *fr = as_block(as_func(parsed_code[0])->base.rhs)->body[1];

  ASSERT_EQ(ND_FOR, fr->kind);
  ASSERT_EQ(ND_ASSIGN, as_ctrl(fr)->init->kind);  // init: a=0
  ASSERT_EQ(ND_LT, fr->lhs->kind);      // 条件: a<10
  ASSERT_EQ(ND_ASSIGN, as_ctrl(fr)->inc->kind);   // inc: a=a+1
  ASSERT_EQ(ND_LVAR, fr->rhs->kind);     // 本体: a
}

static void test_stmt_for_with_decl_init() {
  printf("test_stmt_for_with_decl_init...\n");
  parsed_code = parse_program_input("main() { for (int i=0; i<3; i=i+1) i; }");
  node_t *fr = as_block(as_func(parsed_code[0])->base.rhs)->body[0];

  ASSERT_EQ(ND_FOR, fr->kind);
  ASSERT_EQ(ND_ASSIGN, as_ctrl(fr)->init->kind);  // init: int i=0
  ASSERT_EQ(ND_LT, fr->lhs->kind);                // 条件: i<3
  ASSERT_EQ(ND_ASSIGN, as_ctrl(fr)->inc->kind);   // inc: i=i+1
  ASSERT_EQ(ND_LVAR, fr->rhs->kind);              // 本体: i

  parsed_code = parse_program_input("main() { for (int i=0, j=2; i<j; i=i+1) i; }");
  fr = as_block(as_func(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_FOR, fr->kind);
  ASSERT_EQ(ND_COMMA, as_ctrl(fr)->init->kind);   // init: int i=0, j=2
}

static void test_stmt_return() {
  printf("test_stmt_return...\n");
  parsed_code = parse_program_input("main() { return 42; }");
  node_t *ret = as_block(as_func(parsed_code[0])->base.rhs)->body[0];

  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_NUM, ret->lhs->kind);
  ASSERT_EQ(42, as_num(ret->lhs)->val);

  parsed_code = parse_program_input("void noop() { return; }");
  ret = as_block(as_func(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_TRUE(ret->lhs == NULL);

  parsed_code = parse_program_input("_Bool flag(void) { return 200; }");
  ret = as_block(as_func(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_NE, ret->lhs->kind);
  ASSERT_EQ(ND_NUM, ret->lhs->rhs->kind);
  ASSERT_EQ(0, as_num(ret->lhs->rhs)->val);

  parsed_code = parse_program_input("char narrow(int x) { return x; }");
  ret = as_block(as_func(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_SHR, ret->lhs->kind);
  ASSERT_EQ(ND_SHL, ret->lhs->lhs->kind);
  ASSERT_TRUE(!psx_node_shift_operation_is_unsigned(ret->lhs->lhs));
  ASSERT_TRUE(!psx_node_shift_operation_is_unsigned(ret->lhs));

  parsed_code = parse_program_input("int cast_unsigned_local(void) { unsigned u; return (int)u; }");
  ret = as_block(as_func(parsed_code[0])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_CAST, ret->lhs->kind);
  ASSERT_TRUE(!psx_node_conversion_value_is_unsigned(ret->lhs));
  ASSERT_EQ(ND_LVAR, ret->lhs->lhs->kind);
  ASSERT_TRUE(psx_node_integer_value_is_unsigned(ret->lhs->lhs));

  parsed_code = parse_program_input("int cast_pointer_int(int *p) { return (int)p; }");
  ret = as_block(as_func(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_CAST, ret->lhs->kind);
  ASSERT_TRUE(!ps_node_is_pointer(ret->lhs));
  ASSERT_EQ(4, ps_node_type_size(ret->lhs));
  ASSERT_EQ(ND_LVAR, ret->lhs->lhs->kind);
  ASSERT_TRUE(ps_node_is_pointer(ret->lhs->lhs));

  parsed_code = parse_program_input("long cast_pointer_long(int *p) { return (long)p; }");
  ret = as_block(as_func(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_CAST, ret->lhs->kind);
  ASSERT_TRUE(!ps_node_is_pointer(ret->lhs));
  ASSERT_EQ(8, ps_node_type_size(ret->lhs));
  ASSERT_EQ(ND_LVAR, ret->lhs->lhs->kind);
  ASSERT_TRUE(ps_node_is_pointer(ret->lhs->lhs));

  parsed_code = parse_program_input("int deref_intptr_cast(long addr) { return *(int *)addr; }");
  ret = as_block(as_func(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_DEREF, ret->lhs->kind);
  ASSERT_EQ(ND_CAST, ret->lhs->lhs->kind);
  ASSERT_TRUE(ps_node_is_pointer(ret->lhs->lhs));
  ASSERT_EQ(4, ps_node_deref_size(ret->lhs->lhs));
  ASSERT_EQ(ND_LVAR, ret->lhs->lhs->lhs->kind);
  ASSERT_TRUE(!ps_node_is_pointer(ret->lhs->lhs->lhs));

  parsed_code = parse_program_input("double void_cast_keeps_operand_fp(double d) { (void)d; return d; }");
  node_block_t *body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_CAST, body->body[0]->kind);
  ASSERT_TRUE(psx_node_get_type(body->body[0])->kind == PSX_TYPE_VOID);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, body->body[0]->fp_kind);
  ASSERT_EQ(ND_LVAR, body->body[0]->lhs->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, body->body[0]->lhs->fp_kind);
  ret = body->body[1];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_LVAR, ret->lhs->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ret->lhs->fp_kind);

  parsed_code = parse_program_input("unsigned char unarrow(int x) { return x; }");
  ret = as_block(as_func(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_BITAND, ret->lhs->kind);
  ASSERT_EQ(ND_NUM, ret->lhs->rhs->kind);
  ASSERT_EQ(0xff, as_num(ret->lhs->rhs)->val);
}

static void test_stmt_block() {
  printf("test_stmt_block...\n");
  parsed_code = parse_program_input("main() { { 1; 2; } }");
  node_t *blk = as_block(as_func(parsed_code[0])->base.rhs)->body[0];

  ASSERT_EQ(ND_BLOCK, blk->kind);
  ASSERT_EQ(ND_NUM, as_block(blk)->body[0]->kind);
  ASSERT_EQ(1, as_num(as_block(blk)->body[0])->val);
  ASSERT_EQ(ND_NUM, as_block(blk)->body[1]->kind);
  ASSERT_EQ(2, as_num(as_block(blk)->body[1])->val);
  ASSERT_TRUE(as_block(blk)->body[2] == NULL);
}

static void test_stmt_goto_label() {
  printf("test_stmt_goto_label...\n");
  parsed_code = parse_program_input("main() { goto L1; L1: return 42; }");
  node_block_t *body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_GOTO, body->body[0]->kind);
  ASSERT_EQ(ND_LABEL, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->rhs->kind);
}

static void test_expr_deref_addr() {
  printf("test_expr_deref_addr...\n");
  // &a
    node_t *addr = parse_expr_input("&a");
  ASSERT_EQ(ND_ADDR, addr->kind);
  ASSERT_EQ(ND_LVAR, addr->lhs->kind);

  // *p (p が変数として存在する前提)
    node_t *deref = parse_expr_input("*a");
  ASSERT_EQ(ND_DEREF, deref->kind);
  ASSERT_EQ(ND_LVAR, deref->lhs->kind);
}

static void test_expr_member_access() {
  printf("test_expr_member_access...\n");
  parsed_code = parse_program_input("main() { struct S { int a; int b; }; struct S s; s.b = 3; return s.b; }");
  node_block_t *body = as_block(as_func(parsed_code[0])->base.rhs);
  node_t *assign = body->body[2];
  ASSERT_EQ(ND_ASSIGN, assign->kind);
  ASSERT_EQ(ND_DEREF, assign->lhs->kind);
  ASSERT_EQ(4, as_mem(assign->lhs)->type_size);
  ASSERT_EQ(ND_ADD, assign->lhs->lhs->kind);

  node_t *ret = body->body[3];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_DEREF, ret->lhs->kind);

  parsed_code = parse_program_input("main() { struct S { int a; int b; }; struct S s; struct S *p; p=&s; p->b=5; return p->b; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  assign = body->body[4];
  ASSERT_EQ(ND_ASSIGN, assign->kind);
  ASSERT_EQ(ND_DEREF, assign->lhs->kind);
  ASSERT_EQ(4, as_mem(assign->lhs)->type_size);

  parsed_code = parse_program_input("main() { struct S { int a[2]; }; struct S s={{1,2}}; return s.a[0]; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_TRUE(body->body[1]->kind == ND_ASSIGN || body->body[1]->kind == ND_COMMA);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input(
      "int first(char *p) { return p[0]; } "
      "int main() { struct C { char x[1]; }; "
      "struct C c = {\"Z\"}; return first(c.x); }");
  body = as_block(as_func(parsed_code[1])->base.rhs);
  ret = body->body[2];
  ASSERT_EQ(ND_RETURN, ret->kind);
  node_t *call = ret->lhs;
  ASSERT_EQ(ND_FUNCALL, call->kind);
  ASSERT_EQ(1, as_func(call)->nargs);
  node_t *array_member = as_func(call)->args[0];
  ASSERT_EQ(ND_DEREF, array_member->kind);
  ASSERT_EQ(1, as_mem(array_member)->is_array_member);
  ASSERT_EQ(1, as_mem(array_member)->type_size);
  ASSERT_EQ(1, as_mem(array_member)->deref_size);
}

static void test_expr_string() {
  printf("test_expr_string...\n");
  string_literals = NULL; // リセット
    node_t *node = parse_expr_input("\"hello\"");

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
    node_t *node = parse_expr_input("\"he\" \"llo\"");

  ASSERT_EQ(ND_STRING, node->kind);
  ASSERT_TRUE(string_literals != NULL);
  ASSERT_EQ(5, string_literals->len);
  ASSERT_TRUE(strncmp(string_literals->str, "hello", 5) == 0);
}

static void test_type_decl() {
  printf("test_type_decl...\n");
  // int x = 5; → ND_ASSIGN
  parsed_code = parse_program_input("main() { int x = 5; }");
  node_t *stmt = as_block(as_func(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_ASSIGN, stmt->kind);
  ASSERT_EQ(ND_LVAR, stmt->lhs->kind);
  ASSERT_EQ(5, as_num(stmt->rhs)->val);

  // int x; → ND_NUM(0) ダミー
  parsed_code = parse_program_input("main() { int x; }");
  stmt = as_block(as_func(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_NUM, stmt->kind);
  ASSERT_EQ(0, as_num(stmt)->val);

  // int a, b=1; → 初期化のある宣言子のみ式木に残る
  parsed_code = parse_program_input("main() { int a, b=1; }");
  stmt = as_block(as_func(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_ASSIGN, stmt->kind);
  ASSERT_EQ(ND_NUM, stmt->rhs->kind);
  ASSERT_EQ(1, as_num(stmt->rhs)->val);

  parsed_code = parse_program_input("main() { int a=1, b=2; }");
  stmt = as_block(as_func(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_COMMA, stmt->kind);

  parsed_code = parse_program_input("main() { struct S; union U; enum E; return 0; }");
  node_block_t *body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_NUM, body->body[1]->kind);
  ASSERT_EQ(ND_NUM, body->body[2]->kind);
  ASSERT_EQ(ND_RETURN, body->body[3]->kind);

  parsed_code = parse_program_input("main() { struct S; struct S *p; p=0; return p==0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_NUM, body->body[1]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[2]->kind);
  ASSERT_EQ(ND_RETURN, body->body[3]->kind);

  parsed_code = parse_program_input("main() { struct S { int x; }; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("main() { struct S { int x; } *p; p=0; return p==0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("main() { union U { int x; char y; }; enum E { A=1, B=2 }; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_NUM, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("main() { enum E { A=1, B, C=10 }; return A+B+C; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("main() { enum E { A=1, B=A+2, C=(B*2)-1 }; return C; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("main() { enum E { A=1, B=~A, C=(A<<3)|2, D=(C&10)^1 }; return D; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("main() { enum E { A=1, B=(A<2), C=(A==1)&&(B||0), D=C?7:9 }; return D; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("main() { unsigned u = 3; _Bool b = 1; signed s = 2; return u+b+s; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[2]->kind);
  ASSERT_EQ(ND_RETURN, body->body[3]->kind);

  parsed_code = parse_program_input("typedef int myint; main() { myint x = 3; return x; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("typedef int *intptr; main() { int a=7; intptr p=&a; return *p; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("typedef int (*fp_t)(int); int f(int x){ return x+1; } main() { fp_t p; return 0; }");
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[1]->kind);

  parsed_code = parse_program_input("typedef int (((*fp_t)))(int); int f(int x){ return x+1; } main() { fp_t p; return 0; }");
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[1]->kind);

  parsed_code = parse_program_input("extern int a[]; int main(){ return 0; }");
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  {
    global_var_t *gv = global_vars;
    int found = 0;
    for (; gv; gv = gv->next) {
      if (gv->name_len == 1 && gv->name[0] == 'a') {
        found = 1;
        ASSERT_EQ(1, gv->is_extern_decl);
        ASSERT_EQ(1, gv->is_array);
        ASSERT_EQ(0, gv->type_size);
        break;
      }
    }
    ASSERT_TRUE(found);
  }

  parsed_code = parse_program_input("main() { unsigned long long v = 13; signed char c = 7; return v+c; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("typedef unsigned long long ull; main() { ull v = 5; return v; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  /* static ローカル変数はグローバル変数として lowering されるため、
   * 関数本体にはダミーの ND_NUM(0) だけが残る (init はデータセクションで行う)。
   * 続く register/auto/restrict 宣言は通常どおり ND_ASSIGN を生成。 */
  parsed_code = parse_program_input("main() { static int x=3; register int r=2; auto int a=1; int *restrict p=0; return a+r+x+(p==0); }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[2]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[3]->kind);
  ASSERT_EQ(ND_RETURN, body->body[4]->kind);

  parsed_code = parse_program_input(
      "main() { const const int x=3; volatile volatile int y=4; int *restrict restrict p=0; return x+y+(p==0); }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[2]->kind);
  ASSERT_EQ(ND_RETURN, body->body[3]->kind);

  parsed_code = parse_program_input(
      "int sumq(const const int a, volatile volatile int b, int *restrict restrict p){ return a+b+(p==0); }"
      "main(){ return sumq(3,4,0); }");
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[1]->kind);

  parsed_code = parse_program_input("main() { _Alignas(16) int x=3; _Atomic int y=4; return x+y; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("main() { _Atomic(int) z=5; return z; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("main() { _Atomic(int*) p=0; _Atomic int q=1; return q + (p==0); }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("main() { _Complex double cx=1.0; _Imaginary float iy=2.0f; return (cx!=0)+(iy!=0); }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("main() { _Complex double a=1.0; _Complex double b=2.0; _Complex double c=a+b; return c!=0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[2]->kind);
  ASSERT_EQ(ND_RETURN, body->body[3]->kind);

  parsed_code = parse_program_input("main() { int a[1+2]; a[0]=3; return a[0]; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("main() { int a[2][3]; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("main() { struct S { int x:3; int y; }; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("main() { struct S { struct { int x; }; int y; }; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("main() { struct S { union { int x; char c; }; int y; }; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("main() { _Static_assert(1, \"ok\"); int x=3; return x; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("main() { int x={3}; return x; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(0, as_lvar(body->body[0]->lhs)->mem.is_const_qualified);
  ASSERT_EQ(0, as_lvar(body->body[0]->lhs)->mem.is_volatile_qualified);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("main() { enum E { A=1 }; return (enum E)42; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("main() { const int cx=1; volatile int vx=2; return cx+vx; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(1, as_lvar(body->body[0]->lhs)->mem.is_const_qualified);
  ASSERT_EQ(0, as_lvar(body->body[0]->lhs)->mem.is_volatile_qualified);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(0, as_lvar(body->body[1]->lhs)->mem.is_const_qualified);
  ASSERT_EQ(1, as_lvar(body->body[1]->lhs)->mem.is_volatile_qualified);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("main() { int *const pc=0; int *volatile pv=0; return (pc==0)+(pv==0); }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(1, as_lvar(body->body[0]->lhs)->mem.is_pointer_const_qualified);
  ASSERT_EQ(0, as_lvar(body->body[0]->lhs)->mem.is_pointer_volatile_qualified);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(0, as_lvar(body->body[1]->lhs)->mem.is_pointer_const_qualified);
  ASSERT_EQ(1, as_lvar(body->body[1]->lhs)->mem.is_pointer_volatile_qualified);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("main() { int *const *volatile pp=0; return pp==0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(2, as_lvar(body->body[0]->lhs)->mem.pointer_qual_levels);
  ASSERT_EQ(1u, as_lvar(body->body[0]->lhs)->mem.pointer_const_qual_mask);
  ASSERT_EQ(2u, as_lvar(body->body[0]->lhs)->mem.pointer_volatile_qual_mask);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("main() { int x=42; int *p=&x; int **pp=&p; return **pp; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[2]->kind);
  ASSERT_EQ(ND_RETURN, body->body[3]->kind);

  parsed_code = parse_program_input("main() { int a[3]={1,2,3}; return a[2]; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_COMMA, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("main() { int a[2]=1; return a[0]+a[1]; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_COMMA, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("main() { char s[4]=\"abc\"; return s[2]; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_COMMA, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("main() { struct S { int x; int y; }; struct S s={1,2}; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("main() { struct S { int x; int y; }; struct S t={1,2}; struct S s=t; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_COMMA, body->body[2]->kind);
  ASSERT_EQ(ND_RETURN, body->body[3]->kind);

  parsed_code = parse_program_input("main() { struct S { int x; int y; }; struct S a={1,2}; struct S b={3,4}; struct S s=(0?a:b); return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_COMMA, body->body[2]->kind);
  ASSERT_EQ(ND_TERNARY, body->body[3]->kind);
  ASSERT_EQ(ND_RETURN, body->body[4]->kind);

  parsed_code = parse_program_input("main() { struct S { int x; int y; }; struct S t={1,2}; struct S s=(t.y=9,t); return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_COMMA, body->body[2]->kind);
  ASSERT_EQ(ND_RETURN, body->body[3]->kind);

  parsed_code = parse_program_input("main() { struct S { int a[2]; int z; }; struct S t={{1,2},3}; struct S s=t; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_COMMA, body->body[2]->kind);
  ASSERT_EQ(ND_RETURN, body->body[3]->kind);

  parsed_code = parse_program_input("main() { struct I { int x; int y; }; struct S { struct I i; int z; }; struct S t={{1,2},3}; struct S s=t; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_NUM, body->body[1]->kind);
  ASSERT_EQ(ND_COMMA, body->body[2]->kind);
  ASSERT_EQ(ND_COMMA, body->body[3]->kind);
  ASSERT_EQ(ND_RETURN, body->body[4]->kind);

  parsed_code = parse_program_input("main() { union U { int x; char y; }; union U u={7}; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("main() { union U { int x; char y; }; union U u=7; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("main() { union U { int x; char y; }; union U v={7}; union U u=v; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_COMMA, body->body[2]->kind);
  ASSERT_EQ(ND_RETURN, body->body[3]->kind);

  parsed_code = parse_program_input("main() { union U { int x; char y; }; union U v={7}; union U u=(v.x=9,v); return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_COMMA, body->body[2]->kind);
  ASSERT_EQ(ND_RETURN, body->body[3]->kind);

  parsed_code = parse_program_input("main() { union U { int x; char y; }; union U u={.x=7}; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("main() { union U { int x; char y; }; union U u={.x=7,.y=2}; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("main() { struct S { int x; int y; }; struct S s={.y=2,.x=1}; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("main() { struct S { int x; }; struct S s=(struct S)(struct S){1}; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("main() { struct A { int x; }; struct B { int x; }; struct A a={1}; struct B b=(struct B)a; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  bool has_return = false;
  for (int i = 1; body->body[i]; i++) {
    if (body->body[i]->kind == ND_RETURN) {
      has_return = true;
      break;
    }
  }
  ASSERT_TRUE(has_return);

  parsed_code = parse_program_input("main() { union U { int x; char y; }; union U u=(union U)(union U){.x=7}; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_TRUE(body->body[1]->kind == ND_ASSIGN || body->body[1]->kind == ND_COMMA);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("main() { union A { int x; }; union B { int x; }; union A a={.x=1}; union B b=(union B)a; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  has_return = false;
  for (int i = 1; body->body[i]; i++) {
    if (body->body[i]->kind == ND_RETURN) {
      has_return = true;
      break;
    }
  }
  ASSERT_TRUE(has_return);

  parsed_code = parse_program_input("main() { struct I { int x; int y; }; struct O { struct I i; int z; }; struct O o={{1,2},3}; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_TRUE(body->body[1]->kind == ND_NUM || body->body[1]->kind == ND_COMMA || body->body[1]->kind == ND_ASSIGN);
  ASSERT_TRUE((body->body[2] && body->body[2]->kind == ND_RETURN) ||
              (body->body[3] && body->body[3]->kind == ND_RETURN));

  parsed_code = parse_program_input("main() { struct I { int x; int y; }; union U { struct I i; int raw; }; union U u={{4,5}}; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_TRUE(body->body[1]->kind == ND_NUM || body->body[1]->kind == ND_ASSIGN);
  ASSERT_TRUE((body->body[2] && body->body[2]->kind == ND_RETURN) ||
              (body->body[3] && body->body[3]->kind == ND_RETURN));

  parsed_code = parse_program_input("main() { union U { int x; char y; }; union U u={.x=1,.y=2}; return u.x; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_TRUE(body->body[1]->kind == ND_COMMA || body->body[1]->kind == ND_ASSIGN);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("main() { struct S { int a[2]; int z; }; struct S s={{1,2},3}; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("main() { struct S { int a[2]; int z; }; struct S s={1,2,3}; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("main() { int src[2]={5,6}; struct S { int a[2]; int z; }; struct S s={src,7}; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_COMMA, body->body[0]->kind);
  ASSERT_TRUE(body->body[1]->kind == ND_NUM || body->body[1]->kind == ND_COMMA || body->body[1]->kind == ND_ASSIGN);
  ASSERT_TRUE((body->body[2] && body->body[2]->kind == ND_RETURN) ||
              (body->body[3] && body->body[3]->kind == ND_RETURN));

  parsed_code = parse_program_input("main() { struct S { char a[4]; int z; }; struct S s={\"ab\",7}; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("main() { int a[4]={[2]=7,[0]=1}; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_COMMA, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  // 入れ子 designator: struct の配列メンバへの .member[idx]=val
  parsed_code = parse_program_input("main() { struct S { int a[2]; }; struct S s={.a[1]=3}; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  // brace init 開始で struct 全体を 0 で埋める処理が入り、init_chain は
  // ND_COMMA チェイン (zero-fills → 明示 .a[1]=3) になる。
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  // 入れ子 designator: struct の配列メンバへの複数指定
  parsed_code = parse_program_input("main() { struct S { int a[2]; }; struct S s={.a[0]=1,.a[1]=2}; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  // 入れ子 designator: union の配列メンバへの .member[idx]=val
  parsed_code = parse_program_input("main() { union U { int a[2]; int z; }; union U u={.a[1]=3}; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);
}

static void test_type_metadata_bridge() {
  printf("test_type_metadata_bridge...\n");

  parsed_code = parse_program_input("main() { unsigned int x=1; return x; }");
  node_func_t *fn = as_func(parsed_code[0]);
  node_block_t *body = as_block(fn->base.rhs);
  node_t *assign = body->body[0];
  ASSERT_EQ(ND_ASSIGN, assign->kind);
  psx_type_t *unsigned_ty = psx_node_get_type(assign->lhs);
  ASSERT_TRUE(unsigned_ty != NULL);
  ASSERT_TRUE(assign->lhs->type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, unsigned_ty->kind);
  ASSERT_EQ(4, psx_type_sizeof(unsigned_ty));
  ASSERT_TRUE(psx_type_is_unsigned(unsigned_ty));
  lvar_t *x_lvar = find_func_lvar(fn, "x");
  ASSERT_TRUE(x_lvar != NULL);
  ASSERT_TRUE(psx_node_lvar_symbol(assign->lhs) == x_lvar);
  ASSERT_TRUE(x_lvar->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, x_lvar->decl_type->kind);
  ASSERT_EQ(4, psx_type_sizeof(x_lvar->decl_type));
  ASSERT_TRUE(psx_type_is_unsigned(x_lvar->decl_type));
  x_lvar->decl_type = NULL;
  psx_type_t *x_decl_a = psx_lvar_get_decl_type(x_lvar);
  psx_type_t *x_decl_b = psx_lvar_get_decl_type(x_lvar);
  ASSERT_TRUE(x_decl_a != NULL);
  ASSERT_TRUE(x_decl_a == x_decl_b);
  ASSERT_TRUE(x_lvar->decl_type == x_decl_a);

  lvar_t tmp_lvar = {0};
  psx_decl_init_lvar_storage_type(&tmp_lvar, 4, 4, 0,
                                  TK_FLOAT_KIND_NONE, 0, TK_EOF, NULL, 0, 0);
  psx_type_t *tmp_lvar_int = psx_lvar_get_decl_type(&tmp_lvar);
  ASSERT_TRUE(tmp_lvar_int != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, tmp_lvar_int->kind);
  psx_decl_set_lvar_pointer_derived_type(&tmp_lvar, 1, 4, 0);
  tmp_lvar.size = 8;
  tmp_lvar.elem_size = 4;
  ASSERT_TRUE(tmp_lvar.decl_type == NULL);
  psx_type_t *tmp_lvar_ptr = psx_lvar_refresh_decl_type(&tmp_lvar);
  ASSERT_TRUE(tmp_lvar_ptr != NULL);
  ASSERT_TRUE(tmp_lvar_ptr != tmp_lvar_int);
  ASSERT_TRUE(tmp_lvar.decl_type == tmp_lvar_ptr);
  ASSERT_EQ(PSX_TYPE_POINTER, tmp_lvar_ptr->kind);

  lvar_t tmp_fp_ptr_lvar = {0};
  psx_decl_init_lvar_storage_type(&tmp_fp_ptr_lvar, 8, 8, 0,
                                  TK_FLOAT_KIND_NONE, 0, TK_EOF, NULL, 0, 0);
  psx_decl_set_lvar_pointer_derived_type(&tmp_fp_ptr_lvar, 1, 8, 0);
  psx_type_t *tmp_fp_ptr_int = psx_lvar_get_decl_type(&tmp_fp_ptr_lvar);
  ASSERT_TRUE(tmp_fp_ptr_int != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, tmp_fp_ptr_int->kind);
  psx_decl_set_lvar_pointee_fp_kind(&tmp_fp_ptr_lvar, TK_FLOAT_KIND_DOUBLE);
  ASSERT_TRUE(tmp_fp_ptr_lvar.decl_type == NULL);
  psx_type_t *tmp_fp_ptr_double = psx_lvar_refresh_decl_type(&tmp_fp_ptr_lvar);
  ASSERT_TRUE(tmp_fp_ptr_double != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, tmp_fp_ptr_double->kind);
  ASSERT_TRUE(tmp_fp_ptr_double->base != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, tmp_fp_ptr_double->base->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, tmp_fp_ptr_double->base->fp_kind);

  lvar_t tmp_complex_lvar = {0};
  psx_decl_init_lvar_storage_type(&tmp_complex_lvar, 16, 16, 0,
                                  TK_FLOAT_KIND_DOUBLE, 0, TK_EOF, NULL, 0, 0);
  psx_type_t *tmp_scalar_double = psx_lvar_get_decl_type(&tmp_complex_lvar);
  ASSERT_TRUE(tmp_scalar_double != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, tmp_scalar_double->kind);
  psx_decl_set_lvar_complex(&tmp_complex_lvar, 1);
  ASSERT_TRUE(tmp_complex_lvar.decl_type == NULL);
  psx_type_t *tmp_complex_type = psx_lvar_refresh_decl_type(&tmp_complex_lvar);
  ASSERT_TRUE(tmp_complex_type != NULL);
  ASSERT_EQ(PSX_TYPE_COMPLEX, tmp_complex_type->kind);

  node_mem_t typed_mem = {0};
  typed_mem.base.kind = ND_LVAR;
  typed_mem.type_size = 4;
  typed_mem.base.type = psx_type_new_integer(TK_LONG, 8, 0);
  ASSERT_TRUE(psx_node_get_type((node_t *)&typed_mem) == typed_mem.base.type);
  ASSERT_EQ(8, ps_node_type_size((node_t *)&typed_mem));
  typed_mem.type_size = 16;
  typed_mem.is_pointer = 1;
  typed_mem.deref_size = 4;
  typed_mem.is_unsigned = 1;
  typed_mem.is_long_long = 1;
  typed_mem.is_plain_char = 1;
  typed_mem.is_long_double = 1;
  typed_mem.tag_kind = TK_STRUCT;
  typed_mem.tag_name = "Stale";
  typed_mem.tag_len = 5;
  typed_mem.tag_scope_depth_p1 = 4;
  typed_mem.is_tag_pointer = 1;
  typed_mem.inner_deref_size = 9;
  typed_mem.next_deref_size = 5;
  typed_mem.extra_strides_count = 1;
  typed_mem.extra_strides[0] = 3;
  typed_mem.vla_row_stride_frame_off = 64;
  typed_mem.vla_strides_remaining = 4;
  typed_mem.base_deref_size = 11;
  typed_mem.ptr_array_pointee_bytes = 22;
  ASSERT_TRUE(!ps_node_is_pointer((node_t *)&typed_mem));
  ASSERT_EQ(0, ps_node_deref_size((node_t *)&typed_mem));
  ASSERT_TRUE(!psx_node_is_unsigned_type((node_t *)&typed_mem));
  ASSERT_TRUE(!psx_node_conversion_value_is_unsigned((node_t *)&typed_mem));
  ASSERT_TRUE(!psx_node_is_long_long_type((node_t *)&typed_mem));
  ASSERT_TRUE(!psx_node_is_plain_char_type((node_t *)&typed_mem));
  ASSERT_TRUE(!psx_node_is_long_double_type((node_t *)&typed_mem));
  token_kind_t typed_scalar_tag_kind = TK_EOF;
  char *typed_scalar_tag_name = NULL;
  int typed_scalar_tag_len = 0;
  int typed_scalar_is_tag_pointer = 0;
  psx_node_get_tag_type((node_t *)&typed_mem, &typed_scalar_tag_kind,
                        &typed_scalar_tag_name, &typed_scalar_tag_len,
                        &typed_scalar_is_tag_pointer);
  ASSERT_EQ(TK_EOF, typed_scalar_tag_kind);
  ASSERT_TRUE(typed_scalar_tag_name == NULL);
  ASSERT_EQ(0, typed_scalar_tag_len);
  ASSERT_EQ(0, typed_scalar_is_tag_pointer);
  ASSERT_EQ(-1, psx_node_get_tag_scope_depth((node_t *)&typed_mem));
  int typed_scalar_inner_stride = 123;
  int typed_scalar_next_stride = 124;
  int typed_scalar_extra_strides[5] = {1, 2, 3, 4, 5};
  int typed_scalar_extra_count = 9;
  ASSERT_TRUE(!psx_node_pointer_stride_metadata(
      (node_t *)&typed_mem, &typed_scalar_inner_stride, &typed_scalar_next_stride,
      typed_scalar_extra_strides, &typed_scalar_extra_count));
  ASSERT_EQ(0, typed_scalar_inner_stride);
  ASSERT_EQ(0, typed_scalar_next_stride);
  ASSERT_EQ(0, typed_scalar_extra_count);
  ASSERT_EQ(0, typed_scalar_extra_strides[0]);
  ASSERT_EQ(0, psx_node_vla_row_stride_frame_off((node_t *)&typed_mem));
  ASSERT_EQ(0, psx_node_base_deref_size((node_t *)&typed_mem));
  ASSERT_EQ(0, psx_node_ptr_array_pointee_bytes((node_t *)&typed_mem));

  node_mem_t typed_missing_size_mem = {0};
  typed_missing_size_mem.base.kind = ND_LVAR;
  typed_missing_size_mem.type_size = 13;
  typed_missing_size_mem.base.type = psx_type_new(PSX_TYPE_INTEGER);
  ASSERT_EQ(0, ps_node_type_size((node_t *)&typed_missing_size_mem));

  node_mem_t typed_unsigned_mem = {0};
  typed_unsigned_mem.base.kind = ND_LVAR;
  typed_unsigned_mem.base.type = psx_type_new_integer(TK_UNSIGNED, 4, 1);
  ASSERT_TRUE(psx_node_is_unsigned_type((node_t *)&typed_unsigned_mem));
  ASSERT_TRUE(psx_node_conversion_value_is_unsigned((node_t *)&typed_unsigned_mem));

  node_mem_t typed_bool_lhs_mem = {0};
  typed_bool_lhs_mem.base.kind = ND_LVAR;
  typed_bool_lhs_mem.base.type = psx_type_new(PSX_TYPE_BOOL);
  node_mem_t *typed_bool_assign =
      psx_node_new_assign((node_t *)&typed_bool_lhs_mem, psx_node_new_num(3));
  ASSERT_TRUE(typed_bool_assign->base.rhs != NULL);
  ASSERT_EQ(ND_NE, typed_bool_assign->base.rhs->kind);

  node_mem_t typed_stale_bool_lhs_mem = {0};
  typed_stale_bool_lhs_mem.base.kind = ND_LVAR;
  typed_stale_bool_lhs_mem.is_bool = 1;
  typed_stale_bool_lhs_mem.base.type = psx_type_new_integer(TK_INT, 4, 0);
  node_mem_t *typed_stale_bool_assign =
      psx_node_new_assign((node_t *)&typed_stale_bool_lhs_mem, psx_node_new_num(3));
  ASSERT_TRUE(typed_stale_bool_assign->base.rhs != NULL);
  ASSERT_EQ(ND_NUM, typed_stale_bool_assign->base.rhs->kind);

  node_mem_t typed_assign_ptr_lhs_mem = {0};
  typed_assign_ptr_lhs_mem.base.kind = ND_LVAR;
  typed_assign_ptr_lhs_mem.type_size = 4;
  typed_assign_ptr_lhs_mem.deref_size = 0;
  typed_assign_ptr_lhs_mem.is_pointer = 0;
  typed_assign_ptr_lhs_mem.base.type =
      psx_type_new_pointer(psx_type_new_integer(TK_INT, 4, 0), 8);
  node_mem_t *typed_assign_ptr =
      psx_node_new_assign((node_t *)&typed_assign_ptr_lhs_mem, psx_node_new_num(0));
  ASSERT_TRUE(ps_node_is_pointer((node_t *)typed_assign_ptr));
  ASSERT_EQ(8, ps_node_type_size((node_t *)typed_assign_ptr));
  ASSERT_EQ(8, ps_node_deref_size((node_t *)typed_assign_ptr));
  ASSERT_EQ(4, psx_node_base_deref_size((node_t *)typed_assign_ptr));
  ASSERT_TRUE(psx_node_get_type((node_t *)typed_assign_ptr) ==
              typed_assign_ptr_lhs_mem.base.type);

  node_mem_t typed_addr_ptr_operand_mem = {0};
  typed_addr_ptr_operand_mem.base.kind = ND_LVAR;
  typed_addr_ptr_operand_mem.type_size = 4;
  typed_addr_ptr_operand_mem.deref_size = 0;
  typed_addr_ptr_operand_mem.is_pointer = 0;
  typed_addr_ptr_operand_mem.pointer_const_qual_mask = 0;
  typed_addr_ptr_operand_mem.base.type =
      psx_type_new_pointer(psx_type_new_integer(TK_INT, 4, 0), 8);
  typed_addr_ptr_operand_mem.base.type->pointer_qual_levels = 1;
  typed_addr_ptr_operand_mem.base.type->base_deref_size = 4;
  typed_addr_ptr_operand_mem.base.type->pointer_const_qual_mask = 1;
  node_t *typed_addr_ptr =
      psx_node_new_unary_addr_for((node_t *)&typed_addr_ptr_operand_mem);
  ASSERT_TRUE(ps_node_is_pointer(typed_addr_ptr));
  ASSERT_EQ(8, ps_node_type_size(typed_addr_ptr));
  ASSERT_EQ(8, ps_node_deref_size(typed_addr_ptr));
  ASSERT_EQ(2, psx_node_pointer_qual_levels(typed_addr_ptr));
  ASSERT_EQ(4, psx_node_base_deref_size(typed_addr_ptr));
  ASSERT_EQ(2u, psx_node_pointer_const_qual_mask(typed_addr_ptr));
  ASSERT_TRUE(psx_node_get_type(typed_addr_ptr)->base ==
              typed_addr_ptr_operand_mem.base.type);

  node_mem_t typed_deref_ptrptr_operand_mem = {0};
  typed_deref_ptrptr_operand_mem.base.kind = ND_DEREF;
  typed_deref_ptrptr_operand_mem.type_size = 4;
  typed_deref_ptrptr_operand_mem.deref_size = 0;
  typed_deref_ptrptr_operand_mem.is_pointer = 0;
  psx_type_t *typed_deref_inner_ptr =
      psx_type_new_pointer(psx_type_new_integer(TK_INT, 4, 0), 8);
  typed_deref_inner_ptr->pointer_qual_levels = 1;
  typed_deref_inner_ptr->base_deref_size = 4;
  psx_type_t *typed_deref_outer_ptr =
      psx_type_new_pointer(typed_deref_inner_ptr, 8);
  typed_deref_outer_ptr->pointer_qual_levels = 2;
  typed_deref_outer_ptr->base_deref_size = 4;
  typed_deref_ptrptr_operand_mem.base.type = typed_deref_outer_ptr;
  node_t *typed_deref_ptr =
      psx_node_new_unary_deref_for((node_t *)&typed_deref_ptrptr_operand_mem);
  ASSERT_TRUE(ps_node_is_pointer(typed_deref_ptr));
  ASSERT_EQ(8, ps_node_type_size(typed_deref_ptr));
  ASSERT_EQ(4, ps_node_deref_size(typed_deref_ptr));
  ASSERT_EQ(1, psx_node_pointer_qual_levels(typed_deref_ptr));
  ASSERT_EQ(4, psx_node_base_deref_size(typed_deref_ptr));
  ASSERT_TRUE(psx_node_get_type(typed_deref_ptr) == typed_deref_inner_ptr);

  node_mem_t typed_deref_flat_ptrptr_operand_mem = {0};
  typed_deref_flat_ptrptr_operand_mem.base.kind = ND_DEREF;
  psx_type_t *typed_deref_flat_ptrptr =
      psx_type_new_pointer(psx_type_new_integer(TK_INT, 4, 0), 8);
  typed_deref_flat_ptrptr->pointer_qual_levels = 2;
  typed_deref_flat_ptrptr->base_deref_size = 4;
  typed_deref_flat_ptrptr->funcptr_sig.param_int_mask = 3;
  typed_deref_flat_ptrptr->funcptr_sig.ret_is_data_pointer = 1;
  typed_deref_flat_ptrptr_operand_mem.base.type = typed_deref_flat_ptrptr;
  node_t *typed_deref_flat_ptr =
      psx_node_new_unary_deref_for((node_t *)&typed_deref_flat_ptrptr_operand_mem);
  ASSERT_TRUE(ps_node_is_pointer(typed_deref_flat_ptr));
  ASSERT_EQ(8, ps_node_type_size(typed_deref_flat_ptr));
  ASSERT_EQ(4, ps_node_deref_size(typed_deref_flat_ptr));
  ASSERT_EQ(1, psx_node_pointer_qual_levels(typed_deref_flat_ptr));
  ASSERT_EQ(4, psx_node_base_deref_size(typed_deref_flat_ptr));
  ASSERT_TRUE(psx_node_get_type(typed_deref_flat_ptr)->base ==
              typed_deref_flat_ptrptr->base);
  ASSERT_EQ(3u, psx_node_get_type(typed_deref_flat_ptr)->funcptr_sig.param_int_mask);
  ASSERT_EQ(4, psx_node_get_type(typed_deref_flat_ptr)->funcptr_sig.ret_int_width);
  ASSERT_EQ(0, psx_node_get_type(typed_deref_flat_ptr)->funcptr_sig.ret_is_data_pointer);

  node_mem_t typed_ptr_mem = {0};
  typed_ptr_mem.base.kind = ND_LVAR;
  typed_ptr_mem.type_size = 4;
  typed_ptr_mem.is_unsigned = 1;
  typed_ptr_mem.pointee_fp_kind = TK_FLOAT_KIND_FLOAT;
  typed_ptr_mem.pointer_qual_levels = 7;
  typed_ptr_mem.base_deref_size = 99;
  typed_ptr_mem.ptr_array_pointee_bytes = 88;
  typed_ptr_mem.pointer_const_qual_mask = 9;
  typed_ptr_mem.pointer_volatile_qual_mask = 8;
  typed_ptr_mem.pointee_is_unsigned = 1;
  typed_ptr_mem.pointee_is_bool = 1;
  typed_ptr_mem.pointee_is_void = 1;
  typed_ptr_mem.is_volatile_qualified = 1;
  typed_ptr_mem.vla_row_stride_frame_off = 48;
  typed_ptr_mem.vla_strides_remaining = 7;
  typed_ptr_mem.inner_deref_size = 99;
  typed_ptr_mem.next_deref_size = 88;
  psx_type_t *typed_ptr_base = psx_type_new_float(TK_FLOAT_KIND_DOUBLE, 8);
  typed_ptr_base->is_const_qualified = 1;
  typed_ptr_mem.base.type = psx_type_new_pointer(typed_ptr_base, 8);
  typed_ptr_mem.base.type->pointer_qual_levels = 2;
  typed_ptr_mem.base.type->pointer_const_qual_mask = 3;
  typed_ptr_mem.base.type->pointer_volatile_qual_mask = 1;
  typed_ptr_mem.base.type->base_deref_size = 4;
  typed_ptr_mem.base.type->ptr_array_pointee_bytes = 16;
  typed_ptr_mem.base.type->vla_row_stride_frame_off = 24;
  typed_ptr_mem.base.type->vla_strides_remaining = 3;
  typed_ptr_mem.base.type->outer_stride = 12;
  typed_ptr_mem.base.type->mid_stride = 6;
  ASSERT_TRUE(ps_node_is_pointer((node_t *)&typed_ptr_mem));
  ASSERT_TRUE(!psx_node_conversion_value_is_unsigned((node_t *)&typed_ptr_mem));
  ASSERT_EQ(8, ps_node_deref_size((node_t *)&typed_ptr_mem));
  ASSERT_EQ(2, psx_node_pointer_qual_levels((node_t *)&typed_ptr_mem));
  ASSERT_EQ(3u, psx_node_pointer_const_qual_mask((node_t *)&typed_ptr_mem));
  ASSERT_EQ(1u, psx_node_pointer_volatile_qual_mask((node_t *)&typed_ptr_mem));
  ASSERT_EQ(4, psx_node_base_deref_size((node_t *)&typed_ptr_mem));
  ASSERT_EQ(16, psx_node_ptr_array_pointee_bytes((node_t *)&typed_ptr_mem));
  ASSERT_EQ(24, psx_node_vla_row_stride_frame_off((node_t *)&typed_ptr_mem));
  int typed_inner_stride = 0;
  int typed_next_stride = 0;
  ASSERT_TRUE(psx_node_pointer_stride_metadata((node_t *)&typed_ptr_mem,
                                               &typed_inner_stride,
                                               &typed_next_stride,
                                               NULL, NULL));
  ASSERT_EQ(12, typed_inner_stride);
  ASSERT_EQ(6, typed_next_stride);
  node_mem_t *typed_vla_sub = (node_mem_t *)psx_node_new_subscript_deref_for(
      (node_t *)&typed_ptr_mem, psx_node_new_num(0), psx_node_new_num(0),
      4, 8, 4, NULL, 0);
  ASSERT_EQ(32, typed_vla_sub->vla_row_stride_frame_off);
  ASSERT_EQ(2, typed_vla_sub->vla_strides_remaining);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, psx_node_pointee_fp_kind((node_t *)&typed_ptr_mem));
  ASSERT_TRUE(psx_node_pointee_is_const_qualified((node_t *)&typed_ptr_mem));
  ASSERT_TRUE(!psx_node_pointee_is_volatile_qualified((node_t *)&typed_ptr_mem));
  ASSERT_TRUE(!psx_node_pointee_is_unsigned((node_t *)&typed_ptr_mem));
  ASSERT_TRUE(!psx_node_pointee_is_bool((node_t *)&typed_ptr_mem));
  ASSERT_TRUE(!psx_node_pointee_is_void((node_t *)&typed_ptr_mem));

  node_mem_t typed_signed_ptr_stale_unsigned_mem = {0};
  typed_signed_ptr_stale_unsigned_mem.base.kind = ND_LVAR;
  typed_signed_ptr_stale_unsigned_mem.is_unsigned = 1;
  typed_signed_ptr_stale_unsigned_mem.base.type =
      psx_type_new_pointer(psx_type_new_integer(TK_INT, 4, 0), 8);
  node_mem_t *typed_signed_ptr_sub = (node_mem_t *)psx_node_new_subscript_deref_for(
      (node_t *)&typed_signed_ptr_stale_unsigned_mem, psx_node_new_num(0),
      psx_node_new_num(0), 4, 0, 0, NULL, 0);
  ASSERT_TRUE(!psx_node_is_unsigned_type((node_t *)typed_signed_ptr_sub));

  node_mem_t typed_ptr_stale_pointee_scalar_ptr_mem = {0};
  typed_ptr_stale_pointee_scalar_ptr_mem.base.kind = ND_LVAR;
  typed_ptr_stale_pointee_scalar_ptr_mem.pointee_is_scalar_ptr = 1;
  typed_ptr_stale_pointee_scalar_ptr_mem.base_deref_size = 99;
  typed_ptr_stale_pointee_scalar_ptr_mem.base.type =
      psx_type_new_pointer(psx_type_new_integer(TK_INT, 4, 0), 8);
  node_mem_t *typed_ptr_stale_pointee_scalar_sub =
      (node_mem_t *)psx_node_new_subscript_deref_for(
          (node_t *)&typed_ptr_stale_pointee_scalar_ptr_mem, psx_node_new_num(0),
          psx_node_new_num(0), 4, 0, 0, NULL, 0);
  ASSERT_TRUE(!ps_node_is_pointer((node_t *)typed_ptr_stale_pointee_scalar_sub));
  ASSERT_EQ(0, ps_node_deref_size((node_t *)typed_ptr_stale_pointee_scalar_sub));

  node_mem_t typed_deref_stale_scalar_ptr_member = {0};
  typed_deref_stale_scalar_ptr_member.base.kind = ND_DEREF;
  typed_deref_stale_scalar_ptr_member.is_scalar_ptr_member = 1;
  typed_deref_stale_scalar_ptr_member.base.type = psx_type_new_pointer(
      psx_type_new_pointer(psx_type_new_integer(TK_INT, 4, 0), 8), 8);
  typed_deref_stale_scalar_ptr_member.base.type->pointer_qual_levels = 2;
  typed_deref_stale_scalar_ptr_member.base.type->base_deref_size = 4;
  node_mem_t *typed_deref_sub = (node_mem_t *)psx_node_new_subscript_deref_for(
      (node_t *)&typed_deref_stale_scalar_ptr_member, psx_node_new_num(0),
      psx_node_new_num(0), 8, 0, 0, NULL, 0);
  ASSERT_EQ(2, psx_node_pointer_qual_levels((node_t *)typed_deref_sub));
  ASSERT_EQ(4, psx_node_base_deref_size((node_t *)typed_deref_sub));

  node_mem_t typed_deref_stale_tag_mem = {0};
  typed_deref_stale_tag_mem.base.kind = ND_DEREF;
  typed_deref_stale_tag_mem.is_tag_pointer = 1;
  typed_deref_stale_tag_mem.deref_size = 32;
  psx_type_t *typed_deref_stale_tag_inner =
      psx_type_new_pointer(psx_type_new_integer(TK_INT, 4, 0), 16);
  typed_deref_stale_tag_mem.base.type =
      psx_type_new_pointer(typed_deref_stale_tag_inner, 8);
  typed_deref_stale_tag_mem.base.type->pointer_qual_levels = 2;
  typed_deref_stale_tag_mem.base.type->base_deref_size = 16;
  typed_deref_stale_tag_mem.base.type->outer_stride = 4;
  node_mem_t *typed_deref_unary =
      (node_mem_t *)psx_node_new_unary_deref_for((node_t *)&typed_deref_stale_tag_mem);
  ASSERT_EQ(16, ps_node_deref_size((node_t *)typed_deref_unary));

  node_mem_t typed_missing_ptr_mem = {0};
  typed_missing_ptr_mem.base.kind = ND_LVAR;
  typed_missing_ptr_mem.base_deref_size = 77;
  typed_missing_ptr_mem.ptr_array_pointee_bytes = 66;
  typed_missing_ptr_mem.vla_row_stride_frame_off = 55;
  typed_missing_ptr_mem.vla_strides_remaining = 5;
  typed_missing_ptr_mem.base.type =
      psx_type_new_pointer(psx_type_new_integer(TK_INT, 4, 0), 8);
  typed_missing_ptr_mem.base.type->base_deref_size = 0;
  typed_missing_ptr_mem.base.type->ptr_array_pointee_bytes = 0;
  typed_missing_ptr_mem.base.type->vla_row_stride_frame_off = 0;
  typed_missing_ptr_mem.base.type->vla_strides_remaining = 0;
  ASSERT_EQ(0, psx_node_base_deref_size((node_t *)&typed_missing_ptr_mem));
  ASSERT_EQ(0, psx_node_ptr_array_pointee_bytes((node_t *)&typed_missing_ptr_mem));
  ASSERT_EQ(0, psx_node_vla_row_stride_frame_off((node_t *)&typed_missing_ptr_mem));

  node_mem_t typed_unsigned_ptr_mem = {0};
  typed_unsigned_ptr_mem.base.kind = ND_LVAR;
  typed_unsigned_ptr_mem.base.type =
      psx_type_new_pointer(psx_type_new_integer(TK_INT, 4, 1), 8);
  ASSERT_TRUE(psx_node_pointee_is_unsigned((node_t *)&typed_unsigned_ptr_mem));

  node_mem_t typed_bool_ptr_mem = {0};
  typed_bool_ptr_mem.base.kind = ND_LVAR;
  psx_type_t *typed_bool_type = psx_type_new(PSX_TYPE_BOOL);
  typed_bool_type->size = 1;
  typed_bool_ptr_mem.base.type = psx_type_new_pointer(typed_bool_type, 8);
  ASSERT_TRUE(psx_node_pointee_is_bool((node_t *)&typed_bool_ptr_mem));

  node_mem_t typed_void_ptr_mem = {0};
  typed_void_ptr_mem.base.kind = ND_LVAR;
  typed_void_ptr_mem.base.type = psx_type_new_pointer(psx_type_new(PSX_TYPE_VOID), 8);
  ASSERT_TRUE(psx_node_pointee_is_void((node_t *)&typed_void_ptr_mem));

  node_mem_t typed_const_view_mem = {0};
  typed_const_view_mem.base.kind = ND_LVAR;
  typed_const_view_mem.base.type = psx_type_new_tag(TK_STRUCT, "TypedView", 9, 1, 4);
  typed_const_view_mem.is_const_qualified = 1;
  typed_const_view_mem.is_volatile_qualified = 1;
  typed_const_view_mem.pointer_qual_levels = 4;
  typed_const_view_mem.pointer_const_qual_mask = 5;
  typed_const_view_mem.pointer_volatile_qual_mask = 6;
  typed_const_view_mem.base_deref_size = 7;
  typed_const_view_mem.ptr_array_pointee_bytes = 28;
  typed_const_view_mem.pointee_fp_kind = TK_FLOAT_KIND_DOUBLE;
  typed_const_view_mem.pointee_is_unsigned = 1;
  typed_const_view_mem.pointee_is_bool = 1;
  typed_const_view_mem.pointee_is_void = 1;
  ASSERT_EQ(0, psx_node_pointer_qual_levels((node_t *)&typed_const_view_mem));
  ASSERT_EQ(0u, psx_node_pointer_const_qual_mask((node_t *)&typed_const_view_mem));
  ASSERT_EQ(0u, psx_node_pointer_volatile_qual_mask((node_t *)&typed_const_view_mem));
  ASSERT_EQ(0, psx_node_base_deref_size((node_t *)&typed_const_view_mem));
  ASSERT_EQ(0, psx_node_ptr_array_pointee_bytes((node_t *)&typed_const_view_mem));
  ASSERT_EQ(TK_FLOAT_KIND_NONE, psx_node_pointee_fp_kind((node_t *)&typed_const_view_mem));
  ASSERT_TRUE(!psx_node_pointee_is_const_qualified((node_t *)&typed_const_view_mem));
  ASSERT_TRUE(!psx_node_pointee_is_volatile_qualified((node_t *)&typed_const_view_mem));
  ASSERT_TRUE(!psx_node_pointee_is_unsigned((node_t *)&typed_const_view_mem));
  ASSERT_TRUE(!psx_node_pointee_is_bool((node_t *)&typed_const_view_mem));
  ASSERT_TRUE(!psx_node_pointee_is_void((node_t *)&typed_const_view_mem));

  node_mem_t typed_stale_self_const_mem = {0};
  typed_stale_self_const_mem.base.kind = ND_LVAR;
  typed_stale_self_const_mem.is_const_qualified = 1;
  typed_stale_self_const_mem.base.type = psx_type_new_integer(TK_INT, 4, 0);
  expect_const_assign_ok_for_node((node_t *)&typed_stale_self_const_mem);

  node_mem_t typed_nonconst_ptr_lhs_mem = {0};
  typed_nonconst_ptr_lhs_mem.base.kind = ND_LVAR;
  typed_nonconst_ptr_lhs_mem.is_pointer = 1;
  typed_nonconst_ptr_lhs_mem.is_const_qualified = 1;
  typed_nonconst_ptr_lhs_mem.base.type =
      psx_type_new_pointer(psx_type_new_integer(TK_INT, 4, 0), 8);
  node_mem_t typed_const_ptr_rhs_mem = {0};
  typed_const_ptr_rhs_mem.base.kind = ND_LVAR;
  psx_type_t *typed_const_rhs_base = psx_type_new_integer(TK_INT, 4, 0);
  typed_const_rhs_base->is_const_qualified = 1;
  typed_const_ptr_rhs_mem.base.type = psx_type_new_pointer(typed_const_rhs_base, 8);
  expect_const_qual_discard_fail_for_nodes((node_t *)&typed_nonconst_ptr_lhs_mem,
                                           (node_t *)&typed_const_ptr_rhs_mem);

  node_mem_t typed_funcptr_view_mem = {0};
  typed_funcptr_view_mem.base.kind = ND_LVAR;
  typed_funcptr_view_mem.base.type =
      psx_type_new_pointer(psx_type_new_integer(TK_INT, 4, 0), 8);
  typed_funcptr_view_mem.base.type->funcptr_sig.param_fp_mask = 1;
  typed_funcptr_view_mem.base.type->funcptr_sig.ret_fp_kind = TK_FLOAT_KIND_DOUBLE;
  typed_funcptr_view_mem.funcptr_sig.param_int_mask = 2;
  typed_funcptr_view_mem.funcptr_sig.ret_is_complex = 1;
  typed_funcptr_view_mem.funcptr_sig.ret_pointee_array =
      psx_ret_pointee_array_make(3, 2, 4);
  ASSERT_EQ(1u, psx_node_funcptr_param_fp_mask((node_t *)&typed_funcptr_view_mem));
  ASSERT_EQ(0u, psx_node_funcptr_param_int_mask((node_t *)&typed_funcptr_view_mem));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            psx_node_funcptr_ret_fp_kind((node_t *)&typed_funcptr_view_mem));
  ASSERT_TRUE(!psx_node_funcptr_returns_complex((node_t *)&typed_funcptr_view_mem));
  ASSERT_TRUE(!psx_node_funcptr_returns_pointee_array((node_t *)&typed_funcptr_view_mem));

  node_mem_t typed_funcptr_callee_mem = {0};
  typed_funcptr_callee_mem.base.kind = ND_LVAR;
  typed_funcptr_callee_mem.base.type =
      psx_type_new_pointer(psx_type_new_integer(TK_INT, 4, 0), 8);
  typed_funcptr_callee_mem.base.type->funcptr_sig.ret_int_width = 8;
  typed_funcptr_callee_mem.funcptr_sig.ret_is_data_pointer = 1;
  typed_funcptr_callee_mem.base_deref_size = 4;
  node_func_t typed_indirect_call = {0};
  typed_indirect_call.base.kind = ND_FUNCALL;
  typed_indirect_call.callee = (node_t *)&typed_funcptr_callee_mem;
  psx_type_t *typed_indirect_ty = psx_node_get_type((node_t *)&typed_indirect_call);
  ASSERT_TRUE(typed_indirect_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, typed_indirect_ty->kind);
  ASSERT_EQ(8, psx_type_sizeof(typed_indirect_ty));

  node_mem_t typed_stale_funcptr_callee_mem = {0};
  typed_stale_funcptr_callee_mem.base.kind = ND_LVAR;
  typed_stale_funcptr_callee_mem.base.type =
      psx_type_new_pointer(psx_type_new_integer(TK_INT, 4, 0), 8);
  typed_stale_funcptr_callee_mem.funcptr_sig.ret_is_data_pointer = 1;
  typed_stale_funcptr_callee_mem.base_deref_size = 4;
  node_func_t typed_stale_indirect_call = {0};
  typed_stale_indirect_call.base.kind = ND_FUNCALL;
  typed_stale_indirect_call.callee = (node_t *)&typed_stale_funcptr_callee_mem;
  ASSERT_TRUE(psx_node_get_type((node_t *)&typed_stale_indirect_call) == NULL);

  node_mem_t typed_tag_mem = {0};
  typed_tag_mem.base.kind = ND_LVAR;
  typed_tag_mem.tag_kind = TK_UNION;
  typed_tag_mem.tag_name = "Old";
  typed_tag_mem.tag_len = 3;
  typed_tag_mem.tag_scope_depth_p1 = 9;
  psx_type_t *typed_tag = psx_type_new_tag(TK_STRUCT, "Typed", 5, 3, 4);
  typed_tag_mem.base.type = psx_type_new_pointer(typed_tag, 4);
  token_kind_t typed_tag_kind = TK_EOF;
  char *typed_tag_name = NULL;
  int typed_tag_len = 0;
  int typed_is_tag_pointer = 0;
  psx_node_get_tag_type((node_t *)&typed_tag_mem, &typed_tag_kind, &typed_tag_name,
                        &typed_tag_len, &typed_is_tag_pointer);
  ASSERT_EQ(TK_STRUCT, typed_tag_kind);
  ASSERT_EQ(5, typed_tag_len);
  ASSERT_TRUE(strncmp(typed_tag_name, "Typed", 5) == 0);
  ASSERT_EQ(1, typed_is_tag_pointer);
  ASSERT_EQ(2, psx_node_get_tag_scope_depth((node_t *)&typed_tag_mem));

  parsed_code = parse_program_input("main() { struct S { int x; } *p; p=0; return p==0; }");
  fn = as_func(parsed_code[0]);
  body = as_block(fn->base.rhs);
  assign = body->body[1];
  ASSERT_EQ(ND_ASSIGN, assign->kind);
  psx_type_t *ptr_ty = psx_node_get_type(assign->lhs);
  ASSERT_TRUE(ptr_ty != NULL);
  ASSERT_TRUE(assign->lhs->type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ptr_ty->kind);
  ASSERT_TRUE(psx_type_is_pointer(ptr_ty));
  ASSERT_TRUE(ptr_ty->base != NULL);
  ASSERT_EQ(TK_STRUCT, ptr_ty->base->tag_kind);
  ASSERT_EQ(1, ptr_ty->base->tag_len);
  ASSERT_TRUE(strncmp(ptr_ty->base->tag_name, "S", 1) == 0);
  lvar_t *p_lvar = find_func_lvar(fn, "p");
  ASSERT_TRUE(p_lvar != NULL);
  ASSERT_TRUE(psx_node_lvar_symbol(assign->lhs) == p_lvar);
  ASSERT_TRUE(p_lvar->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, p_lvar->decl_type->kind);
  ASSERT_TRUE(p_lvar->decl_type->base != NULL);
  ASSERT_EQ(TK_STRUCT, p_lvar->decl_type->base->tag_kind);

  parsed_code = parse_program_input("double __tm_param_fp(double *p) { return p[0]; }");
  fn = as_func(parsed_code[0]);
  lvar_t *param_fp_lvar = find_func_lvar(fn, "p");
  ASSERT_TRUE(param_fp_lvar != NULL);
  ASSERT_TRUE(param_fp_lvar->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, param_fp_lvar->decl_type->kind);
  ASSERT_TRUE(param_fp_lvar->decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, param_fp_lvar->decl_type->base->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, param_fp_lvar->decl_type->base->fp_kind);

  parsed_code = parse_program_input(
      "main() { struct R { int r[4]; }; struct R r1={{1,2,3,4}}; r1.r; return 0; }");
  fn = as_func(parsed_code[0]);
  body = as_block(fn->base.rhs);
  node_t *member = body->body[2];
  ASSERT_EQ(ND_DEREF, member->kind);
  psx_type_t *array_ty = psx_node_get_type(member);
  ASSERT_TRUE(array_ty != NULL);
  ASSERT_TRUE(member->type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, array_ty->kind);
  ASSERT_EQ(16, psx_type_sizeof(array_ty));
  ASSERT_TRUE(psx_type_is_pointer(array_ty));
  lvar_t *r1_lvar = find_func_lvar(fn, "r1");
  ASSERT_TRUE(r1_lvar != NULL);
  ASSERT_TRUE(r1_lvar->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_STRUCT, r1_lvar->decl_type->kind);
  ASSERT_EQ(16, psx_type_sizeof(r1_lvar->decl_type));

  parsed_code = parse_program_input("unsigned int __tm_gu; int *__tm_gp; int __tm_ga[3]; main(){ return 0; }");
  (void)parsed_code;
  global_var_t *gu = psx_find_global_var("__tm_gu", 7);
  ASSERT_TRUE(gu != NULL);
  ASSERT_TRUE(gu->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, gu->decl_type->kind);
  ASSERT_EQ(4, psx_type_sizeof(gu->decl_type));
  ASSERT_TRUE(psx_type_is_unsigned(gu->decl_type));
  gu->decl_type = NULL;
  psx_type_t *gu_decl_a = psx_gvar_get_decl_type(gu);
  psx_type_t *gu_decl_b = psx_gvar_get_decl_type(gu);
  ASSERT_TRUE(gu_decl_a != NULL);
  ASSERT_TRUE(gu_decl_a == gu_decl_b);
  ASSERT_TRUE(gu->decl_type == gu_decl_a);

  global_var_t tmp_gv = {0};
  psx_decl_init_gvar_storage_type(&tmp_gv, 4, 4, 0,
                                  TK_FLOAT_KIND_NONE, 0, TK_EOF, NULL, 0, 0);
  psx_type_t *tmp_gv_int = psx_gvar_get_decl_type(&tmp_gv);
  ASSERT_TRUE(tmp_gv_int != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, tmp_gv_int->kind);
  psx_decl_init_gvar_storage_type(&tmp_gv, 8, 4, 0,
                                  TK_FLOAT_KIND_NONE, 0, TK_EOF, NULL, 0, 0);
  psx_decl_set_gvar_pointer_qual_levels(&tmp_gv, 1);
  ASSERT_TRUE(tmp_gv.decl_type == NULL);
  psx_type_t *tmp_gv_ptr = psx_gvar_refresh_decl_type(&tmp_gv);
  ASSERT_TRUE(tmp_gv_ptr != NULL);
  ASSERT_TRUE(tmp_gv_ptr != tmp_gv_int);
  ASSERT_TRUE(tmp_gv.decl_type == tmp_gv_ptr);
  ASSERT_EQ(PSX_TYPE_POINTER, tmp_gv_ptr->kind);
  psx_decl_set_gvar_pointer_derived_type(&tmp_gv, 8, 8, 0);
  psx_decl_set_gvar_pointee_fp_kind(&tmp_gv, TK_FLOAT_KIND_DOUBLE);
  ASSERT_TRUE(tmp_gv.decl_type == NULL);
  psx_type_t *tmp_gv_double_ptr = psx_gvar_refresh_decl_type(&tmp_gv);
  ASSERT_TRUE(tmp_gv_double_ptr != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, tmp_gv_double_ptr->kind);
  ASSERT_TRUE(tmp_gv_double_ptr->base != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, tmp_gv_double_ptr->base->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, tmp_gv_double_ptr->base->fp_kind);

  global_var_t tmp_arr_gv = {0};
  psx_decl_init_gvar_storage_type(&tmp_arr_gv, 0, 4, 1,
                                  TK_FLOAT_KIND_NONE, 0, TK_EOF, NULL, 0, 0);
  psx_type_t *tmp_arr_empty = psx_gvar_get_decl_type(&tmp_arr_gv);
  ASSERT_TRUE(tmp_arr_empty != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, tmp_arr_empty->kind);
  psx_decl_set_gvar_type_size(&tmp_arr_gv, 12);
  ASSERT_TRUE(tmp_arr_gv.decl_type == NULL);
  psx_type_t *tmp_arr_sized = psx_gvar_refresh_decl_type(&tmp_arr_gv);
  ASSERT_TRUE(tmp_arr_sized != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, tmp_arr_sized->kind);
  ASSERT_EQ(12, psx_type_sizeof(tmp_arr_sized));

  global_var_t *gp = psx_find_global_var("__tm_gp", 7);
  ASSERT_TRUE(gp != NULL);
  ASSERT_TRUE(gp->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, gp->decl_type->kind);
  ASSERT_TRUE(psx_type_is_pointer(gp->decl_type));
  global_var_t *ga = psx_find_global_var("__tm_ga", 7);
  ASSERT_TRUE(ga != NULL);
  ASSERT_TRUE(ga->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ga->decl_type->kind);
  ASSERT_EQ(12, psx_type_sizeof(ga->decl_type));

  parsed_code = parse_program_input("extern int __tm_extern_arr[]; int __tm_extern_arr[3]; main(){ return 0; }");
  (void)parsed_code;
  global_var_t *gext = psx_find_global_var("__tm_extern_arr", 15);
  ASSERT_TRUE(gext != NULL);
  ASSERT_TRUE(gext->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, gext->decl_type->kind);
  ASSERT_EQ(12, psx_type_sizeof(gext->decl_type));

  parsed_code = parse_program_input("int __tm_static_decl_type(void) { static double sd = 1.0; return 0; }");
  fn = as_func(parsed_code[0]);
  lvar_t *sd = find_func_lvar(fn, "sd");
  ASSERT_TRUE(sd != NULL);
  ASSERT_TRUE(sd->is_static_local);
  ASSERT_TRUE(sd->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, sd->decl_type->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, sd->decl_type->fp_kind);
  global_var_t *sd_gv = psx_find_global_var(sd->static_global_name, sd->static_global_name_len);
  ASSERT_TRUE(sd_gv != NULL);
  ASSERT_TRUE(sd_gv->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, sd_gv->decl_type->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, sd_gv->decl_type->fp_kind);

  parsed_code = parse_program_input("int __tm_static_arr_decl_type(void) { static int sa[2] = {1,2}; return sa[0]; }");
  fn = as_func(parsed_code[0]);
  lvar_t *sa = find_func_lvar(fn, "sa");
  ASSERT_TRUE(sa != NULL);
  ASSERT_TRUE(sa->is_static_local);
  ASSERT_TRUE(sa->decl_type != NULL);
  global_var_t *sa_gv = psx_find_global_var(sa->static_global_name, sa->static_global_name_len);
  ASSERT_TRUE(sa_gv != NULL);
  ASSERT_TRUE(sa_gv->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, sa_gv->decl_type->kind);
  ASSERT_EQ(8, psx_type_sizeof(sa_gv->decl_type));

  parsed_code = parse_program_input("int __tm_local_extern_decl_type(void) { extern double __tm_local_extern_dp; return 0; }");
  (void)parsed_code;
  const char *local_extern_name = "__tm_local_extern_dp";
  global_var_t *local_extern_dp =
      psx_find_global_var((char *)local_extern_name, (int)(sizeof("__tm_local_extern_dp") - 1));
  ASSERT_TRUE(local_extern_dp != NULL);
  ASSERT_TRUE(local_extern_dp->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, local_extern_dp->decl_type->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, local_extern_dp->decl_type->fp_kind);

  node_t *compound_lit_expr = parse_expr_input("(int[3]){1,2,3}");
  ASSERT_EQ(ND_COMMA, compound_lit_expr->kind);
  ASSERT_TRUE(compound_lit_expr->rhs != NULL);
  ASSERT_EQ(ND_ADDR, compound_lit_expr->rhs->kind);
  ASSERT_TRUE(compound_lit_expr->rhs->lhs != NULL);
  ASSERT_EQ(ND_LVAR, compound_lit_expr->rhs->lhs->kind);
  lvar_t *compound_lit_local = as_lvar(compound_lit_expr->rhs->lhs)->var;
  ASSERT_TRUE(compound_lit_local != NULL);
  ASSERT_TRUE(compound_lit_local->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, compound_lit_local->decl_type->kind);
  ASSERT_EQ(12, psx_type_sizeof(compound_lit_local->decl_type));

  ps_reset_translation_unit_state();
  parsed_code = parse_program_input("int *__tm_compound_lit_global = (int[]){1,2,3}; int main(void) { return 0; }");
  (void)parsed_code;
  global_var_t *compound_lit_global =
      psx_find_global_var("__compound_lit_0", (int)(sizeof("__compound_lit_0") - 1));
  ASSERT_TRUE(compound_lit_global != NULL);
  ASSERT_TRUE(compound_lit_global->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, compound_lit_global->decl_type->kind);
  ASSERT_EQ(12, psx_type_sizeof(compound_lit_global->decl_type));

  parsed_code = parse_program_input(
      "long __tm_lf(void); int *__tm_ip(void); int **__tm_pp(void); "
      "double (*__tm_dp(void))[2]; "
      "int main(void){ int x; int *p; x + 1L; p + 1; "
      "double (*(*dpa)(void))[2]=__tm_dp; "
      "__tm_lf(); __tm_ip(); __tm_pp(); __tm_dp(); dpa(); return 0; }");
  fn = as_func(parsed_code[0]);
  body = as_block(fn->base.rhs);
  node_t *long_add = NULL;
  node_t *ptr_add = NULL;
  node_t *long_call = NULL;
  node_t *ptr_call = NULL;
  node_t *ptrptr_call = NULL;
  node_t *double_ptr_to_array_call = NULL;
  node_t *indirect_double_ptr_to_array_call = NULL;
  for (int i = 0; body->body[i]; i++) {
    node_t *n = body->body[i];
    if (n->kind == ND_ADD && ps_node_is_pointer(n)) ptr_add = n;
    if (n->kind == ND_ADD && !ps_node_is_pointer(n) && ps_node_type_size(n) == 8) long_add = n;
    if (n->kind == ND_FUNCALL) {
      node_func_t *call = as_func(n);
      if (call->funcname_len == 7 && strncmp(call->funcname, "__tm_lf", 7) == 0) long_call = n;
      if (call->funcname_len == 7 && strncmp(call->funcname, "__tm_ip", 7) == 0) ptr_call = n;
      if (call->funcname_len == 7 && strncmp(call->funcname, "__tm_pp", 7) == 0) ptrptr_call = n;
      if (call->funcname_len == 7 && strncmp(call->funcname, "__tm_dp", 7) == 0)
        double_ptr_to_array_call = n;
      if (call->callee && call->callee->kind == ND_LVAR) {
        lvar_t *callee_lvar = psx_node_lvar_symbol(call->callee);
        if (callee_lvar && callee_lvar->len == 3 &&
            strncmp(callee_lvar->name, "dpa", 3) == 0) {
          indirect_double_ptr_to_array_call = n;
        }
      }
    }
  }
  psx_type_t *long_add_ty = psx_node_get_type(long_add);
  ASSERT_TRUE(long_add_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, long_add_ty->kind);
  ASSERT_EQ(8, psx_type_sizeof(long_add_ty));
  psx_type_t *ptr_add_ty = psx_node_get_type(ptr_add);
  ASSERT_TRUE(ptr_add_ty != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ptr_add_ty->kind);
  ASSERT_EQ(4, psx_type_deref_size(ptr_add_ty));
  ASSERT_TRUE(long_call->type != NULL);
  psx_type_t *long_call_ty = psx_node_get_type(long_call);
  ASSERT_TRUE(long_call_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, long_call_ty->kind);
  ASSERT_EQ(8, psx_type_sizeof(long_call_ty));
  ASSERT_EQ(8, ps_node_type_size(long_call));
  ASSERT_TRUE(ptr_call->type != NULL);
  psx_type_t *ptr_call_ty = psx_node_get_type(ptr_call);
  ASSERT_TRUE(ptr_call_ty != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ptr_call_ty->kind);
  ASSERT_EQ(4, psx_type_deref_size(ptr_call_ty));
  ASSERT_TRUE(ps_node_is_pointer(ptr_call));
  ASSERT_EQ(4, ps_node_deref_size(ptr_call));
  ASSERT_TRUE(ptrptr_call->type != NULL);
  psx_type_t *ptrptr_call_ty = psx_node_get_type(ptrptr_call);
  ASSERT_TRUE(ptrptr_call_ty != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ptrptr_call_ty->kind);
  ASSERT_EQ(8, psx_type_deref_size(ptrptr_call_ty));
  ASSERT_EQ(8, ps_node_deref_size(ptrptr_call));
  ASSERT_EQ(2, psx_node_pointer_qual_levels(ptrptr_call));
  ASSERT_TRUE(double_ptr_to_array_call->type != NULL);
  psx_type_t *double_ptr_to_array_ty = psx_node_get_type(double_ptr_to_array_call);
  ASSERT_TRUE(double_ptr_to_array_ty != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, double_ptr_to_array_ty->kind);
  ASSERT_EQ(16, psx_type_deref_size(double_ptr_to_array_ty));
  ASSERT_EQ(16, ps_node_deref_size(double_ptr_to_array_call));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, psx_node_pointee_fp_kind(double_ptr_to_array_call));
  ASSERT_EQ(2, double_ptr_to_array_ty->funcptr_sig.ret_pointee_array.first_dim);
  ASSERT_EQ(8, double_ptr_to_array_ty->funcptr_sig.ret_pointee_array.elem_size);
  ASSERT_TRUE(indirect_double_ptr_to_array_call->type != NULL);
  psx_type_t *indirect_double_ptr_to_array_ty =
      psx_node_get_type(indirect_double_ptr_to_array_call);
  ASSERT_TRUE(indirect_double_ptr_to_array_ty != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, indirect_double_ptr_to_array_ty->kind);
  ASSERT_EQ(16, psx_type_deref_size(indirect_double_ptr_to_array_ty));
  ASSERT_EQ(16, ps_node_deref_size(indirect_double_ptr_to_array_call));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            psx_node_pointee_fp_kind(indirect_double_ptr_to_array_call));
  ASSERT_EQ(2, indirect_double_ptr_to_array_ty->funcptr_sig.ret_pointee_array.first_dim);
  ASSERT_EQ(8, indirect_double_ptr_to_array_ty->funcptr_sig.ret_pointee_array.elem_size);
  lvar_t *dpa_lvar = find_func_lvar(fn, "dpa");
  ASSERT_TRUE(dpa_lvar != NULL);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, dpa_lvar->funcptr_sig.ret_fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, dpa_lvar->funcptr_sig.ret_pointee_fp_kind);

  parsed_code = parse_program_input(
      "int (*__tm_deref_getrow(void))[3]; "
      "int main(void){ int (*(*direct)(void))[3]=__tm_deref_getrow; "
      "return (*direct())[2]; }");
  fn = as_func(parsed_code[0]);
  body = as_block(fn->base.rhs);
  node_t *indirect_explicit_row_ret = NULL;
  for (int i = 0; body->body[i]; i++) {
    if (body->body[i]->kind == ND_RETURN) {
      indirect_explicit_row_ret = body->body[i];
      break;
    }
  }
  ASSERT_TRUE(indirect_explicit_row_ret != NULL);
  node_t *indirect_explicit_row_elem = indirect_explicit_row_ret->lhs;
  ASSERT_EQ(ND_DEREF, indirect_explicit_row_elem->kind);
  ASSERT_EQ(4, ps_node_type_size(indirect_explicit_row_elem));
  ASSERT_EQ(0, ps_node_deref_size(indirect_explicit_row_elem));
  ASSERT_TRUE(!ps_node_is_pointer(indirect_explicit_row_elem));
  psx_type_t *indirect_explicit_row_elem_ty =
      psx_node_get_type(indirect_explicit_row_elem);
  ASSERT_TRUE(indirect_explicit_row_elem_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, indirect_explicit_row_elem_ty->kind);
  ASSERT_EQ(4, psx_type_sizeof(indirect_explicit_row_elem_ty));

  parsed_code = parse_program_input(
      "struct TM695 { double *dp; double (*fp)(void); }; int main(void){ return 0; }");
  (void)parsed_code;
  tag_member_info_t dp_info = {0};
  ASSERT_TRUE(psx_ctx_find_tag_member_info(TK_STRUCT, "TM695", 5, "dp", 2, &dp_info));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, dp_info.fp_kind);
  ASSERT_EQ(0, dp_info.is_funcptr);
  ASSERT_TRUE(!psx_decl_funcptr_sig_has_payload(dp_info.funcptr_sig));
  psx_decl_funcptr_sig_t dp_sig = psx_ctx_tag_member_funcptr_sig(&dp_info);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, dp_sig.ret_fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, dp_sig.ret_pointee_fp_kind);
  ASSERT_TRUE(!psx_decl_funcptr_sig_has_payload(dp_sig));
  tag_member_info_t fp_info = {0};
  ASSERT_TRUE(psx_ctx_find_tag_member_info(TK_STRUCT, "TM695", 5, "fp", 2, &fp_info));
  ASSERT_EQ(TK_FLOAT_KIND_NONE, fp_info.fp_kind);
  ASSERT_EQ(1, fp_info.is_funcptr);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, fp_info.funcptr_sig.ret_fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, fp_info.funcptr_sig.ret_pointee_fp_kind);
  psx_decl_funcptr_sig_t fp_sig = psx_ctx_tag_member_funcptr_sig(&fp_info);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, fp_sig.ret_fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, fp_sig.ret_pointee_fp_kind);
  ASSERT_TRUE(psx_decl_funcptr_sig_has_payload(fp_sig));

  parsed_code = parse_program_input(
      "double __tm696_ret_d(void){ return 1.0; } "
      "double *__tm696_gdp; double (*__tm696_gfp)(void)=__tm696_ret_d; "
      "int main(void){ double d; double *dp=&d; double (*fp)(void)=__tm696_ret_d; return 0; }");
  fn = as_func(parsed_code[1]);
  lvar_t *dp_lvar = find_func_lvar(fn, "dp");
  ASSERT_TRUE(dp_lvar != NULL);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, dp_lvar->pointee_fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, dp_lvar->funcptr_sig.ret_fp_kind);
  node_mem_t dp_mem = {0};
  psx_node_copy_funcptr_metadata_from_lvar(&dp_mem, dp_lvar);
  ASSERT_TRUE(!psx_node_mem_has_funcptr_metadata(&dp_mem));
  lvar_t *fp_lvar = find_func_lvar(fn, "fp");
  ASSERT_TRUE(fp_lvar != NULL);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, fp_lvar->pointee_fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, fp_lvar->funcptr_sig.ret_fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, fp_lvar->funcptr_sig.ret_pointee_fp_kind);
  node_mem_t fp_mem = {0};
  psx_node_copy_funcptr_metadata_from_lvar(&fp_mem, fp_lvar);
  ASSERT_TRUE(psx_node_mem_has_funcptr_metadata(&fp_mem));
  ASSERT_EQ(TK_FLOAT_KIND_NONE, fp_mem.pointee_fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, fp_mem.funcptr_sig.ret_fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, fp_mem.funcptr_sig.ret_pointee_fp_kind);
  global_var_t *gdp = psx_find_global_var("__tm696_gdp", 11);
  ASSERT_TRUE(gdp != NULL);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, gdp->pointee_fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, gdp->funcptr_sig.ret_fp_kind);
  node_mem_t gdp_mem = {0};
  psx_node_copy_funcptr_metadata_from_gvar(&gdp_mem, gdp);
  ASSERT_TRUE(!psx_node_mem_has_funcptr_metadata(&gdp_mem));
  global_var_t *gfp = psx_find_global_var("__tm696_gfp", 11);
  ASSERT_TRUE(gfp != NULL);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, gfp->pointee_fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, gfp->funcptr_sig.ret_fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, gfp->funcptr_sig.ret_pointee_fp_kind);
  node_mem_t gfp_mem = {0};
  psx_node_copy_funcptr_metadata_from_gvar(&gfp_mem, gfp);
  ASSERT_TRUE(psx_node_mem_has_funcptr_metadata(&gfp_mem));
  ASSERT_EQ(TK_FLOAT_KIND_NONE, gfp_mem.pointee_fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, gfp_mem.funcptr_sig.ret_fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, gfp_mem.funcptr_sig.ret_pointee_fp_kind);

  parsed_code = parse_program_input(
      "double __tm696_rows[2][2]; double (*__tm696_gpa)[2]=__tm696_rows; "
      "int main(void){ return 0; }");
  (void)parsed_code;
  global_var_t *gpa = psx_find_global_var("__tm696_gpa", 11);
  ASSERT_TRUE(gpa != NULL);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, gpa->pointee_fp_kind);
  ASSERT_EQ(16, gpa->ptr_array_pointee_bytes);
  node_t *gpa_node = psx_node_new_gvar_for(gpa);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, psx_node_pointee_fp_kind(gpa_node));
  ASSERT_EQ(16, psx_node_ptr_array_pointee_bytes(gpa_node));
  node_t *gpa_row = psx_node_new_unary_deref_for(gpa_node);
  ASSERT_EQ(16, ps_node_type_size(gpa_row));
  ASSERT_EQ(8, ps_node_deref_size(gpa_row));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, psx_node_pointee_fp_kind(gpa_row));

  parsed_code = parse_program_input(
      "double __tm697_ret_d(void){ return 1.0; } "
      "typedef double *TM697_DP; typedef double (*TM697_FP)(void); "
      "int main(void){ double d; TM697_DP dp=&d; TM697_FP fp=__tm697_ret_d; "
      "{ typedef double (*TM697_BFP)(void); TM697_BFP bfp=__tm697_ret_d; bfp(); } "
      "return fp() == *dp; }");
  (void)parsed_code;
  psx_typedef_info_t td_dp = {0};
  ASSERT_TRUE(psx_ctx_find_typedef_name("TM697_DP", 8, &td_dp));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, td_dp.fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, td_dp.funcptr_sig.ret_fp_kind);
  ASSERT_TRUE(!psx_decl_funcptr_sig_has_payload(psx_ctx_typedef_funcptr_sig(&td_dp)));
  psx_typedef_info_t td_fp = {0};
  ASSERT_TRUE(psx_ctx_find_typedef_name("TM697_FP", 8, &td_fp));
  ASSERT_EQ(TK_FLOAT_KIND_NONE, td_fp.fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, td_fp.funcptr_sig.ret_fp_kind);
  psx_decl_funcptr_sig_t td_fp_sig = psx_ctx_typedef_funcptr_sig(&td_fp);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, td_fp_sig.ret_fp_kind);
  ASSERT_TRUE(psx_decl_funcptr_sig_has_payload(td_fp_sig));
  fn = as_func(parsed_code[1]);
  lvar_t *td_dp_lvar = find_func_lvar(fn, "dp");
  ASSERT_TRUE(td_dp_lvar != NULL);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, td_dp_lvar->pointee_fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, td_dp_lvar->funcptr_sig.ret_fp_kind);
  lvar_t *td_fp_lvar = find_func_lvar(fn, "fp");
  ASSERT_TRUE(td_fp_lvar != NULL);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, td_fp_lvar->pointee_fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, td_fp_lvar->funcptr_sig.ret_fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, td_fp_lvar->funcptr_sig.ret_pointee_fp_kind);
  lvar_t *td_bfp_lvar = find_func_lvar(fn, "bfp");
  ASSERT_TRUE(td_bfp_lvar != NULL);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, td_bfp_lvar->pointee_fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, td_bfp_lvar->funcptr_sig.ret_fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, td_bfp_lvar->funcptr_sig.ret_pointee_fp_kind);

  parsed_code = parse_program_input(
      "int __tm817_i; int *__tm817_retp(void){ return &__tm817_i; } "
      "int __tm817_inc(int x){ return x + 1; } "
      "typedef int *TM817_IP; typedef int *(*TM817_GET)(void); "
      "typedef TM817_IP (*TM817_GET2)(void); typedef int (**TM817_PP)(int); "
      "int main(void){ int (*p)(int)=__tm817_inc; TM817_GET get=__tm817_retp; "
      "TM817_GET2 get2=__tm817_retp; "
      "TM817_PP pp=&p; { typedef int *(*TM817_LOCAL_GET)(void); "
      "typedef int (**TM817_LOCAL_PP)(int); TM817_LOCAL_GET lget=__tm817_retp; "
      "TM817_LOCAL_PP lpp=&p; lget(); (*lpp)(1); } return *get()+*get2()+(*pp)(1); }");
  psx_typedef_info_t tm817_get_td = {0};
  ASSERT_TRUE(psx_ctx_find_typedef_name("TM817_GET", 9, &tm817_get_td));
  ASSERT_EQ(1, tm817_get_td.funcptr_sig.ret_is_data_pointer);
  ASSERT_EQ(0, tm817_get_td.funcptr_sig.ret_int_width);
  ASSERT_EQ(0u, tm817_get_td.funcptr_sig.param_int_mask);
  ASSERT_EQ(1, psx_ctx_get_typedef_pointer_levels("TM817_GET", 9));
  psx_typedef_info_t tm817_get2_td = {0};
  ASSERT_TRUE(psx_ctx_find_typedef_name("TM817_GET2", 10, &tm817_get2_td));
  ASSERT_EQ(1, tm817_get2_td.funcptr_sig.ret_is_data_pointer);
  ASSERT_EQ(0, tm817_get2_td.funcptr_sig.ret_int_width);
  ASSERT_EQ(0u, tm817_get2_td.funcptr_sig.param_int_mask);
  ASSERT_EQ(1, psx_ctx_get_typedef_pointer_levels("TM817_GET2", 10));
  psx_typedef_info_t tm817_pp_td = {0};
  ASSERT_TRUE(psx_ctx_find_typedef_name("TM817_PP", 8, &tm817_pp_td));
  ASSERT_EQ(0, tm817_pp_td.funcptr_sig.ret_is_data_pointer);
  ASSERT_EQ(4, tm817_pp_td.funcptr_sig.ret_int_width);
  ASSERT_EQ(1u, tm817_pp_td.funcptr_sig.param_int_mask);
  ASSERT_EQ(2, psx_ctx_get_typedef_pointer_levels("TM817_PP", 8));
  fn = as_func(parsed_code[2]);
  lvar_t *tm817_get_lvar = find_func_lvar(fn, "get");
  ASSERT_TRUE(tm817_get_lvar != NULL);
  ASSERT_EQ(1, tm817_get_lvar->pointer_qual_levels);
  ASSERT_EQ(1, tm817_get_lvar->funcptr_sig.ret_is_data_pointer);
  ASSERT_EQ(0, tm817_get_lvar->funcptr_sig.ret_int_width);
  lvar_t *tm817_get2_lvar = find_func_lvar(fn, "get2");
  ASSERT_TRUE(tm817_get2_lvar != NULL);
  ASSERT_EQ(1, tm817_get2_lvar->pointer_qual_levels);
  ASSERT_EQ(1, tm817_get2_lvar->funcptr_sig.ret_is_data_pointer);
  ASSERT_EQ(0, tm817_get2_lvar->funcptr_sig.ret_int_width);
  lvar_t *tm817_pp_lvar = find_func_lvar(fn, "pp");
  ASSERT_TRUE(tm817_pp_lvar != NULL);
  ASSERT_EQ(2, tm817_pp_lvar->pointer_qual_levels);
  ASSERT_EQ(0, tm817_pp_lvar->funcptr_sig.ret_is_data_pointer);
  ASSERT_EQ(4, tm817_pp_lvar->funcptr_sig.ret_int_width);
  ASSERT_EQ(1u, tm817_pp_lvar->funcptr_sig.param_int_mask);
  lvar_t *tm817_lget_lvar = find_func_lvar(fn, "lget");
  ASSERT_TRUE(tm817_lget_lvar != NULL);
  ASSERT_EQ(1, tm817_lget_lvar->pointer_qual_levels);
  ASSERT_EQ(1, tm817_lget_lvar->funcptr_sig.ret_is_data_pointer);
  ASSERT_EQ(0, tm817_lget_lvar->funcptr_sig.ret_int_width);
  lvar_t *tm817_lpp_lvar = find_func_lvar(fn, "lpp");
  ASSERT_TRUE(tm817_lpp_lvar != NULL);
  ASSERT_EQ(2, tm817_lpp_lvar->pointer_qual_levels);
  ASSERT_EQ(0, tm817_lpp_lvar->funcptr_sig.ret_is_data_pointer);
  ASSERT_EQ(4, tm817_lpp_lvar->funcptr_sig.ret_int_width);
  ASSERT_EQ(1u, tm817_lpp_lvar->funcptr_sig.param_int_mask);

  parsed_code = parse_program_input(
      "double __tm700_d; double *__tm700_ret_dp(void){ return &__tm700_d; } "
      "double *(*__tm700_gfp)(void)=__tm700_ret_dp; "
      "int main(void){ double *(*fp)(void)=__tm700_ret_dp; fp(); __tm700_gfp(); return 0; }");
  fn = as_func(parsed_code[1]);
  lvar_t *tm700_fp_lvar = find_func_lvar(fn, "fp");
  ASSERT_TRUE(tm700_fp_lvar != NULL);
  ASSERT_EQ(1, tm700_fp_lvar->funcptr_sig.ret_is_data_pointer);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, tm700_fp_lvar->funcptr_sig.ret_fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, tm700_fp_lvar->funcptr_sig.ret_pointee_fp_kind);
  global_var_t *tm700_gfp = psx_find_global_var("__tm700_gfp", 11);
  ASSERT_TRUE(tm700_gfp != NULL);
  ASSERT_EQ(1, tm700_gfp->funcptr_sig.ret_is_data_pointer);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, tm700_gfp->funcptr_sig.ret_fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, tm700_gfp->funcptr_sig.ret_pointee_fp_kind);

  parsed_code = parse_program_input(
      "int __tm814_inc(int x){ return x + 1; } "
      "int main(void){ int (*p)(int)=__tm814_inc; int (**pp)(int)=&p; return (*pp)(41); }");
  fn = as_func(parsed_code[1]);
  lvar_t *tm814_pp_lvar = find_func_lvar(fn, "pp");
  ASSERT_TRUE(tm814_pp_lvar != NULL);
  ASSERT_EQ(2, tm814_pp_lvar->pointer_qual_levels);
  ASSERT_EQ(0, tm814_pp_lvar->funcptr_sig.ret_is_data_pointer);
  ASSERT_EQ(4, tm814_pp_lvar->funcptr_sig.ret_int_width);
  ASSERT_EQ(1u, tm814_pp_lvar->funcptr_sig.param_int_mask);
  node_t *tm814_pp_node = psx_node_new_lvar_for(tm814_pp_lvar);
  node_t *tm814_deref_pp = psx_node_new_unary_deref_for(tm814_pp_node);
  psx_decl_funcptr_sig_t tm814_deref_sig =
      psx_node_mem_funcptr_sig((node_mem_t *)tm814_deref_pp);
  ASSERT_EQ(0, tm814_deref_sig.ret_is_data_pointer);
  ASSERT_EQ(4, tm814_deref_sig.ret_int_width);
  ASSERT_EQ(1u, tm814_deref_sig.param_int_mask);

  parsed_code = parse_program_input(
      "int __tm815_inc(int x){ return x + 1; } "
      "int (*__tm815_gp)(int)=__tm815_inc; int (**__tm815_gpp)(int)=&__tm815_gp; "
      "int __tm815_apply(int (**pp)(int)){ return (*pp)(41); } "
      "int main(void){ return (*__tm815_gpp)(41) + __tm815_apply(__tm815_gpp); }");
  global_var_t *tm815_gpp = psx_find_global_var("__tm815_gpp", 11);
  ASSERT_TRUE(tm815_gpp != NULL);
  ASSERT_EQ(2, tm815_gpp->pointer_qual_levels);
  ASSERT_EQ(0, tm815_gpp->funcptr_sig.ret_is_data_pointer);
  ASSERT_EQ(4, tm815_gpp->funcptr_sig.ret_int_width);
  ASSERT_EQ(1u, tm815_gpp->funcptr_sig.param_int_mask);
  fn = as_func(parsed_code[1]);
  lvar_t *tm815_param_pp = find_func_lvar(fn, "pp");
  ASSERT_TRUE(tm815_param_pp != NULL);
  ASSERT_EQ(2, tm815_param_pp->pointer_qual_levels);
  ASSERT_EQ(0, tm815_param_pp->funcptr_sig.ret_is_data_pointer);
  ASSERT_EQ(4, tm815_param_pp->funcptr_sig.ret_int_width);
  ASSERT_EQ(1u, tm815_param_pp->funcptr_sig.param_int_mask);

  parsed_code = parse_program_input(
      "int __tm816_i; int *__tm816_retp(void){ return &__tm816_i; } "
      "int __tm816_inc(int x){ return x + 1; } "
      "int *(*__tm816_gfp)(void)=__tm816_retp; "
      "struct TM816 { int *(*fp)(void); int (**pp)(int); }; "
      "int *__tm816_apply(int *(*fp)(void)){ return fp(); } "
      "int __tm816_call(struct TM816 *s){ return *s->fp() + (*s->pp)(1); } "
      "int main(void){ return *__tm816_apply(__tm816_retp); }");
  global_var_t *tm816_gfp = psx_find_global_var("__tm816_gfp", 11);
  ASSERT_TRUE(tm816_gfp != NULL);
  ASSERT_EQ(1, tm816_gfp->funcptr_sig.ret_is_data_pointer);
  ASSERT_EQ(0, tm816_gfp->funcptr_sig.ret_int_width);
  ASSERT_EQ(0u, tm816_gfp->funcptr_sig.param_int_mask);
  fn = as_func(parsed_code[2]);
  lvar_t *tm816_param_fp = find_func_lvar(fn, "fp");
  ASSERT_TRUE(tm816_param_fp != NULL);
  ASSERT_EQ(1, tm816_param_fp->funcptr_sig.ret_is_data_pointer);
  ASSERT_EQ(0, tm816_param_fp->funcptr_sig.ret_int_width);
  ASSERT_EQ(0u, tm816_param_fp->funcptr_sig.param_int_mask);
  tag_member_info_t tm816_fp_info = {0};
  ASSERT_TRUE(psx_ctx_find_tag_member_info(TK_STRUCT, "TM816", 5, "fp", 2,
                                           &tm816_fp_info));
  ASSERT_EQ(1, tm816_fp_info.funcptr_sig.ret_is_data_pointer);
  ASSERT_EQ(0, tm816_fp_info.funcptr_sig.ret_int_width);
  ASSERT_EQ(0u, tm816_fp_info.funcptr_sig.param_int_mask);
  tag_member_info_t tm816_pp_info = {0};
  ASSERT_TRUE(psx_ctx_find_tag_member_info(TK_STRUCT, "TM816", 5, "pp", 2,
                                           &tm816_pp_info));
  ASSERT_EQ(2, tm816_pp_info.pointer_qual_levels);
  ASSERT_EQ(0, tm816_pp_info.funcptr_sig.ret_is_data_pointer);
  ASSERT_EQ(4, tm816_pp_info.funcptr_sig.ret_int_width);
  ASSERT_EQ(1u, tm816_pp_info.funcptr_sig.param_int_mask);

  parsed_code = parse_program_input(
      "double __tm_sq(double x){ return x*x; } "
      "int *__tm_makep(void){ static int x=1; return &x; } "
      "int main(void){ double (*df)(double)=__tm_sq; int *(*pf)(void)=__tm_makep; "
      "df(2.0); pf(); return 0; }");
  fn = as_func(parsed_code[2]);
  body = as_block(fn->base.rhs);
  node_t *indirect_double_call = NULL;
  node_t *indirect_ptr_call = NULL;
  for (int i = 0; body->body[i]; i++) {
    node_t *n = body->body[i];
    if (n->kind != ND_FUNCALL) continue;
    node_func_t *call = as_func(n);
    if (!call->callee || call->callee->kind != ND_LVAR) continue;
    lvar_t *callee_lvar = psx_node_lvar_symbol(call->callee);
    if (callee_lvar && callee_lvar->len == 2 && strncmp(callee_lvar->name, "df", 2) == 0)
      indirect_double_call = n;
    if (callee_lvar && callee_lvar->len == 2 && strncmp(callee_lvar->name, "pf", 2) == 0)
      indirect_ptr_call = n;
	  }
	  ASSERT_TRUE(indirect_double_call->type != NULL);
	  psx_type_t *indirect_double_ty = psx_node_get_type(indirect_double_call);
	  ASSERT_TRUE(indirect_double_ty != NULL);
	  ASSERT_EQ(PSX_TYPE_FLOAT, indirect_double_ty->kind);
	  ASSERT_EQ(8, psx_type_sizeof(indirect_double_ty));
	  ASSERT_TRUE(indirect_ptr_call->type != NULL);
	  psx_type_t *indirect_ptr_ty = psx_node_get_type(indirect_ptr_call);
	  ASSERT_TRUE(indirect_ptr_ty != NULL);
	  ASSERT_EQ(PSX_TYPE_POINTER, indirect_ptr_ty->kind);
	  ASSERT_EQ(4, psx_type_deref_size(indirect_ptr_ty));
	  ASSERT_TRUE(ps_node_is_pointer(indirect_ptr_call));

  parsed_code = parse_program_input(
      "struct CQ { int v; }; const struct CQ *__tm_cq(void){ return 0; } "
      "int main(void){ __tm_cq(); return 0; }");
  fn = as_func(parsed_code[1]);
  body = as_block(fn->base.rhs);
  node_t *const_struct_ptr_call = NULL;
  for (int i = 0; body->body[i]; i++) {
    node_t *n = body->body[i];
    if (n->kind != ND_FUNCALL) continue;
    node_func_t *call = as_func(n);
    if (call->funcname_len == 7 && strncmp(call->funcname, "__tm_cq", 7) == 0) {
      const_struct_ptr_call = n;
      break;
    }
  }
  ASSERT_TRUE(const_struct_ptr_call != NULL);
  psx_type_t *const_struct_ptr_ty = psx_node_get_type(const_struct_ptr_call);
  ASSERT_TRUE(const_struct_ptr_ty != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, const_struct_ptr_ty->kind);
  ASSERT_TRUE(const_struct_ptr_ty->base != NULL);
  ASSERT_EQ(TK_STRUCT, const_struct_ptr_ty->base->tag_kind);
  ASSERT_TRUE(const_struct_ptr_ty->base->is_const_qualified);
  token_kind_t call_tag = TK_EOF;
  char *call_tag_name = NULL;
  int call_tag_len = 0;
  int call_is_tag_ptr = 0;
  psx_node_get_tag_type(const_struct_ptr_call, &call_tag, &call_tag_name,
                        &call_tag_len, &call_is_tag_ptr);
  ASSERT_EQ(TK_STRUCT, call_tag);
  ASSERT_EQ(2, call_tag_len);
  ASSERT_TRUE(strncmp(call_tag_name, "CQ", 2) == 0);
  ASSERT_EQ(1, call_is_tag_ptr);

  parsed_code = parse_program_input(
      "int __tm_zero(void){ return 0; } "
      "struct FS { int (*zerofunc)(void); }; "
      "struct FS __tm_fs = { __tm_zero }; "
      "struct FS *__tm_anon(void){ return &__tm_fs; } "
      "typedef struct FS *(*__tm_fty)(void); "
      "__tm_fty __tm_go(void){ return __tm_anon; } "
      "int main(void){ __tm_go()(); return 0; }");
  node_func_t *go_def = as_func(parsed_code[2]);
  psx_decl_funcptr_sig_t go_node_sig = psx_node_funcdef_ret_funcptr_sig(go_def);
  ASSERT_TRUE(psx_decl_funcptr_sig_has_payload(go_node_sig));
  ASSERT_EQ(1, go_node_sig.ret_is_data_pointer);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, go_node_sig.ret_fp_kind);
  psx_decl_funcptr_sig_t go_ctx_sig =
      psx_ctx_get_function_ret_funcptr_sig("__tm_go", 7);
  ASSERT_EQ(go_ctx_sig.ret_is_data_pointer, go_node_sig.ret_is_data_pointer);
  ASSERT_EQ(go_ctx_sig.ret_fp_kind, go_node_sig.ret_fp_kind);
  node_mem_t go_ret_mem = {0};
  psx_node_store_funcptr_metadata(&go_ret_mem, go_node_sig);
  ASSERT_TRUE(psx_node_mem_has_funcptr_metadata(&go_ret_mem));
  ASSERT_EQ(1, go_ret_mem.funcptr_sig.ret_is_data_pointer);
  fn = as_func(parsed_code[3]);
  body = as_block(fn->base.rhs);
  node_t *funcptr_chain_call = NULL;
  for (int i = 0; body->body[i]; i++) {
    node_t *n = body->body[i];
    if (n->kind != ND_FUNCALL) continue;
    node_func_t *call = as_func(n);
    if (call->callee && call->callee->kind == ND_FUNCALL) {
      funcptr_chain_call = n;
      break;
    }
  }
  ASSERT_TRUE(funcptr_chain_call != NULL);
  psx_type_t *funcptr_chain_ty = psx_node_get_type(funcptr_chain_call);
  ASSERT_TRUE(funcptr_chain_ty != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, funcptr_chain_ty->kind);
  ASSERT_TRUE(funcptr_chain_ty->base != NULL);
  ASSERT_EQ(TK_STRUCT, funcptr_chain_ty->base->tag_kind);
  call_tag = TK_EOF;
  call_tag_name = NULL;
  call_tag_len = 0;
  call_is_tag_ptr = 0;
  psx_node_get_tag_type(funcptr_chain_call, &call_tag, &call_tag_name,
                        &call_tag_len, &call_is_tag_ptr);
  ASSERT_EQ(TK_STRUCT, call_tag);
  ASSERT_EQ(2, call_tag_len);
  ASSERT_TRUE(strncmp(call_tag_name, "FS", 2) == 0);
  ASSERT_EQ(1, call_is_tag_ptr);

  parsed_code = parse_program_input(
      "double __tm698_add(double x){ return x + 0.5; } "
      "typedef double (*TM698_DF)(double); "
      "TM698_DF __tm698_pick(void){ return __tm698_add; } "
      "int main(void){ return __tm698_pick()(3.0) == 3.5; }");
  node_func_t *pick_def = as_func(parsed_code[1]);
  psx_decl_funcptr_sig_t pick_node_sig = psx_node_funcdef_ret_funcptr_sig(pick_def);
  ASSERT_TRUE(psx_decl_funcptr_sig_has_payload(pick_node_sig));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, pick_node_sig.ret_fp_kind);
  ASSERT_EQ(2u, pick_node_sig.param_fp_mask);
  psx_decl_funcptr_sig_t pick_ctx_sig =
      psx_ctx_get_function_ret_funcptr_sig("__tm698_pick", 12);
  ASSERT_EQ(pick_ctx_sig.ret_fp_kind, pick_node_sig.ret_fp_kind);
  ASSERT_EQ(pick_ctx_sig.param_fp_mask, pick_node_sig.param_fp_mask);

  parsed_code = parse_program_input(
      "int __tm818_inc(int x){ return x + 1; } "
      "int (*__tm818_fp)(int)=__tm818_inc; "
      "int (**__tm818_getpp(void))(int){ return &__tm818_fp; } "
      "int main(void){ return (*__tm818_getpp())(20) + (**__tm818_getpp())(30); }");
  node_func_t *tm818_getpp_def = as_func(parsed_code[1]);
  psx_decl_funcptr_sig_t tm818_getpp_sig =
      psx_node_funcdef_ret_funcptr_sig(tm818_getpp_def);
  ASSERT_TRUE(psx_decl_funcptr_sig_has_payload(tm818_getpp_sig));
  ASSERT_EQ(0, tm818_getpp_sig.ret_is_data_pointer);
  ASSERT_EQ(4, tm818_getpp_sig.ret_int_width);
  ASSERT_EQ(1u, tm818_getpp_sig.param_int_mask);
  ASSERT_EQ(2, psx_ctx_get_function_ret_pointer_levels("__tm818_getpp", 13));
}

static void test_translation_unit_reset_static_local_state() {
  printf("test_translation_unit_reset_static_local_state...\n");

  const char *input = "int f(void) { static int x=1; return x; }";
  ps_reset_translation_unit_state();
  parsed_code = parse_program_input(input);
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_TRUE(psx_find_global_var("f.x.0", 5) != NULL);
  ASSERT_TRUE(psx_find_global_var("f.x.1", 5) == NULL);

  ps_reset_translation_unit_state();
  parsed_code = parse_program_input(input);
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_TRUE(psx_find_global_var("f.x.0", 5) != NULL);
  ASSERT_TRUE(psx_find_global_var("f.x.1", 5) == NULL);
  ps_reset_translation_unit_state();
}

static void test_translation_unit_reset_anonymous_tag_state() {
  printf("test_translation_unit_reset_anonymous_tag_state...\n");

  const char *input = "struct { int x; } g; int main(void) { return 0; }";
  ps_reset_translation_unit_state();
  parsed_code = parse_program_input(input);
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_TRUE(psx_ctx_has_tag_type(TK_STRUCT, "__anon_tag_0", 12));
  ASSERT_TRUE(!psx_ctx_has_tag_type(TK_STRUCT, "__anon_tag_1", 12));

  ps_reset_translation_unit_state();
  parsed_code = parse_program_input(input);
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_TRUE(psx_ctx_has_tag_type(TK_STRUCT, "__anon_tag_0", 12));
  ASSERT_TRUE(!psx_ctx_has_tag_type(TK_STRUCT, "__anon_tag_1", 12));
  ps_reset_translation_unit_state();
}

static void test_translation_unit_reset_decl_locals_state() {
  printf("test_translation_unit_reset_decl_locals_state...\n");

  char name[] = "x";
  psx_decl_reset_locals();
  ASSERT_TRUE(psx_decl_register_lvar(name, 1) != NULL);
  ASSERT_TRUE(psx_decl_find_lvar(name, 1) != NULL);
  ps_reset_translation_unit_state();
  ASSERT_TRUE(psx_decl_find_lvar(name, 1) == NULL);
}

static void test_translation_unit_reset_pragma_pack_state() {
  printf("test_translation_unit_reset_pragma_pack_state...\n");

  pragma_pack_reset();
  pragma_pack_set(8);
  pragma_pack_push(1);
  ASSERT_EQ(1, pragma_pack_current);
  ps_reset_translation_unit_state();
  ASSERT_EQ(0, pragma_pack_current);
  pragma_pack_pop();
  ASSERT_EQ(0, pragma_pack_current);
}

static void test_multiple_funcdefs() {
  printf("test_multiple_funcdefs...\n");
  parsed_code = parse_program_input("foo() { 1; } bar() { 2; }");

  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_TRUE(strncmp(as_func(parsed_code[0])->funcname, "foo", 3) == 0);

  ASSERT_TRUE(parsed_code[1] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[1]->kind);
  ASSERT_TRUE(strncmp(as_func(parsed_code[1])->funcname, "bar", 3) == 0);

  ASSERT_TRUE(parsed_code[2] == NULL);

  parsed_code = parse_program_input("int add(int a, int b); int add(int a, int b) { return a+b; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_TRUE(strncmp(as_func(parsed_code[0])->funcname, "add", 3) == 0);
  ASSERT_TRUE(parsed_code[1] == NULL);

  parsed_code = parse_program_input("int log(const char *fmt, ...); int main() { return 0; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_TRUE(strncmp(as_func(parsed_code[0])->funcname, "main", 4) == 0);
  ASSERT_TRUE(parsed_code[1] == NULL);

  parsed_code = parse_program_input("int **sig_proto_pp(void); int main(void) { return 0; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(2, psx_ctx_get_function_ret_pointer_levels("sig_proto_pp", 12));

  parsed_code = parse_program_input("int **sig_def_pp(void) { return 0; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(2, psx_ctx_get_function_ret_pointer_levels("sig_def_pp", 10));

  parsed_code = parse_program_input("int variadic(...){ return 0; } int main() { return variadic(); }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_TRUE(strncmp(as_func(parsed_code[0])->funcname, "variadic", 8) == 0);
  ASSERT_TRUE(parsed_code[1] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[1]->kind);
  ASSERT_TRUE(parsed_code[2] == NULL);

  parsed_code = parse_program_input("int f(int a[static 3], int b[restrict static 2]) { return 7; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_TRUE(strncmp(as_func(parsed_code[0])->funcname, "f", 1) == 0);
  ASSERT_TRUE(parsed_code[1] == NULL);

  parsed_code = parse_program_input("struct S { int x; }; int main() { return 0; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_TRUE(strncmp(as_func(parsed_code[0])->funcname, "main", 4) == 0);
  ASSERT_TRUE(parsed_code[1] == NULL);

  parsed_code = parse_program_input("struct S { int x; } *gp; int main() { return 0; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_TRUE(strncmp(as_func(parsed_code[0])->funcname, "main", 4) == 0);
  ASSERT_TRUE(parsed_code[1] == NULL);

  parsed_code = parse_program_input("int g=1; int main() { return 0; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_TRUE(strncmp(as_func(parsed_code[0])->funcname, "main", 4) == 0);
  ASSERT_TRUE(parsed_code[1] == NULL);

  parsed_code = parse_program_input("extern int g; inline int add(int a, int b) { return a+b; } int main() { return add(3,4); }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_TRUE(strncmp(as_func(parsed_code[0])->funcname, "add", 3) == 0);
  ASSERT_TRUE(parsed_code[1] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[1]->kind);
  ASSERT_TRUE(strncmp(as_func(parsed_code[1])->funcname, "main", 4) == 0);
  ASSERT_TRUE(parsed_code[2] == NULL);

  parsed_code = parse_program_input("_Noreturn void die() { return; } int main() { return 0; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_TRUE(strncmp(as_func(parsed_code[0])->funcname, "die", 3) == 0);
  ASSERT_TRUE(parsed_code[1] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[1]->kind);
  ASSERT_TRUE(strncmp(as_func(parsed_code[1])->funcname, "main", 4) == 0);
  ASSERT_TRUE(parsed_code[2] == NULL);

  parsed_code = parse_program_input("_Static_assert(1, \"ok\"); int main() { return 0; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_TRUE(strncmp(as_func(parsed_code[0])->funcname, "main", 4) == 0);
  ASSERT_TRUE(parsed_code[1] == NULL);
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
  expect_parse_ok("main() { float f=1.0; ++f; }");       // C11: 浮動小数点の ++ は合法
  expect_parse_ok("main() { double d=1.0; d--; }");      // C11: 浮動小数点の -- は合法
  expect_parse_fail("main() { 1 += 2; }");               // lvalueでない
  expect_parse_fail("main() { return; }");               // 非void関数で式なしreturn
  expect_parse_fail("void f() { return 1; }");           // void関数で値return
  expect_parse_fail("main() { goto MISSING; return 0; }"); // 未定義ラベル
  expect_parse_fail("main() { struct T x; return 0; }");   // 未定義タグ参照
  expect_parse_ok("main() { { struct T { int x; }; } struct T *p; return 0; }"); // 外側スコープで新規前方宣言
  expect_parse_fail("main() { struct S { int x; }; union U { int y; }; union U u={1}; return (struct S)u; }"); // 非同種非スカラ型cast未対応
  expect_parse_fail("main() { short double x; return 0; }");   // 不正な型指定子組み合わせ
  expect_parse_fail("main() { _Complex int x; return 0; }");   // 浮動小数型以外との組み合わせは不正
  expect_parse_fail("main() { _Imaginary int x; return 0; }"); // 浮動小数型以外との組み合わせは不正
  expect_parse_fail("main() { return (_Thread_local int)1; }"); // cast型名のストレージ指定は未対応
  expect_parse_fail("main() { int a[-1]; return 0; }");         // 配列サイズは負数不可
  expect_parse_fail("main() { return _Generic(1, float:2); }"); // 一致なし + defaultなし
  expect_parse_fail("int bad(int a, ..., int b) { return 0; }"); // ... は末尾のみ
  expect_parse_fail("int bad(int) { return 0; }"); // 関数定義の仮引数には名前が必要
  expect_parse_fail("main() { _Static_assert(0, \"ng\"); return 0; }"); // static_assert失敗
  expect_parse_fail("main() { _Static_assert(x, \"ng\"); return 0; }"); // 非定数式
  expect_parse_fail("main() { int x; x.y=1; }");            // 非構造体への .
  expect_parse_fail("main() { int *p; p->y=1; }");          // 非構造体ポインタへの ->
  expect_parse_fail("main() { int x; int *p=&x; return *(void *)p; }"); // void* deref
  expect_parse_fail("main() { break; }");                // ループ/switch外
  expect_parse_fail("main() { continue; }");             // ループ外
  expect_parse_fail("int main(void) { int x = ({ continue; 0; }); return x; }");
  expect_parse_fail("int main(void) { while (({ continue; 1; })) return 0; return 0; }");
  expect_parse_fail("main() { case 1: return 0; }");     // switch外のcase
  expect_parse_fail("main() { default: return 0; }");    // switch外のdefault
  expect_parse_fail("main() { switch (1) { case 1: 0; case 1: 0; } }"); // case 重複
  expect_parse_fail("main() { switch (0) { case 1+2: 0; case 3: 0; } }"); // 定数式評価後のcase重複
  expect_parse_fail("main() { switch (1) { default: 0; default: 1; } }"); // default 重複
  expect_parse_fail("enum E { A = 2147483648 }; int main(void){ return 0; }"); // enum定数はint幅
  expect_parse_fail("main() { int x={1,2}; return x; }"); // スカラ波括弧初期化は単一要素のみ
  expect_parse_fail("main() { int a[2]={1,2,3}; return 0; }"); // 配列初期化子過多
  expect_parse_fail("main() { struct S { int x; }; struct S s=1; return 0; }"); // 構造体単一式初期化は未対応
  expect_parse_fail("main() { struct S { int x; }; struct S t={1}; struct S s=(t,1); return 0; }"); // 最終値が同型オブジェクトでない
  expect_parse_fail("main() { union U { int x; char y; }; union U u={1,2}; return 0; }"); // 共用体は1要素のみ
  expect_parse_fail("main() { union U { int x; char y; }; union U u={.x=1,2}; return 0; }"); // designatedでも1要素のみ
  expect_parse_fail("main() { int a[2]={[3]=1}; return 0; }"); // array designator 範囲外
  // C11 6.7.9p19: 同一 subobject への複数指定初期化子は後勝ちで受理される。
  expect_parse_ok("main() { struct S { int x; int y; }; struct S s={.x=1,.x=2}; return 0; }"); // struct重複designator (後勝ち)
  expect_parse_ok("main() { struct __BraceDup { int a[2]; int z; }; struct __BraceDup s={1,2,.a={3,4}}; return 0; }"); // brace elision後の上書き
  expect_parse_ok("main() { int a[2]={[0]=1,[0]=2}; return 0; }"); // array重複designator (後勝ち)
}

static void test_parse_invalid_diagnostics() {
  printf("test_parse_invalid_diagnostics...\n");
  expect_parse_fail_with_message("main() { goto MISSING; return 0; }", "[goto] 未定義ラベル 'MISSING'");
  expect_parse_fail_with_message("main() { L1: return 0; L1: return 1; }", "識別子が重複しています (ラベル): 'L1'");
  expect_parse_fail_with_message("main() { struct T x; return 0; }", "不完全型のオブジェクトは宣言できません");
  expect_parse_fail_with_message("main() { struct S { int x; }; union U { int y; }; union U u={1}; return (struct S)u; }", "[cast] struct 値へのキャストは未対応です（型不整合）");
  expect_parse_fail_with_message("main() { union U { int x; char y; }; struct S { int z; } s={1}; return (union U)s; }", "[cast] union 値へのキャストは未対応です（型不整合）");
  expect_parse_fail_with_message("main() { struct S { int x; }; struct S s=1; return 0; }", "[decl] 構造体の単一式初期化は同型オブジェクトのみ対応です");
  expect_parse_fail_with_message("main() { struct S { int x; }; struct S t={1}; struct S s=(t,1); return 0; }", "[decl] 構造体の単一式初期化は同型オブジェクトのみ対応です");
  expect_parse_fail_with_message("main() { union U { int x; char y; }; union U u={1,2}; return 0; }", "[decl] 共用体初期化子は現状1要素のみ対応です");
  expect_parse_fail_with_message("main() { union U { int x; char y; }; union U u={.x=1,2}; return 0; }", "[decl] 共用体初期化子は現状1要素のみ対応です");
  expect_parse_fail_with_message("main() { _Complex int x; return 0; }", "_Complex/_Imaginary は浮動小数型にのみ指定できます");
  expect_parse_fail_with_message("main() { return (_Complex int)1; }", "_Complex/_Imaginary cast は浮動小数型のみ対応です");
  expect_parse_fail_with_message("main() { return (_Thread_local int)1; }", "[cast] cast 型名にストレージ指定子は使えません");
  expect_parse_fail_with_message("main() { struct __IncOnly; struct __HasInc { struct __IncOnly m; }; return 0; }", "[decl] 不完全型のメンバは定義できません");
  expect_parse_fail_with_message("main() { struct T { int f(int); }; return 0; }", "[decl] 関数型のメンバは定義できません");
  expect_parse_fail_with_message("int bad(int) { return 0; }", "必要な項目がありません: 仮引数");
  expect_parse_fail_with_message("main() { int x; int *p=&x; return *(void *)p; }",
                                 "void* の deref はできません");
  expect_parse_fail_with_message("void f(void); int main(void){ int x; x=f(); return 0; }",
                                 "void 戻り値関数の結果は代入/初期化に使えません");
  expect_parse_fail_with_message("void f(void); int main(void){ void (*fp)(void)=f; int x; x=fp(); return 0; }",
                                 "void 戻り値関数の結果は代入/初期化に使えません");

  // 汎用cast未対応診断（"この型へのキャストは未対応です"）は現状到達しないことを固定する。
  expect_parse_fail_without_message("main() { return (_Thread_local int)1; }", "[cast] この型へのキャストは未対応です");
  expect_parse_fail_without_message("main() { struct S { int x; }; union U { int y; }; union U u={1}; return (struct S)u; }", "[cast] この型へのキャストは未対応です");

  // Parser拡張設定: 同種同サイズの非スカラcast受理を無効化できること。
  ps_set_enable_size_compatible_nonscalar_cast(false);
  expect_parse_fail_with_message(
      "main() { struct A { int x; }; struct B { int x; }; struct A a={7}; return ((struct B)a).x; }",
      "[cast] struct 値へのキャストは未対応です（型不整合）");
  ps_set_enable_size_compatible_nonscalar_cast(true);

  // Parser拡張設定: struct への scalar/pointer cast 受理を無効化できること。
  ps_set_enable_struct_scalar_pointer_cast(false);
  expect_parse_fail_with_message(
      "main() { struct S { int x; }; int a=0; return (struct S)a; }",
      "[cast] struct への scalar/pointer cast は設定で無効です");
  ps_set_enable_struct_scalar_pointer_cast(true);

  // Parser拡張設定: union への scalar/pointer cast 受理を無効化できること。
  ps_set_enable_union_scalar_pointer_cast(false);
  expect_parse_fail_with_message(
      "main() { union U { int x; char y; }; int a=0; return (union U)a; }",
      "[cast] union への scalar/pointer cast は設定で無効です");
  ps_set_enable_union_scalar_pointer_cast(true);

  // Parser拡張設定: union 先頭配列メンバの非波括弧初期化受理を無効化できること。
  ps_set_enable_union_array_member_nonbrace_init(false);
  expect_parse_fail_with_message(
      "main() { union U { int a[2]; int z; }; union U u={1,2}; return 0; }",
      "[decl] 共用体の配列メンバ非波括弧初期化は設定で無効です");
  ps_set_enable_union_array_member_nonbrace_init(true);

  // 入れ子 designator: 非配列メンバに .member[idx]=val は診断エラー
  expect_parse_fail_with_message("main() { struct S { int x; }; struct S s={.x[0]=3}; return 0; }",
                                 "入れ子designatorの対象が配列メンバではありません");

  // decl.c の「1/2/4/8 byte スカラのみ」診断は、現行型セットでは到達不能であることを固定する。
  // 将来 16-byte などの新スカラ型導入時は、ここを陽性診断テストへ置き換える。
  expect_parse_fail_without_message("main() { struct __IncOnly; struct __HasInc { struct __IncOnly m; }; return 0; }",
                                    "[decl] 構造体/共用体初期化は現在 1/2/4/8 byte スカラのみ対応です");
  expect_parse_fail_without_message("main() { struct T { int f(int); }; return 0; }",
                                    "[decl] 構造体/共用体初期化は現在 1/2/4/8 byte スカラのみ対応です");
}

// 意地悪テスト: パーサーの境界ケース
static void test_parse_evil_edge_cases() {
  printf("test_parse_evil_edge_cases...\n");

  // ネストした三項演算子: a?b?c:d:e は a?(b?c:d):e
    node_t *tn = parse_expr_input("1 ? 2 ? 3 : 4 : 5");
  ASSERT_EQ(ND_TERNARY, tn->kind);
  ASSERT_EQ(1, as_num(tn->lhs)->val);         // 条件: 1
  ASSERT_EQ(ND_TERNARY, tn->rhs->kind);       // then: 2?3:4
  ASSERT_EQ(2, as_num(tn->rhs->lhs)->val);    // 内側条件: 2
  ASSERT_EQ(3, as_num(tn->rhs->rhs)->val);    // 内側then: 3
  ASSERT_EQ(4, as_num(as_ctrl(tn->rhs)->els)->val); // 内側else: 4
  ASSERT_EQ(5, as_num(as_ctrl(tn)->els)->val);      // 外側else: 5

  // 複雑な優先順位: 1+2*3==7&&1||0 → (((1+(2*3))==7)&&1)||0
    node_t *cp = parse_expr_input("1+2*3==7&&1||0");
  ASSERT_EQ(ND_LOGOR, cp->kind);
  ASSERT_EQ(0, as_num(cp->rhs)->val);         // ||0
  ASSERT_EQ(ND_LOGAND, cp->lhs->kind);
  ASSERT_EQ(1, as_num(cp->lhs->rhs)->val);    // &&1
  ASSERT_EQ(ND_EQ, cp->lhs->lhs->kind);       // ==
  ASSERT_EQ(7, as_num(cp->lhs->lhs->rhs)->val); // ==7
  ASSERT_EQ(ND_ADD, cp->lhs->lhs->lhs->kind); // 1+...
  ASSERT_EQ(ND_MUL, cp->lhs->lhs->lhs->rhs->kind); // 2*3

  // ビット演算と論理演算の優先順位: 1&2|3^4
  // → (1&2) | (3^4) → ND_BITOR( ND_BITAND(1,2), ND_BITXOR(3,4) )
    node_t *bw = parse_expr_input("1&2|3^4");
  ASSERT_EQ(ND_BITOR, bw->kind);
  ASSERT_EQ(ND_BITAND, bw->lhs->kind);
  ASSERT_EQ(1, as_num(bw->lhs->lhs)->val);
  ASSERT_EQ(2, as_num(bw->lhs->rhs)->val);
  ASSERT_EQ(ND_BITXOR, bw->rhs->kind);
  ASSERT_EQ(3, as_num(bw->rhs->lhs)->val);
  ASSERT_EQ(4, as_num(bw->rhs->rhs)->val);

  // x+++y の式解析: (x++) + y
  parsed_code = parse_program_input("main() { int x=1; int y=2; return x+++y; }");
  // パースが成功すればOK

  // キャストと単項マイナスのネスト: (int)-(char)5
    node_t *cn = parse_expr_input("(int)-(char)5");
  // (int)(0-(char)5) → ND_CAST(ND_SUB(0, ND_CAST(5)))のような構造
  // パースが壊れずに完了することを確認
  ASSERT_TRUE(cn != NULL);

  // シフトと比較の優先順位: 1<<2<8 → (1<<2)<8
    node_t *sh = parse_expr_input("1<<2<8");
  ASSERT_EQ(ND_LT, sh->kind);
  ASSERT_EQ(TK_LT, sh->source_op);
  ASSERT_EQ(ND_SHL, sh->lhs->kind);
  ASSERT_EQ(1, as_num(sh->lhs->lhs)->val);
  ASSERT_EQ(2, as_num(sh->lhs->rhs)->val);
  ASSERT_EQ(8, as_num(sh->rhs)->val);

  // `>`/`>=` は AST では `<`/`<=` へ正規化されるが、後段 warning 用に元演算子を保持する。
  node_t *gt = parse_expr_input("1>2");
  ASSERT_EQ(ND_LT, gt->kind);
  ASSERT_EQ(TK_GT, gt->source_op);
  ASSERT_EQ(2, as_num(gt->lhs)->val);
  ASSERT_EQ(1, as_num(gt->rhs)->val);
  node_t *ge = parse_expr_input("1>=2");
  ASSERT_EQ(ND_LE, ge->kind);
  ASSERT_EQ(TK_GE, ge->source_op);
  ASSERT_EQ(2, as_num(ge->lhs)->val);
  ASSERT_EQ(1, as_num(ge->rhs)->val);

  // カンマ演算子と代入の優先順位: a=1,b=2 → (a=1),(b=2)
  parsed_code = parse_program_input("main() { int a; int b; a=1,b=2; }");
  // パースが成功すればOK

  // 複雑な式文のパース
  expect_parse_ok("main() { int x; x = 1 + 2 * 3 - 4 / 2 + (5 % 3); return x; }");
  expect_parse_ok_without_message("main() { int x; *(&x) = 1; return x; }", "W3004");
  expect_parse_ok_without_message(
      "typedef _Atomic int atomic_int; int main(void){ atomic_int x; ((void)(*(&x)=10)); return x; }",
      "W3004");
  expect_parse_ok_without_message(
      "int main(void){ struct S { int a; int b; }; struct S s; s.a=2; s.b=5; return s.a+s.b; }",
      "W3004");
  expect_parse_ok_without_message(
      "int main(void){ struct B { unsigned a:3; unsigned b:5; }; struct B s; s.a=5; s.b=10; return s.a; }",
      "W3004");
  expect_parse_ok_without_message("int main(void){ int x; int *p=&x; return p==0; }", "W3004");
  expect_parse_ok_without_message("int main(void){ int x; int *p=&x; return p==0; }", "W3003");
  expect_parse_ok_without_message("int main(void){ int x; x=1; return 0; }", "W3003");
  expect_parse_ok_without_message("int main(void){ int x; return &x==0; }", "W3003");
  expect_parse_ok_without_message("int main(void){ int x; return &x==0; }", "W3004");
  expect_parse_ok_with_message("int main(void){ int x=1; return 0; }", "W3003");
  expect_parse_ok_with_message("int main(void){ int x; x=x; return 0; }", "W3012");
  expect_parse_ok_with_message("int main(void){ int x=1.5; return 0; }", "W3010");
  expect_parse_ok_with_message("int main(void){ int x; x=1.5; return 0; }", "W3010");
  expect_parse_ok_with_message("int main(void){ return 1.5; }", "W3010");
  expect_parse_ok_with_message("int main(void){ int x=1; return x==x; }", "W3013");
  expect_parse_ok_with_message("int main(void){ int x=1; return x&&x; }", "W3020");
  expect_parse_ok_with_message("int main(void){ unsigned int u=1; int s=-1; return s<u; }",
                               "W3018");
  expect_parse_ok_without_message("int main(void){ unsigned int u=1; long s=-1; return s<u; }",
                                  "W3018");
  expect_parse_ok_without_message(
      "int main(void){ unsigned char u=1; int s=-1; return s<u; }", "W3018");
  expect_parse_ok_with_message(
      "int main(void){ unsigned long u=1; long s=-1; return s<u; }", "W3018");
  expect_parse_ok_with_message("int main(void){ unsigned int u=1; return u>=0; }", "W3019");
  expect_parse_ok_with_message("int main(void){ unsigned int u=1; return 0>u; }", "W3019");
  expect_parse_ok_with_message("int main(void){ unsigned char u=1; return u<0; }", "W3019");
  expect_parse_ok_with_message("int main(void){ unsigned short u=1; return 0<=u; }", "W3019");
  expect_parse_ok_with_message(
      "unsigned char f(void){ return 1; } int main(void){ return f()<0; }", "W3019");
  expect_parse_ok_with_message("int main(void){ int x=1; return (unsigned char)x<0; }",
                               "W3019");
  expect_parse_ok_without_message("int main(void){ signed char s=1; return s<0; }", "W3019");
  expect_parse_ok_with_message("int main(void){ int *p; return p==5; }", "W3022");
  expect_parse_ok_with_message("int main(void){ int x=1; return !x==0; }", "W3021");
  expect_parse_ok_with_message("int main(void){ int x=0; if (x=1) return x; return 0; }",
                               "W3007");
  expect_parse_ok_with_message("int main(void){ int x=0; while (x=1) return x; return 0; }",
                               "W3007");
  expect_parse_ok_with_message("int main(void){ int x=0; if (x,1) return x; return 0; }",
                               "W3008");
  expect_parse_ok_with_message("int main(void){ int x=0; if (x); return x; }", "W3009");
  expect_parse_ok_with_message("int *f(void){ int x=0; return &x; }", "W3006");
  expect_parse_ok_without_message("int *f(void){ static int x; return &x; }", "W3006");
  expect_parse_ok_without_message("int g; int *f(void){ return &g; }", "W3006");
  expect_parse_ok_with_message(
      "int main(void){ int x=0; switch(x){ case 0: x=1; case 1: return x; } return 0; }",
      "W3017");
  expect_parse_ok_without_message(
      "int main(void){ int x=0; switch(x){ case 0: x=1; break; case 1: return x; } return 0; }",
      "W3017");
  expect_parse_ok_without_message(
      "int main(void){ int x=0; switch(x){ case 0: case 1: return 1; } return x; }",
      "W3017");
  expect_parse_ok_with_message("int main(void){ char c=200; return c; }", "W3011");
  expect_parse_ok_with_message("int main(void){ unsigned char c=300; return c; }", "W3011");
  expect_parse_ok_without_message("int main(void){ unsigned char c=-1; return c; }", "W3011");
  expect_parse_ok_without_message("int main(void){ _Bool b=300; return b; }", "W3011");
  expect_parse_ok_with_message("int main(void){ return 2147483647 + 1; }", "W3023");
  expect_parse_ok_with_message("int main(void){ return 2147483647 * 2; }", "W3023");
  expect_parse_ok_without_message("int main(void){ return 2147483647L + 1L; }", "W3023");
  expect_parse_ok_without_message("int main(void){ int a[4]; int *p=a; return *(p + 2147483647); }",
                                  "W3023");
  expect_parse_ok_with_message("int main(void){ return 1 << 32; }", "W3014");
  expect_parse_ok_with_message("long main(void){ return 1L << 64; }", "W3014");
  expect_parse_ok_with_message("int main(void){ return 1 / 0; }", "W3015");
  expect_parse_ok_with_message("int main(void){ return 1 % 0; }", "W3015");
  expect_parse_ok_with_message("int main(void){ return f(); } int f(void){ return 1; }", "W3016");
  expect_parse_ok_without_message("int f(void); int main(void){ return f(); }", "W3016");
  expect_parse_ok_with_message("main(void){ return 0; }", "W3001");
  expect_parse_ok_without_message("int main(void){ return 0; }", "W3001");
  expect_parse_ok_without_message("int main(void){ int x=2.0; return 0; }", "W3010");
  expect_parse_ok_without_message("int main(void){ int x; x=2.0; return 0; }", "W3010");
  expect_parse_ok_without_message("double main(void){ return 1.5; }", "W3010");
  expect_parse_ok_without_message("int f(int x){ return x; } int main(void){ return f(3); }", "W3004");
  expect_parse_ok_without_message("int main(void){ int x=7; return x; }", "W3004");
  expect_parse_ok_without_message("int main(void){ static int x; return x; }", "W3004");
  expect_parse_ok("int main(void){ while(1){ ({ continue; 0; }); } return 0; }");
  expect_parse_ok("int main(void){ int x=0; switch(x){ case 0: ({ break; 0; }); } return 0; }");
  expect_parse_ok("int main(void){ int x=0; switch(x){ case (0 ? 1 : 2147483648): return 1; } return 0; }");
  expect_parse_ok("int main(void){ int x=0; switch(x){ case 1: switch(x){ case 1: return 1; } return 0; } return 0; }");
  expect_parse_ok_without_message(
      "int main(void){ struct S { char c; int i; }; struct S s; long o=(char*)&s.i-(char*)&s; return o>0; }",
      "W3004");
  expect_parse_ok_with_message("int main(void){ int *p; return &*p == 0; }", "W3004");
  expect_parse_ok_with_message("int main(void){ goto L; int x; return x; L: return 0; }", "W3002");
  expect_parse_ok_without_message("int main(void){ goto L; int x; return x; L: return 0; }", "W3004");
  expect_parse_ok_without_message("int main(void){ goto L; int x; return x; L: return 0; }", "W3003");
  expect_parse_ok_with_message("int main(void){ goto L; { int x; x=1; return x; } L: return 0; }", "W3002");
  expect_parse_ok_without_message("int main(void){ goto L; { int x; x=1; return x; } L: return 0; }", "W3003");
  expect_parse_ok_without_message("int main(void){ goto L; { int x; x=1; return x; } L: return 0; }", "W3004");
  expect_parse_ok("main() { int a; int b; int c; a = b = c = 42; return a; }");
  expect_parse_ok("main() { return 1?2:3?4:5?6:7; }");
  expect_parse_ok("main() { int x=1; return x<<1|x<<2|x<<3; }");
  expect_parse_ok("main() { int x=1; return !!!!!x; }");
  expect_parse_ok("main() { int x=255; return ~~~x; }");
  expect_parse_ok("struct S { int x; }; int f(struct S (*p)) { return p->x; } int main() { struct S s={3}; return f(&s); }");

  // sizeof内の型
  expect_parse_ok("main() { return sizeof(int); }");
  expect_parse_ok("main() { return sizeof(int*); }");
  expect_parse_ok("main() { return sizeof(int (*[3])(int)); }");
  expect_parse_ok("main() { return sizeof(int (*[2])[3]); }");
  expect_parse_ok("main() { return sizeof(int (*(*[2])[3])); }");
  expect_parse_ok("main() { return sizeof(int (*(*)(void))[3]); }");
  expect_parse_ok("main() { return sizeof(int (*(*[2])(void))[3]); }");
  expect_parse_ok("main() { return sizeof(int (*(*)(void))(int)); }");
  expect_parse_ok("main() { return sizeof(int (*(*(*)(void))(int))[3]); }");
  expect_parse_ok("main() { return _Generic((int (*)(int))0, int (*[3])(int): 1, default: 2); }");
  expect_parse_ok("main() { return _Generic((int (*(*)(void))[3])0, int (*(*)(void))[3]: 1, default: 2); }");

  // 関数宣言のプロトタイプ
  expect_parse_ok("int f(int a, int b, int c); int main() { return f(1,2,3); }");
  expect_parse_ok("int (f)(int x) { return x; } int main() { return f(42); }");
  expect_parse_ok("int (*f(void))(int) { return 0; } int main() { return 0; }");
  expect_parse_ok("int (*f(int n))(int) { return 0; } int main() { return 0; }");
  expect_parse_ok("int (*(*f(void))(int))[3] { return 0; } int main() { return 0; }");
  expect_parse_ok("struct S { int x; } (f)(void){ struct S s; s.x=3; return s; } int main(){ return f().x; }");
  expect_parse_ok("union U { int x; } (f)(void){ union U u; return u; } int main(){ return 0; }");
  expect_parse_ok("int g=1; _Static_assert(sizeof(int)==4, \"ok\"); int main(){ return g; }");
  expect_parse_ok("typedef int myint; _Static_assert(1, \"ok\"); myint g=1; int main(){ return g; }");
  expect_parse_ok("typedef int myint; _Static_assert(sizeof(myint)==4, \"ok\"); int main(){ return 0; }");
  expect_parse_ok("typedef int A3[3]; _Static_assert(sizeof(A3)==12, \"ok\"); int main(){ return 0; }");

  // for文の複雑な初期化
  expect_parse_ok("main() { int i; int s=0; for(i=0; i<10; i=i+1) s=s+i; return s; }");

  // do-while の後に式文
  expect_parse_ok("main() { int x=0; do { x=x+1; } while(x<3); return x; }");

  // switch内のfall-through
  expect_parse_ok("main() { int x=2; int r=0; switch(x) { case 1: r=10; case 2: r=r+20; case 3: r=r+30; default: r=r+1; } return r; }");

  // ネストしたブロック
  expect_parse_ok("main() { { { { int x=42; return x; } } } }");

  // 意地悪テスト: 宣言・型の境界ケース

  // typedefで作った型名の使用
  expect_parse_ok("typedef int myint; myint add(myint a, myint b) { return a+b; } int main() { return add(20,22); }");
  expect_parse_ok("struct S { int x; } f(void){ struct S s; s.x=3; return s; } int main(){ return f().x; }");
  expect_parse_ok("union U { int x; } f(void){ union U u; return u; } int main(){ return 0; }");
  expect_parse_ok("typedef struct S S; struct S { int x; }; int main(){ S s; s.x=7; return s.x; }");
  expect_parse_ok("typedef struct { int x; } S; int main(){ S s; s.x=5; return s.x; }");
  expect_parse_ok("typedef union U U; union U { int x; }; int main(){ U u; u.x=8; return u.x; }");
  expect_parse_ok("typedef union { int x; } U; int main(){ U u; u.x=6; return u.x; }");
  expect_parse_ok("typedef int (*(*arr_t)[2])(int); int main() { arr_t p; return 0; }");
  expect_parse_ok("int main(){ typedef int (*(*fp_t))(int); return 0; }");
  expect_parse_ok("int main(){ typedef struct L L; return 0; }");
  expect_parse_ok("int main(){ typedef struct { int y; } L; L l; l.y=9; return l.y; }");
  expect_parse_ok("int main(){ typedef union L L; return 0; }");
  expect_parse_ok("int main(){ typedef union { int y; } L; L l; l.y=4; return l.y; }");
  expect_parse_ok("int main(){ typedef int A[]; A *p=0; return p==0; }");
  expect_parse_ok("int main(){ extern int (*fp)(int); return 0; }");
  expect_parse_ok("int main(){ extern int (*arr[2])(int); return 0; }");
  expect_parse_ok("int main(){ extern int a[]; return 0; }");
  expect_parse_ok("int (*arr[2])(int); int main(){ return 0; }");
  expect_parse_ok("int main(){ int (*arr[2])(int); return 0; }");

  // 複数の変数宣言（カンマ区切り）
  expect_parse_ok("main() { int a=1, b=2, c=3; return a+b+c; }");

  // 関数ポインタ宣言
  expect_parse_ok("int add(int a, int b) { return a+b; } int main() { int (*f)(int,int) = add; return f(20,22); }");
  expect_parse_ok("int inc(int x){return x+1;} int apply(int (**pp)(int), int x){ return (*pp)(x); } int main(){ int (*p)(int)=inc; int (**pp)(int)=&p; return apply(pp,41); }");
  expect_parse_ok("int main(){ int (*(*pp))(int); return 0; }");
  expect_parse_ok("main() { struct S { int (*fp)(int); }; return 0; }");
  expect_parse_ok("main() { struct S { int (*arr[2])(int); }; return 0; }");

  // enumの値パース
  expect_parse_ok("main() { enum Color { RED, GREEN, BLUE }; enum Color c = GREEN; return c; }");
  // 匿名enumの値指定は既知のバグ（enum初期化子パース未対応）で現在パースエラー
  // expect_parse_ok("main() { enum { A=10, B=20, C=30 }; return B; }");

  // 構造体の前方参照と自己参照ポインタ
  // 自己参照ポインタメンバは現在パースエラー（不完全型ポインタ未対応の可能性）
  // expect_parse_ok("main() { struct Node { int val; struct Node *next; }; struct Node n; n.val=42; n.next=0; return n.val; }");

  // void* の宣言と使用
  expect_parse_ok("main() { void *p = 0; return p == 0 ? 42 : 0; }");

  // const修飾
  expect_parse_ok("main() { const int x = 42; return x; }");
  expect_parse_ok("static const char *__const_leak_roots[]={\"\"}; typedef struct __ConstLeakFrame __ConstLeakFrame; struct __ConstLeakFrame{__ConstLeakFrame *next; const char *path;}; static __ConstLeakFrame *__const_leak_g; void f(void){ __ConstLeakFrame *p=0; __const_leak_g=p; }");
  expect_parse_ok("static const char *__const_ptr_tbl[4]; void f(const char *name){ __const_ptr_tbl[0]=name; }");
  expect_parse_ok("struct __ConstMemberPtr{const char *path;}; void f(struct __ConstMemberPtr *m,const char *path){ m->path=path; }");
  expect_parse_ok("struct __PtrSubSym814{char *name; int len;}; struct __PtrSubData814{struct __PtrSubSym814 *symbols;}; static struct __PtrSubData814 __ptr_sub_g814; int main(void){__ptr_sub_g814.symbols[0].name=\"main\";return __ptr_sub_g814.symbols[0].name[0];}");
  // 後置const (int const x) は変数宣言で現在パースエラー
  // expect_parse_ok("main() { int const x = 42; return x; }");

  // 配列の宣言と初期化
  expect_parse_ok("main() { int a[3] = {1, 2, 3}; return a[0] + a[1] + a[2]; }");

  // 構造体のネストした初期化
  expect_parse_ok("main() { struct P { int x; int y; }; struct R { struct P p; int z; }; struct R r = {{1,2},3}; return r.p.x + r.p.y + r.z; }");

  // for文のスコープ付き変数宣言
  expect_parse_ok("main() { int s=0; for (int i=0; i<5; i=i+1) s=s+i; return s; }");

  // 構造体のサイズオフ
  expect_parse_ok("main() { struct S { char a; int b; char c; }; return sizeof(struct S); }");

  // union の基本使用
  expect_parse_ok("main() { union U { int x; char c; }; union U u; u.x=42; return u.x; }");

  // _Static_assert 正常系
  // _Static_assert with sizeof==4 — 定数式評価で==未対応の可能性
  // expect_parse_ok("_Static_assert(sizeof(int)==4, \"int is 4 bytes\"); int main() { return 42; }");

  // _Generic の複雑なケース
  expect_parse_ok("main() { double d=1.0; return _Generic(d, int:0, double:42, default:99); }");

  // 複合リテラルの使用 — 現在パースエラーの可能性があるため個別検証
  //expect_parse_ok("main() { struct P { int x; int y; }; struct P p = (struct P){10, 32}; return p.x + p.y; }");

  // 意地悪テスト: 異常系の追加
  // 自己参照は不完全型エラーにならない（ポインタ非ポインタを問わずパース通過する可能性）
  // expect_parse_fail("main() { struct S { int x; struct S s; }; return 0; }");
  // 負のサイズは現在エラーにならない
  // expect_parse_fail("main() { int a[-1]; return 0; }");
}

static void test_parser_config_matrix() {
  printf("test_parser_config_matrix...\n");
  const char *struct_scalar_cast = "main() { struct S { int x; int y; }; return ((struct S)7).x; }";
  const char *struct_pointer_cast = "main() { struct S { int *p; int q; }; int x=3; return *((struct S)&x).p; }";
  const char *union_scalar_cast = "main() { union U { int x; char y; }; return ((union U)7).x; }";
  const char *union_pointer_cast = "main() { union U { int *p; int q; }; int x=3; return ((union U)&x).p==&x; }";
  const char *union_nonbrace_init = "main() { union U { int a[2]; int z; }; union U u={1,2}; return 0; }";
  const char *same_size_nonscalar_cast =
      "main() { struct A { int x; }; struct B { int x; }; struct A a={7}; return ((struct B)a).x; }";

  // baseline: all extensions enabled
  ps_set_enable_struct_scalar_pointer_cast(true);
  ps_set_enable_union_scalar_pointer_cast(true);
  ps_set_enable_union_array_member_nonbrace_init(true);
  ps_set_enable_size_compatible_nonscalar_cast(true);
  expect_parse_ok(struct_scalar_cast);
  expect_parse_ok(struct_pointer_cast);
  expect_parse_ok(union_scalar_cast);
  expect_parse_ok(union_pointer_cast);
  expect_parse_ok(union_nonbrace_init);
  expect_parse_ok(same_size_nonscalar_cast);

  // all extensions disabled: all extension snippets should fail
  ps_set_enable_struct_scalar_pointer_cast(false);
  ps_set_enable_union_scalar_pointer_cast(false);
  ps_set_enable_union_array_member_nonbrace_init(false);
  ps_set_enable_size_compatible_nonscalar_cast(false);
  expect_parse_fail(struct_scalar_cast);
  expect_parse_fail(struct_pointer_cast);
  expect_parse_fail(union_scalar_cast);
  expect_parse_fail(union_pointer_cast);
  expect_parse_fail(union_nonbrace_init);
  expect_parse_fail(same_size_nonscalar_cast);

  // restore defaults for subsequent tests
  ps_set_enable_struct_scalar_pointer_cast(true);
  ps_set_enable_union_scalar_pointer_cast(true);
  ps_set_enable_union_array_member_nonbrace_init(true);
  ps_set_enable_size_compatible_nonscalar_cast(true);
}

static char *build_nested_paren_program(int depth) {
  if (depth <= 0) return NULL;
  size_t cap = (size_t)depth * 2 + 64;
  char *buf = calloc(cap, 1);
  if (!buf) return NULL;
  size_t pos = 0;
  pos += (size_t)snprintf(buf + pos, cap - pos, "int main(){ return ");
  for (int i = 0; i < depth; i++) buf[pos++] = '(';
  buf[pos++] = '1';
  for (int i = 0; i < depth; i++) buf[pos++] = ')';
  pos += (size_t)snprintf(buf + pos, cap - pos, "; }\n");
  return buf;
}

static void test_expr_nest_limits() {
  printf("test_expr_nest_limits...\n");
  char *ok = build_nested_paren_program(256);
  ASSERT_TRUE(ok != NULL);
  expect_parse_ok(ok);
  free(ok);

  char *too_deep = build_nested_paren_program(1300);
  ASSERT_TRUE(too_deep != NULL);
  expect_parse_fail_with_message(too_deep, "深すぎます");
  free(too_deep);
}

static char *build_many_declarators_program(int ndecls) {
  if (ndecls <= 0) return NULL;
  size_t cap = (size_t)ndecls * 16 + 64;
  char *buf = calloc(cap, 1);
  if (!buf) return NULL;
  size_t pos = 0;
  pos += (size_t)snprintf(buf + pos, cap - pos, "int main(){ int ");
  for (int i = 0; i < ndecls; i++) {
    int n = snprintf(buf + pos, cap - pos, i == 0 ? "v%d" : ",v%d", i);
    if (n < 0 || (size_t)n >= cap - pos) {
      free(buf);
      return NULL;
    }
    pos += (size_t)n;
  }
  snprintf(buf + pos, cap - pos, "; return 0; }\n");
  return buf;
}

static char *build_many_array_init_elements_program(int nelems) {
  if (nelems <= 0) return NULL;
  size_t cap = (size_t)nelems * 4 + 128;
  char *buf = calloc(cap, 1);
  if (!buf) return NULL;
  size_t pos = 0;
  pos += (size_t)snprintf(buf + pos, cap - pos, "int main(){ int a[%d]={", nelems);
  for (int i = 0; i < nelems; i++) {
    int n = snprintf(buf + pos, cap - pos, i == 0 ? "1" : ",1");
    if (n < 0 || (size_t)n >= cap - pos) {
      free(buf);
      return NULL;
    }
    pos += (size_t)n;
  }
  snprintf(buf + pos, cap - pos, "}; return a[0]; }\n");
  return buf;
}

static void test_parser_width_limits() {
  printf("test_parser_width_limits...\n");

  char *ok_decls = build_many_declarators_program(64);
  ASSERT_TRUE(ok_decls != NULL);
  expect_parse_ok(ok_decls);
  free(ok_decls);

  char *too_many_decls = build_many_declarators_program(1300);
  ASSERT_TRUE(too_many_decls != NULL);
  expect_parse_fail_with_message(too_many_decls, "宣言子列が多すぎます");
  free(too_many_decls);

  char *ok_inits = build_many_array_init_elements_program(128);
  ASSERT_TRUE(ok_inits != NULL);
  expect_parse_ok(ok_inits);
  free(ok_inits);

  char *too_many_inits = build_many_array_init_elements_program(5000);
  ASSERT_TRUE(too_many_inits != NULL);
  expect_parse_fail_with_message(too_many_inits, "初期化子要素数が多すぎます");
  free(too_many_inits);
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
  test_expr_generic();
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
  test_expr_member_access();
  test_expr_string();
  test_expr_concat_string();
  test_expr_float();
  test_expr_long_double_suffix_metadata();
  test_expr_compound_literal();
  test_expr_compound_literal_array_subscript();
  test_type_decl();
  test_type_metadata_bridge();
  test_translation_unit_reset_static_local_state();
  test_translation_unit_reset_anonymous_tag_state();
  test_translation_unit_reset_decl_locals_state();
  test_translation_unit_reset_pragma_pack_state();
  test_multiple_funcdefs();
  test_parse_invalid();
  test_parse_invalid_diagnostics();
  test_parse_evil_edge_cases();
  test_parser_config_matrix();
  test_expr_nest_limits();
  test_parser_width_limits();

  printf("OK: All unit tests passed!\n");
  return 0;
}
