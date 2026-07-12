#include "../src/parser/parser.h"
#include "../src/declaration_pipeline.h"
#include "../src/parser/parser_public.h"
#include "../src/parser/decl.h"
#include "../src/parser/expr.h"
#include "../src/parser/global_registry.h"
#include "../src/parser/node_utils.h"
#include "../src/parser/config_runtime.h"
#include "../src/parser/semantic_ctx.h"
#include "../src/semantic/semantic_pass.h"
#include "../src/parser/aggregate_member_syntax.h"
#include "../src/semantic/declaration_application.h"
#include "../src/parser/struct_layout.h"
#include "../src/semantic/aggregate_member_resolution.h"
#include "../src/semantic/declaration_resolution.h"
#include "../src/semantic/enum_constant_resolution.h"
#include "../src/semantic/function_declaration_plan.h"
#include "../src/semantic/function_declaration_resolution.h"
#include "../src/semantic/initializer_resolution.h"
#include "../src/semantic/local_declaration_plan.h"
#include "../src/semantic/local_declaration_resolution.h"
#include "../src/semantic/local_type_state.h"
#include "../src/semantic/global_declaration_plan.h"
#include "../src/semantic/global_declaration_resolution.h"
#include "../src/semantic/parameter_declaration_plan.h"
#include "../src/semantic/parameter_declaration_resolution.h"
#include "../src/semantic/static_assert_resolution.h"
#include "../src/semantic/static_initializer_resolution.h"
#include "../src/semantic/tag_declaration_resolution.h"
#include "../src/semantic/typedef_declaration_resolution.h"
#include "../src/lowering/global_object_lowering.h"
#include "../src/lowering/local_object_lowering.h"
#include "../src/lowering/parameter_lowering.h"
#include "../src/lowering/vla_lowering.h"
#include "../src/lowering/static_data_initializer.h"
#include "../src/lowering/static_local_lowering.h"
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

static node_t **parsed_code;

static void find_long_double_float_literal(float_lit_t *lit, void *user) {
  bool *found = user;
  if (lit->float_suffix_kind == TK_FLOAT_SUFFIX_L) *found = true;
}

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
  ps_decl_set_current_funcname((char *)"__test__", 8);
  token_t *head = tk_tokenize((char *)input);
  node_t *expr = ps_expr_from(head);
  psx_semantic_analyze_expression(expr, head);
  return expr;
}

static node_t *parse_expr_input_with_existing_locals(const char *input) {
  ps_decl_set_current_funcname((char *)"__test__", 8);
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

static lvar_t *find_func_lvar(node_func_t *fn, const char *name) {
  int len = (int)strlen(name);
  for (lvar_t *v = fn ? fn->lvars : NULL; v; v = v->next_all) {
    if (v->len == len && strncmp(v->name, name, (size_t)len) == 0) return v;
  }
  return NULL;
}

typedef struct {
  global_var_t *gv;
  int scalar_count;
  long long scalar_offsets[16];
  long long scalar_values[16];
  int scalar_sizes[16];
  int padding_count;
  long long padding_offsets[16];
  int padding_sizes[16];
} aggregate_walk_trace_t;

static void aggregate_walk_trace_scalar(void *user, const tag_member_info_t *mi,
                                        int slot, long long offset) {
  aggregate_walk_trace_t *trace = user;
  if (!trace || trace->scalar_count >= 16) return;
  int idx = trace->scalar_count++;
  psx_gvar_init_member_value_t value =
      ps_gvar_init_member_value(trace->gv, slot, mi);
  trace->scalar_offsets[idx] = offset;
  trace->scalar_values[idx] = value.value;
  trace->scalar_sizes[idx] = value.size;
}

static void aggregate_walk_trace_padding(void *user, long long offset, int size) {
  aggregate_walk_trace_t *trace = user;
  if (!trace || trace->padding_count >= 16) return;
  int idx = trace->padding_count++;
  trace->padding_offsets[idx] = offset;
  trace->padding_sizes[idx] = size;
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
    ps_node_reject_const_assign(node, "=");
    _exit(0);
  }
  int status;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(0, WEXITSTATUS(status));
}

static void expect_const_assign_fail_for_node(node_t *node) {
  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    ps_node_reject_const_assign(node, "=");
    _exit(0);
  }
  int status;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_TRUE(WEXITSTATUS(status) != 0);
}

static void expect_const_qual_discard_fail_for_nodes(node_t *lhs, node_t *rhs) {
  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    ps_node_reject_const_qual_discard(lhs, rhs);
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
  if (!strstr(buf, needle)) {
    fprintf(stderr,
            "Expected diagnostic not found\ninput: %s\nexpected: %s\nactual: %s\n",
            input, needle, buf);
  }
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
  ps_iter_float_literals(find_long_double_float_literal, &found);
  ASSERT_TRUE(found);
}

static void test_expr_compound_literal() {
  printf("test_expr_compound_literal...\n");
    node_t *node = parse_expr_input("(int){3}");
  ASSERT_EQ(ND_COMMA, node->kind);
}

static void test_expr_compound_literal_array_subscript() {
  printf("test_expr_compound_literal_array_subscript...\n");
  node_t *array_literal = parse_expr_input("(int[3]){1,2,3}");
  ASSERT_EQ(ND_COMMA, array_literal->kind);
  ASSERT_EQ(12, ps_node_compound_literal_array_size(array_literal));
  ASSERT_EQ(ND_ADDR, array_literal->rhs->kind);
  ASSERT_EQ(12, ps_node_compound_literal_array_size(array_literal));

  // 配列型複合リテラルへの添字アクセス: ((int[2]){1,2})[1]
    node_t *node = parse_expr_input("((int[2]){1,2})[1]");
  // 外側 primary() の括弧グループ: ND_COMMA(init, ND_DEREF(...))
  ASSERT_EQ(ND_COMMA, node->kind);
  // rhs が添字アクセス結果 (ND_DEREF)
  ASSERT_EQ(ND_DEREF, node->rhs->kind);

  node_t *inferred = parse_expr_input("(int[]){1,2,3}");
  ASSERT_EQ(ND_COMMA, inferred->kind);
  ASSERT_EQ(12, ps_node_compound_literal_array_size(inferred));

  node_t *string_inferred = parse_expr_input("(char[]){\"abc\"}");
  ASSERT_EQ(ND_COMMA, string_inferred->kind);
  ASSERT_EQ(4, ps_node_compound_literal_array_size(string_inferred));

  node_t *pointer_inferred = parse_expr_input("(int *[]){0,0}");
  ASSERT_EQ(ND_COMMA, pointer_inferred->kind);
  ASSERT_EQ(16, ps_node_compound_literal_array_size(pointer_inferred));
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

static void test_additive_semantic_lowering_boundary() {
  printf("test_additive_semantic_lowering_boundary...\n");
  psx_decl_reset_locals();
  lvar_t *pointer = psx_decl_register_lvar_sized((char *)"p", 1, 8, 4, 0);
  psx_decl_set_lvar_decl_type(
      pointer,
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 4));

  node_t *node = parse_expr_input_with_existing_locals("p + 2");
  ASSERT_EQ(ND_ADD, node->kind);
  ASSERT_EQ(TK_PLUS, node->source_op);
  ASSERT_EQ(ND_NUM, node->rhs->kind);
  ASSERT_EQ(2, as_num(node->rhs)->val);

  psx_semantic_analyze_expression(node, NULL);
  ASSERT_EQ(ND_ADD, node->kind);
  ASSERT_EQ(TK_EOF, node->source_op);
  ASSERT_EQ(ND_MUL, node->rhs->kind);
  ASSERT_EQ(2, as_num(node->rhs->lhs)->val);
  ASSERT_EQ(4, as_num(node->rhs->rhs)->val);
}

static void test_cast_semantic_lowering_boundary() {
  printf("test_cast_semantic_lowering_boundary...\n");
  psx_decl_reset_locals();
  preregister_test_locals();
  node_t *node =
      parse_expr_input_with_existing_locals("(int)(unsigned long)a");
  ASSERT_EQ(ND_CAST, node->kind);
  ASSERT_TRUE(node->is_source_cast);
  ASSERT_EQ(ND_CAST, node->lhs->kind);
  ASSERT_TRUE(node->lhs->is_source_cast);

  psx_semantic_analyze_expression(node, NULL);
  ASSERT_EQ(ND_CAST, node->kind);
  ASSERT_TRUE(!node->is_source_cast);
  ASSERT_EQ(ND_SHR, node->lhs->kind);
  ASSERT_EQ(ND_SHL, node->lhs->lhs->kind);
}

static void test_aggregate_cast_semantic_lowering_boundary() {
  printf("test_aggregate_cast_semantic_lowering_boundary...\n");
  node_t **program = parse_program_input(
      "int main(void) { struct S { int x; int y; }; "
      "return ((struct S)7).x; }");
  ASSERT_TRUE(program != NULL);
  ASSERT_TRUE(program[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, program[0]->kind);

  node_func_t *fn = as_func(program[0]);
  lvar_t *temp = NULL;
  for (lvar_t *var = fn->lvars; var; var = var->next_all) {
    if (var->len >= 17 &&
        strncmp(var->name, "__aggregate_cast_", 17) == 0) {
      temp = var;
      break;
    }
  }
  ASSERT_TRUE(temp != NULL);

  psx_type_t *type = ps_lvar_get_decl_type(temp);
  ASSERT_TRUE(type != NULL);
  ASSERT_EQ(PSX_TYPE_STRUCT, type->kind);
  ASSERT_TRUE(type->aggregate_definition != NULL);
  ASSERT_EQ(2, type->aggregate_definition->member_count);
  ASSERT_EQ(1, type->aggregate_definition->members[0].len);
  ASSERT_TRUE(strncmp(type->aggregate_definition->members[0].name,
                      "x", 1) == 0);
}

static void test_implicit_conversion_semantic_lowering_boundary() {
  printf("test_implicit_conversion_semantic_lowering_boundary...\n");
  node_t **program = parse_program_input(
      "double id(double x) { return x; } "
      "double retconv(int x) { return x; } "
      "int main(void) { int x=3; double d=x; d=x; return (int)id(x); }");

  node_block_t *retconv_body = as_block(as_func(program[1])->base.rhs);
  ASSERT_EQ(ND_RETURN, retconv_body->body[0]->kind);
  ASSERT_EQ(ND_INT_TO_FP, retconv_body->body[0]->lhs->kind);

  node_block_t *main_body = as_block(as_func(program[2])->base.rhs);
  node_t *decl_init = main_body->body[1];
  ASSERT_EQ(ND_ASSIGN, decl_init->kind);
  ASSERT_TRUE(decl_init->is_decl_initializer);
  ASSERT_EQ(ND_INT_TO_FP, decl_init->rhs->kind);

  node_t *source_assign = main_body->body[2];
  ASSERT_EQ(ND_ASSIGN, source_assign->kind);
  ASSERT_TRUE(source_assign->is_source_assignment);
  ASSERT_EQ(ND_INT_TO_FP, source_assign->rhs->kind);

  node_t *ret = main_body->body[3];
  ASSERT_EQ(ND_RETURN, ret->kind);
  node_t *return_value = ret->lhs->kind == ND_CAST
                             ? ret->lhs->lhs : ret->lhs;
  ASSERT_EQ(ND_FP_TO_INT, return_value->kind);
  ASSERT_EQ(ND_FUNCALL, return_value->lhs->kind);
  node_func_t *call = as_func(return_value->lhs);
  ASSERT_EQ(1, call->nargs);
  ASSERT_EQ(ND_INT_TO_FP, call->args[0]->kind);
}

static void test_compound_assignment_semantic_lowering_boundary() {
  printf("test_compound_assignment_semantic_lowering_boundary...\n");
  psx_decl_reset_locals();
  lvar_t *value = psx_decl_register_lvar_sized((char *)"a", 1, 4, 4, 0);
  psx_decl_set_lvar_decl_type(
      value, ps_type_new_integer(TK_INT, 4, 0));
  node_t *node = parse_expr_input_with_existing_locals("a += 2");
  ASSERT_EQ(ND_ASSIGN, node->kind);
  ASSERT_TRUE(node->is_source_compound_assignment);
  ASSERT_EQ(TK_PLUSEQ, node->source_op);
  ASSERT_EQ(ND_NUM, node->rhs->kind);

  psx_semantic_analyze_expression(node, NULL);
  ASSERT_EQ(ND_ASSIGN, node->kind);
  ASSERT_TRUE(!node->is_source_compound_assignment);
  ASSERT_EQ(ND_ADD, node->rhs->kind);

  psx_decl_reset_locals();
  lvar_t *pointer = psx_decl_register_lvar_sized((char *)"p", 1, 8, 4, 0);
  psx_decl_set_lvar_decl_type(
      pointer,
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 4));
  node = parse_expr_input_with_existing_locals("*p += 2");
  ASSERT_EQ(ND_ASSIGN, node->kind);
  ASSERT_TRUE(node->is_source_compound_assignment);
  psx_semantic_analyze_expression(node, NULL);
  ASSERT_EQ(ND_COMMA, node->kind);
  ASSERT_EQ(ND_ASSIGN, node->lhs->kind);
  ASSERT_EQ(ND_ASSIGN, node->rhs->kind);
}

static void test_complex_initializer_semantic_lowering_boundary() {
  printf("test_complex_initializer_semantic_lowering_boundary...\n");
  psx_decl_reset_locals();
  lvar_t *value = psx_decl_register_lvar_sized((char *)"z", 1, 16, 16, 0);
  psx_type_t *complex_type = ps_type_new(PSX_TYPE_COMPLEX);
  complex_type->fp_kind = TK_FLOAT_KIND_DOUBLE;
  complex_type->size = 16;
  complex_type->align = 8;
  psx_decl_set_lvar_decl_type(value, complex_type);

  psx_initializer_entry_t *complex_entries =
      calloc(2, sizeof(*complex_entries));
  complex_entries[0].value = ps_node_new_num(3);
  complex_entries[1].value = ps_node_new_num(4);
  node_t *raw = ps_node_new_decl_initializer_list(
      ps_node_new_lvar_expr_ref_for(value, 0),
      PSX_DECL_INIT_LIST, complex_entries, 2, NULL);
  ASSERT_EQ(ND_DECL_INIT, raw->kind);

  psx_semantic_analyze_expression(raw, NULL);
  ASSERT_EQ(ND_COMMA, raw->kind);
  ASSERT_EQ(ND_ASSIGN, raw->lhs->kind);
  ASSERT_EQ(ND_ASSIGN, raw->rhs->kind);
  ASSERT_TRUE(raw->lhs->is_decl_initializer);
  ASSERT_TRUE(raw->rhs->is_decl_initializer);
  ASSERT_EQ(ND_INT_TO_FP, raw->lhs->rhs->kind);
  ASSERT_EQ(ND_INT_TO_FP, raw->rhs->rhs->kind);

  lvar_t *float_value = psx_decl_register_lvar_sized((char *)"f", 1, 8, 8, 0);
  psx_type_t *float_complex_type = ps_type_new(PSX_TYPE_COMPLEX);
  float_complex_type->fp_kind = TK_FLOAT_KIND_FLOAT;
  float_complex_type->size = 8;
  float_complex_type->align = 4;
  psx_decl_set_lvar_decl_type(float_value, float_complex_type);
  complex_entries = calloc(2, sizeof(*complex_entries));
  complex_entries[0].value = ps_node_new_num(1);
  complex_entries[1].value = ps_node_new_num(2);
  raw = ps_node_new_decl_initializer_list(
      ps_node_new_lvar_expr_ref_for(float_value, 0),
      PSX_DECL_INIT_LIST, complex_entries, 2, NULL);
  psx_semantic_analyze_expression(raw, NULL);
  ASSERT_EQ(4, ((node_lvar_t *)raw->rhs->lhs)->offset -
                   ((node_lvar_t *)raw->lhs->lhs)->offset);
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT, ps_node_value_fp_kind(raw->lhs->lhs));
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT, ps_node_value_fp_kind(raw->rhs->lhs));

  psx_decl_reset_locals();
  lvar_t *array = psx_decl_register_lvar_sized((char *)"a", 1, 12, 4, 1);
  psx_decl_set_lvar_decl_type(
      array,
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 3, 12, 4, 0));
  raw = ps_node_new_decl_initializer(
      ps_node_new_lvar_object_ref_for(array), ps_node_new_num(7),
      PSX_DECL_INIT_EXPR, NULL);
  ASSERT_EQ(ND_DECL_INIT, raw->kind);

  psx_semantic_analyze_expression(raw, NULL);
  ASSERT_EQ(ND_COMMA, raw->kind);
  ASSERT_EQ(ND_COMMA, raw->lhs->kind);
  ASSERT_EQ(ND_ASSIGN, raw->lhs->lhs->kind);
  ASSERT_EQ(7, as_num(raw->lhs->lhs->rhs)->val);
  ASSERT_EQ(ND_ASSIGN, raw->lhs->rhs->kind);
  ASSERT_EQ(0, as_num(raw->lhs->rhs->rhs)->val);
  ASSERT_EQ(ND_ASSIGN, raw->rhs->kind);
  ASSERT_EQ(0, as_num(raw->rhs->rhs)->val);

  psx_initializer_entry_t *entries = calloc(2, sizeof(*entries));
  entries[0].value = ps_node_new_num(8);
  entries[0].index_exprs[0] = ps_node_new_num(1);
  entries[0].index_expr_count = 1;
  entries[0].has_index = 1;
  entries[1].value = ps_node_new_num(9);
  raw = ps_node_new_decl_initializer_list(
      ps_node_new_lvar_object_ref_for(array), PSX_DECL_INIT_LIST,
      entries, 2, NULL);
  ASSERT_EQ(ND_DECL_INIT, raw->kind);
  ASSERT_EQ(ND_INIT_LIST, raw->rhs->kind);
  ASSERT_EQ(2, ((node_init_list_t *)raw->rhs)->entry_count);

  psx_semantic_analyze_expression(raw, NULL);
  ASSERT_EQ(ND_COMMA, raw->kind);
  ASSERT_EQ(ND_COMMA, raw->lhs->kind);
  ASSERT_EQ(8, as_num(raw->lhs->lhs->rhs)->val);
  ASSERT_EQ(ps_lvar_offset(array) + 4,
            ((node_lvar_t *)raw->lhs->lhs->lhs)->offset);
  ASSERT_EQ(9, as_num(raw->lhs->rhs->rhs)->val);
  ASSERT_EQ(ps_lvar_offset(array) + 8,
            ((node_lvar_t *)raw->lhs->rhs->lhs)->offset);
  ASSERT_EQ(0, as_num(raw->rhs->rhs)->val);
  ASSERT_EQ(ps_lvar_offset(array), ((node_lvar_t *)raw->rhs->lhs)->offset);
}

static void test_local_declaration_storage_plan_boundary() {
  printf("test_local_declaration_storage_plan_boundary...\n");
  psx_type_t *integer = ps_type_new_integer(TK_INT, 4, 0);
  psx_type_t *row = ps_type_new_array(integer, 3, 12, 4, 0);
  psx_type_t *matrix = ps_type_new_array(row, 2, 24, 12, 0);
  psx_complete_array_storage_plan_t plan = {0};
  ASSERT_TRUE(psx_plan_complete_array_storage(matrix, &plan));
  ASSERT_EQ(24, plan.storage_size);
  ASSERT_EQ(4, plan.scalar_element_size);
  ASSERT_EQ(4, plan.alignment);

  psx_type_t *pointer = ps_type_new_pointer(integer, 4);
  psx_type_t *pointers = ps_type_new_array(pointer, 3, 24, 8, 0);
  ASSERT_TRUE(psx_plan_complete_array_storage(pointers, &plan));
  ASSERT_EQ(24, plan.storage_size);
  ASSERT_EQ(8, plan.scalar_element_size);

  psx_type_t *incomplete = ps_type_new_array(integer, 0, 0, 4, 0);
  ASSERT_TRUE(!psx_plan_complete_array_storage(incomplete, &plan));
  ASSERT_TRUE(psx_resolve_incomplete_array_type(
      incomplete,
      &(psx_incomplete_array_resolution_t){
          .initializer_count = 5,
      }));
  ASSERT_EQ(5, incomplete->array_len);
  ASSERT_EQ(20, ps_type_sizeof(incomplete));
  ASSERT_TRUE(psx_plan_complete_array_storage(incomplete, &plan));

  psx_type_t *partial_flat_matrix = ps_type_new_array(row, 0, 0, 12, 0);
  ASSERT_TRUE(psx_resolve_incomplete_array_type(
      partial_flat_matrix,
      &(psx_incomplete_array_resolution_t){
          .initializer_count = 5,
          .entries_initialize_outer_elements = 0,
      }));
  ASSERT_EQ(2, partial_flat_matrix->array_len);
  ASSERT_EQ(24, ps_type_sizeof(partial_flat_matrix));

  psx_type_t *nested_matrix = ps_type_new_array(row, 0, 0, 12, 0);
  ASSERT_TRUE(psx_resolve_incomplete_array_type(
      nested_matrix,
      &(psx_incomplete_array_resolution_t){
          .initializer_count = 2,
          .entries_initialize_outer_elements = 1,
      }));
  ASSERT_EQ(2, nested_matrix->array_len);
  ASSERT_EQ(24, ps_type_sizeof(nested_matrix));
  psx_type_t *vla = ps_type_new_array(integer, 0, 0, 4, 1);
  ASSERT_TRUE(!psx_plan_complete_array_storage(vla, &plan));

  psx_complete_object_storage_plan_t object_plan = {0};
  ASSERT_TRUE(psx_plan_complete_object_storage(pointer, &object_plan));
  ASSERT_EQ(8, object_plan.storage_size);
  ASSERT_EQ(4, object_plan.element_size);
  ASSERT_EQ(8, object_plan.alignment);
  ASSERT_TRUE(psx_plan_complete_object_storage(integer, &object_plan));
  ASSERT_EQ(4, object_plan.storage_size);
  ASSERT_EQ(4, object_plan.element_size);
  ASSERT_TRUE(!psx_plan_complete_object_storage(vla, &object_plan));

  psx_decl_reset_locals();
  psx_local_object_result_t lowered = {0};
  ASSERT_TRUE(lower_complete_local_object(
      &(psx_local_object_request_t){
          .name = (char *)"matrix",
          .name_len = 6,
          .type = matrix,
          .requested_alignment = 32,
      },
      &lowered));
  ASSERT_TRUE(lowered.var != NULL);
  ASSERT_EQ(24, lowered.storage_size);
  ASSERT_EQ(4, lowered.element_size);
  ASSERT_EQ(32, lowered.alignment);
  ASSERT_EQ(1, lowered.is_array);
  ASSERT_EQ(0, ps_lvar_offset(lowered.var) % 32);
  psx_type_t *stored_type = ps_lvar_get_decl_type(lowered.var);
  ASSERT_TRUE(stored_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, stored_type->kind);
  ASSERT_EQ(24, ps_type_sizeof(stored_type));
  ASSERT_EQ(2, stored_type->array_len);
  ASSERT_TRUE(stored_type->base != NULL);
  ASSERT_EQ(3, stored_type->base->array_len);
  ASSERT_EQ(lowered.var, ps_decl_find_lvar((char *)"matrix", 6));

  psx_decl_reset_locals();
  psx_type_t *deferred_type =
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 0, 0, 4, 0);
  psx_local_object_result_t declared = {0};
  ASSERT_TRUE(declare_incomplete_local_object(
      &(psx_local_object_request_t){
          .name = (char *)"deferred",
          .name_len = 8,
          .type = deferred_type,
      },
      &declared));
  ASSERT_TRUE(declared.var != NULL);
  ASSERT_EQ(0, declared.var->size);
  ASSERT_EQ(declared.var,
            ps_decl_find_lvar((char *)"deferred", 8));
  ASSERT_TRUE(psx_resolve_incomplete_array_type(
      deferred_type,
      &(psx_incomplete_array_resolution_t){
          .initializer_count = 3,
      }));
  psx_local_object_result_t completed = {0};
  ASSERT_TRUE(complete_declared_local_object(
      declared.var,
      &(psx_local_object_request_t){
          .name = (char *)"deferred",
          .name_len = 8,
          .type = deferred_type,
      },
      &completed));
  ASSERT_EQ(declared.var, completed.var);
  ASSERT_EQ(12, completed.storage_size);
  ASSERT_EQ(12, completed.var->size);
  ASSERT_EQ(3, ps_lvar_get_decl_type(completed.var)->array_len);

  expect_parse_ok(
      "int main(void){ int *a[]={(int *)a}; return sizeof(a)==8; }");

}

static void test_vla_lowering_request_boundary() {
  printf("test_vla_lowering_request_boundary...\n");
  psx_decl_reset_locals();
  psx_type_t *integer = ps_type_new_integer(TK_INT, 4, 0);
  psx_type_t *vla_type = ps_type_new_array(integer, 0, 0, 4, 1);
  psx_vla_lowering_request_t request = {0};
  request.name = (char *)"v";
  request.name_len = 1;
  request.element_size = 4;
  request.dimensions[0] = ps_node_new_num(3);
  request.dimension_count = 1;
  request.type = vla_type;
  request.requested_alignment = 16;
  psx_vla_lowering_result_t result = lower_vla_declaration(&request);
  ASSERT_TRUE(result.var != NULL);
  ASSERT_EQ(16, ps_lvar_storage_size(result.var, 0));
  ASSERT_EQ(ND_VLA_ALLOC, result.init->kind);
  ASSERT_EQ(4, ps_lvar_array_scalar_element_size(result.var));
  ASSERT_TRUE(result.type_attached);
  ASSERT_EQ(0, ps_lvar_offset(result.var) % 16);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_lvar_get_decl_type(result.var)->kind);
  ASSERT_TRUE(ps_lvar_get_decl_type(result.var)->is_vla);

  psx_decl_reset_locals();
  request.name = (char *)"m";
  request.dimensions[0] = ps_node_new_num(2);
  request.dimensions[1] = ps_node_new_num(3);
  request.dimensions[2] = ps_node_new_num(4);
  request.dimension_count = 3;
  request.type = NULL;
  request.requested_alignment = 0;
  result = lower_vla_declaration(&request);
  ASSERT_EQ(32, ps_lvar_storage_size(result.var, 0));
  ASSERT_EQ(ND_COMMA, result.init->kind);
  ASSERT_EQ(ND_VLA_ALLOC, result.init->lhs->kind);
  ASSERT_EQ(ND_ASSIGN, result.init->rhs->kind);
  ASSERT_TRUE(ps_lvar_vla_row_stride_frame_off(result.var) > 0);

  psx_decl_reset_locals();
  node_t *row_dimension = ps_node_new_num(5);
  psx_type_t *row_type = ps_type_new_array(integer, 0, 0, 4, 1);
  psx_type_t *pointer_type = ps_type_new_pointer(row_type, 0);
  result = lower_pointer_to_vla_declaration(
      &(psx_pointer_vla_lowering_request_t){
          .name = (char *)"p",
          .name_len = 1,
          .element_size = 4,
          .row_dimension = row_dimension,
          .type = pointer_type,
          .requested_alignment = 32,
      });
  ASSERT_TRUE(result.var != NULL);
  ASSERT_TRUE(result.type_attached);
  ASSERT_EQ(16, ps_lvar_storage_size(result.var, 0));
  ASSERT_EQ(8, ps_lvar_decl_sizeof(result.var, 0));
  ASSERT_EQ(0, ps_lvar_offset(result.var) % 32);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_lvar_get_decl_type(result.var)->kind);
  ASSERT_EQ(ps_lvar_offset(result.var) + 8,
            ps_lvar_vla_row_stride_frame_off(result.var));
  ASSERT_EQ(ND_ASSIGN, result.init->kind);
  ASSERT_EQ(ps_lvar_vla_row_stride_frame_off(result.var),
            as_lvar(result.init->lhs)->offset);
  ASSERT_EQ(ND_MUL, result.init->rhs->kind);
  ASSERT_EQ(row_dimension, result.init->rhs->lhs);
  ASSERT_EQ(4, as_num(result.init->rhs->rhs)->val);

  psx_decl_reset_locals();
  lvar_t *n = psx_decl_register_lvar_sized(
      (char *)"n", 1, 4, 4, 0);
  lvar_t *k = psx_decl_register_lvar_sized(
      (char *)"k", 1, 4, 4, 0);
  n->is_param = 1;
  k->is_param = 1;
  psx_type_t *parameter_type = ps_type_new_pointer(
      ps_type_new_array(integer, 0, 0, 4, 1), 0);
  psx_parameter_vla_lowering_request_t parameter_request = {
      .name = (char *)"tensor",
      .name_len = 6,
      .element_size = 4,
      .inner_dimension_count = 3,
      .type = parameter_type,
  };
  parameter_request.inner_dimensions[0].source_name = (char *)"n";
  parameter_request.inner_dimensions[0].source_name_len = 1;
  parameter_request.inner_dimensions[1].constant = 3;
  parameter_request.inner_dimensions[2].source_name = (char *)"k";
  parameter_request.inner_dimensions[2].source_name_len = 1;
  psx_parameter_vla_lowering_result_t parameter_result =
      lower_parameter_vla_declaration(&parameter_request);
  ASSERT_TRUE(parameter_result.var != NULL);
  ASSERT_TRUE(parameter_result.stride_storage != NULL);
  ASSERT_TRUE(parameter_result.type_attached);
  ASSERT_TRUE(parameter_result.var->is_param);
  ASSERT_EQ(8, ps_lvar_storage_size(parameter_result.var, 0));
  ASSERT_EQ(24, ps_lvar_storage_size(
                    parameter_result.stride_storage, 0));
  ASSERT_EQ(parameter_result.stride_storage,
            ps_decl_find_lvar((char *)"__rs_tensor", 11));
  ASSERT_EQ(parameter_result.stride_storage->offset,
            ps_lvar_vla_row_stride_frame_off(parameter_result.var));
  ASSERT_EQ(3, ps_lvar_vla_param_inner_dim_count(
                   parameter_result.var));
  ASSERT_EQ(n->offset, ps_lvar_vla_param_inner_dim_src_offset(
                           parameter_result.var, 0));
  ASSERT_EQ(3, ps_lvar_vla_param_inner_dim_const(
                   parameter_result.var, 1));
  ASSERT_EQ(k->offset, ps_lvar_vla_param_inner_dim_src_offset(
                           parameter_result.var, 2));
  ASSERT_EQ(PSX_TYPE_POINTER,
            ps_lvar_get_decl_type(parameter_result.var)->kind);
}

static void test_parameter_declaration_storage_plan_boundary() {
  printf("test_parameter_declaration_storage_plan_boundary...\n");
  psx_type_t *integer = ps_type_new_integer(TK_INT, 4, 0);
  psx_parameter_storage_plan_t plan = {0};
  ASSERT_TRUE(psx_plan_parameter_storage(integer, &plan));
  ASSERT_EQ(PSX_PARAMETER_STORAGE_SCALAR, plan.kind);
  ASSERT_EQ(4, plan.storage_size);
  ASSERT_EQ(4, plan.element_size);

  psx_type_t *pointer = ps_type_new_pointer(integer, 4);
  ASSERT_TRUE(psx_plan_parameter_storage(pointer, &plan));
  ASSERT_EQ(PSX_PARAMETER_STORAGE_POINTER, plan.kind);
  ASSERT_EQ(8, plan.storage_size);
  ASSERT_EQ(4, plan.element_size);

  psx_type_t *small_aggregate = ps_type_new_tag(
      TK_STRUCT, (char *)"SmallParam", 10, 0, 12);
  ASSERT_TRUE(psx_plan_parameter_storage(small_aggregate, &plan));
  ASSERT_EQ(PSX_PARAMETER_STORAGE_AGGREGATE_VALUE, plan.kind);
  ASSERT_EQ(12, plan.storage_size);
  ASSERT_EQ(12, plan.element_size);
  ASSERT_EQ(8, plan.alignment);
  ASSERT_TRUE(!plan.is_byref);

  psx_type_t *large_aggregate = ps_type_new_tag(
      TK_STRUCT, (char *)"LargeParam", 10, 0, 24);
  ASSERT_TRUE(psx_plan_parameter_storage(large_aggregate, &plan));
  ASSERT_EQ(PSX_PARAMETER_STORAGE_AGGREGATE_BYREF, plan.kind);
  ASSERT_EQ(8, plan.storage_size);
  ASSERT_EQ(24, plan.element_size);
  ASSERT_TRUE(plan.is_byref);

  psx_type_t *complex = ps_type_new(PSX_TYPE_COMPLEX);
  complex->size = 16;
  complex->align = 8;
  complex->fp_kind = TK_FLOAT_KIND_DOUBLE;
  ASSERT_TRUE(psx_plan_parameter_storage(complex, &plan));
  ASSERT_EQ(PSX_PARAMETER_STORAGE_COMPLEX, plan.kind);
  ASSERT_EQ(16, plan.storage_size);
  ASSERT_EQ(16, plan.element_size);
  ASSERT_EQ(8, plan.alignment);

  psx_decl_reset_locals();
  psx_parameter_lowering_result_t lowered = {0};
  ASSERT_TRUE(lower_parameter_declaration(
      &(psx_parameter_lowering_request_t){
          .name = (char *)"value",
          .name_len = 5,
          .type = large_aggregate,
      },
      &lowered));
  ASSERT_TRUE(lowered.var != NULL);
  ASSERT_TRUE(lowered.type_attached);
  ASSERT_TRUE(lowered.var->is_param);
  ASSERT_TRUE(lowered.var->is_byref_param);
  ASSERT_EQ(8, lowered.var->size);
  ASSERT_EQ(24, lowered.var->elem_size);
  ASSERT_EQ(PSX_TYPE_STRUCT,
            ps_lvar_get_decl_type(lowered.var)->kind);

  psx_declarator_shape_t vla_parameter_shape;
  ps_declarator_shape_init(&vla_parameter_shape);
  ASSERT_TRUE(ps_declarator_shape_append_vla_array(
      &vla_parameter_shape));
  psx_parameter_declaration_resolution_request_t parameter_request = {
      .type = {
          .base_kind = TK_INT,
          .elem_size = 4,
          .declarator_shape = &vla_parameter_shape,
      },
      .is_array_declarator = 1,
      .inner_dimension_count = 1,
  };
  parameter_request.inner_dimensions[0].source_name = (char *)"n";
  parameter_request.inner_dimensions[0].source_name_len = 1;
  psx_parameter_declaration_resolution_t parameter_resolution;
  ASSERT_TRUE(psx_resolve_parameter_declaration(
      &parameter_request, &parameter_resolution));
  ASSERT_EQ(PSX_PARAMETER_LOWER_VLA,
            parameter_resolution.lowering_kind);
  ASSERT_EQ(PSX_PARAMETER_STORAGE_POINTER,
            parameter_resolution.storage.kind);
  ASSERT_EQ(4, parameter_resolution.element_size);
  ASSERT_EQ(PSX_TYPE_POINTER, parameter_resolution.type->kind);

  psx_decl_reset_locals();
  lvar_t *dimension = psx_decl_register_lvar_sized(
      (char *)"n", 1, 4, 4, 0);
  dimension->is_param = 1;
  psx_parameter_lowering_result_t resolved_lowered;
  ASSERT_TRUE(lower_resolved_parameter_declaration(
      &(psx_resolved_parameter_lowering_request_t){
          .name = (char *)"values",
          .name_len = 6,
          .resolution = &parameter_resolution,
      },
      &resolved_lowered));
  ASSERT_TRUE(resolved_lowered.var != NULL);
  ASSERT_TRUE(resolved_lowered.type_attached);
  ASSERT_TRUE(resolved_lowered.var->is_param);
  ASSERT_EQ(PSX_TYPE_POINTER,
            ps_lvar_get_decl_type(resolved_lowered.var)->kind);
  ASSERT_EQ(dimension->offset,
            ps_lvar_vla_row_stride_src_offset(resolved_lowered.var));

  psx_type_t *parameter_types[2] = {integer, pointer};
  psx_function_declaration_plan_t function_plan = {0};
  ASSERT_TRUE(psx_plan_function_declaration(
      &(psx_function_declaration_request_t){
          .return_type = pointer,
          .parameter_types = parameter_types,
          .parameter_count = 2,
          .is_variadic = 1,
      },
      &function_plan));
  ASSERT_TRUE(function_plan.function_type != NULL);
  ASSERT_EQ(PSX_TYPE_FUNCTION, function_plan.function_type->kind);
  ASSERT_EQ(PSX_TYPE_POINTER,
            function_plan.function_type->base->kind);
  ASSERT_EQ(2, function_plan.function_type->param_count);
  ASSERT_TRUE(function_plan.function_type->is_variadic_function);
  ASSERT_TRUE(ps_type_shape_matches(
      function_plan.function_type->param_types[0], integer));
  ASSERT_TRUE(ps_type_shape_matches(
      function_plan.function_type->param_types[1], pointer));

  psx_declarator_shape_t returned_funcptr_shape;
  psx_declarator_shape_t returned_value_shape;
  ps_declarator_shape_init(&returned_funcptr_shape);
  ps_declarator_shape_init(&returned_value_shape);
  ASSERT_TRUE(ps_declarator_shape_append_pointer(
      &returned_funcptr_shape, 0, 0));
  psx_decl_funcptr_sig_t returned_callable = {0};
  returned_callable.function.callable.signature.nargs_fixed = 1;
  returned_callable.function.callable.signature.param_int_mask = 1;
  ASSERT_TRUE(ps_declarator_shape_append_function(
      &returned_funcptr_shape, returned_callable));
  ASSERT_TRUE(ps_declarator_shape_append_pointer(
      &returned_value_shape, 0, 0));
  ASSERT_TRUE(ps_declarator_shape_append_shape(
      &returned_funcptr_shape, &returned_value_shape));
  ASSERT_EQ(2, ps_declarator_shape_count_ops(
                   &returned_funcptr_shape, PSX_DECL_OP_POINTER));
  ASSERT_EQ(1, ps_declarator_shape_count_ops(
                   &returned_funcptr_shape, PSX_DECL_OP_FUNCTION));
  ASSERT_EQ(0, ps_declarator_shape_count_ops(
                   &returned_funcptr_shape, PSX_DECL_OP_ARRAY));
  psx_type_t *returned_funcptr = ps_type_apply_declarator_shape(
      ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8),
      &returned_funcptr_shape);
  psx_function_declaration_plan_t funcptr_plan = {0};
  ASSERT_TRUE(psx_plan_function_declaration(
      &(psx_function_declaration_request_t){
          .return_type = returned_funcptr,
      },
      &funcptr_plan));
  ASSERT_TRUE(funcptr_plan.returns_function_pointer);
  ASSERT_EQ(1, funcptr_plan.returned_funcptr_signature.function.callable
                   .signature.nargs_fixed);
  ASSERT_EQ(1u, funcptr_plan.returned_funcptr_signature.function.callable
                    .signature.param_int_mask);
  ASSERT_TRUE(funcptr_plan.returned_funcptr_signature.function.callable
                  .return_shape.is_data_pointer);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            funcptr_plan.returned_funcptr_signature.function.callable
                .return_shape.pointee_fp_kind);

  ps_reset_translation_unit_state();
  integer = ps_type_new_integer(TK_INT, 4, 0);
  pointer = ps_type_new_pointer(integer, 4);
  parameter_types[0] = integer;
  parameter_types[1] = pointer;
  psx_function_declaration_resolution_request_t resolution_request = {
      .name = (char *)"__resolution_fn",
      .name_len = 15,
      .return_type = integer,
      .parameter_types = parameter_types,
      .parameter_count = 2,
      .is_variadic = 1,
  };
  psx_function_declaration_resolution_t resolution = {0};
  psx_resolve_function_declaration(&resolution_request, &resolution);
  ASSERT_EQ(PSX_FUNCTION_DECLARATION_OK, resolution.status);
  ASSERT_TRUE(ps_ctx_has_function_name("__resolution_fn", 15));
  ASSERT_TRUE(ps_ctx_get_function_type("__resolution_fn", 15) != NULL);

  psx_resolve_function_declaration(&resolution_request, &resolution);
  ASSERT_EQ(PSX_FUNCTION_DECLARATION_OK, resolution.status);
  ASSERT_EQ(0, ps_ctx_is_function_defined("__resolution_fn", 15));
  resolution_request.return_type = pointer;
  psx_resolve_function_declaration(&resolution_request, &resolution);
  ASSERT_EQ(PSX_FUNCTION_DECLARATION_TYPE_CONFLICT, resolution.status);

  resolution_request.return_type = integer;
  resolution_request.is_definition = 1;
  psx_resolve_function_declaration(&resolution_request, &resolution);
  ASSERT_EQ(PSX_FUNCTION_DECLARATION_OK, resolution.status);
  ASSERT_TRUE(ps_ctx_is_function_defined("__resolution_fn", 15));
  psx_resolve_function_declaration(&resolution_request, &resolution);
  ASSERT_EQ(PSX_FUNCTION_DECLARATION_DUPLICATE_DEFINITION,
            resolution.status);

  parsed_code = parse_program_input("int __resolution_object;");
  ASSERT_TRUE(ps_find_global_var("__resolution_object", 19) != NULL);
  resolution_request.name = (char *)"__resolution_object";
  resolution_request.name_len = 19;
  resolution_request.return_type = integer;
  psx_resolve_function_declaration(&resolution_request, &resolution);
  ASSERT_EQ(PSX_FUNCTION_DECLARATION_OBJECT_NAME_CONFLICT,
            resolution.status);
}

static void test_global_declaration_storage_plan_boundary() {
  printf("test_global_declaration_storage_plan_boundary...\n");
  ps_global_registry_reset_translation_unit();
  psx_type_t *integer = ps_type_new_integer(TK_INT, 4, 0);
  psx_type_t *incomplete = ps_type_new_array(
      integer, 0, 0, 4, 0);
  psx_global_storage_plan_t plan = {0};
  ASSERT_TRUE(psx_plan_global_object_storage(incomplete, &plan));
  ASSERT_TRUE(plan.is_incomplete_array);
  ASSERT_EQ(0, plan.storage_size);

  psx_global_declaration_resolution_t first_resolution;
  psx_resolve_global_declaration(
      &(psx_global_declaration_resolution_request_t){
          .name = (char *)"__boundary_global",
          .name_len = 17,
          .type = incomplete,
          .is_extern_decl = 1,
      },
      &first_resolution);
  ASSERT_EQ(PSX_GLOBAL_DECLARATION_OK, first_resolution.status);
  ASSERT_TRUE(first_resolution.existing == NULL);
  psx_global_object_result_t first = {0};
  ASSERT_TRUE(lower_resolved_global_object_declaration(
      &(psx_resolved_global_object_request_t){
          .name = (char *)"__boundary_global",
          .name_len = 17,
          .type = incomplete,
          .is_extern_decl = 1,
          .resolution = &first_resolution,
      },
      &first));
  ASSERT_TRUE(first.global != NULL);
  ASSERT_TRUE(first.created);
  ASSERT_TRUE(first.global->is_extern_decl);
  ASSERT_EQ(0, first.global->type_size);

  psx_type_t *complete = ps_type_new_array(
      integer, 3, 12, 4, 0);
  psx_global_declaration_resolution_t merged_resolution;
  psx_resolve_global_declaration(
      &(psx_global_declaration_resolution_request_t){
          .name = (char *)"__boundary_global",
          .name_len = 17,
          .type = complete,
      },
      &merged_resolution);
  ASSERT_EQ(PSX_GLOBAL_DECLARATION_OK, merged_resolution.status);
  ASSERT_EQ(first.global, merged_resolution.existing);
  ASSERT_TRUE(merged_resolution.replace_existing_type);
  ASSERT_TRUE(merged_resolution.clear_existing_extern);
  psx_global_object_result_t merged = {0};
  ASSERT_TRUE(lower_resolved_global_object_declaration(
      &(psx_resolved_global_object_request_t){
          .name = (char *)"__boundary_global",
          .name_len = 17,
          .type = complete,
          .resolution = &merged_resolution,
      },
      &merged));
  ASSERT_EQ(first.global, merged.global);
  ASSERT_TRUE(!merged.created);
  ASSERT_TRUE(!merged.global->is_extern_decl);
  ASSERT_EQ(12, merged.global->type_size);
  ASSERT_EQ(3, ps_gvar_get_decl_type(merged.global)->array_len);

  psx_global_declaration_resolution_t rejected_resolution;
  psx_resolve_global_declaration(
      &(psx_global_declaration_resolution_request_t){
          .name = (char *)"__boundary_incomplete",
          .name_len = 21,
          .type = incomplete,
      },
      &rejected_resolution);
  ASSERT_EQ(PSX_GLOBAL_DECLARATION_INCOMPLETE_OBJECT,
            rejected_resolution.status);
  psx_resolve_global_declaration(
      &(psx_global_declaration_resolution_request_t){
          .name = (char *)"__boundary_incomplete",
          .name_len = 21,
          .type = incomplete,
          .has_initializer = 1,
      },
      &rejected_resolution);
  ASSERT_EQ(PSX_GLOBAL_DECLARATION_OK, rejected_resolution.status);

  psx_type_t *pointer = ps_type_new_pointer(integer, 4);
  psx_resolve_global_declaration(
      &(psx_global_declaration_resolution_request_t){
          .name = (char *)"__boundary_global",
          .name_len = 17,
          .type = pointer,
      },
      &rejected_resolution);
  ASSERT_EQ(PSX_GLOBAL_DECLARATION_TYPE_CONFLICT,
            rejected_resolution.status);
  ASSERT_TRUE(psx_plan_global_object_storage(pointer, &plan));
  ASSERT_EQ(8, plan.storage_size);
  psx_global_object_result_t internal = {0};
  ASSERT_TRUE(lower_global_object_declaration(
      &(psx_global_object_request_t){
          .name = (char *)"__boundary_static",
          .name_len = 17,
          .type = pointer,
          .is_static = 1,
      },
      &internal));
  ASSERT_TRUE(internal.global->is_static);
  ASSERT_EQ(8, internal.global->type_size);
}

typedef struct {
  char *name;
  int name_len;
  int saw_registered_symbol;
} pipeline_initializer_test_context_t;

static void parse_pipeline_test_initializer(
    void *context, psx_type_t *type,
    psx_parsed_initializer_t *initializer) {
  (void)type;
  pipeline_initializer_test_context_t *test = context;
  test->saw_registered_symbol =
      ps_find_global_var(test->name, test->name_len) != NULL;
  token_t *assign_tok = tk_get_current_token();
  ASSERT_EQ(TK_ASSIGN, assign_tok->kind);
  tk_set_current_token(assign_tok->next);
  ps_parse_initializer_syntax_value(initializer, assign_tok);
}

static void test_declaration_pipeline_order_boundary() {
  printf("test_declaration_pipeline_order_boundary...\n");
  ps_reset_translation_unit_state();
  token_t *tokens = tk_tokenize((char *)"= 37");
  tk_set_current_token(tokens);
  psx_parsed_initializer_t initializer;
  ps_prepare_optional_initializer_syntax(&initializer);
  pipeline_initializer_test_context_t context = {
      .name = (char *)"__pipeline_object",
      .name_len = 17,
  };
  psx_global_declaration_pipeline_result_t result;
  ASSERT_TRUE(psx_apply_global_declaration_pipeline(
      &(psx_global_declaration_pipeline_request_t){
          .name = context.name,
          .name_len = context.name_len,
          .type = ps_type_new_integer(TK_INT, 4, 0),
          .initializer = &initializer,
          .parse_initializer = parse_pipeline_test_initializer,
          .parse_context = &context,
          .diag_tok = tokens,
      },
      &result));
  ASSERT_TRUE(context.saw_registered_symbol);
  ASSERT_TRUE(result.global != NULL);
  ASSERT_TRUE(result.initialized);
  ASSERT_EQ(37, result.global->init_val);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);
}

static void test_tag_declaration_resolution_boundary() {
  printf("test_tag_declaration_resolution_boundary...\n");
  ps_reset_translation_unit_state();
  psx_tag_declaration_resolution_request_t request = {
      .kind = TK_STRUCT,
      .name = (char *)"__TagBoundary",
      .name_len = 13,
      .mode = PSX_TAG_DECLARATION_FORWARD,
  };
  psx_tag_declaration_resolution_t resolution;
  psx_resolve_tag_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_OK, resolution.status);
  ASSERT_TRUE(resolution.registered);
  ASSERT_EQ(0, resolution.scope_depth);

  request.mode = PSX_TAG_DECLARATION_REFERENCE;
  psx_resolve_tag_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_OK, resolution.status);
  ASSERT_TRUE(!resolution.registered);
  psx_aggregate_definition_t *cached_definition =
      ps_ctx_get_tag_definition(
          TK_STRUCT, (char *)"__TagBoundary", 13);
  ASSERT_TRUE(cached_definition != NULL);
  ASSERT_EQ(0, cached_definition->align);

  request.mode = PSX_TAG_DECLARATION_DEFINITION;
  request.alignment = 1;
  psx_resolve_tag_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_OK, resolution.status);
  ASSERT_TRUE(resolution.registered);
  ASSERT_EQ(cached_definition,
            ps_ctx_get_tag_definition(
                TK_STRUCT, (char *)"__TagBoundary", 13));
  ASSERT_EQ(1, cached_definition->align);
  psx_resolve_tag_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_REDEFINITION, resolution.status);

  request.kind = TK_UNION;
  request.mode = PSX_TAG_DECLARATION_FORWARD;
  request.alignment = 0;
  psx_resolve_tag_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_KIND_CONFLICT, resolution.status);

  ps_ctx_enter_block_scope();
  request.kind = TK_STRUCT;
  request.mode = PSX_TAG_DECLARATION_FORWARD;
  psx_resolve_tag_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_OK, resolution.status);
  ASSERT_TRUE(resolution.registered);
  ASSERT_EQ(1, resolution.scope_depth);
  request.mode = PSX_TAG_DECLARATION_DEFINITION;
  request.alignment = 1;
  psx_resolve_tag_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_OK, resolution.status);
  ASSERT_EQ(1, resolution.scope_depth);
  ps_ctx_leave_block_scope();
  ASSERT_EQ(0, ps_ctx_get_tag_scope_depth(
                   TK_STRUCT, (char *)"__TagBoundary", 13));
}

static int register_boundary_tag_member(
    token_kind_t tag_kind, char *tag_name, int tag_name_len,
    char *member_name, int member_name_len, int offset, psx_type_t *type,
    int bit_width, int bit_offset, int bit_is_signed) {
  tag_member_info_t member = {
      .name = member_name,
      .len = member_name_len,
      .offset = offset,
      .bit_width = bit_width,
      .bit_offset = bit_offset,
      .bit_is_signed = bit_is_signed,
      .decl_type = type,
  };
  int created = 0;
  return ps_ctx_register_tag_member(
             tag_kind, tag_name, tag_name_len, &member, &created) &&
         created;
}

static void test_aggregate_body_phase_boundary() {
  printf("test_aggregate_body_phase_boundary...\n");
  ps_reset_translation_unit_state();

  psx_tag_declaration_resolution_t tag;
  psx_resolve_tag_declaration(
      &(psx_tag_declaration_resolution_request_t){
          .kind = TK_STRUCT,
          .name = (char *)"__ParsedBody",
          .name_len = 12,
          .mode = PSX_TAG_DECLARATION_FORWARD,
      },
      &tag);
  ASSERT_EQ(PSX_TAG_DECLARATION_OK, tag.status);

  token_t *tokens = tk_tokenize(
      (char *)"int a, *b; _Static_assert(1, \"ok\"); char c; "
               "struct PhaseInnerTag { int x; } inner; "
               "enum PhaseEnumTag { PhaseEnumZero = 3, "
               "PhaseEnumNext = PhaseEnumZero + 2 } e; "
               "int arr[PhaseEnumNext]; "
               "unsigned flags:PhaseEnumZero; "
               "_Alignas(PhaseEnumNext + 3) char aligned; "
               "DeferredAlias late; "
               "int (*callback)(DeferredParam, int *, "
               "struct PrototypeOnly *, ...); }");
  tk_set_current_token(tokens);
  psx_parsed_aggregate_body_t body;
  ps_parse_aggregate_body(&body);
  ASSERT_EQ(10, body.item_count);
  ASSERT_EQ(PSX_PARSED_AGGREGATE_MEMBER_DECLARATION,
            body.items[0].kind);
  ASSERT_EQ(2, body.items[0].value.member_declaration.declarator_count);
  ASSERT_EQ(PSX_PARSED_AGGREGATE_STATIC_ASSERT, body.items[1].kind);
  ASSERT_TRUE(body.items[1].value.static_assertion.condition != NULL);
  ASSERT_EQ(PSX_PARSED_AGGREGATE_MEMBER_DECLARATION,
            body.items[2].kind);
  ASSERT_EQ(PSX_PARSED_TAG_DEFINITION,
            body.items[3].value.member_declaration.specifier
                .tag_action.action);
  ASSERT_TRUE(body.items[3].value.member_declaration.specifier
                  .tag_action.aggregate_body != NULL);
  ASSERT_EQ(PSX_PARSED_TAG_DEFINITION,
            body.items[4].value.member_declaration.specifier
                .tag_action.action);
  ASSERT_TRUE(body.items[4].value.member_declaration.specifier
                  .tag_action.enum_body != NULL);
  ASSERT_EQ(1, body.items[5].value.member_declaration.declarators[0]
                   .array_bound_count);
  ASSERT_TRUE(body.items[6].value.member_declaration.declarators[0]
                  .bit_width_expression.start != NULL);
  ASSERT_EQ(1, body.items[7].value.member_declaration.specifier
                   .alignas_expression_count);
  ASSERT_EQ(PSX_PARSED_DECL_TYPEDEF_NAME,
            body.items[8].value.member_declaration.specifier.source);
  ASSERT_EQ(13, body.items[8].value.member_declaration.specifier
                    .typedef_name->len);
  ASSERT_EQ(1, body.items[9].value.member_declaration.declarators[0]
                   .function_suffix_count);
  psx_parsed_function_parameters_t *callback_parameters =
      body.items[9].value.member_declaration.declarators[0]
          .function_suffixes[0].parameters;
  ASSERT_TRUE(callback_parameters != NULL);
  ASSERT_EQ(3, callback_parameters->count);
  ASSERT_EQ(PSX_PARSED_DECL_TYPEDEF_NAME,
            callback_parameters->items[0].specifier.source);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  tag_member_info_t member = {0};
  ASSERT_TRUE(!ps_ctx_find_tag_member_info(
      TK_STRUCT, (char *)"__ParsedBody", 12,
      (char *)"a", 1, &member));
  ASSERT_TRUE(!ps_ctx_has_tag_type(
      TK_STRUCT, (char *)"PhaseInnerTag", 13));
  ASSERT_TRUE(!ps_ctx_has_tag_type(
      TK_ENUM, (char *)"PhaseEnumTag", 12));
  long long enum_value = 0;
  ASSERT_TRUE(!ps_ctx_find_enum_const(
      (char *)"PhaseEnumZero", 13, &enum_value));
  const psx_type_t *deferred_alias_type = NULL;
  ASSERT_TRUE(!ps_ctx_find_typedef_decl_type(
      (char *)"DeferredAlias", 13, &deferred_alias_type));

  psx_typedef_info_t deferred_alias = {0};
  ps_ctx_typedef_set_decl_type(
      &deferred_alias, ps_type_new_integer(TK_INT, 4, 0));
  ASSERT_TRUE(ps_ctx_define_typedef_name(
      (char *)"DeferredAlias", 13, &deferred_alias));
  psx_typedef_info_t deferred_parameter = {0};
  ps_ctx_typedef_set_decl_type(
      &deferred_parameter,
      ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8));
  ASSERT_TRUE(ps_ctx_define_typedef_name(
      (char *)"DeferredParam", 13, &deferred_parameter));

  int size = 0;
  int alignment = 0;
  ASSERT_EQ(10, ps_apply_parsed_aggregate_body_layout(
                   &body, TK_STRUCT, (char *)"__ParsedBody", 12,
                   &size, &alignment));
  ASSERT_EQ(72, size);
  ASSERT_EQ(8, alignment);
  ASSERT_TRUE(ps_ctx_find_tag_member_info(
      TK_STRUCT, (char *)"__ParsedBody", 12,
      (char *)"a", 1, &member));
  ASSERT_EQ(0, member.offset);
  ASSERT_TRUE(ps_ctx_find_tag_member_info(
      TK_STRUCT, (char *)"__ParsedBody", 12,
      (char *)"b", 1, &member));
  ASSERT_EQ(8, member.offset);
  ASSERT_TRUE(ps_ctx_find_tag_member_info(
      TK_STRUCT, (char *)"__ParsedBody", 12,
      (char *)"c", 1, &member));
  ASSERT_EQ(16, member.offset);
  ASSERT_TRUE(ps_ctx_has_tag_type(
      TK_STRUCT, (char *)"PhaseInnerTag", 13));
  ASSERT_EQ(4, ps_ctx_get_tag_size(
                   TK_STRUCT, (char *)"PhaseInnerTag", 13));
  ASSERT_TRUE(ps_ctx_has_tag_type(
      TK_ENUM, (char *)"PhaseEnumTag", 12));
  ASSERT_TRUE(ps_ctx_find_enum_const(
      (char *)"PhaseEnumZero", 13, &enum_value));
  ASSERT_EQ(3, enum_value);
  ASSERT_TRUE(ps_ctx_find_enum_const(
      (char *)"PhaseEnumNext", 13, &enum_value));
  ASSERT_EQ(5, enum_value);
  ASSERT_TRUE(ps_ctx_find_tag_member_info(
      TK_STRUCT, (char *)"__ParsedBody", 12,
      (char *)"arr", 3, &member));
  ASSERT_EQ(28, member.offset);
  ASSERT_TRUE(member.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, member.decl_type->kind);
  ASSERT_EQ(5, member.decl_type->array_len);
  ASSERT_TRUE(ps_ctx_find_tag_member_info(
      TK_STRUCT, (char *)"__ParsedBody", 12,
      (char *)"flags", 5, &member));
  ASSERT_EQ(48, member.offset);
  ASSERT_EQ(3, member.bit_width);
  ASSERT_TRUE(ps_ctx_find_tag_member_info(
      TK_STRUCT, (char *)"__ParsedBody", 12,
      (char *)"aligned", 7, &member));
  ASSERT_EQ(56, member.offset);
  ASSERT_TRUE(ps_ctx_find_tag_member_info(
      TK_STRUCT, (char *)"__ParsedBody", 12,
      (char *)"late", 4, &member));
  ASSERT_EQ(60, member.offset);
  ASSERT_EQ(PSX_TYPE_INTEGER, member.decl_type->kind);
  ASSERT_EQ(4, member.decl_type->size);
  ASSERT_TRUE(ps_ctx_find_tag_member_info(
      TK_STRUCT, (char *)"__ParsedBody", 12,
      (char *)"callback", 8, &member));
  ASSERT_EQ(64, member.offset);
  const psx_type_t *callback_type =
      ps_type_find_function(member.decl_type);
  ASSERT_TRUE(callback_type != NULL);
  ASSERT_EQ(3, callback_type->param_count);
  ASSERT_TRUE(callback_type->is_variadic_function);
  ASSERT_EQ(PSX_TYPE_FLOAT, callback_type->param_types[0]->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, callback_type->param_types[0]->fp_kind);
  ASSERT_EQ(PSX_TYPE_POINTER, callback_type->param_types[1]->kind);
  ASSERT_EQ(PSX_TYPE_INTEGER, callback_type->param_types[1]->base->kind);
  ASSERT_EQ(PSX_TYPE_POINTER, callback_type->param_types[2]->kind);
  ASSERT_EQ(PSX_TYPE_STRUCT, callback_type->param_types[2]->base->kind);
  ASSERT_TRUE(!ps_ctx_has_tag_type(
      TK_STRUCT, (char *)"PrototypeOnly", 13));
  ps_dispose_parsed_aggregate_body(&body);
}

static void test_declaration_phase_boundary() {
  printf("test_declaration_phase_boundary...\n");
  ps_reset_translation_unit_state();

  token_t *tokens = tk_tokenize(
      (char *)"struct __PhaseObject { int value; }");
  tk_set_current_token(tokens);
  psx_declaration_phase_t phase;
  psx_parse_declaration_phase_syntax(&phase, NULL);
  ASSERT_EQ(PSX_DECLARATION_PHASE_SYNTAX, phase.state);
  ASSERT_TRUE(phase.base_type == NULL);
  ASSERT_EQ(-1, ps_ctx_get_tag_member_count(
                    TK_STRUCT, (char *)"__PhaseObject", 13));

  ASSERT_TRUE(psx_apply_declaration_phase(&phase, 0));
  ASSERT_EQ(PSX_DECLARATION_PHASE_RESOLVED_TYPE, phase.state);
  ASSERT_TRUE(phase.base_type != NULL);
  ASSERT_EQ(PSX_TYPE_STRUCT, phase.base_type->kind);
  ASSERT_TRUE(phase.base_type->aggregate_definition != NULL);
  ASSERT_EQ(1, phase.base_type->aggregate_definition->member_count);
  ASSERT_EQ(1, ps_ctx_get_tag_member_count(
                   TK_STRUCT, (char *)"__PhaseObject", 13));
  psx_dispose_declaration_phase(&phase);

  tokens = tk_tokenize((char *)"_Alignas(16) int");
  tk_set_current_token(tokens);
  psx_parse_declaration_phase_syntax(&phase, NULL);
  ASSERT_EQ(0, phase.requested_alignment);
  ASSERT_TRUE(psx_apply_declaration_phase(&phase, 0));
  ASSERT_EQ(16, phase.requested_alignment);
  ASSERT_EQ(PSX_TYPE_INTEGER, phase.base_type->kind);
  psx_dispose_declaration_phase(&phase);
}

static void test_type_name_phase_boundary() {
  printf("test_type_name_phase_boundary...\n");
  ps_reset_translation_unit_state();
  token_t *tokens = tk_tokenize((char *)"int (*)(double),");
  tk_set_current_token(tokens);
  psx_parsed_type_name_t syntax;
  ASSERT_TRUE(ps_parse_type_name_syntax_at(tokens, NULL, &syntax));
  ASSERT_TRUE(tk_get_current_token() == tokens);
  ASSERT_TRUE(syntax.end != NULL);
  ASSERT_EQ(TK_COMMA, syntax.end->kind);
  ASSERT_TRUE(syntax.atomic_inner == NULL);

  psx_type_t *type = psx_apply_parsed_type_name(&syntax);
  ASSERT_TRUE(type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, type->kind);
  ASSERT_TRUE(type->base != NULL);
  ASSERT_EQ(PSX_TYPE_FUNCTION, type->base->kind);
  ASSERT_EQ(1, type->base->param_count);
  ASSERT_EQ(PSX_TYPE_FLOAT, type->base->param_types[0]->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            type->base->param_types[0]->fp_kind);
  ps_dispose_type_name_syntax(&syntax);
}

static void test_toplevel_declarator_phase_boundary() {
  printf("test_toplevel_declarator_phase_boundary...\n");
  ps_reset_translation_unit_state();

  parse_program_input("int __phase_fn(int), __phase_object;");

  const psx_type_t *function =
      ps_ctx_get_function_type((char *)"__phase_fn", 10);
  ASSERT_TRUE(function != NULL);
  ASSERT_EQ(PSX_TYPE_FUNCTION, function->kind);
  ASSERT_EQ(1, function->param_count);
  ASSERT_EQ(PSX_TYPE_INTEGER, function->param_types[0]->kind);

  global_var_t *object =
      ps_find_global_var((char *)"__phase_object", 14);
  ASSERT_TRUE(object != NULL);
  ASSERT_TRUE(ps_gvar_get_decl_type(object) != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, ps_gvar_get_decl_type(object)->kind);
}

static void test_local_declarator_application_boundary() {
  printf("test_local_declarator_application_boundary...\n");
  ps_reset_translation_unit_state();
  psx_decl_register_lvar((char *)"n", 1);

  token_t *tokens = tk_tokenize((char *)"matrix[n][4]");
  tk_set_current_token(tokens);
  psx_parsed_declarator_t syntax =
      ps_parse_declarator_syntax_tree();
  ASSERT_TRUE(syntax.identifier != NULL);
  ASSERT_EQ(2, syntax.array_bound_count);
  ASSERT_EQ(2, syntax.declarator_shape.count);
  ASSERT_EQ(0, syntax.declarator_shape.ops[0].array_len);
  ASSERT_TRUE(!syntax.declarator_shape.ops[0].is_vla_array);
  ASSERT_EQ(0, syntax.declarator_shape.ops[1].array_len);
  ASSERT_TRUE(!syntax.declarator_shape.ops[1].is_vla_array);
  token_t *after_syntax = tk_get_current_token();

  psx_runtime_declarator_application_t applied;
  psx_apply_runtime_parsed_declarator(&syntax, &applied);
  ASSERT_TRUE(tk_get_current_token() == after_syntax);
  ASSERT_EQ(2, applied.array_bound_count);
  ASSERT_TRUE(applied.shape.ops[0].is_vla_array);
  ASSERT_EQ(0, applied.shape.ops[0].array_len);
  ASSERT_TRUE(!applied.array_bounds[0].is_constant);
  ASSERT_TRUE(applied.array_bounds[0].expression != NULL);
  ASSERT_TRUE(!applied.shape.ops[1].is_vla_array);
  ASSERT_EQ(4, applied.shape.ops[1].array_len);
  ASSERT_TRUE(applied.array_bounds[1].is_constant);
  ASSERT_EQ(4, applied.array_bounds[1].constant_value);
  ps_dispose_declarator_syntax(&syntax);
}

static void test_local_declaration_resolution_boundary() {
  printf("test_local_declaration_resolution_boundary...\n");
  psx_type_t *integer = ps_type_new_integer(TK_INT, 4, 0);
  psx_runtime_declarator_application_t application = {0};
  psx_local_declaration_resolution_t resolution;

  psx_resolve_local_declaration(
      &(psx_local_declaration_resolution_request_t){
          .type = integer,
          .application = &application,
      },
      &resolution);
  ASSERT_EQ(PSX_LOCAL_DECLARATION_OK, resolution.status);
  ASSERT_EQ(PSX_LOCAL_STORAGE_COMPLETE, resolution.storage_kind);

  psx_type_t *incomplete = ps_type_new_array(integer, 0, 0, 4, 0);
  application.shape.count = 1;
  application.shape.ops[0] = (psx_declarator_op_t){
      .kind = PSX_DECL_OP_ARRAY,
      .is_incomplete_array = 1,
  };
  psx_resolve_local_declaration(
      &(psx_local_declaration_resolution_request_t){
          .type = incomplete,
          .application = &application,
      },
      &resolution);
  ASSERT_EQ(PSX_LOCAL_DECLARATION_INCOMPLETE_ARRAY_NEEDS_INITIALIZER,
            resolution.status);
  psx_resolve_local_declaration(
      &(psx_local_declaration_resolution_request_t){
          .type = incomplete,
          .application = &application,
          .has_initializer = 1,
      },
      &resolution);
  ASSERT_EQ(PSX_LOCAL_DECLARATION_OK, resolution.status);
  ASSERT_EQ(PSX_LOCAL_STORAGE_INCOMPLETE_ARRAY,
            resolution.storage_kind);

  node_t *runtime_bound = ps_node_new_num(7);
  psx_type_t *vla = ps_type_new_array(integer, 0, 0, 4, 1);
  application = (psx_runtime_declarator_application_t){0};
  application.shape.count = 1;
  application.shape.ops[0] = (psx_declarator_op_t){
      .kind = PSX_DECL_OP_ARRAY,
      .is_vla_array = 1,
  };
  application.array_bound_count = 1;
  application.array_bounds[0] = (psx_runtime_array_bound_t){
      .declarator_op_index = 0,
      .expression = runtime_bound,
  };
  psx_resolve_local_declaration(
      &(psx_local_declaration_resolution_request_t){
          .type = vla,
          .application = &application,
      },
      &resolution);
  ASSERT_EQ(PSX_LOCAL_DECLARATION_OK, resolution.status);
  ASSERT_EQ(PSX_LOCAL_STORAGE_VLA_OBJECT, resolution.storage_kind);
  ASSERT_EQ(1, resolution.dimension_count);
  ASSERT_TRUE(resolution.dimensions[0].expression == runtime_bound);

  psx_type_t *pointer_to_vla = ps_type_new_pointer(vla, 0);
  application = (psx_runtime_declarator_application_t){0};
  application.shape.count = 2;
  application.shape.ops[0].kind = PSX_DECL_OP_POINTER;
  application.shape.ops[1] = (psx_declarator_op_t){
      .kind = PSX_DECL_OP_ARRAY,
      .is_vla_array = 1,
  };
  application.array_bound_count = 1;
  application.array_bounds[0] = (psx_runtime_array_bound_t){
      .declarator_op_index = 1,
      .expression = runtime_bound,
  };
  psx_resolve_local_declaration(
      &(psx_local_declaration_resolution_request_t){
          .type = pointer_to_vla,
          .application = &application,
      },
      &resolution);
  ASSERT_EQ(PSX_LOCAL_DECLARATION_OK, resolution.status);
  ASSERT_EQ(PSX_LOCAL_STORAGE_POINTER_TO_VLA, resolution.storage_kind);
  ASSERT_TRUE(resolution.pointer_row_dimension == runtime_bound);
}

static void test_aggregate_member_resolution_boundary() {
  printf("test_aggregate_member_resolution_boundary...\n");
  ps_reset_translation_unit_state();

  psx_aggregate_layout_state_t layout;
  psx_aggregate_layout_init(&layout, TK_STRUCT);
  psx_declarator_shape_t boundary_shape;
  ps_declarator_shape_init(&boundary_shape);
  psx_aggregate_member_declaration_resolution_t boundary;
  psx_resolve_aggregate_member_declaration(
      &layout,
      &(psx_aggregate_member_declaration_request_t){
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"__LayoutBoundary",
          .target_tag_name_len = 16,
          .base_type = ps_type_new_integer(TK_CHAR, 1, 0),
          .declarator_shape = &boundary_shape,
          .member_name = (char *)"c",
          .member_name_len = 1,
      },
      &boundary);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_OK, boundary.status);
  ASSERT_EQ(0, boundary.offset);

  psx_type_t *bitfield_integer = ps_type_new_integer(TK_INT, 4, 0);
  psx_resolve_aggregate_member_declaration(
      &layout,
      &(psx_aggregate_member_declaration_request_t){
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"__LayoutBoundary",
          .target_tag_name_len = 16,
          .base_type = bitfield_integer,
          .declarator_shape = &boundary_shape,
          .member_name = (char *)"a",
          .member_name_len = 1,
          .has_bitfield = 1,
          .bit_width = 20,
      },
      &boundary);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_OK, boundary.status);
  ASSERT_EQ(0, boundary.offset);
  ASSERT_EQ(8, boundary.bit_offset);
  ASSERT_TRUE(boundary.bit_is_signed);
  psx_resolve_aggregate_member_declaration(
      &layout,
      &(psx_aggregate_member_declaration_request_t){
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"__LayoutBoundary",
          .target_tag_name_len = 16,
          .base_type = bitfield_integer,
          .declarator_shape = &boundary_shape,
          .member_name = (char *)"b",
          .member_name_len = 1,
          .has_bitfield = 1,
          .bit_width = 16,
      },
      &boundary);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_OK, boundary.status);
  ASSERT_EQ(4, boundary.offset);
  ASSERT_EQ(0, boundary.bit_offset);
  psx_resolve_aggregate_member_declaration(
      &layout,
      &(psx_aggregate_member_declaration_request_t){
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"__LayoutBoundary",
          .target_tag_name_len = 16,
          .base_type = bitfield_integer,
          .declarator_shape = &boundary_shape,
          .member_name = (char *)"overflow",
          .member_name_len = 8,
          .has_bitfield = 1,
          .bit_width = 33,
      },
      &boundary);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_BIT_WIDTH_EXCEEDS_STORAGE,
            boundary.status);
  ASSERT_EQ(4, boundary.storage_size);
  psx_resolve_aggregate_member_declaration(
      &layout,
      &(psx_aggregate_member_declaration_request_t){
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"__LayoutBoundary",
          .target_tag_name_len = 16,
          .base_type = ps_type_new_pointer(bitfield_integer, 4),
          .declarator_shape = &boundary_shape,
          .member_name = (char *)"pointer",
          .member_name_len = 7,
          .has_bitfield = 1,
          .bit_width = 1,
      },
      &boundary);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_INVALID_BITFIELD_TYPE,
            boundary.status);

  psx_aggregate_layout_state_t bitfield_sign_layout;
  psx_aggregate_layout_init(&bitfield_sign_layout, TK_STRUCT);
  psx_type_t *canonical_enum = psx_resolve_decl_type(
      &(psx_decl_type_request_t){
          .tag_kind = TK_ENUM,
          .tag_name = (char *)"E",
          .tag_len = 1,
          .elem_size = 4,
      });
  ASSERT_TRUE(canonical_enum != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, canonical_enum->kind);
  ASSERT_EQ(TK_ENUM, canonical_enum->scalar_kind);
  ASSERT_EQ(TK_ENUM, canonical_enum->tag_kind);
  ASSERT_TRUE(ps_type_shape_matches(
      canonical_enum, ps_type_clone(canonical_enum)));
  ASSERT_TRUE(!ps_type_shape_matches(
      canonical_enum,
      ps_type_new_enum((char *)"F", 1, 1, 4)));
  ASSERT_TRUE(!ps_type_shape_matches(canonical_enum, bitfield_integer));
  psx_resolve_aggregate_member_declaration(
      &bitfield_sign_layout,
      &(psx_aggregate_member_declaration_request_t){
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"__SignBoundary",
          .target_tag_name_len = 14,
          .base_type = canonical_enum,
          .declarator_shape = &boundary_shape,
          .member_name = (char *)"e",
          .member_name_len = 1,
          .has_bitfield = 1,
          .bit_width = 2,
      },
      &boundary);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_OK, boundary.status);
  ASSERT_TRUE(!boundary.bit_is_signed);
  psx_resolve_aggregate_member_declaration(
      &bitfield_sign_layout,
      &(psx_aggregate_member_declaration_request_t){
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"__SignBoundary",
          .target_tag_name_len = 14,
          .base_type = ps_type_new_integer(TK_BOOL, 1, 1),
          .declarator_shape = &boundary_shape,
          .member_name = (char *)"b",
          .member_name_len = 1,
          .has_bitfield = 1,
          .bit_width = 1,
      },
      &boundary);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_OK, boundary.status);
  ASSERT_TRUE(!boundary.bit_is_signed);

  psx_type_t *packed_member = ps_type_new_integer(TK_SHORT, 2, 0);
  packed_member->align = 8;
  psx_resolve_aggregate_member_declaration(
      &layout,
      &(psx_aggregate_member_declaration_request_t){
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"__LayoutBoundary",
          .target_tag_name_len = 16,
          .base_type = packed_member,
          .declarator_shape = &boundary_shape,
          .member_name = (char *)"tail",
          .member_name_len = 4,
          .pack_alignment = 2,
      },
      &boundary);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_OK, boundary.status);
  ASSERT_EQ(8, boundary.offset);
  ASSERT_EQ(12, psx_aggregate_layout_size(&layout));
  ASSERT_EQ(4, psx_aggregate_layout_alignment(&layout));

  psx_aggregate_layout_init(&layout, TK_UNION);
  psx_resolve_aggregate_member_declaration(
      &layout,
      &(psx_aggregate_member_declaration_request_t){
          .target_tag_kind = TK_UNION,
          .target_tag_name = (char *)"__UnionBoundary",
          .target_tag_name_len = 15,
          .base_type = bitfield_integer,
          .declarator_shape = &boundary_shape,
          .member_name = (char *)"i",
          .member_name_len = 1,
      },
      &boundary);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_OK, boundary.status);
  ASSERT_EQ(0, boundary.offset);
  psx_resolve_aggregate_member_declaration(
      &layout,
      &(psx_aggregate_member_declaration_request_t){
          .target_tag_kind = TK_UNION,
          .target_tag_name = (char *)"__UnionBoundary",
          .target_tag_name_len = 15,
          .base_type = ps_type_new_integer(TK_LONG, 8, 0),
          .declarator_shape = &boundary_shape,
          .member_name = (char *)"l",
          .member_name_len = 1,
      },
      &boundary);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_OK, boundary.status);
  ASSERT_EQ(0, boundary.offset);
  ASSERT_EQ(8, psx_aggregate_layout_size(&layout));
  ASSERT_EQ(8, psx_aggregate_layout_alignment(&layout));

  psx_declarator_shape_t member_shape;
  ps_declarator_shape_init(&member_shape);
  ps_declarator_shape_append_pointer_levels(&member_shape, 1, 0, 0);
  ps_declarator_shape_append_array_ex(&member_shape, 3, 0);
  psx_type_t *member_type = psx_resolve_decl_type(
      &(psx_decl_type_request_t){
          .base_kind = TK_INT,
          .elem_size = 4,
          .declarator_shape = &member_shape,
      });
  ASSERT_TRUE(member_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, member_type->kind);
  ASSERT_TRUE(member_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, member_type->base->kind);
  ASSERT_EQ(3, member_type->base->array_len);
  ASSERT_TRUE(member_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, member_type->base->base->kind);

  ASSERT_EQ(12, member_type->ptr_array_pointee_bytes);
  ASSERT_EQ(12, member_type->outer_stride);
  ASSERT_EQ(8, ps_type_sizeof(member_type));
  ASSERT_EQ(8, member_type->align);
  ASSERT_EQ(1, ps_type_pointer_depth(member_type));
  ASSERT_EQ(12, ps_type_sizeof(member_type->base));
  ASSERT_EQ(4, ps_type_sizeof(member_type->base->base));

  psx_declarator_shape_t pointer_array_shape;
  ps_declarator_shape_init(&pointer_array_shape);
  ps_declarator_shape_append_array_ex(&pointer_array_shape, 2, 0);
  ps_declarator_shape_append_pointer_levels(
      &pointer_array_shape, 1, 0, 0);
  ps_declarator_shape_append_array_ex(&pointer_array_shape, 3, 0);
  psx_type_t *pointer_array_member = psx_resolve_decl_type(
      &(psx_decl_type_request_t){
          .base_kind = TK_INT,
          .elem_size = 4,
          .declarator_shape = &pointer_array_shape,
      });
  ASSERT_TRUE(pointer_array_member != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, pointer_array_member->kind);
  ASSERT_EQ(2, pointer_array_member->array_len);
  ASSERT_EQ(16, ps_type_sizeof(pointer_array_member));
  ASSERT_EQ(PSX_TYPE_POINTER, pointer_array_member->base->kind);
  ASSERT_EQ(PSX_TYPE_ARRAY, pointer_array_member->base->base->kind);
  ASSERT_EQ(3, pointer_array_member->base->base->array_len);
  ASSERT_EQ(12, ps_type_sizeof(pointer_array_member->base->base));

  psx_tag_declaration_resolution_t tag_resolution;
  psx_resolve_tag_declaration(
      &(psx_tag_declaration_resolution_request_t){
          .kind = TK_STRUCT,
          .name = (char *)"__AlignedMember",
          .name_len = 15,
          .mode = PSX_TAG_DECLARATION_DEFINITION,
          .size = 12,
          .alignment = 4,
      },
      &tag_resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_OK, tag_resolution.status);
  psx_type_t *aligned_member = psx_resolve_decl_type(
      &(psx_decl_type_request_t){
          .tag_kind = TK_STRUCT,
          .tag_name = (char *)"__AlignedMember",
          .tag_len = 15,
          .elem_size = 12,
      });
  ASSERT_TRUE(aligned_member != NULL);
  ASSERT_EQ(12, ps_type_sizeof(aligned_member));
  ASSERT_EQ(4, aligned_member->align);

  psx_resolve_tag_declaration(
      &(psx_tag_declaration_resolution_request_t){
          .kind = TK_STRUCT,
          .name = (char *)"__IncompleteMember",
          .name_len = 18,
          .mode = PSX_TAG_DECLARATION_FORWARD,
      },
      &tag_resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_OK, tag_resolution.status);
  psx_type_t *incomplete_base = psx_resolve_decl_type(
      &(psx_decl_type_request_t){
          .tag_kind = TK_STRUCT,
          .tag_name = (char *)"__IncompleteMember",
          .tag_len = 18,
      });
  ASSERT_TRUE(incomplete_base != NULL);
  psx_aggregate_layout_state_t constraint_layout;
  psx_aggregate_layout_init(&constraint_layout, TK_STRUCT);
  psx_resolve_aggregate_member_declaration(
      &constraint_layout,
      &(psx_aggregate_member_declaration_request_t){
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"__ConstraintBoundary",
          .target_tag_name_len = 20,
          .base_type = incomplete_base,
          .declarator_shape = &boundary_shape,
          .member_name = (char *)"incomplete",
          .member_name_len = 10,
      },
      &boundary);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_INCOMPLETE_TYPE, boundary.status);
  psx_declarator_shape_t incomplete_pointer_shape;
  ps_declarator_shape_init(&incomplete_pointer_shape);
  ps_declarator_shape_append_pointer_levels(
      &incomplete_pointer_shape, 1, 0, 0);
  psx_type_t *incomplete_pointer = psx_resolve_decl_type(
      &(psx_decl_type_request_t){
          .base_decl_type = incomplete_base,
          .declarator_shape = &incomplete_pointer_shape,
      });
  psx_resolve_aggregate_member_declaration(
      &constraint_layout,
      &(psx_aggregate_member_declaration_request_t){
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"__ConstraintBoundary",
          .target_tag_name_len = 20,
          .base_type = incomplete_pointer,
          .declarator_shape = &boundary_shape,
          .member_name = (char *)"pointer",
          .member_name_len = 7,
      },
      &boundary);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_OK, boundary.status);

  psx_type_t *function_member = ps_type_new_function(
      ps_type_new_integer(TK_INT, 4, 0),
      (psx_decl_funcptr_sig_t){0});
  psx_resolve_aggregate_member_declaration(
      &constraint_layout,
      &(psx_aggregate_member_declaration_request_t){
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"__ConstraintBoundary",
          .target_tag_name_len = 20,
          .base_type = function_member,
          .declarator_shape = &boundary_shape,
          .member_name = (char *)"function",
          .member_name_len = 8,
      },
      &boundary);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_FUNCTION_TYPE, boundary.status);

  psx_type_t *bool_base = psx_resolve_decl_type(
      &(psx_decl_type_request_t){
          .base_kind = TK_BOOL,
          .elem_size = 1,
          .is_unsigned = 1,
          .is_atomic = 1,
      });
  ASSERT_TRUE(bool_base != NULL);
  ASSERT_EQ(PSX_TYPE_BOOL, bool_base->kind);
  ASSERT_TRUE(ps_type_is_unsigned(bool_base));
  ASSERT_TRUE(bool_base->is_atomic);
  ASSERT_EQ(TK_BOOL, bool_base->scalar_kind);

  psx_type_t *integer = ps_type_new_integer(TK_INT, 4, 0);
  psx_declarator_shape_t transaction_shape;
  ps_declarator_shape_init(&transaction_shape);
  psx_aggregate_layout_state_t transaction_layout;
  psx_aggregate_layout_init(&transaction_layout, TK_STRUCT);
  psx_aggregate_member_declaration_resolution_t transaction;
  psx_resolve_aggregate_member_declaration(
      &transaction_layout,
      &(psx_aggregate_member_declaration_request_t){
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"Txn",
          .target_tag_name_len = 3,
          .base_type = integer,
          .declarator_shape = &transaction_shape,
          .member_name = (char *)"a",
          .member_name_len = 1,
      },
      &transaction);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_OK, transaction.status);
  ASSERT_EQ(0, transaction.offset);
  ASSERT_EQ(4, transaction.storage_size);
  ASSERT_EQ(1, transaction.registered_member_count);
  ASSERT_TRUE(transaction.type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, transaction.type->kind);
  psx_resolve_aggregate_member_declaration(
      &transaction_layout,
      &(psx_aggregate_member_declaration_request_t){
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"Txn",
          .target_tag_name_len = 3,
          .base_type = integer,
          .declarator_shape = &transaction_shape,
          .member_name = (char *)"b",
          .member_name_len = 1,
          .has_bitfield = 1,
          .bit_width = 3,
      },
      &transaction);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_OK, transaction.status);
  ASSERT_EQ(4, transaction.offset);
  ASSERT_EQ(0, transaction.bit_offset);
  ASSERT_TRUE(transaction.bit_is_signed);
  ASSERT_EQ(1, transaction.registered_member_count);
  int transaction_size_before_duplicate =
      psx_aggregate_layout_size(&transaction_layout);
  psx_resolve_aggregate_member_declaration(
      &transaction_layout,
      &(psx_aggregate_member_declaration_request_t){
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"Txn",
          .target_tag_name_len = 3,
          .base_type = integer,
          .declarator_shape = &transaction_shape,
          .member_name = (char *)"a",
          .member_name_len = 1,
      },
      &transaction);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_DUPLICATE, transaction.status);
  ASSERT_EQ(1, transaction.conflicting_name_len);
  ASSERT_EQ(transaction_size_before_duplicate,
            psx_aggregate_layout_size(&transaction_layout));
  psx_resolve_aggregate_member_declaration(
      &transaction_layout,
      &(psx_aggregate_member_declaration_request_t){
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"Txn",
          .target_tag_name_len = 3,
          .base_type = integer,
          .declarator_shape = &transaction_shape,
      },
      &transaction);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_MISSING_NAME, transaction.status);

  psx_declarator_shape_t unnamed_pointer_shape;
  ps_declarator_shape_init(&unnamed_pointer_shape);
  ps_declarator_shape_append_pointer_levels(
      &unnamed_pointer_shape, 1, 0, 0);
  psx_resolve_aggregate_member_declaration(
      &transaction_layout,
      &(psx_aggregate_member_declaration_request_t){
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"Txn",
          .target_tag_name_len = 3,
          .base_type = aligned_member,
          .declarator_shape = &unnamed_pointer_shape,
      },
      &transaction);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_MISSING_NAME, transaction.status);
  ASSERT_EQ(transaction_size_before_duplicate,
            psx_aggregate_layout_size(&transaction_layout));

  psx_declarator_shape_t unnamed_array_shape;
  ps_declarator_shape_init(&unnamed_array_shape);
  ps_declarator_shape_append_array_ex(&unnamed_array_shape, 2, 0);
  psx_resolve_aggregate_member_declaration(
      &transaction_layout,
      &(psx_aggregate_member_declaration_request_t){
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"Txn",
          .target_tag_name_len = 3,
          .base_type = aligned_member,
          .declarator_shape = &unnamed_array_shape,
      },
      &transaction);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_MISSING_NAME, transaction.status);
  ASSERT_EQ(transaction_size_before_duplicate,
            psx_aggregate_layout_size(&transaction_layout));

  ASSERT_TRUE(register_boundary_tag_member(
      TK_STRUCT, (char *)"__MemberBoundary", 16,
      (char *)"x", 1, 4, integer, 3, 5, 1));
  tag_member_info_t resolved_named = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_at_scope(
      TK_STRUCT, (char *)"__MemberBoundary", 16, 0,
      (char *)"x", 1, &resolved_named));
  ASSERT_EQ(4, resolved_named.offset);
  ASSERT_EQ(3, resolved_named.bit_width);
  ASSERT_EQ(5, resolved_named.bit_offset);
  ASSERT_TRUE(resolved_named.bit_is_signed);
  ASSERT_TRUE(resolved_named.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, resolved_named.decl_type->kind);
  ASSERT_TRUE(!register_boundary_tag_member(
      TK_STRUCT, (char *)"__MemberBoundary", 16,
      (char *)"x", 1, 4, integer, 0, 0, 0));

  ASSERT_TRUE(register_boundary_tag_member(
      TK_STRUCT, (char *)"__MemberBoundary", 16,
      (char *)"p", 1, 8, member_type, 0, 0, 0));
  tag_member_info_t resolved_pointer = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_at_scope(
      TK_STRUCT, (char *)"__MemberBoundary", 16, 0,
      (char *)"p", 1, &resolved_pointer));
  ASSERT_EQ(12, resolved_pointer.ptr_array_pointee_bytes);
  ASSERT_EQ(12, resolved_pointer.outer_stride);
  ASSERT_EQ(4, resolved_pointer.deref_size);

  for (int i = 0; i < 2; i++) {
    ASSERT_TRUE(register_boundary_tag_member(
        TK_STRUCT, (char *)"__MemberBoundary", 16,
        (char *)"", 0, 4 + i * 4, integer, 0, 0, 0));
  }

  psx_resolve_tag_declaration(
      &(psx_tag_declaration_resolution_request_t){
          .kind = TK_STRUCT,
          .name = (char *)"PromSrc",
          .name_len = 7,
          .mode = PSX_TAG_DECLARATION_DEFINITION,
          .member_count = 1,
          .size = 8,
          .alignment = 4,
      },
      &tag_resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_OK, tag_resolution.status);
  psx_resolve_tag_declaration(
      &(psx_tag_declaration_resolution_request_t){
          .kind = TK_STRUCT,
          .name = (char *)"PromDst",
          .name_len = 7,
          .mode = PSX_TAG_DECLARATION_FORWARD,
      },
      &tag_resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_OK, tag_resolution.status);
  ASSERT_TRUE(register_boundary_tag_member(
      TK_STRUCT, (char *)"PromSrc", 7,
      (char *)"b", 1, 4, integer, 3, 2, 1));
  psx_aggregate_layout_state_t promoted_layout;
  psx_aggregate_layout_init(&promoted_layout, TK_STRUCT);
  psx_declarator_shape_t promoted_shape;
  ps_declarator_shape_init(&promoted_shape);
  psx_resolve_aggregate_member_declaration(
      &promoted_layout,
      &(psx_aggregate_member_declaration_request_t){
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"PromDst",
          .target_tag_name_len = 7,
          .base_type = ps_type_new_integer(TK_LONG, 8, 0),
          .declarator_shape = &promoted_shape,
          .member_name = (char *)"prefix",
          .member_name_len = 6,
      },
      &transaction);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_OK, transaction.status);
  psx_resolve_aggregate_member_declaration(
      &promoted_layout,
      &(psx_aggregate_member_declaration_request_t){
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"PromDst",
          .target_tag_name_len = 7,
          .base_type = ps_type_new_tag(
              TK_STRUCT, (char *)"PromSrc", 7, 1, 8),
          .declarator_shape = &promoted_shape,
      },
      &transaction);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_OK, transaction.status);
  ASSERT_EQ(2, transaction.registered_member_count);
  tag_member_info_t promoted = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info(
      TK_STRUCT, (char *)"PromDst", 7, (char *)"b", 1, &promoted));
  ASSERT_EQ(12, promoted.offset);
  ASSERT_EQ(3, promoted.bit_width);
  ASSERT_EQ(2, promoted.bit_offset);
  ASSERT_TRUE(promoted.bit_is_signed);
  ASSERT_TRUE(promoted.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, promoted.decl_type->kind);

  psx_resolve_tag_declaration(
      &(psx_tag_declaration_resolution_request_t){
          .kind = TK_STRUCT,
          .name = (char *)"BatchSrc",
          .name_len = 8,
          .mode = PSX_TAG_DECLARATION_DEFINITION,
          .member_count = 2,
          .size = 8,
          .alignment = 4,
      },
      &tag_resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_OK, tag_resolution.status);
  psx_resolve_tag_declaration(
      &(psx_tag_declaration_resolution_request_t){
          .kind = TK_STRUCT,
          .name = (char *)"BatchDst",
          .name_len = 8,
          .mode = PSX_TAG_DECLARATION_FORWARD,
      },
      &tag_resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_OK, tag_resolution.status);
  ASSERT_TRUE(register_boundary_tag_member(
      TK_STRUCT, (char *)"BatchSrc", 8,
      (char *)"a", 1, 0, integer, 0, 0, 0));
  ASSERT_TRUE(register_boundary_tag_member(
      TK_STRUCT, (char *)"BatchSrc", 8,
      (char *)"b", 1, 4, integer, 0, 0, 0));
  ASSERT_TRUE(register_boundary_tag_member(
      TK_STRUCT, (char *)"BatchDst", 8,
      (char *)"b", 1, 0, integer, 0, 0, 0));

  tag_member_info_t absent_promoted = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info(
      TK_STRUCT, (char *)"BatchDst", 8,
      (char *)"b", 1, &absent_promoted));
  ASSERT_TRUE(!ps_ctx_find_tag_member_info(
      TK_STRUCT, (char *)"BatchDst", 8,
      (char *)"a", 1, &absent_promoted));

  psx_aggregate_layout_state_t anonymous_layout;
  psx_aggregate_layout_init(&anonymous_layout, TK_STRUCT);
  psx_declarator_shape_t anonymous_shape;
  ps_declarator_shape_init(&anonymous_shape);
  psx_type_t *batch_source_type =
      ps_type_new_tag(TK_STRUCT, (char *)"BatchSrc", 8, 1, 8);
  psx_resolve_aggregate_member_declaration(
      &anonymous_layout,
      &(psx_aggregate_member_declaration_request_t){
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"BatchDst",
          .target_tag_name_len = 8,
          .base_type = batch_source_type,
          .declarator_shape = &anonymous_shape,
      },
      &transaction);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_DUPLICATE, transaction.status);
  ASSERT_EQ(1, transaction.conflicting_name_len);
  ASSERT_EQ(0, psx_aggregate_layout_size(&anonymous_layout));
  ASSERT_TRUE(!ps_ctx_find_tag_member_info(
      TK_STRUCT, (char *)"BatchDst", 8,
      (char *)"a", 1, &absent_promoted));
  ASSERT_TRUE(!ps_ctx_find_tag_member_info(
      TK_STRUCT, (char *)"BatchDst", 8,
      (char *)"", 0, &absent_promoted));
}

static void test_static_assert_resolution_boundary() {
  printf("test_static_assert_resolution_boundary...\n");
  psx_static_assert_resolution_t resolution;
  psx_resolve_static_assert(
      &(psx_static_assert_request_t){.is_constant = 1, .value = 1},
      &resolution);
  ASSERT_EQ(PSX_STATIC_ASSERT_OK, resolution.status);
  psx_resolve_static_assert(
      &(psx_static_assert_request_t){.is_constant = 1, .value = 0},
      &resolution);
  ASSERT_EQ(PSX_STATIC_ASSERT_FAILED, resolution.status);
  psx_resolve_static_assert(
      &(psx_static_assert_request_t){.is_constant = 0, .value = 1},
      &resolution);
  ASSERT_EQ(PSX_STATIC_ASSERT_NOT_CONSTANT, resolution.status);
}

static void test_typedef_declaration_resolution_boundary() {
  printf("test_typedef_declaration_resolution_boundary...\n");
  ps_reset_translation_unit_state();
  psx_type_t *integer = ps_type_new_integer(TK_INT, 4, 0);
  psx_typedef_declaration_resolution_request_t request = {
      .name = (char *)"__TypeBoundary",
      .name_len = 14,
      .type = integer,
  };
  psx_typedef_declaration_resolution_t resolution;
  psx_resolve_typedef_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TYPEDEF_DECLARATION_OK, resolution.status);
  ASSERT_TRUE(resolution.created);
  ASSERT_EQ(0, resolution.scope_depth);

  psx_resolve_typedef_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TYPEDEF_DECLARATION_OK, resolution.status);
  ASSERT_TRUE(resolution.redeclared);

  psx_type_t *long_type = ps_type_new_integer(TK_LONG, 8, 0);
  request.type = long_type;
  psx_resolve_typedef_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TYPEDEF_DECLARATION_TYPE_CONFLICT, resolution.status);

  ps_ctx_enter_block_scope();
  psx_resolve_typedef_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TYPEDEF_DECLARATION_OK, resolution.status);
  ASSERT_TRUE(resolution.created);
  ASSERT_EQ(1, resolution.scope_depth);
  ps_ctx_leave_block_scope();

  psx_global_object_result_t object;
  ASSERT_TRUE(lower_global_object_declaration(
      &(psx_global_object_request_t){
          .name = (char *)"__TypeObject",
          .name_len = 12,
          .type = integer,
      },
      &object));
  request.name = (char *)"__TypeObject";
  request.name_len = 12;
  request.type = integer;
  psx_resolve_typedef_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TYPEDEF_DECLARATION_OBJECT_NAME_CONFLICT,
            resolution.status);

  psx_function_declaration_resolution_t function_resolution;
  psx_resolve_function_declaration(
      &(psx_function_declaration_resolution_request_t){
          .name = (char *)"__TypeFunction",
          .name_len = 14,
          .return_type = integer,
      },
      &function_resolution);
  ASSERT_EQ(PSX_FUNCTION_DECLARATION_OK, function_resolution.status);
  request.name = (char *)"__TypeFunction";
  request.name_len = 14;
  psx_resolve_typedef_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TYPEDEF_DECLARATION_FUNCTION_NAME_CONFLICT,
            resolution.status);

  ASSERT_TRUE(ps_ctx_define_enum_const(
      (char *)"__TypeEnum", 10, 1));
  request.name = (char *)"__TypeEnum";
  request.name_len = 10;
  psx_resolve_typedef_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TYPEDEF_DECLARATION_ENUM_NAME_CONFLICT,
            resolution.status);
}

static void test_enum_constant_resolution_boundary() {
  printf("test_enum_constant_resolution_boundary...\n");
  ps_reset_translation_unit_state();
  psx_enum_constant_resolution_request_t request = {
      .name = (char *)"__EnumBoundary",
      .name_len = 14,
      .value = 7,
  };
  psx_enum_constant_resolution_t resolution;
  psx_resolve_enum_constant(&request, &resolution);
  ASSERT_EQ(PSX_ENUM_CONSTANT_OK, resolution.status);
  ASSERT_TRUE(resolution.created);
  ASSERT_EQ(0, resolution.scope_depth);
  psx_resolve_enum_constant(&request, &resolution);
  ASSERT_EQ(PSX_ENUM_CONSTANT_DUPLICATE, resolution.status);

  ps_ctx_enter_block_scope();
  request.value = 11;
  psx_resolve_enum_constant(&request, &resolution);
  ASSERT_EQ(PSX_ENUM_CONSTANT_OK, resolution.status);
  ASSERT_TRUE(resolution.created);
  ASSERT_EQ(1, resolution.scope_depth);
  long long value = 0;
  ASSERT_TRUE(ps_ctx_find_enum_const(
      (char *)"__EnumBoundary", 14, &value));
  ASSERT_EQ(11, value);
  ps_ctx_leave_block_scope();
  ASSERT_TRUE(ps_ctx_find_enum_const(
      (char *)"__EnumBoundary", 14, &value));
  ASSERT_EQ(7, value);

  psx_type_t *integer = ps_type_new_integer(TK_INT, 4, 0);
  psx_global_object_result_t object;
  ASSERT_TRUE(lower_global_object_declaration(
      &(psx_global_object_request_t){
          .name = (char *)"__EnumObject",
          .name_len = 12,
          .type = integer,
      },
      &object));
  request.name = (char *)"__EnumObject";
  request.name_len = 12;
  psx_resolve_enum_constant(&request, &resolution);
  ASSERT_EQ(PSX_ENUM_CONSTANT_OBJECT_NAME_CONFLICT, resolution.status);

  psx_function_declaration_resolution_t function_resolution;
  psx_resolve_function_declaration(
      &(psx_function_declaration_resolution_request_t){
          .name = (char *)"__EnumFunction",
          .name_len = 14,
          .return_type = integer,
      },
      &function_resolution);
  ASSERT_EQ(PSX_FUNCTION_DECLARATION_OK, function_resolution.status);
  request.name = (char *)"__EnumFunction";
  request.name_len = 14;
  psx_resolve_enum_constant(&request, &resolution);
  ASSERT_EQ(PSX_ENUM_CONSTANT_FUNCTION_NAME_CONFLICT, resolution.status);

  psx_typedef_declaration_resolution_t typedef_resolution;
  psx_resolve_typedef_declaration(
      &(psx_typedef_declaration_resolution_request_t){
          .name = (char *)"__EnumType",
          .name_len = 10,
          .type = integer,
      },
      &typedef_resolution);
  ASSERT_EQ(PSX_TYPEDEF_DECLARATION_OK, typedef_resolution.status);
  request.name = (char *)"__EnumType";
  request.name_len = 10;
  psx_resolve_enum_constant(&request, &resolution);
  ASSERT_EQ(PSX_ENUM_CONSTANT_TYPEDEF_NAME_CONFLICT, resolution.status);

  ps_ctx_enter_block_scope();
  ps_decl_enter_scope();
  ASSERT_TRUE(psx_decl_register_lvar_sized(
      (char *)"__EnumLocal", 11, 4, 4, 0) != NULL);
  request.name = (char *)"__EnumLocal";
  request.name_len = 11;
  psx_resolve_enum_constant(&request, &resolution);
  ASSERT_EQ(PSX_ENUM_CONSTANT_OBJECT_NAME_CONFLICT, resolution.status);
  ps_decl_leave_scope();
  ps_ctx_leave_block_scope();
}

static void test_initializer_resolution_boundary() {
  printf("test_initializer_resolution_boundary...\n");
  psx_type_t *integer = ps_type_new_integer(TK_INT, 4, 0);
  psx_type_t *array = ps_type_new_array(integer, 2, 8, 4, 0);
  tag_member_info_t members[2] = {
      {.name = (char *)"x", .len = 1, .offset = 0,
       .type_size = 4, .decl_type = integer},
      {.name = (char *)"a", .len = 1, .offset = 4,
       .type_size = 8, .array_len = 2, .decl_type = array},
  };
  psx_aggregate_definition_t definition = {
      .tag_kind = TK_STRUCT,
      .tag_name = (char *)"InitBoundary",
      .tag_len = 12,
      .size = 12,
      .align = 4,
      .member_count = 2,
      .members = members,
  };
  psx_type_t *aggregate = ps_type_new_tag(
      TK_STRUCT, definition.tag_name, definition.tag_len, 0, 12);
  aggregate->aggregate_definition = &definition;

  psx_initializer_scalar_leaf_list_t leaves = {0};
  ASSERT_TRUE(psx_collect_initializer_scalar_leaves(
      aggregate, 0, &leaves));
  ASSERT_EQ(3, leaves.count);
  ASSERT_EQ(0, leaves.items[0].relative_offset);
  ASSERT_EQ(4, leaves.items[1].relative_offset);
  ASSERT_EQ(8, leaves.items[2].relative_offset);
  ASSERT_EQ(integer, leaves.items[2].type);

  node_num_t index = {0};
  index.base.kind = ND_NUM;
  index.val = 1;
  psx_initializer_entry_t entry = {0};
  entry.designator_count = 2;
  entry.designators[0] = (psx_initializer_designator_t){
      .kind = PSX_INIT_DESIGNATOR_MEMBER,
      .member_name = (char *)"a",
      .member_len = 1,
  };
  entry.designators[1] = (psx_initializer_designator_t){
      .kind = PSX_INIT_DESIGNATOR_INDEX,
      .index_expr = (node_t *)&index,
  };
  psx_initializer_target_t target =
      psx_resolve_initializer_designator_path(
          &entry, aggregate, 0, NULL);
  ASSERT_EQ(integer, target.type);
  ASSERT_EQ(8, target.relative_offset);
  ASSERT_EQ(1, target.first_member_index);
  ASSERT_EQ(1, target.first_array_index);
  ASSERT_EQ(3, psx_initializer_leaf_cursor_after_target(
                   &leaves, &target));
  psx_initializer_scalar_leaf_list_dispose(&leaves);
}

static void test_local_initializer_parse_lowering_boundary() {
  printf("test_local_initializer_parse_lowering_boundary...\n");
  psx_decl_reset_locals();

  psx_type_t *integer = ps_type_new_integer(TK_INT, 4, 0);
  psx_decl_funcptr_sig_t signature = {0};
  signature.function.callable.return_shape.int_width = 4;
  psx_type_t *function_pointer = ps_type_attach_funcptr_signature(
      ps_type_new_pointer(integer, 4), signature);
  tag_member_info_t members[2] = {
      {.name = (char *)"fn", .len = 2, .offset = 0,
       .type_size = 8, .decl_type = function_pointer},
      {.name = (char *)"value", .len = 5, .offset = 8,
       .type_size = 4, .decl_type = integer},
  };
  psx_aggregate_definition_t definition = {
      .tag_kind = TK_STRUCT,
      .tag_name = (char *)"LocalInitBoundary",
      .tag_len = 17,
      .size = 16,
      .align = 8,
      .member_count = 2,
      .members = members,
  };
  psx_type_t *aggregate = ps_type_new_tag(
      TK_STRUCT, definition.tag_name, definition.tag_len, 0, 16);
  aggregate->aggregate_definition = &definition;

  lvar_t *object = psx_decl_register_lvar_sized(
      (char *)"object", 6, 16, 16, 0);
  psx_decl_set_lvar_decl_type(object, aggregate);
  token_t *tokens = tk_tokenize((char *)"{0, 7}");
  tk_set_current_token(tokens);

  node_t *raw = ps_decl_parse_initializer_for_var(object, 0);
  ASSERT_EQ(ND_DECL_INIT, raw->kind);
  ASSERT_EQ(PSX_DECL_INIT_LIST,
            ((node_decl_init_t *)raw)->init_kind);
  ASSERT_EQ(ND_INIT_LIST, raw->rhs->kind);
  ASSERT_EQ(2, ((node_init_list_t *)raw->rhs)->entry_count);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  psx_semantic_analyze_expression(raw, NULL);
  ASSERT_TRUE(raw->kind != ND_DECL_INIT);

  lvar_t *source = psx_decl_register_lvar_sized(
      (char *)"source", 6, 16, 16, 0);
  psx_decl_set_lvar_decl_type(source, aggregate);
  tokens = tk_tokenize((char *)"source");
  tk_set_current_token(tokens);
  raw = ps_decl_parse_initializer_for_var(object, 0);
  ASSERT_EQ(ND_DECL_INIT, raw->kind);
  ASSERT_EQ(PSX_DECL_INIT_EXPR,
            ((node_decl_init_t *)raw)->init_kind);
  ASSERT_EQ(ND_LVAR, raw->rhs->kind);
  psx_semantic_analyze_expression(raw, NULL);
  ASSERT_EQ(ND_ASSIGN, raw->kind);
  ASSERT_TRUE(raw->is_decl_initializer);

  tag_member_info_t union_members[1] = {
      {.name = (char *)"value", .len = 5, .offset = 0,
       .type_size = 4, .decl_type = integer},
  };
  psx_aggregate_definition_t union_definition = {
      .tag_kind = TK_UNION,
      .tag_name = (char *)"LocalUnionBoundary",
      .tag_len = 18,
      .size = 4,
      .align = 4,
      .member_count = 1,
      .members = union_members,
  };
  psx_type_t *union_type = ps_type_new_tag(
      TK_UNION, union_definition.tag_name, union_definition.tag_len, 0, 4);
  union_type->aggregate_definition = &union_definition;
  lvar_t *union_object = psx_decl_register_lvar_sized(
      (char *)"u", 1, 4, 4, 0);
  psx_decl_set_lvar_decl_type(union_object, union_type);
  tokens = tk_tokenize((char *)"9");
  tk_set_current_token(tokens);
  raw = ps_decl_parse_initializer_for_var(union_object, 0);
  ASSERT_EQ(ND_DECL_INIT, raw->kind);
  ASSERT_EQ(PSX_DECL_INIT_EXPR,
            ((node_decl_init_t *)raw)->init_kind);
  psx_semantic_analyze_expression(raw, NULL);
  ASSERT_EQ(ND_ASSIGN, raw->kind);
  ASSERT_TRUE(raw->is_decl_initializer);
  ASSERT_EQ(9, as_num(raw->rhs)->val);

  lvar_t *scalar = psx_decl_register_lvar_sized(
      (char *)"scalar", 6, 4, 4, 0);
  psx_decl_set_lvar_decl_type(scalar, integer);
  tokens = tk_tokenize((char *)"{7,}");
  tk_set_current_token(tokens);
  raw = ps_decl_parse_initializer_for_var(scalar, 0);
  ASSERT_EQ(ND_DECL_INIT, raw->kind);
  ASSERT_EQ(PSX_DECL_INIT_LIST,
            ((node_decl_init_t *)raw)->init_kind);
  ASSERT_EQ(1, ((node_init_list_t *)raw->rhs)->entry_count);
  psx_semantic_analyze_expression(raw, NULL);
  ASSERT_EQ(ND_ASSIGN, raw->kind);
  ASSERT_EQ(7, as_num(raw->rhs)->val);

  psx_type_t *complex_type = ps_type_new(PSX_TYPE_COMPLEX);
  complex_type->size = 16;
  complex_type->align = 8;
  complex_type->fp_kind = TK_FLOAT_KIND_DOUBLE;
  lvar_t *complex_value = psx_decl_register_lvar_sized(
      (char *)"complex", 7, 16, 16, 0);
  psx_decl_set_lvar_decl_type(complex_value, complex_type);
  tokens = tk_tokenize((char *)"{3, 4}");
  tk_set_current_token(tokens);
  raw = ps_decl_parse_initializer_for_var(complex_value, 0);
  ASSERT_EQ(ND_DECL_INIT, raw->kind);
  ASSERT_EQ(PSX_DECL_INIT_LIST,
            ((node_decl_init_t *)raw)->init_kind);
  ASSERT_EQ(2, ((node_init_list_t *)raw->rhs)->entry_count);
  psx_semantic_analyze_expression(raw, NULL);
  ASSERT_EQ(ND_COMMA, raw->kind);
  ASSERT_EQ(ND_ASSIGN, raw->lhs->kind);
  ASSERT_EQ(ND_ASSIGN, raw->rhs->kind);
}

static void test_static_data_initializer_boundary() {
  printf("test_static_data_initializer_boundary...\n");
  psx_type_t *integer = ps_type_new_integer(TK_INT, 4, 0);
  psx_type_t *array = ps_type_new_array(integer, 3, 12, 4, 0);
  node_num_t one = {0};
  one.base.kind = ND_NUM;
  one.val = 1;
  node_num_t two = {0};
  two.base.kind = ND_NUM;
  two.val = 2;
  node_num_t seven = {0};
  seven.base.kind = ND_NUM;
  seven.val = 7;
  psx_initializer_entry_t entries[2] = {
      {.value = (node_t *)&one},
      {.value = (node_t *)&seven, .designator_count = 1},
  };
  entries[1].designators[0] = (psx_initializer_designator_t){
      .kind = PSX_INIT_DESIGNATOR_INDEX,
      .index_expr = (node_t *)&two,
  };
  node_init_list_t list = {0};
  list.base.kind = ND_INIT_LIST;
  list.entries = entries;
  list.entry_count = 2;
  global_var_t global = {0};
  global.type_size = 12;
  ASSERT_TRUE(lower_static_scalar_array_initializer(
      &global, array, &list, NULL));
  ASSERT_EQ(3, global.init_count);
  ASSERT_EQ(1, ps_gvar_init_slot_view(&global, 0).value);
  ASSERT_EQ(0, ps_gvar_init_slot_view(&global, 1).value);
  ASSERT_EQ(7, ps_gvar_init_slot_view(&global, 2).value);

  psx_type_t *wide = ps_type_new_integer(TK_LONG, 8, 0);
  psx_type_t *pair = ps_type_new_array(integer, 2, 8, 4, 0);
  tag_member_info_t union_members[2] = {
      {.name = (char *)"raw", .len = 3, .offset = 0,
       .type_size = 8, .decl_type = wide},
      {.name = (char *)"a", .len = 1, .offset = 0,
       .type_size = 8, .array_len = 2, .decl_type = pair},
  };
  psx_aggregate_definition_t union_definition = {
      .tag_kind = TK_UNION,
      .tag_name = (char *)"InitUnion",
      .tag_len = 9,
      .size = 8,
      .align = 8,
      .member_count = 2,
      .members = union_members,
  };
  psx_type_t *union_type = ps_type_new_tag(
      TK_UNION, union_definition.tag_name, union_definition.tag_len, 0, 8);
  union_type->aggregate_definition = &union_definition;
  node_num_t union_index = {0};
  union_index.base.kind = ND_NUM;
  union_index.val = 1;
  node_num_t union_value = {0};
  union_value.base.kind = ND_NUM;
  union_value.val = 7;
  psx_initializer_entry_t union_entry = {
      .value = (node_t *)&union_value,
      .designator_count = 2,
  };
  union_entry.designators[0] = (psx_initializer_designator_t){
      .kind = PSX_INIT_DESIGNATOR_MEMBER,
      .member_name = (char *)"a",
      .member_len = 1,
  };
  union_entry.designators[1] = (psx_initializer_designator_t){
      .kind = PSX_INIT_DESIGNATOR_INDEX,
      .index_expr = (node_t *)&union_index,
  };
  node_init_list_t union_list = {0};
  union_list.base.kind = ND_INIT_LIST;
  union_list.entries = &union_entry;
  union_list.entry_count = 1;
  global_var_t union_global = {0};
  union_global.type_size = 8;
  ASSERT_TRUE(lower_static_object_initializer(
      &union_global, union_type, &union_list, NULL));
  ASSERT_EQ(2, union_global.init_count);
  ASSERT_EQ(0, ps_gvar_init_slot_view(&union_global, 0).value);
  ASSERT_EQ(7, ps_gvar_init_slot_view(&union_global, 1).value);
  ASSERT_EQ(1, ps_gvar_union_init_slot_ordinal(&union_global, 0));

  psx_type_t *incomplete = ps_type_new_array(integer, 0, 0, 4, 0);
  global_var_t inferred_global = {0};
  ps_decl_set_gvar_decl_type(&inferred_global, incomplete);
  psx_type_t *inferred_type = ps_gvar_get_decl_type(&inferred_global);
  psx_initializer_entry_t inferred_entries[3] = {
      {.value = (node_t *)&one},
      {.value = (node_t *)&two},
      {.value = (node_t *)&seven},
  };
  node_init_list_t inferred_list = {0};
  inferred_list.base.kind = ND_INIT_LIST;
  inferred_list.entries = inferred_entries;
  inferred_list.entry_count = 3;
  psx_static_initializer_resolution_t inferred_resolution;
  psx_resolve_static_initializer(
      &(psx_static_initializer_resolution_request_t){
          .type = inferred_type,
          .kind = PSX_DECL_INIT_LIST,
          .initializer = (node_t *)&inferred_list,
      },
      &inferred_resolution);
  ASSERT_EQ(PSX_STATIC_INITIALIZER_OK, inferred_resolution.status);
  ASSERT_EQ(1, inferred_resolution.type_completed);
  ASSERT_EQ(3, inferred_type->array_len);
  ASSERT_EQ(0, inferred_global.type_size);

  psx_static_declaration_initializer_result_t inferred_result = {0};
  ASSERT_TRUE(lower_resolved_static_initializer(
      &inferred_global, &inferred_resolution, NULL,
      &inferred_result));
  ASSERT_EQ(1, inferred_result.type_completed);
  ASSERT_EQ(1, inferred_result.initialized);
  ASSERT_EQ(12, inferred_global.type_size);
  ASSERT_EQ(3, inferred_global.init_count);
  ASSERT_EQ(7, ps_gvar_init_slot_view(&inferred_global, 2).value);

  psx_type_t *char_type = ps_type_new_integer(TK_CHAR, 1, 0);
  psx_type_t *char_pointer = ps_type_new_pointer(char_type, 1);
  psx_type_t *pointer_array = ps_type_new_array(char_pointer, 0, 0, 8, 0);
  global_var_t pointer_array_global = {0};
  ps_decl_set_gvar_decl_type(&pointer_array_global, pointer_array);
  psx_type_t *pointer_array_type =
      ps_gvar_get_decl_type(&pointer_array_global);
  node_string_t string = {0};
  string.base.kind = ND_STRING;
  string.string_label = (char *)".Lboundary";
  string.char_width = TK_CHAR_WIDTH_CHAR;
  psx_initializer_entry_t pointer_entry = {
      .value = (node_t *)&string,
  };
  node_init_list_t pointer_list = {0};
  pointer_list.base.kind = ND_INIT_LIST;
  pointer_list.entries = &pointer_entry;
  pointer_list.entry_count = 1;
  psx_static_initializer_resolution_t pointer_resolution;
  psx_resolve_static_initializer(
      &(psx_static_initializer_resolution_request_t){
          .type = pointer_array_type,
          .kind = PSX_DECL_INIT_LIST,
          .initializer = (node_t *)&pointer_list,
      },
      &pointer_resolution);
  ASSERT_EQ(PSX_STATIC_INITIALIZER_OK, pointer_resolution.status);
  ASSERT_TRUE(lower_resolved_static_initializer(
      &pointer_array_global, &pointer_resolution, NULL, NULL));
  ASSERT_EQ(1, pointer_array_type->array_len);
  ASSERT_EQ(8, pointer_array_global.type_size);
  ASSERT_EQ(1, pointer_array_global.init_count);
  psx_gvar_init_slot_t pointer_slot =
      ps_gvar_init_slot_view(&pointer_array_global, 0);
  ASSERT_TRUE(pointer_slot.symbol == string.string_label);
  ASSERT_EQ(-1, pointer_slot.symbol_len);

  ps_global_registry_reset_translation_unit();
  psx_decl_reset_locals();
  psx_static_local_lowering_reset();
  psx_type_t *static_incomplete =
      ps_type_new_array(integer, 0, 0, 4, 0);
  psx_static_initializer_resolution_t static_initializer_resolution;
  psx_resolve_static_initializer(
      &(psx_static_initializer_resolution_request_t){
          .type = static_incomplete,
          .kind = PSX_DECL_INIT_LIST,
          .initializer = (node_t *)&inferred_list,
      },
      &static_initializer_resolution);
  ASSERT_EQ(PSX_STATIC_INITIALIZER_OK,
            static_initializer_resolution.status);
  psx_static_local_declaration_result_t static_result = {0};
  ASSERT_TRUE(lower_static_local_declaration(
      &(psx_static_local_declaration_request_t){
          .kind = PSX_STATIC_LOCAL_ARRAY,
          .function_name = (char *)"boundary",
          .function_name_len = 8,
          .name = (char *)"values",
          .name_len = 6,
          .alias_element_size = 4,
          .type = static_incomplete,
          .initializer_resolution = &static_initializer_resolution,
      },
      &static_result));
  ASSERT_TRUE(static_result.global != NULL);
  ASSERT_TRUE(static_result.alias != NULL);
  ASSERT_EQ(1, static_result.type_completed);
  ASSERT_TRUE(ps_find_global_var(
      static_result.alias->static_global_name,
      static_result.alias->static_global_name_len) == static_result.global);
  psx_type_t *static_global_type =
      ps_gvar_get_decl_type(static_result.global);
  psx_type_t *static_alias_type =
      ps_lvar_get_decl_type(static_result.alias);
  ASSERT_EQ(3, static_global_type->array_len);
  ASSERT_EQ(12, ps_type_sizeof(static_global_type));
  ASSERT_EQ(3, static_alias_type->array_len);
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
  ASSERT_TRUE(!ps_node_integer_promotion_is_unsigned(promoted_signed_shift->lhs));
  ASSERT_TRUE(!ps_node_shift_operation_is_unsigned(promoted_signed_shift));

    node_t *promoted_unsigned_shift = parse_expr_input("(unsigned int)a >> 1");
  ASSERT_EQ(ND_SHR, promoted_unsigned_shift->kind);
  ASSERT_TRUE(ps_node_integer_promotion_is_unsigned(promoted_unsigned_shift->lhs));
  ASSERT_TRUE(ps_node_shift_operation_is_unsigned(promoted_unsigned_shift));

    node_t *forced_signed_shift = parse_expr_input("(int)(unsigned long)a");
  ASSERT_EQ(ND_CAST, forced_signed_shift->kind);
  ASSERT_EQ(ND_SHR, forced_signed_shift->lhs->kind);
  ASSERT_EQ(ND_SHL, forced_signed_shift->lhs->lhs->kind);
  ASSERT_TRUE(!ps_node_shift_operation_is_unsigned(forced_signed_shift->lhs->lhs));
  ASSERT_TRUE(!ps_node_shift_operation_is_unsigned(forced_signed_shift->lhs));
  ASSERT_TRUE(!ps_node_conversion_value_is_unsigned(forced_signed_shift));

    node_t *forced_signed_keyword_shift = parse_expr_input("(signed)(unsigned long)a");
  ASSERT_EQ(ND_CAST, forced_signed_keyword_shift->kind);
  ASSERT_EQ(ND_SHR, forced_signed_keyword_shift->lhs->kind);
  ASSERT_EQ(ND_SHL, forced_signed_keyword_shift->lhs->lhs->kind);
  ASSERT_TRUE(!ps_node_shift_operation_is_unsigned(forced_signed_keyword_shift->lhs->lhs));
  ASSERT_TRUE(!ps_node_shift_operation_is_unsigned(forced_signed_keyword_shift->lhs));
  ASSERT_TRUE(!ps_node_conversion_value_is_unsigned(forced_signed_keyword_shift));

    node_t *forced_unsigned_shift = parse_expr_input("(unsigned)(long)a");
  ASSERT_EQ(ND_CAST, forced_unsigned_shift->kind);
  ASSERT_EQ(ND_SHR, forced_unsigned_shift->lhs->kind);
  ASSERT_EQ(ND_SHL, forced_unsigned_shift->lhs->lhs->kind);
  ASSERT_TRUE(ps_node_shift_operation_is_unsigned(forced_unsigned_shift->lhs->lhs));
  ASSERT_TRUE(ps_node_shift_operation_is_unsigned(forced_unsigned_shift->lhs));
  ASSERT_TRUE(ps_node_conversion_value_is_unsigned(forced_unsigned_shift));
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
  ASSERT_TRUE(ps_node_get_type(voidcast)->kind == PSX_TYPE_VOID);
  ASSERT_EQ(ND_NUM, voidcast->lhs->kind);
  ASSERT_EQ(1, as_num(voidcast->lhs)->val);

  node_t *ptr_const_cast = parse_expr_input("(int *)0x1000");
  ASSERT_EQ(ND_CAST, ptr_const_cast->kind);
  ASSERT_TRUE(ps_node_is_pointer(ptr_const_cast));
  ASSERT_EQ(ND_NUM, ptr_const_cast->lhs->kind);
  ASSERT_EQ(0x1000, as_num(ptr_const_cast->lhs)->val);

  node_t *ptrarr_cast = parse_expr_input("(double (*)[2])0");
  ASSERT_EQ(ND_CAST, ptrarr_cast->kind);
  ASSERT_TRUE(ps_node_is_pointer(ptrarr_cast));
  ASSERT_EQ(8, ps_node_type_size(ptrarr_cast));
  ASSERT_EQ(16, ps_node_deref_size(ptrarr_cast));
  ASSERT_EQ(8, ps_node_base_deref_size(ptrarr_cast));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_node_pointee_fp_kind(ptrarr_cast));
  ASSERT_TRUE(ps_node_get_type(ptrarr_cast) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(ptrarr_cast)->kind);
  ASSERT_TRUE(ps_node_get_type(ptrarr_cast)->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(ptrarr_cast)->base->kind);

  node_t *ptrptr_cast = parse_expr_input("(int **)0");
  ASSERT_EQ(ND_CAST, ptrptr_cast->kind);
  ASSERT_TRUE(ps_node_is_pointer(ptrptr_cast));
  ASSERT_EQ(8, ps_node_deref_size(ptrptr_cast));
  ASSERT_TRUE(ps_node_get_type(ptrptr_cast) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(ptrptr_cast)->kind);
  ASSERT_EQ(2, ps_node_pointer_qual_levels(ptrptr_cast));
  ASSERT_TRUE(ps_node_get_type(ptrptr_cast)->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(ptrptr_cast)->base->kind);
  ASSERT_EQ(4, ps_type_deref_size(ps_node_get_type(ptrptr_cast)->base));
  ASSERT_TRUE(ps_node_get_type(ptrptr_cast)->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, ps_node_get_type(ptrptr_cast)->base->base->kind);

  node_t *long_ptrarr_cast = parse_expr_input("(long (*)[2])0");
  ASSERT_EQ(ND_CAST, long_ptrarr_cast->kind);
  ASSERT_TRUE(ps_node_is_pointer(long_ptrarr_cast));
  ASSERT_EQ(16, ps_node_deref_size(long_ptrarr_cast));
  ASSERT_TRUE(ps_node_get_type(long_ptrarr_cast) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(long_ptrarr_cast)->kind);
  ASSERT_TRUE(ps_node_get_type(long_ptrarr_cast)->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(long_ptrarr_cast)->base->kind);
  ASSERT_TRUE(ps_node_get_type(long_ptrarr_cast)->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, ps_node_get_type(long_ptrarr_cast)->base->base->kind);

  node_t *long_ptr_elem_cast = parse_expr_input("(long * (*)[2])0");
  ASSERT_EQ(ND_CAST, long_ptr_elem_cast->kind);
  ASSERT_TRUE(ps_node_is_pointer(long_ptr_elem_cast));
  ASSERT_EQ(16, ps_node_deref_size(long_ptr_elem_cast));
  ASSERT_TRUE(ps_node_get_type(long_ptr_elem_cast) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(long_ptr_elem_cast)->kind);
  ASSERT_TRUE(ps_node_get_type(long_ptr_elem_cast)->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(long_ptr_elem_cast)->base->kind);
  ASSERT_TRUE(ps_node_get_type(long_ptr_elem_cast)->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(long_ptr_elem_cast)->base->base->kind);

  node_t *ptr_elem_2d_cast = parse_expr_input("(int * (*)[2][3])0");
  ASSERT_EQ(ND_CAST, ptr_elem_2d_cast->kind);
  ASSERT_TRUE(ps_node_is_pointer(ptr_elem_2d_cast));
  ASSERT_EQ(8, ps_node_type_size(ptr_elem_2d_cast));
  ASSERT_EQ(48, ps_node_deref_size(ptr_elem_2d_cast));
  ASSERT_EQ(4, ps_node_base_deref_size(ptr_elem_2d_cast));
  ASSERT_TRUE(ps_node_get_type(ptr_elem_2d_cast) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(ptr_elem_2d_cast)->kind);
  ASSERT_TRUE(ps_node_get_type(ptr_elem_2d_cast)->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(ptr_elem_2d_cast)->base->kind);
  ASSERT_TRUE(ps_node_get_type(ptr_elem_2d_cast)->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(ptr_elem_2d_cast)->base->base->kind);
  ASSERT_TRUE(ps_node_get_type(ptr_elem_2d_cast)->base->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER,
            ps_node_get_type(ptr_elem_2d_cast)->base->base->base->kind);
  int ptr_elem_2d_inner = 0;
  int ptr_elem_2d_next = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(ptr_elem_2d_cast,
                                               &ptr_elem_2d_inner,
                                               &ptr_elem_2d_next,
                                               NULL, NULL));
  ASSERT_EQ(24, ptr_elem_2d_inner);
  ASSERT_EQ(8, ptr_elem_2d_next);

  node_t *uchar_ptrarr_cast = parse_expr_input("(unsigned char (*)[3])0");
  ASSERT_EQ(ND_CAST, uchar_ptrarr_cast->kind);
  ASSERT_TRUE(ps_node_is_pointer(uchar_ptrarr_cast));
  ASSERT_EQ(3, ps_node_deref_size(uchar_ptrarr_cast));
  ASSERT_EQ(1, ps_node_base_deref_size(uchar_ptrarr_cast));
  ASSERT_TRUE(ps_node_pointee_is_unsigned(uchar_ptrarr_cast));

  node_t *bool_ptrarr_cast = parse_expr_input("(_Bool (*)[2])0");
  ASSERT_EQ(ND_CAST, bool_ptrarr_cast->kind);
  ASSERT_TRUE(ps_node_is_pointer(bool_ptrarr_cast));
  ASSERT_EQ(2, ps_node_deref_size(bool_ptrarr_cast));
  ASSERT_EQ(1, ps_node_base_deref_size(bool_ptrarr_cast));
  ASSERT_TRUE(ps_node_pointee_is_bool(bool_ptrarr_cast));

  node_t *bool_ptr_cast = parse_expr_input("(_Bool *)0");
  ASSERT_EQ(ND_CAST, bool_ptr_cast->kind);
  ASSERT_TRUE(ps_node_is_pointer(bool_ptr_cast));
  ASSERT_TRUE(ps_node_pointee_is_bool(bool_ptr_cast));
  ASSERT_EQ(1, ps_node_deref_size(bool_ptr_cast));

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
  ASSERT_TRUE(long_unsigned_int_cast->widen_zext_i64);
  ASSERT_TRUE(ps_node_i64_widen_source_is_unsigned(long_unsigned_int_cast->lhs));

  node_t *long_signed_int_cast = parse_expr_input("(long)(int)a");
  ASSERT_EQ(ND_CAST, long_signed_int_cast->kind);
  ASSERT_TRUE(!long_signed_int_cast->widen_zext_i64);
  ASSERT_TRUE(!ps_node_i64_widen_source_is_unsigned(long_signed_int_cast->lhs));

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
  ASSERT_TRUE(long_unsigned_char_cast->widen_zext_i64);
  ASSERT_TRUE(ps_node_integer_value_is_unsigned(long_unsigned_char_cast->lhs));

  node_t *long_unsigned_short_cast = parse_expr_input("(long)(unsigned short)a");
  ASSERT_EQ(ND_CAST, long_unsigned_short_cast->kind);
  ASSERT_TRUE(long_unsigned_short_cast->widen_zext_i64);
  ASSERT_TRUE(ps_node_integer_value_is_unsigned(long_unsigned_short_cast->lhs));

  node_t *long_signed_short_cast = parse_expr_input("(long)(short)a");
  ASSERT_EQ(ND_CAST, long_signed_short_cast->kind);
  ASSERT_TRUE(!long_signed_short_cast->widen_zext_i64);
  ASSERT_TRUE(!ps_node_integer_value_is_unsigned(long_signed_short_cast->lhs));

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

  parsed_code = parse_program_input(
      "typedef double ExprCanonicalParam; "
      "int main(void){ return _Generic("
      "(int (*)(ExprCanonicalParam, int *, ...))0, "
      "int (*)(ExprCanonicalParam, int *, ...): 53, default: 7); }");
  node_t *canonical_generic_return =
      as_block(as_func(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, canonical_generic_return->kind);
  ASSERT_EQ(53, as_num(canonical_generic_return->lhs)->val);
  node_t *canonical_expr_funcptr = parse_expr_input_with_existing_locals(
      "(int (*)(ExprCanonicalParam, int *, ...))0");
  const psx_type_t *canonical_expr_function =
      ps_type_find_function(ps_node_get_type(canonical_expr_funcptr));
  ASSERT_TRUE(canonical_expr_function != NULL);
  ASSERT_EQ(2, canonical_expr_function->param_count);
  ASSERT_TRUE(canonical_expr_function->is_variadic_function);
  ASSERT_EQ(PSX_TYPE_FLOAT,
            canonical_expr_function->param_types[0]->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            canonical_expr_function->param_types[0]->fp_kind);
  ASSERT_EQ(PSX_TYPE_POINTER,
            canonical_expr_function->param_types[1]->kind);

  parsed_code = parse_program_input(
      "typedef int (*unary_fn)(int); int generic_id(int x){return x;} "
      "int main(){return 0;}");
  psx_typedef_info_t unary_info = {0};
  ASSERT_TRUE(ps_ctx_find_typedef_name((char *)"unary_fn", 8, &unary_info));
  const psx_type_t *generic_id_type =
      ps_ctx_get_function_type((char *)"generic_id", 10);
  ASSERT_TRUE(generic_id_type != NULL);
  ASSERT_EQ(PSX_TYPE_FUNCTION, generic_id_type->kind);
  psx_type_t *generic_id_pointer =
      ps_type_new_pointer(ps_type_clone(generic_id_type), 0);
  const psx_type_t *unary_type = ps_ctx_typedef_decl_type(&unary_info);
  ASSERT_TRUE(unary_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, unary_type->kind);
  ASSERT_TRUE(ps_type_find_function(unary_type) != NULL);
  psx_decl_funcptr_sig_t generic_id_sig =
      ps_type_funcptr_signature(generic_id_pointer);
  psx_decl_funcptr_sig_t unary_sig = ps_type_funcptr_signature(unary_type);
  ASSERT_EQ(1, generic_id_sig.function.callable.signature.nargs_fixed);
  ASSERT_EQ(1, generic_id_sig.function.callable.signature.param_int_mask & 3);
  ASSERT_EQ(1, unary_sig.function.callable.signature.param_int_mask & 3);
  ASSERT_EQ(4, generic_id_sig.function.callable.return_shape.int_width);
  ASSERT_EQ(4, unary_sig.function.callable.return_shape.int_width);
  ASSERT_TRUE(ps_type_generic_matches(generic_id_pointer, unary_type));
  node_t *generic_id_ref = parse_expr_input("generic_id");
  const psx_type_t *generic_id_ref_type = ps_node_get_type(generic_id_ref);
  ASSERT_TRUE(generic_id_ref_type != NULL);
  psx_decl_funcptr_sig_t generic_id_ref_sig =
      ps_type_funcptr_signature(generic_id_ref_type);
  ASSERT_EQ(1, generic_id_ref_sig.function.callable.signature.nargs_fixed);
  ASSERT_EQ(1, generic_id_ref_sig.function.callable.signature.param_int_mask & 3);
  ASSERT_EQ(4, generic_id_ref_sig.function.callable.return_shape.int_width);
  ASSERT_TRUE(ps_type_generic_matches(generic_id_ref_type, unary_type));
  parsed_code = parse_program_input(
      "typedef int (*unary_fn)(int); int id(int x){return x;} "
      "int main(){return _Generic(id, unary_fn:9, int:4, default:5);}");
  node_t *typedef_funcref_return =
      as_block(as_func(parsed_code[1])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, typedef_funcref_return->kind);
  ASSERT_EQ(ND_NUM, typedef_funcref_return->lhs->kind);
  ASSERT_EQ(9, as_num(typedef_funcref_return->lhs)->val);

    node_t *g1 = parse_expr_input("_Generic(1, int: 11, default: 22)");
  ASSERT_EQ(ND_NUM, g1->kind);
  ASSERT_EQ(11, as_num(g1)->val);

    node_t *g2 = parse_expr_input("_Generic(1.0, float: 11, double: 33, default: 22)");
  ASSERT_EQ(ND_NUM, g2->kind);
  ASSERT_EQ(33, as_num(g2)->val);

  node_t *generic_atomic_scalar =
      parse_expr_input("_Generic(1, _Atomic(int): 41, default: 42)");
  ASSERT_EQ(ND_NUM, generic_atomic_scalar->kind);
  expect_parse_ok(
      "main(){ int *p=0; return _Generic(p, _Atomic(int *):1, default:2); }");
  expect_parse_ok(
      "main(){ return _Generic(1, _Atomic(_Atomic(int)):1, default:2); }");

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

  parsed_code = parse_program_input(
      "double fd(double x){ return x; } "
      "main(){ return _Generic(fd, double (*)(double): 17, default: 7); }");
  node_t *ret_func_designator = as_block(as_func(parsed_code[1])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret_func_designator->kind);
  ASSERT_EQ(ND_NUM, ret_func_designator->lhs->kind);
  ASSERT_EQ(17, as_num(ret_func_designator->lhs)->val);

  parsed_code = parse_program_input(
      "int fg(int x){ return x; } "
      "main(){ int (*p)(int)=fg; return _Generic((p), int (*)(int): 19, default: 7); }");
  node_t *ret_parenthesized_fp = as_block(as_func(parsed_code[1])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret_parenthesized_fp->kind);
  ASSERT_EQ(ND_NUM, ret_parenthesized_fp->lhs->kind);
  ASSERT_EQ(19, as_num(ret_parenthesized_fp->lhs)->val);

  parsed_code = parse_program_input(
      "int (*__tm_gen_rowfn(void))[3] { return 0; } "
      "main(){ int (*(*p)(void))[3]=__tm_gen_rowfn; "
      "return _Generic((p), int (*(*)(void))[3]: 23, default: 7); }");
  node_t *ret_parenthesized_nested_fp =
      as_block(as_func(parsed_code[1])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret_parenthesized_nested_fp->kind);
  ASSERT_EQ(ND_NUM, ret_parenthesized_nested_fp->lhs->kind);
  ASSERT_EQ(23, as_num(ret_parenthesized_nested_fp->lhs)->val);

  parsed_code = parse_program_input(
      "int (*__tm_gen_growfn(void))[3] { return 0; } "
      "int (*(*__tm_gen_gfp)(void))[3]; "
      "main(){ return _Generic((__tm_gen_gfp), int (*(*)(void))[3]: 29, default: 7); }");
  node_t *ret_parenthesized_global_nested_fp =
      as_block(as_func(parsed_code[1])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret_parenthesized_global_nested_fp->kind);
  ASSERT_EQ(ND_NUM, ret_parenthesized_global_nested_fp->lhs->kind);
  ASSERT_EQ(29, as_num(ret_parenthesized_global_nested_fp->lhs)->val);

  psx_decl_reset_locals();
  char synthetic_nested_name[] = "p";
  lvar_t *synthetic_nested =
      psx_decl_register_lvar_sized(synthetic_nested_name, 1, 8, 4, 0);
  psx_funcptr_signature_t synthetic_nested_suffix = {0};
  psx_decl_funcptr_sig_t synthetic_nested_sig =
      ps_decl_make_funcptr_sig_from_kind(
          &synthetic_nested_suffix, TK_INT, TK_FLOAT_KIND_NONE, 1, 0, 0,
          ps_ret_pointee_array_make(3, 0, 4));
  psx_type_t *synthetic_nested_type = ps_type_new_pointer(
      ps_type_new_integer(TK_INT, 4, 0), 4);
  synthetic_nested_type = ps_type_attach_funcptr_signature(
      synthetic_nested_type, synthetic_nested_sig);
  psx_decl_set_lvar_decl_type(synthetic_nested, synthetic_nested_type);
  node_t *ret_structural_nested_fp = parse_expr_input_with_existing_locals(
      "_Generic(p, int (*(*)(void))[3]: 31, default: 7)");
  ASSERT_EQ(ND_NUM, ret_structural_nested_fp->kind);
  ASSERT_EQ(31, as_num(ret_structural_nested_fp)->val);

  psx_decl_reset_locals();
  char synthetic_ret_funcptr_name[] = "q";
  lvar_t *synthetic_ret_funcptr =
      psx_decl_register_lvar_sized(synthetic_ret_funcptr_name, 1, 8, 4, 0);
  psx_funcptr_signature_t synthetic_ret_funcptr_suffix = {0};
  psx_decl_funcptr_sig_t synthetic_ret_funcptr_sig =
      ps_decl_make_funcptr_sig_from_kind(
          &synthetic_ret_funcptr_suffix, TK_INT, TK_FLOAT_KIND_NONE, 0, 1, 0,
          (psx_ret_pointee_array_t){0});
  psx_funcptr_signature_t synthetic_returned_sig = {0};
  synthetic_returned_sig.param_int_mask = 1;
  ps_decl_funcptr_sig_promote_return_to_funcptr(&synthetic_ret_funcptr_sig,
                                                 &synthetic_returned_sig);
  psx_type_t *synthetic_ret_funcptr_type = ps_type_new_pointer(
      ps_type_new_integer(TK_INT, 4, 0), 4);
  synthetic_ret_funcptr_type = ps_type_attach_funcptr_signature(
      synthetic_ret_funcptr_type, synthetic_ret_funcptr_sig);
  psx_decl_set_lvar_decl_type(synthetic_ret_funcptr,
                              synthetic_ret_funcptr_type);
  ASSERT_TRUE(synthetic_ret_funcptr_sig.function.returned_funcptr.type != NULL);
  synthetic_ret_funcptr_sig.function.returned_funcptr.type->callable.return_shape.int_width = 8;
  psx_type_t *synthetic_ret_funcptr_ty =
      ps_lvar_get_decl_type(synthetic_ret_funcptr);
  ASSERT_TRUE(synthetic_ret_funcptr_ty != NULL);
  ASSERT_EQ(1, synthetic_ret_funcptr_ty->funcptr_sig.function.returned_funcptr.is_funcptr);
  ASSERT_EQ(1, synthetic_ret_funcptr_ty->funcptr_sig.function.returned_funcptr.type->callable.signature.param_int_mask);
  ASSERT_EQ(4, synthetic_ret_funcptr_ty->funcptr_sig.function.returned_funcptr.type->callable.return_shape.int_width);
  ASSERT_EQ(0, synthetic_ret_funcptr_ty->funcptr_sig.function.callable.return_shape.int_width);
  node_t *synthetic_ret_funcptr_ref =
      ps_node_new_lvar_identifier_ref_for(synthetic_ret_funcptr);
  ASSERT_EQ(1, ps_node_funcptr_sig(synthetic_ret_funcptr_ref).function.returned_funcptr.is_funcptr);
  ASSERT_EQ(1, ps_node_funcptr_sig(synthetic_ret_funcptr_ref)
                   .function.returned_funcptr.type->callable.signature.param_int_mask);
  ASSERT_EQ(4, ps_node_funcptr_sig(synthetic_ret_funcptr_ref)
                   .function.returned_funcptr.type->callable.return_shape.int_width);
  ASSERT_EQ(0, ps_node_funcptr_sig(synthetic_ret_funcptr_ref).function.callable.return_shape.int_width);
  node_t *ret_structural_ret_funcptr = parse_expr_input_with_existing_locals(
      "_Generic(q, int (*(*)(void))(int): 37, default: 7)");
  ASSERT_EQ(ND_NUM, ret_structural_ret_funcptr->kind);
  ASSERT_EQ(37, as_num(ret_structural_ret_funcptr)->val);
  node_t *ret_structural_ret_funcptr_nomatch = parse_expr_input_with_existing_locals(
      "_Generic(q, int (*(*)(void))(double): 41, default: 7)");
  ASSERT_EQ(ND_NUM, ret_structural_ret_funcptr_nomatch->kind);
  ASSERT_EQ(7, as_num(ret_structural_ret_funcptr_nomatch)->val);
  node_t *ret_structural_ret_funcptr_ret_nomatch = parse_expr_input_with_existing_locals(
      "_Generic(q, double (*(*)(void))(int): 43, default: 7)");
  ASSERT_EQ(ND_NUM, ret_structural_ret_funcptr_ret_nomatch->kind);
  ASSERT_EQ(7, as_num(ret_structural_ret_funcptr_ret_nomatch)->val);

  psx_decl_reset_locals();
  char synthetic_double_ret_funcptr_name[] = "r";
  lvar_t *synthetic_double_ret_funcptr =
      psx_decl_register_lvar_sized(synthetic_double_ret_funcptr_name, 1, 8, 8, 0);
  psx_decl_funcptr_sig_t synthetic_double_ret_funcptr_sig =
      ps_decl_make_funcptr_sig_from_kind(
          &synthetic_ret_funcptr_suffix, TK_DOUBLE, TK_FLOAT_KIND_DOUBLE, 0, 1, 0,
          (psx_ret_pointee_array_t){0});
  ps_decl_funcptr_sig_promote_return_to_funcptr(&synthetic_double_ret_funcptr_sig,
                                                 &synthetic_returned_sig);
  psx_type_t *synthetic_double_ret_funcptr_type = ps_type_new_pointer(
      ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8), 8);
  synthetic_double_ret_funcptr_type = ps_type_attach_funcptr_signature(
      synthetic_double_ret_funcptr_type, synthetic_double_ret_funcptr_sig);
  psx_decl_set_lvar_decl_type(synthetic_double_ret_funcptr,
                              synthetic_double_ret_funcptr_type);
  psx_type_t *synthetic_double_ret_funcptr_ty =
      ps_lvar_get_decl_type(synthetic_double_ret_funcptr);
  ASSERT_TRUE(synthetic_double_ret_funcptr_ty != NULL);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            synthetic_double_ret_funcptr_ty->funcptr_sig.function.returned_funcptr.type->callable.return_shape.fp_kind);
  node_t *ret_structural_double_ret_funcptr = parse_expr_input_with_existing_locals(
      "_Generic(r, double (*(*)(void))(int): 47, default: 7)");
  ASSERT_EQ(ND_NUM, ret_structural_double_ret_funcptr->kind);
  ASSERT_EQ(47, as_num(ret_structural_double_ret_funcptr)->val);
  node_t *ret_structural_double_ret_funcptr_nomatch = parse_expr_input_with_existing_locals(
      "_Generic(r, int (*(*)(void))(int): 49, default: 7)");
  ASSERT_EQ(ND_NUM, ret_structural_double_ret_funcptr_nomatch->kind);
  ASSERT_EQ(7, as_num(ret_structural_double_ret_funcptr_nomatch)->val);

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
      "main(){ return _Generic((1 ? (char)1 : (char)2), char:1, int:2, default:3); }");
  node_t *ret_ternary_promoted_char = as_block(as_func(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret_ternary_promoted_char->kind);
  ASSERT_EQ(ND_NUM, ret_ternary_promoted_char->lhs->kind);
  ASSERT_EQ(2, as_num(ret_ternary_promoted_char->lhs)->val);

  parsed_code = parse_program_input(
      "main(){ return _Generic((1 ? (long double)1.0 : (double)2.0), long double:4, double:5, default:6); }");
  node_t *ret_ternary_long_double = as_block(as_func(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret_ternary_long_double->kind);
  ASSERT_EQ(ND_NUM, ret_ternary_long_double->lhs->kind);
  ASSERT_EQ(4, as_num(ret_ternary_long_double->lhs)->val);

  parsed_code = parse_program_input(
      "main(){ return _Generic((long double){1.0}, long double:4, double:5, default:6); }");
  node_t *ret_compound_long_double = as_block(as_func(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret_compound_long_double->kind);
  ASSERT_EQ(ND_NUM, ret_compound_long_double->lhs->kind);
  ASSERT_EQ(4, as_num(ret_compound_long_double->lhs)->val);

  parsed_code = parse_program_input(
      "main(){ return _Generic((_Complex double)1, double:5, _Complex double:4, default:6); }");
  node_t *ret_complex_cast = as_block(as_func(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret_complex_cast->kind);
  ASSERT_EQ(ND_NUM, ret_complex_cast->lhs->kind);
  ASSERT_EQ(4, as_num(ret_complex_cast->lhs)->val);

  parsed_code = parse_program_input(
      "main(){ _Complex double z=1; return _Generic(z, double:5, _Complex double:4, default:6); }");
  node_t *ret_complex_lvar = as_block(as_func(parsed_code[0])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret_complex_lvar->kind);
  ASSERT_EQ(ND_NUM, ret_complex_lvar->lhs->kind);
  ASSERT_EQ(4, as_num(ret_complex_lvar->lhs)->val);

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
  ASSERT_EQ(8, as_num(a3)->val);

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
  psx_type_t *c2_ty = ps_node_get_type(c2);
  ASSERT_TRUE(c2_ty != NULL);
  ASSERT_EQ(PSX_TYPE_COMPLEX, c2_ty->kind);
  ASSERT_EQ(16, ps_type_sizeof(c2_ty));
  ASSERT_EQ(16, ps_node_type_size(c2));

    node_t *c3 = parse_expr_input("(float _Imaginary)1");
  ASSERT_EQ(ND_INT_TO_FP, c3->kind);
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT, c3->fp_kind);
  ASSERT_EQ(ND_NUM, c3->lhs->kind);
  psx_type_t *c3_ty = ps_node_get_type(c3);
  ASSERT_TRUE(c3_ty != NULL);
  ASSERT_EQ(PSX_TYPE_COMPLEX, c3_ty->kind);
  ASSERT_EQ(8, ps_type_sizeof(c3_ty));
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
  psx_type_t *ci_ty = ps_node_get_type(ci);
  ASSERT_TRUE(ci_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, ci_ty->kind);
  ASSERT_EQ(4, ps_type_sizeof(ci_ty));
  ASSERT_TRUE(!ps_type_is_unsigned(ci_ty));
  ASSERT_TRUE(!ps_node_integer_value_is_unsigned(ci));

    node_t *cus = parse_expr_input("(unsigned short)a");
  psx_type_t *cus_ty = ps_node_get_type(cus);
  ASSERT_TRUE(cus_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, cus_ty->kind);
  ASSERT_EQ(2, ps_type_sizeof(cus_ty));
  ASSERT_EQ(2, ps_node_type_size(cus));
  ASSERT_TRUE(ps_type_is_unsigned(cus_ty));
  ASSERT_TRUE(ps_node_integer_value_is_unsigned(cus));

    node_t *cul = parse_expr_input("(unsigned long)a");
  psx_type_t *cul_ty = ps_node_get_type(cul);
  ASSERT_TRUE(cul_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, cul_ty->kind);
  ASSERT_EQ(8, ps_type_sizeof(cul_ty));
  ASSERT_EQ(8, ps_node_type_size(cul));
  ASSERT_TRUE(ps_type_is_unsigned(cul_ty));
  ASSERT_TRUE(ps_node_integer_value_is_unsigned(cul));

    node_t *cf = parse_expr_input("(float)a");
  psx_type_t *cf_ty = ps_node_get_type(cf);
  ASSERT_TRUE(cf_ty != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, cf_ty->kind);
  ASSERT_EQ(4, ps_type_sizeof(cf_ty));

    node_t *cp = parse_expr_input("(double*)a");
  psx_type_t *cp_ty = ps_node_get_type(cp);
  ASSERT_TRUE(cp_ty != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, cp_ty->kind);
  ASSERT_EQ(8, ps_node_type_size(cp));
  ASSERT_EQ(8, ps_type_deref_size(cp_ty));
  ASSERT_EQ(8, ps_node_deref_size(cp));
  ASSERT_EQ(1, ps_node_pointer_qual_levels(cp));
  ASSERT_EQ(8, ps_node_base_deref_size(cp));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_node_pointee_fp_kind(cp));
  ASSERT_TRUE(ps_node_is_pointer(cp));

    node_t *uac_promote = parse_expr_input("(unsigned char)1 + (short)2");
  psx_type_t *uac_promote_ty = ps_node_get_type(uac_promote);
  ASSERT_TRUE(uac_promote_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, uac_promote_ty->kind);
  ASSERT_EQ(4, ps_type_sizeof(uac_promote_ty));
  ASSERT_TRUE(!ps_type_is_unsigned(uac_promote_ty));
  ASSERT_EQ(4, ps_node_type_size(uac_promote));
  ASSERT_TRUE(!ps_node_integer_value_is_unsigned(uac_promote));
  ASSERT_TRUE(!ps_node_usual_arith_is_unsigned(uac_promote));

    node_t *uac_signed_wider = parse_expr_input("(unsigned int)1 + (long)-1");
  psx_type_t *uac_signed_wider_ty = ps_node_get_type(uac_signed_wider);
  ASSERT_TRUE(uac_signed_wider_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, uac_signed_wider_ty->kind);
  ASSERT_EQ(8, ps_type_sizeof(uac_signed_wider_ty));
  ASSERT_TRUE(!ps_type_is_unsigned(uac_signed_wider_ty));
  ASSERT_EQ(8, ps_node_type_size(uac_signed_wider));
  ASSERT_TRUE(!ps_node_integer_value_is_unsigned(uac_signed_wider));
  ASSERT_TRUE(!ps_node_usual_arith_is_unsigned(uac_signed_wider));

    node_t *uac_unsigned_same_width = parse_expr_input("(unsigned long)1 + (long)-1");
  psx_type_t *uac_unsigned_same_width_ty = ps_node_get_type(uac_unsigned_same_width);
  ASSERT_TRUE(uac_unsigned_same_width_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, uac_unsigned_same_width_ty->kind);
  ASSERT_EQ(8, ps_type_sizeof(uac_unsigned_same_width_ty));
  ASSERT_TRUE(ps_type_is_unsigned(uac_unsigned_same_width_ty));
  ASSERT_EQ(8, ps_node_type_size(uac_unsigned_same_width));
  ASSERT_TRUE(ps_node_integer_value_is_unsigned(uac_unsigned_same_width));
  ASSERT_TRUE(ps_node_usual_arith_is_unsigned(uac_unsigned_same_width));

    node_t *uac_long_long = parse_expr_input("((unsigned long long)9ULL) ^ ((unsigned short)3)");
  psx_type_t *uac_long_long_ty = ps_node_get_type(uac_long_long);
  ASSERT_TRUE(uac_long_long_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, uac_long_long_ty->kind);
  ASSERT_EQ(8, ps_type_sizeof(uac_long_long_ty));
  ASSERT_TRUE(ps_type_is_unsigned(uac_long_long_ty));
  ASSERT_TRUE(uac_long_long_ty->is_long_long);
  ASSERT_EQ(8, ps_node_type_size(uac_long_long));
  ASSERT_TRUE(ps_node_integer_value_is_unsigned(uac_long_long));
  ASSERT_TRUE(ps_node_usual_arith_is_unsigned(uac_long_long));

    node_t *ternary_uac = parse_expr_input("1 ? (unsigned int)1 : (long)-1");
  psx_type_t *ternary_uac_ty = ps_node_get_type(ternary_uac);
  ASSERT_TRUE(ternary_uac_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, ternary_uac_ty->kind);
  ASSERT_EQ(8, ps_type_sizeof(ternary_uac_ty));
  ASSERT_TRUE(!ps_type_is_unsigned(ternary_uac_ty));
  ASSERT_EQ(8, ps_node_type_size(ternary_uac));
  ASSERT_TRUE(!ps_node_integer_value_is_unsigned(ternary_uac));
  ASSERT_TRUE(!ps_node_usual_arith_is_unsigned(ternary_uac));

    node_t *cmp_uac = parse_expr_input("(unsigned int)1 < (long)-1");
  ASSERT_TRUE(!ps_node_usual_arith_is_unsigned(cmp_uac));
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

  parsed_code = parse_program_input(
      "int inc(int x){ return x + 1; } "
      "int main(void){ int (*fp)(int)=inc; (*(int (*)(int))fp)(1); }");
  node_t *cast_deref_call = as_block(as_func(parsed_code[1])->base.rhs)->body[1];
  ASSERT_EQ(ND_FUNCALL, cast_deref_call->kind);
  ASSERT_EQ(ND_CAST, as_func(cast_deref_call)->callee->kind);
  ASSERT_TRUE(ps_node_has_funcptr_signature(as_func(cast_deref_call)->callee));
  ASSERT_EQ(1, ps_node_pointer_qual_levels(as_func(cast_deref_call)->callee));

  parsed_code = parse_program_input(
      "int inc(int x){ return x + 1; } "
      "typedef int (*Fn)(int); "
      "int main(void){ int (*fp)(int)=inc; (*(Fn)fp)(1); }");
  node_t *typedef_cast_deref_call = as_block(as_func(parsed_code[1])->base.rhs)->body[1];
  ASSERT_EQ(ND_FUNCALL, typedef_cast_deref_call->kind);
  ASSERT_EQ(ND_CAST, as_func(typedef_cast_deref_call)->callee->kind);
  ASSERT_TRUE(ps_node_has_funcptr_signature(as_func(typedef_cast_deref_call)->callee));
  ASSERT_EQ(1, ps_node_pointer_qual_levels(as_func(typedef_cast_deref_call)->callee));
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

  parsed_code = parse_program_input(
      "typedef void VoidAlias; int no_args(VoidAlias); "
      "int main(void) { return 0; }");
  const psx_type_t *no_args_type =
      ps_ctx_get_function_type((char *)"no_args", 7);
  ASSERT_TRUE(no_args_type != NULL);
  ASSERT_EQ(PSX_TYPE_FUNCTION, no_args_type->kind);
  ASSERT_EQ(0, no_args_type->param_count);

  parsed_code = parse_program_input(
      "typedef int (*F0)(); typedef int (*F1)(F0); "
      "int nested(int (int()), F0); "
      "int nested(F1 fp, F0 arg) { return fp(arg); }");
  const psx_type_t *nested_type =
      ps_ctx_get_function_type((char *)"nested", 6);
  ASSERT_TRUE(nested_type != NULL);
  ASSERT_EQ(PSX_TYPE_FUNCTION, nested_type->kind);
  ASSERT_EQ(2, nested_type->param_count);

  expect_parse_ok(
      "typedef union __attribute__((packed)) AttrUnion { int value; } "
      "AttrUnion; int attr_fn(void) { AttrUnion value; return 0; }");
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
  ASSERT_EQ(ND_CAST, ret->lhs->kind);
  ASSERT_EQ(ND_SHR, ret->lhs->lhs->kind);
  ASSERT_EQ(ND_SHL, ret->lhs->lhs->lhs->kind);
  ASSERT_TRUE(!ps_node_shift_operation_is_unsigned(ret->lhs->lhs->lhs));
  ASSERT_TRUE(!ps_node_shift_operation_is_unsigned(ret->lhs->lhs));

  parsed_code = parse_program_input("int cast_unsigned_local(void) { unsigned u; return (int)u; }");
  ret = as_block(as_func(parsed_code[0])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_CAST, ret->lhs->kind);
  ASSERT_TRUE(!ps_node_conversion_value_is_unsigned(ret->lhs));
  ASSERT_EQ(ND_LVAR, ret->lhs->lhs->kind);
  ASSERT_TRUE(ps_node_integer_value_is_unsigned(ret->lhs->lhs));

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
  ASSERT_TRUE(ps_node_get_type(body->body[0])->kind == PSX_TYPE_VOID);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, body->body[0]->fp_kind);
  ASSERT_EQ(ND_LVAR, body->body[0]->lhs->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            ps_node_value_fp_kind(body->body[0]->lhs));
  ret = body->body[1];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_LVAR, ret->lhs->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_node_value_fp_kind(ret->lhs));

  parsed_code = parse_program_input("unsigned char unarrow(int x) { return x; }");
  ret = as_block(as_func(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_CAST, ret->lhs->kind);
  ASSERT_EQ(ND_BITAND, ret->lhs->lhs->kind);
  ASSERT_EQ(ND_NUM, ret->lhs->lhs->rhs->kind);
  ASSERT_EQ(0xff, as_num(ret->lhs->lhs->rhs)->val);

  parsed_code = parse_program_input(
      "struct __ret_meta_s { int a; int b; } __ret_meta_struct(void) { "
      "struct __ret_meta_s r; return r; }");
  node_func_t *ret_meta_fn = as_func(parsed_code[0]);
  ret = as_block(ret_meta_fn->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_TRUE(ps_node_get_type((node_t *)ret_meta_fn) != NULL);
  ASSERT_EQ(8, ps_node_aggregate_value_size((node_t *)ret_meta_fn));
  psx_semantic_analyze_function((node_t *)ret_meta_fn, NULL);
  ASSERT_EQ(8, ps_node_aggregate_value_size((node_t *)ret_meta_fn));

  parsed_code =
      parse_program_input("_Complex double __ret_meta_complex(void) { return 1; }");
  ret_meta_fn = as_func(parsed_code[0]);
  ret = as_block(ret_meta_fn->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ret->fp_kind);
  ASSERT_TRUE(ps_node_get_type((node_t *)ret_meta_fn) != NULL);
  ret_meta_fn->base.fp_kind = TK_FLOAT_KIND_NONE;
  ret_meta_fn->base.is_complex = 0;
  ret->fp_kind = TK_FLOAT_KIND_NONE;
  psx_semantic_analyze_function((node_t *)ret_meta_fn, NULL);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ret_meta_fn->base.fp_kind);
  ASSERT_EQ(1, ret_meta_fn->base.is_complex);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ret->fp_kind);
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
  ASSERT_EQ(4, ps_node_type_size(assign->lhs));
  ASSERT_EQ(ND_ADD, assign->lhs->lhs->kind);

  node_t *ret = body->body[3];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_DEREF, ret->lhs->kind);

  parsed_code = parse_program_input(
      "int f(void) { static struct { int n; int m; } s = {3, 4}; "
      "s.n += 1; return s.n + s.m; }");
  node_func_t *fn = as_func(parsed_code[0]);
  lvar_t *anonymous_static = find_func_lvar(fn, "s");
  ASSERT_TRUE(anonymous_static != NULL);
  ASSERT_TRUE(anonymous_static->decl_type != NULL);
  ASSERT_TRUE(anonymous_static->decl_type->aggregate_definition != NULL);
  ASSERT_TRUE(ps_type_find_aggregate_member(
                  anonymous_static->decl_type, TK_STRUCT,
                  anonymous_static->decl_type->tag_name,
                  anonymous_static->decl_type->tag_len, "n", 1) != NULL);

  parsed_code = parse_program_input(
      "typedef unsigned char u8; "
      "main() { struct S { u8 a; }; struct S s; return s.a; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ret = NULL;
  for (int i = 0; body->body[i]; i++) {
    if (body->body[i]->kind == ND_RETURN) {
      ret = body->body[i];
      break;
    }
  }
  ASSERT_TRUE(ret != NULL);
  ASSERT_EQ(ND_CAST, ret->lhs->kind);
  ASSERT_EQ(ND_DEREF, ret->lhs->lhs->kind);
  ASSERT_TRUE(ps_node_conversion_value_is_unsigned(ret->lhs->lhs));

  parsed_code = parse_program_input(
      "typedef unsigned char u8; "
      "main() { struct S { u8 a; }; struct S s; return (int)s.a; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ret = NULL;
  for (int i = 0; body->body[i]; i++) {
    if (body->body[i]->kind == ND_RETURN) {
      ret = body->body[i];
      break;
    }
  }
  ASSERT_TRUE(ret != NULL);
  ASSERT_TRUE(ps_node_conversion_value_is_unsigned(
      ret->lhs->kind == ND_CAST ? ret->lhs->lhs : ret->lhs));

  parsed_code = parse_program_input(
      "typedef unsigned char u8; "
      "main() { struct S { u8 a; }; struct S s; return (signed)s.a; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ret = NULL;
  for (int i = 0; body->body[i]; i++) {
    if (body->body[i]->kind == ND_RETURN) {
      ret = body->body[i];
      break;
    }
  }
  ASSERT_TRUE(ret != NULL);
  ASSERT_TRUE(ps_node_conversion_value_is_unsigned(
      ret->lhs->kind == ND_CAST ? ret->lhs->lhs : ret->lhs));

  parsed_code = parse_program_input("main() { struct S { int a; int b; }; struct S s; struct S *p; p=&s; p->b=5; return p->b; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  assign = body->body[4];
  ASSERT_EQ(ND_ASSIGN, assign->kind);
  ASSERT_EQ(ND_DEREF, assign->lhs->kind);
  ASSERT_EQ(4, ps_node_type_size(assign->lhs));

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
  ASSERT_TRUE(ps_node_deref_decays_to_address(array_member));
  ASSERT_EQ(1, ps_node_type_size(array_member));
  ASSERT_EQ(1, ps_node_deref_size(array_member));

  parsed_code = parse_program_input(
      "int main(void) { struct B { signed int x:3; }; "
      "struct B b; return b.x; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ret = NULL;
  for (int i = 0; body->body[i]; i++) {
    if (body->body[i]->kind == ND_RETURN) ret = body->body[i];
  }
  ASSERT_TRUE(ret != NULL);
  node_t *bitfield = ret->lhs;
  int bit_width = 0;
  int bit_offset = 0;
  int bit_is_signed = 0;
  ASSERT_TRUE(ps_node_bitfield_info(bitfield, &bit_width, &bit_offset,
                                     &bit_is_signed));
  ASSERT_EQ(3, bit_width);
  ASSERT_TRUE(ps_node_bitfield_info(bitfield, &bit_width, &bit_offset,
                                     &bit_is_signed));
  ASSERT_EQ(3, bit_width);
  ASSERT_EQ(1, bit_is_signed);

  parsed_code = parse_program_input(
      "enum CanonicalBitfieldEnum { CanonicalBitfieldValue }; "
      "typedef enum CanonicalBitfieldEnum CanonicalBitfieldEnumType; "
      "int main(void) { struct B { CanonicalBitfieldEnumType x:3; }; "
      "struct B b; return b.x; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ret = NULL;
  for (int i = 0; body->body[i]; i++) {
    if (body->body[i]->kind == ND_RETURN) ret = body->body[i];
  }
  ASSERT_TRUE(ret != NULL);
  bitfield = ret->lhs;
  ASSERT_TRUE(ps_node_bitfield_info(bitfield, &bit_width, &bit_offset,
                                     &bit_is_signed));
  ASSERT_EQ(3, bit_width);
  ASSERT_EQ(0, bit_is_signed);
}

static void test_expr_string() {
  printf("test_expr_string...\n");
    node_t *node = parse_expr_input("\"hello\"");

  ASSERT_EQ(ND_STRING, node->kind);
  ASSERT_TRUE(as_string(node)->string_label != NULL);
  ASSERT_TRUE(node->type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, node->type->kind);
  ASSERT_TRUE(node->type->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, node->type->base->kind);
  ASSERT_EQ(TK_CHAR, node->type->base->scalar_kind);
  // 文字列テーブルに登録されている
  string_lit_t *lit = ps_find_string_lit_by_label(as_string(node)->string_label);
  ASSERT_TRUE(lit != NULL);
  ASSERT_EQ(5, lit->len);
  ASSERT_TRUE(strncmp(lit->str, "hello", 5) == 0);
}

static void test_expr_concat_string() {
  printf("test_expr_concat_string...\n");
    node_t *node = parse_expr_input("\"he\" \"llo\"");

  ASSERT_EQ(ND_STRING, node->kind);
  string_lit_t *lit = ps_find_string_lit_by_label(as_string(node)->string_label);
  ASSERT_TRUE(lit != NULL);
  ASSERT_EQ(5, lit->len);
  ASSERT_TRUE(strncmp(lit->str, "hello", 5) == 0);
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
    global_var_t *gv = ps_find_global_var("a", 1);
    ASSERT_TRUE(gv != NULL);
    ASSERT_EQ(1, gv->is_extern_decl);
    ASSERT_EQ(1, ps_gvar_is_array(gv));
    ASSERT_EQ(0, gv->type_size);
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
  ASSERT_TRUE(!ps_node_get_type(body->body[0]->lhs)->is_const_qualified);
  ASSERT_TRUE(!ps_node_get_type(body->body[0]->lhs)->is_volatile_qualified);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("main() { enum E { A=1 }; return (enum E)42; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("main() { const int cx=1; volatile int vx=2; return cx+vx; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_TRUE(ps_node_get_type(body->body[0]->lhs)->is_const_qualified);
  ASSERT_TRUE(!ps_node_get_type(body->body[0]->lhs)->is_volatile_qualified);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_TRUE(!ps_node_get_type(body->body[1]->lhs)->is_const_qualified);
  ASSERT_TRUE(ps_node_get_type(body->body[1]->lhs)->is_volatile_qualified);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("main() { int *const pc=0; int *volatile pv=0; return (pc==0)+(pv==0); }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(1u, ps_node_pointer_const_qual_mask(body->body[0]->lhs));
  ASSERT_EQ(0u, ps_node_pointer_volatile_qual_mask(body->body[0]->lhs));
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(0u, ps_node_pointer_const_qual_mask(body->body[1]->lhs));
  ASSERT_EQ(1u, ps_node_pointer_volatile_qual_mask(body->body[1]->lhs));
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("main() { int *const *volatile pp=0; return pp==0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(2, ps_node_pointer_qual_levels(body->body[0]->lhs));
  ASSERT_EQ(1u, ps_node_pointer_const_qual_mask(body->body[0]->lhs));
  ASSERT_EQ(2u, ps_node_pointer_volatile_qual_mask(body->body[0]->lhs));
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("main() { int x=42; int *p=&x; int **pp=&p; return **pp; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[2]->kind);
  ASSERT_EQ(ND_RETURN, body->body[3]->kind);
  ASSERT_EQ(ND_DEREF, body->body[3]->lhs->kind);
  ASSERT_EQ(ND_DEREF, body->body[3]->lhs->lhs->kind);
  ASSERT_TRUE(ps_node_value_is_pointer_like(body->body[3]->lhs->lhs));
  ASSERT_TRUE(!ps_node_value_is_pointer_like(body->body[3]->lhs));
  ASSERT_EQ(8, ps_node_storage_type_size(body->body[3]->lhs->lhs));
  ASSERT_EQ(4, ps_node_storage_type_size(body->body[3]->lhs));

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
  ASSERT_EQ(ND_ASSIGN, body->body[2]->kind);
  ASSERT_EQ(ND_RETURN, body->body[3]->kind);

  parsed_code = parse_program_input("main() { struct S { int x; int y; }; struct S a={1,2}; struct S b={3,4}; struct S s=(0?a:b); return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_COMMA, body->body[2]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[3]->kind);
  ASSERT_EQ(ND_TERNARY, body->body[3]->rhs->kind);
  ASSERT_EQ(ND_RETURN, body->body[4]->kind);

  parsed_code = parse_program_input("main() { struct S { int x; int y; }; struct S t={1,2}; struct S s=(t.y=9,t); return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[2]->kind);
  ASSERT_EQ(ND_COMMA, body->body[2]->rhs->kind);
  ASSERT_EQ(ND_RETURN, body->body[3]->kind);

  parsed_code = parse_program_input("main() { struct S { int a[2]; int z; }; struct S t={{1,2},3}; struct S s=t; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[2]->kind);
  ASSERT_EQ(ND_RETURN, body->body[3]->kind);

  parsed_code = parse_program_input("main() { struct I { int x; int y; }; struct S { struct I i; int z; }; struct S t={{1,2},3}; struct S s=t; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_NUM, body->body[1]->kind);
  ASSERT_EQ(ND_COMMA, body->body[2]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[3]->kind);
  ASSERT_EQ(ND_RETURN, body->body[4]->kind);

  parsed_code = parse_program_input("main() { union U { int x; char y; }; union U u={7}; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("main() { union U { int x; char y; }; union U u=7; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("main() { union U { int x; char y; }; union U v={7}; union U u=v; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[2]->kind);
  ASSERT_EQ(ND_RETURN, body->body[3]->kind);

  parsed_code = parse_program_input("main() { union U { int x; char y; }; union U v={7}; union U u=(v.x=9,v); return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[2]->kind);
  ASSERT_EQ(ND_COMMA, body->body[2]->rhs->kind);
  ASSERT_EQ(ND_RETURN, body->body[3]->kind);

  parsed_code = parse_program_input("main() { union U { int x; char y; }; union U u={.x=7}; return 0; }");
  body = as_block(as_func(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
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
  bool same_tag_cast_has_return = false;
  for (int i = 1; body->body[i]; i++) {
    if (body->body[i]->kind == ND_RETURN) {
      same_tag_cast_has_return = true;
      break;
    }
  }
  ASSERT_TRUE(same_tag_cast_has_return);

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
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);
}

static void test_type_metadata_bridge() {
  printf("test_type_metadata_bridge...\n");

  parsed_code = parse_program_input("main() { unsigned int x=1; return x; }");
  node_func_t *fn = as_func(parsed_code[0]);
  node_block_t *body = as_block(fn->base.rhs);
  node_t *assign = body->body[0];
  ASSERT_EQ(ND_ASSIGN, assign->kind);
  psx_type_t *unsigned_ty = ps_node_get_type(assign->lhs);
  ASSERT_TRUE(unsigned_ty != NULL);
  ASSERT_TRUE(assign->lhs->type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, unsigned_ty->kind);
  ASSERT_EQ(4, ps_type_sizeof(unsigned_ty));
  ASSERT_TRUE(ps_type_is_unsigned(unsigned_ty));
  lvar_t *x_lvar = find_func_lvar(fn, "x");
  ASSERT_TRUE(x_lvar != NULL);
  ASSERT_TRUE(ps_node_lvar_symbol(assign->lhs) == x_lvar);
  ASSERT_TRUE(x_lvar->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, x_lvar->decl_type->kind);
  ASSERT_EQ(4, ps_type_sizeof(x_lvar->decl_type));
  ASSERT_TRUE(ps_type_is_unsigned(x_lvar->decl_type));
  node_t *x_lvar_ref = ps_node_new_lvar_identifier_ref_for(x_lvar);
  ASSERT_TRUE(ps_node_is_unsigned_type(x_lvar_ref));
  node_t *x_lvar_direct_ref = ps_node_new_lvar_for(x_lvar);
  ASSERT_TRUE(ps_node_is_unsigned_type(x_lvar_direct_ref));
  psx_type_t *x_decl_a = ps_lvar_get_decl_type(x_lvar);
  ASSERT_TRUE(x_decl_a != NULL);
  x_lvar->decl_type = NULL;
  ASSERT_TRUE(ps_lvar_get_decl_type(x_lvar) == NULL);
  x_lvar->decl_type = x_decl_a;

  parsed_code = parse_program_input("int __tm_local_bool_ref(void) { _Bool b=1; return b; }");
  fn = as_func(parsed_code[0]);
  lvar_t *bool_ref_lvar = find_func_lvar(fn, "b");
  ASSERT_TRUE(bool_ref_lvar != NULL);
  ASSERT_TRUE(ps_lvar_get_decl_type(bool_ref_lvar) != NULL);
  ASSERT_EQ(PSX_TYPE_BOOL, bool_ref_lvar->decl_type->kind);
  node_t *bool_ref_node = ps_node_new_lvar_identifier_ref_for(bool_ref_lvar);
  ASSERT_TRUE(ps_node_get_type(bool_ref_node) == bool_ref_lvar->decl_type);
  node_t *bool_direct_ref = ps_node_new_lvar_for(bool_ref_lvar);
  ASSERT_TRUE(ps_node_get_type(bool_direct_ref) == bool_ref_lvar->decl_type);

  parsed_code = parse_program_input("int __tm_local_atomic_ref(void) { _Atomic int a=1; return a; }");
  fn = as_func(parsed_code[0]);
  lvar_t *atomic_ref_lvar = find_func_lvar(fn, "a");
  ASSERT_TRUE(atomic_ref_lvar != NULL);
  ASSERT_TRUE(ps_lvar_get_decl_type(atomic_ref_lvar) != NULL);
  ASSERT_TRUE(atomic_ref_lvar->decl_type->is_atomic);
  node_t *atomic_ref_node = ps_node_new_lvar_for(atomic_ref_lvar);
  ASSERT_TRUE(ps_node_get_type(atomic_ref_node) == atomic_ref_lvar->decl_type);
  ASSERT_TRUE(ps_node_get_type(atomic_ref_node)->is_atomic);

  parsed_code = parse_program_input(
      "int __tm_param_identity(const long long ll, volatile char ch, "
      "_Atomic int ai) { return 0; }");
  fn = as_func(parsed_code[0]);
  lvar_t *param_ll = find_func_lvar(fn, "ll");
  lvar_t *param_ch = find_func_lvar(fn, "ch");
  lvar_t *param_ai = find_func_lvar(fn, "ai");
  ASSERT_TRUE(param_ll != NULL && param_ll->decl_type != NULL);
  ASSERT_TRUE(param_ch != NULL && param_ch->decl_type != NULL);
  ASSERT_TRUE(param_ai != NULL && param_ai->decl_type != NULL);
  ASSERT_TRUE(param_ll->decl_type->is_const_qualified);
  ASSERT_TRUE(param_ll->decl_type->is_long_long);
  ASSERT_TRUE(param_ch->decl_type->is_volatile_qualified);
  ASSERT_TRUE(param_ch->decl_type->is_plain_char);
  ASSERT_TRUE(param_ai->decl_type->is_atomic);

  psx_type_t *typed_atomic_int = ps_type_new_integer(TK_INT, 4, 0);
  typed_atomic_int->is_atomic = 1;
  node_t typed_atomic_ptr_mem = {0};
  typed_atomic_ptr_mem.kind = ND_DEREF;
  typed_atomic_ptr_mem.type = ps_type_new_pointer(typed_atomic_int, 4);
  node_t *typed_atomic_deref =
      ps_node_new_unary_deref_for(&typed_atomic_ptr_mem);
  ASSERT_TRUE(ps_node_get_type(typed_atomic_deref) == typed_atomic_int);
  ASSERT_EQ(1, ps_node_get_type(typed_atomic_deref)->is_atomic);

  lvar_t tmp_lvar = {0};
  tmp_lvar.size = 4;
  tmp_lvar.elem_size = 4;
  psx_decl_set_lvar_decl_type(
      &tmp_lvar, ps_type_new_integer(TK_INT, 4, 0));
  psx_type_t *tmp_lvar_int = ps_lvar_get_decl_type(&tmp_lvar);
  ASSERT_TRUE(tmp_lvar_int != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, tmp_lvar_int->kind);
  ASSERT_TRUE(!ps_lvar_value_is_pointer_like(&tmp_lvar));
  tmp_lvar.size = 8;
  tmp_lvar.elem_size = 4;
  psx_decl_set_lvar_decl_type(
      &tmp_lvar,
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 4));
  ASSERT_TRUE(ps_lvar_value_is_pointer_like(&tmp_lvar));
  ASSERT_TRUE(tmp_lvar.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, tmp_lvar.decl_type->kind);
  psx_type_t *tmp_lvar_ptr = tmp_lvar.decl_type;
  ASSERT_TRUE(tmp_lvar_ptr != NULL);
  ASSERT_TRUE(tmp_lvar_ptr != tmp_lvar_int);
  ASSERT_TRUE(tmp_lvar.decl_type == tmp_lvar_ptr);
  ASSERT_EQ(PSX_TYPE_POINTER, tmp_lvar_ptr->kind);
  ASSERT_EQ(8, ps_lvar_decl_sizeof(&tmp_lvar, 99));
  ASSERT_EQ(8, ps_lvar_storage_size(&tmp_lvar, 99));

  const char nested_tag_ptr_array_name[] = "__tm_nested_tag_ptr_array";
  lvar_t tmp_lvar_nested_tag_ptr_array = {0};
  tmp_lvar_nested_tag_ptr_array.size = 16;
  tmp_lvar_nested_tag_ptr_array.elem_size = 8;
  psx_type_t *tmp_nested_tag = ps_type_new_tag(
      TK_STRUCT, (char *)nested_tag_ptr_array_name,
      (int)sizeof(nested_tag_ptr_array_name) - 1, 0, 8);
  psx_type_t *tmp_nested_ptr = ps_type_apply_pointer_derivation(
      tmp_nested_tag, 2, 8, 8, 0, 3u, 2u);
  psx_type_t *tmp_nested_array = ps_type_new_array(
      tmp_nested_ptr, 2, 16, 8, 0);
  psx_decl_set_lvar_decl_type(&tmp_lvar_nested_tag_ptr_array,
                              tmp_nested_array);
  psx_type_t *tmp_lvar_nested_tag_ptr_array_type =
      ps_lvar_get_decl_type(&tmp_lvar_nested_tag_ptr_array);
  ASSERT_TRUE(tmp_lvar_nested_tag_ptr_array_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, tmp_lvar_nested_tag_ptr_array_type->kind);
  ASSERT_EQ(2, tmp_lvar_nested_tag_ptr_array_type->array_len);
  psx_type_t *tmp_lvar_nested_tag_ptr_array_elem =
      tmp_lvar_nested_tag_ptr_array_type->base;
  ASSERT_TRUE(tmp_lvar_nested_tag_ptr_array_elem != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, tmp_lvar_nested_tag_ptr_array_elem->kind);
  ASSERT_EQ(2, ps_type_pointer_view_structural_qual_levels(
                   tmp_lvar_nested_tag_ptr_array_elem));
  ASSERT_EQ(3u, ps_type_pointer_view_structural_qual_mask(
                    tmp_lvar_nested_tag_ptr_array_elem, 0));
  ASSERT_EQ(2u, ps_type_pointer_view_structural_qual_mask(
                    tmp_lvar_nested_tag_ptr_array_elem, 1));
  ASSERT_TRUE(tmp_lvar_nested_tag_ptr_array_elem->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, tmp_lvar_nested_tag_ptr_array_elem->base->kind);
  ASSERT_EQ(1u, ps_type_pointer_view_structural_qual_mask(
                    tmp_lvar_nested_tag_ptr_array_elem->base, 0));
  ASSERT_TRUE(tmp_lvar_nested_tag_ptr_array_elem->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_STRUCT,
            tmp_lvar_nested_tag_ptr_array_elem->base->base->kind);

  lvar_t tmp_lvar_decl_type_wins = {0};
  tmp_lvar_decl_type_wins.size = 4;
  tmp_lvar_decl_type_wins.elem_size = 4;
  psx_decl_set_lvar_decl_type(
      &tmp_lvar_decl_type_wins,
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 4));
  node_t *tmp_lvar_decl_type_ref =
      ps_node_new_lvar_object_ref_for(&tmp_lvar_decl_type_wins);
  ASSERT_EQ(8, ps_node_type_size(tmp_lvar_decl_type_ref));
  ASSERT_TRUE(ps_node_get_type(tmp_lvar_decl_type_ref) ==
              tmp_lvar_decl_type_wins.decl_type);
  ASSERT_TRUE(ps_node_is_pointer(tmp_lvar_decl_type_ref));
  ASSERT_EQ(4, ps_node_deref_size(tmp_lvar_decl_type_ref));

  lvar_t tmp_lvar_scalar_decl_type_wins = {0};
  psx_type_t *tmp_lvar_scalar_canonical =
      ps_type_new_integer(TK_INT, 4, 0);
  tmp_lvar_scalar_decl_type_wins.size = 8;
  tmp_lvar_scalar_decl_type_wins.elem_size = 8;
  tmp_lvar_scalar_decl_type_wins.is_array = 1;
  tmp_lvar_scalar_decl_type_wins.decl_type = tmp_lvar_scalar_canonical;
  ASSERT_TRUE(ps_lvar_get_decl_type(&tmp_lvar_scalar_decl_type_wins) ==
              tmp_lvar_scalar_canonical);
  ASSERT_TRUE(!ps_lvar_is_array(&tmp_lvar_scalar_decl_type_wins));
  ASSERT_TRUE(!ps_lvar_is_tag_aggregate(&tmp_lvar_scalar_decl_type_wins));
  ASSERT_TRUE(!ps_lvar_value_is_pointer_like(
      &tmp_lvar_scalar_decl_type_wins));
  node_t *tmp_lvar_scalar_decl_type_ref =
      ps_node_new_lvar_for(&tmp_lvar_scalar_decl_type_wins);
  ASSERT_TRUE(ps_node_get_type(tmp_lvar_scalar_decl_type_ref) ==
              tmp_lvar_scalar_canonical);
  ASSERT_EQ(4, ps_node_type_size(tmp_lvar_scalar_decl_type_ref));
  ASSERT_TRUE(!ps_node_is_pointer(tmp_lvar_scalar_decl_type_ref));
  ASSERT_EQ(0, ps_node_deref_size(tmp_lvar_scalar_decl_type_ref));
  node_t *tmp_lvar_scalar_typed_ref =
      ps_node_new_lvar_typed_for(&tmp_lvar_scalar_decl_type_wins, 99);
  ASSERT_TRUE(ps_node_get_type(tmp_lvar_scalar_typed_ref) ==
              tmp_lvar_scalar_canonical);
  ASSERT_EQ(4, ps_node_type_size(tmp_lvar_scalar_typed_ref));
  psx_decl_funcptr_sig_t tmp_lvar_scalar_funcptr =
      ps_lvar_funcptr_sig(&tmp_lvar_scalar_decl_type_wins);
  ASSERT_TRUE(!ps_decl_funcptr_sig_has_payload(tmp_lvar_scalar_funcptr));
  node_t *tmp_lvar_scalar_funcptr_ref =
      ps_node_new_lvar_for(&tmp_lvar_scalar_decl_type_wins);
  ASSERT_TRUE(!ps_node_has_funcptr_signature(tmp_lvar_scalar_funcptr_ref));

  lvar_t tmp_lvar_ptr_array_cache_decl_type_wins = {0};
  psx_type_t *tmp_lvar_ptr_array_cache_canonical =
      ps_type_new_integer(TK_INT, 4, 0);
  tmp_lvar_ptr_array_cache_decl_type_wins.size = 8;
  tmp_lvar_ptr_array_cache_decl_type_wins.elem_size = 8;
  tmp_lvar_ptr_array_cache_decl_type_wins.outer_stride = 16;
  tmp_lvar_ptr_array_cache_decl_type_wins.decl_type =
      tmp_lvar_ptr_array_cache_canonical;
  ASSERT_TRUE(ps_lvar_get_decl_type(
                  &tmp_lvar_ptr_array_cache_decl_type_wins) ==
              tmp_lvar_ptr_array_cache_canonical);
  ASSERT_TRUE(!ps_lvar_value_is_pointer_like(
      &tmp_lvar_ptr_array_cache_decl_type_wins));
  node_t *tmp_lvar_ptr_array_cache_ref =
      ps_node_new_lvar_for(&tmp_lvar_ptr_array_cache_decl_type_wins);
  ASSERT_TRUE(ps_node_get_type(tmp_lvar_ptr_array_cache_ref) ==
              tmp_lvar_ptr_array_cache_canonical);
  ASSERT_EQ(4, ps_node_type_size(tmp_lvar_ptr_array_cache_ref));
  ASSERT_TRUE(!ps_node_is_pointer(tmp_lvar_ptr_array_cache_ref));
  ASSERT_EQ(0, ps_node_deref_size(tmp_lvar_ptr_array_cache_ref));
  ASSERT_EQ(0, ps_node_ptr_array_pointee_bytes(
                   tmp_lvar_ptr_array_cache_ref));
  node_t *tmp_lvar_ptr_array_cache_expr_ref =
      ps_node_new_lvar_expr_ref_for(&tmp_lvar_ptr_array_cache_decl_type_wins, 1);
  ASSERT_TRUE(ps_node_get_type(tmp_lvar_ptr_array_cache_expr_ref) ==
              tmp_lvar_ptr_array_cache_canonical);
  ASSERT_EQ(4, ps_node_type_size(tmp_lvar_ptr_array_cache_expr_ref));
  ASSERT_TRUE(!ps_node_is_pointer(tmp_lvar_ptr_array_cache_expr_ref));
  ASSERT_EQ(0, ps_node_deref_size(tmp_lvar_ptr_array_cache_expr_ref));
  ASSERT_EQ(0, ps_node_ptr_array_pointee_bytes(
                   tmp_lvar_ptr_array_cache_expr_ref));
  node_t *tmp_lvar_ptr_array_cache_param_ref =
      ps_node_new_param_lvar_for(&tmp_lvar_ptr_array_cache_decl_type_wins);
  ASSERT_TRUE(ps_node_get_type(tmp_lvar_ptr_array_cache_param_ref) ==
              tmp_lvar_ptr_array_cache_canonical);
  ASSERT_EQ(4, ps_node_type_size(tmp_lvar_ptr_array_cache_param_ref));
  ASSERT_TRUE(!ps_node_is_pointer(tmp_lvar_ptr_array_cache_param_ref));
  ASSERT_EQ(0, ps_node_deref_size(tmp_lvar_ptr_array_cache_param_ref));
  ASSERT_EQ(0, ps_node_ptr_array_pointee_bytes(
                   tmp_lvar_ptr_array_cache_param_ref));

  lvar_t tmp_lvar_ptr_decl_type_wins = {0};
  tmp_lvar_ptr_decl_type_wins.size = 8;
  tmp_lvar_ptr_decl_type_wins.elem_size = 8;
  tmp_lvar_ptr_decl_type_wins.outer_stride = 32;
  tmp_lvar_ptr_decl_type_wins.mid_stride = 16;
  tmp_lvar_ptr_decl_type_wins.is_vla = 1;
  tmp_lvar_ptr_decl_type_wins.vla_row_stride_frame_off = 44;
  tmp_lvar_ptr_decl_type_wins.vla_strides_remaining = 2;
  tmp_lvar_ptr_decl_type_wins.decl_type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 4);
  tmp_lvar_ptr_decl_type_wins.decl_type->outer_stride = 96;
  tmp_lvar_ptr_decl_type_wins.decl_type->mid_stride = 48;
  tmp_lvar_ptr_decl_type_wins.decl_type->ptr_array_pointee_bytes = 96;
  node_t *tmp_lvar_ptr_decl_type_ref =
      ps_node_new_lvar_identifier_ref_for(&tmp_lvar_ptr_decl_type_wins);
  ASSERT_TRUE(ps_node_get_type(tmp_lvar_ptr_decl_type_ref) ==
              tmp_lvar_ptr_decl_type_wins.decl_type);
  ASSERT_TRUE(ps_node_is_pointer(tmp_lvar_ptr_decl_type_ref));
  ASSERT_EQ(4, ps_node_deref_size(tmp_lvar_ptr_decl_type_ref));
  ASSERT_EQ(0, ps_node_ptr_array_pointee_bytes(
                   tmp_lvar_ptr_decl_type_ref));
  ASSERT_TRUE(!ps_node_pointer_stride_metadata(
      tmp_lvar_ptr_decl_type_ref, NULL, NULL, NULL, NULL));
  ASSERT_EQ(0, ps_node_vla_row_stride_frame_off(
                   tmp_lvar_ptr_decl_type_ref));
  node_t *tmp_lvar_ptr_decl_type_deref =
      ps_node_new_unary_deref_for(tmp_lvar_ptr_decl_type_ref);
  ASSERT_TRUE(ps_node_get_type(tmp_lvar_ptr_decl_type_deref) ==
              tmp_lvar_ptr_decl_type_wins.decl_type->base);
  ASSERT_TRUE(!ps_node_is_pointer(tmp_lvar_ptr_decl_type_deref));
  ASSERT_EQ(4, ps_node_type_size(tmp_lvar_ptr_decl_type_deref));
  ASSERT_EQ(0, ps_node_deref_size(tmp_lvar_ptr_decl_type_deref));
  ASSERT_EQ(0, ps_node_ptr_array_pointee_bytes(
                   tmp_lvar_ptr_decl_type_deref));
  ASSERT_TRUE(!ps_node_pointer_stride_metadata(
      tmp_lvar_ptr_decl_type_deref, NULL, NULL, NULL, NULL));
  node_t *tmp_lvar_ptr_decl_type_addr =
      ps_node_new_lvar_array_addr_for(
          &tmp_lvar_ptr_decl_type_wins, 0);
  ASSERT_TRUE(ps_node_get_type(tmp_lvar_ptr_decl_type_addr) ==
              tmp_lvar_ptr_decl_type_wins.decl_type);
  ASSERT_TRUE(ps_node_is_pointer(tmp_lvar_ptr_decl_type_addr));
  ASSERT_EQ(4, ps_node_deref_size(tmp_lvar_ptr_decl_type_addr));
  ASSERT_EQ(0, ps_node_ptr_array_pointee_bytes(
                   tmp_lvar_ptr_decl_type_addr));
  ASSERT_TRUE(!ps_node_pointer_stride_metadata(
      tmp_lvar_ptr_decl_type_addr, NULL, NULL, NULL, NULL));

  lvar_t explicit_array_addr_var = {0};
  explicit_array_addr_var.decl_type = ps_type_new_array(
      ps_type_new_integer(TK_INT, 4, 0), 3, 12, 4, 0);
  node_t *decayed_array_addr =
      ps_node_new_lvar_array_addr_for(&explicit_array_addr_var, 0);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(decayed_array_addr)->kind);
  ASSERT_EQ(PSX_TYPE_INTEGER,
            ps_node_get_type(decayed_array_addr)->base->kind);
  node_t *explicit_array_addr =
      ps_node_new_explicit_addr_value_for(decayed_array_addr);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(explicit_array_addr)->kind);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(explicit_array_addr)->base->kind);
  ASSERT_EQ(12, ps_node_deref_size(explicit_array_addr));
  ASSERT_EQ(0, ps_node_compound_literal_array_size(explicit_array_addr));

  lvar_t tmp_lvar_flat_ptr_mismatch_decl_type = {0};
  tmp_lvar_flat_ptr_mismatch_decl_type.size = 8;
  tmp_lvar_flat_ptr_mismatch_decl_type.elem_size = 8;
  tmp_lvar_flat_ptr_mismatch_decl_type.decl_type =
      ps_type_new_pointer(NULL, 8);
  tmp_lvar_flat_ptr_mismatch_decl_type.decl_type->pointer_const_qual_mask = 1u;
  tmp_lvar_flat_ptr_mismatch_decl_type.decl_type->pointer_volatile_qual_mask = 1u;
  tmp_lvar_flat_ptr_mismatch_decl_type.decl_type->base_deref_size = 4;
  tmp_lvar_flat_ptr_mismatch_decl_type.decl_type->outer_stride = 32;
  tmp_lvar_flat_ptr_mismatch_decl_type.decl_type->ptr_array_pointee_bytes = 32;
  ASSERT_EQ(0, ps_type_pointer_view_structural_ptr_array_pointee_bytes(
                   tmp_lvar_flat_ptr_mismatch_decl_type.decl_type));
  psx_type_t *tmp_structural_ptrarr_type = ps_type_new_pointer(
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 4, 16, 4, 0),
      16);
  ASSERT_EQ(16, ps_type_pointer_view_structural_ptr_array_pointee_bytes(
                    tmp_structural_ptrarr_type));
  node_t *tmp_lvar_flat_ptr_mismatch_ref =
      ps_node_new_lvar_identifier_ref_for(
          &tmp_lvar_flat_ptr_mismatch_decl_type);
  ASSERT_TRUE(ps_node_get_type(tmp_lvar_flat_ptr_mismatch_ref) ==
              tmp_lvar_flat_ptr_mismatch_decl_type.decl_type);
  ASSERT_TRUE(ps_node_is_pointer(tmp_lvar_flat_ptr_mismatch_ref));
  ASSERT_EQ(8, ps_node_deref_size(tmp_lvar_flat_ptr_mismatch_ref));
  ASSERT_EQ(0, ps_node_pointer_qual_levels(tmp_lvar_flat_ptr_mismatch_ref));
  ASSERT_EQ(0u, ps_node_pointer_const_qual_mask(tmp_lvar_flat_ptr_mismatch_ref));
  ASSERT_EQ(0u, ps_node_pointer_volatile_qual_mask(tmp_lvar_flat_ptr_mismatch_ref));
  ASSERT_EQ(0, ps_node_base_deref_size(tmp_lvar_flat_ptr_mismatch_ref));
  ASSERT_EQ(0, ps_node_ptr_array_pointee_bytes(
                   tmp_lvar_flat_ptr_mismatch_ref));
  ASSERT_TRUE(!ps_node_pointer_stride_metadata(
      tmp_lvar_flat_ptr_mismatch_ref, NULL, NULL, NULL, NULL));

  lvar_t tmp_static_local_fallback_decl_type_wins = {0};
  char *tmp_static_missing_backing_name = "__tm_missing_static_backing";
  tmp_static_local_fallback_decl_type_wins.is_static_local = 1;
  tmp_static_local_fallback_decl_type_wins.static_global_name =
      tmp_static_missing_backing_name;
  tmp_static_local_fallback_decl_type_wins.static_global_name_len =
      (int)strlen(tmp_static_missing_backing_name);
  tmp_static_local_fallback_decl_type_wins.size = 8;
  tmp_static_local_fallback_decl_type_wins.elem_size = 8;
  tmp_static_local_fallback_decl_type_wins.outer_stride = 32;
  tmp_static_local_fallback_decl_type_wins.decl_type =
      ps_type_new_integer(TK_INT, 4, 0);
  node_t *tmp_static_local_fallback_ref =
      ps_node_new_static_local_gvar_for(
          &tmp_static_local_fallback_decl_type_wins,
          tmp_static_local_fallback_decl_type_wins.size);
  ASSERT_TRUE(ps_node_get_type(tmp_static_local_fallback_ref) ==
              tmp_static_local_fallback_decl_type_wins.decl_type);
  ASSERT_EQ(4, ps_node_type_size(tmp_static_local_fallback_ref));
  ASSERT_TRUE(!ps_node_is_pointer(tmp_static_local_fallback_ref));
  ASSERT_EQ(0, ps_node_deref_size(tmp_static_local_fallback_ref));
  ASSERT_EQ(PSX_TYPE_INTEGER,
            ps_node_get_type(tmp_static_local_fallback_ref)->kind);
  ASSERT_EQ(0, ps_node_ptr_array_pointee_bytes(
                   tmp_static_local_fallback_ref));

  parsed_code = parse_program_input(
      "int __tm_sig_f(int x){ return x; } "
      "int __tm_sig_local(void) { "
      "struct LocalTag { int value; }; "
      "_Alignas(16) struct LocalTag aligned_value; "
      "typedef const struct LocalTag LocalAlias; LocalAlias alias_value; "
      "typedef double LocalSigParam; "
      "int (*p)(LocalSigParam, int *, ...); return 0; }");
  fn = as_func(parsed_code[1]);
  lvar_t *sig_lvar = find_func_lvar(fn, "p");
  lvar_t *aligned_value = find_func_lvar(fn, "aligned_value");
  lvar_t *alias_value = find_func_lvar(fn, "alias_value");
  ASSERT_TRUE(aligned_value != NULL);
  ASSERT_TRUE(alias_value != NULL);
  ASSERT_EQ(0, ps_lvar_offset(aligned_value) % 16);
  ASSERT_EQ(16, aligned_value->align_bytes);
  ASSERT_EQ(PSX_TYPE_STRUCT,
            ps_lvar_get_decl_type(aligned_value)->kind);
  ASSERT_EQ(4, ps_type_sizeof(ps_lvar_get_decl_type(aligned_value)));
  ASSERT_EQ(PSX_TYPE_STRUCT,
            ps_lvar_get_decl_type(alias_value)->kind);
  ASSERT_TRUE(ps_lvar_get_decl_type(alias_value)->is_const_qualified);
  ASSERT_TRUE(sig_lvar != NULL);
  psx_type_t *sig_lvar_type = ps_lvar_get_decl_type(sig_lvar);
  ASSERT_TRUE(sig_lvar_type != NULL);
  const psx_type_t *sig_lvar_function =
      ps_type_find_function(sig_lvar_type);
  ASSERT_TRUE(sig_lvar_function != NULL);
  ASSERT_EQ(2, sig_lvar_function->param_count);
  ASSERT_TRUE(sig_lvar_function->is_variadic_function);
  ASSERT_EQ(PSX_TYPE_FLOAT, sig_lvar_function->param_types[0]->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            sig_lvar_function->param_types[0]->fp_kind);
  ASSERT_EQ(PSX_TYPE_POINTER, sig_lvar_function->param_types[1]->kind);
  ASSERT_EQ(PSX_TYPE_INTEGER,
            sig_lvar_function->param_types[1]->base->kind);
  sig_lvar->decl_type = NULL;
  ASSERT_TRUE(ps_lvar_get_decl_type(sig_lvar) == NULL);
  sig_lvar->decl_type = sig_lvar_type;

  psx_decl_reset_locals();
  char funcptr_sync_lvar_name[] = "__tm_funcptr_sync_lvar";
  lvar_t *funcptr_sync_lvar =
      psx_decl_register_lvar_sized(funcptr_sync_lvar_name,
                                   (int)strlen(funcptr_sync_lvar_name), 8, 4, 0);
  psx_funcptr_signature_t funcptr_sync_lvar_suffix = {0};
  funcptr_sync_lvar_suffix.param_int_mask = 1;
  psx_decl_funcptr_sig_t funcptr_sync_lvar_sig =
      ps_decl_make_funcptr_sig_from_kind(
          &funcptr_sync_lvar_suffix, TK_INT, TK_FLOAT_KIND_NONE, 0, 0, 0,
          (psx_ret_pointee_array_t){0});
  psx_type_t *funcptr_sync_lvar_type = ps_type_attach_funcptr_signature(
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 4),
      funcptr_sync_lvar_sig);
  psx_decl_set_lvar_decl_type(funcptr_sync_lvar, funcptr_sync_lvar_type);
  funcptr_sync_lvar_type = ps_lvar_get_decl_type(funcptr_sync_lvar);
  ASSERT_TRUE(funcptr_sync_lvar_type != NULL);
  ASSERT_EQ(1, funcptr_sync_lvar_type->funcptr_sig.function.callable.signature.param_int_mask);
  ASSERT_EQ(4, funcptr_sync_lvar_type->funcptr_sig.function.callable.return_shape.int_width);

  global_var_t funcptr_sync_gvar = {0};
  funcptr_sync_gvar.type_size = 8;
  psx_funcptr_signature_t funcptr_sync_gvar_suffix = {0};
  funcptr_sync_gvar_suffix.param_fp_mask = 2;
  psx_decl_funcptr_sig_t funcptr_sync_gvar_sig =
      ps_decl_make_funcptr_sig_from_kind(
          &funcptr_sync_gvar_suffix, TK_DOUBLE, TK_FLOAT_KIND_DOUBLE, 0, 0, 0,
          (psx_ret_pointee_array_t){0});
  psx_type_t *funcptr_sync_gvar_type = ps_type_attach_funcptr_signature(
      ps_type_new_pointer(ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8), 8),
      funcptr_sync_gvar_sig);
  ps_decl_set_gvar_decl_type(&funcptr_sync_gvar, funcptr_sync_gvar_type);
  funcptr_sync_gvar_type = ps_gvar_get_decl_type(&funcptr_sync_gvar);
  ASSERT_TRUE(funcptr_sync_gvar_type != NULL);
  ASSERT_EQ(2, funcptr_sync_gvar_type->funcptr_sig.function.callable.signature.param_fp_mask);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            funcptr_sync_gvar_type->funcptr_sig.function.callable.return_shape.fp_kind);

  parsed_code = parse_program_input(
      "int __tm_sig_nested_local(void) { "
      "int (*(*q)(void))(int); return _Generic(q, int (*(*)(void))(int): 37, default: 7); }");
  fn = as_func(parsed_code[0]);
  lvar_t *sig_nested_lvar = find_func_lvar(fn, "q");
  ASSERT_TRUE(sig_nested_lvar != NULL);
  psx_type_t *sig_nested_lvar_type = ps_lvar_get_decl_type(sig_nested_lvar);
  ASSERT_TRUE(sig_nested_lvar_type != NULL);
  ASSERT_EQ(1, sig_nested_lvar_type->funcptr_sig.function.returned_funcptr.is_funcptr);
  ASSERT_EQ(0, sig_nested_lvar_type->funcptr_sig.function.callable.signature.param_int_mask);
  ASSERT_EQ(1, sig_nested_lvar_type->funcptr_sig.function.returned_funcptr.type->callable.signature.param_int_mask);
  ASSERT_EQ(4, sig_nested_lvar_type->funcptr_sig.function.returned_funcptr.type->callable.return_shape.int_width);
  ASSERT_EQ(0, sig_nested_lvar_type->funcptr_sig.function.callable.return_shape.int_width);
  psx_decl_funcptr_sig_t sig_nested_lvar_canon = ps_lvar_funcptr_sig(sig_nested_lvar);
  ASSERT_EQ(1, sig_nested_lvar_canon.function.returned_funcptr.is_funcptr);
  ASSERT_EQ(4, sig_nested_lvar_canon.function.returned_funcptr.type->callable.return_shape.int_width);
  node_lvar_t *sig_nested_lvar_node = as_lvar(ps_node_new_lvar_for(sig_nested_lvar));
  ASSERT_EQ(4, ps_node_funcptr_sig((node_t *)sig_nested_lvar_node)
                   .function.returned_funcptr.type->callable.return_shape.int_width);

  parsed_code = parse_program_input(
      "typedef double NestedSigParam; "
      "int __tm_sig_nested_param(int (*(*q)(void))(NestedSigParam)) "
      "{ return 0; }");
  fn = as_func(parsed_code[0]);
  lvar_t *sig_nested_param = find_func_lvar(fn, "q");
  ASSERT_TRUE(sig_nested_param != NULL);
  psx_type_t *sig_nested_param_type = ps_lvar_get_decl_type(sig_nested_param);
  ASSERT_TRUE(sig_nested_param_type != NULL);
  ASSERT_EQ(1, sig_nested_param_type->funcptr_sig.function.returned_funcptr.is_funcptr);
  ASSERT_EQ(0, sig_nested_param_type->funcptr_sig.function.callable.signature.param_int_mask);
  ASSERT_EQ(2, sig_nested_param_type->funcptr_sig.function.returned_funcptr.type->callable.signature.param_fp_mask);
  ASSERT_EQ(4, sig_nested_param_type->funcptr_sig.function.returned_funcptr.type->callable.return_shape.int_width);
  ASSERT_EQ(0, sig_nested_param_type->funcptr_sig.function.callable.return_shape.int_width);
  const psx_type_t *outer_nested_param_function =
      ps_type_find_function(sig_nested_param_type);
  ASSERT_TRUE(outer_nested_param_function != NULL);
  ASSERT_EQ(0, outer_nested_param_function->param_count);
  const psx_type_t *returned_nested_param_function =
      ps_type_find_function(outer_nested_param_function->base);
  ASSERT_TRUE(returned_nested_param_function != NULL);
  ASSERT_EQ(1, returned_nested_param_function->param_count);
  ASSERT_EQ(PSX_TYPE_FLOAT,
            returned_nested_param_function->param_types[0]->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            returned_nested_param_function->param_types[0]->fp_kind);

  parsed_code = parse_program_input(
      "int __tm_sig_gf(int x){ return x; } "
      "int (*__tm_sig_gfp)(int); "
      "typedef double __tm_top_param_t; "
      "int (*__tm_top_fp)(__tm_top_param_t, int *, ...); "
      "int __tm_sig_global(void) { return __tm_sig_gfp(1); }");
  (void)parsed_code;
  global_var_t *sig_gvar = ps_find_global_var("__tm_sig_gfp", 12);
  ASSERT_TRUE(sig_gvar != NULL);
  psx_type_t *sig_gvar_type = ps_gvar_get_decl_type(sig_gvar);
  ASSERT_TRUE(sig_gvar_type != NULL);
  sig_gvar->decl_type = NULL;
  ASSERT_TRUE(ps_gvar_get_decl_type(sig_gvar) == NULL);
  sig_gvar->decl_type = sig_gvar_type;
  global_var_t *top_fp = ps_find_global_var(
      "__tm_top_fp", (int)(sizeof("__tm_top_fp") - 1));
  ASSERT_TRUE(top_fp != NULL);
  const psx_type_t *top_function =
      ps_type_find_function(ps_gvar_get_decl_type(top_fp));
  ASSERT_TRUE(top_function != NULL);
  ASSERT_EQ(2, top_function->param_count);
  ASSERT_TRUE(top_function->is_variadic_function);
  ASSERT_EQ(PSX_TYPE_FLOAT, top_function->param_types[0]->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, top_function->param_types[0]->fp_kind);
  ASSERT_EQ(PSX_TYPE_POINTER, top_function->param_types[1]->kind);
  ASSERT_EQ(PSX_TYPE_INTEGER, top_function->param_types[1]->base->kind);

  parsed_code = parse_program_input(
      "typedef const struct __tm_ret_tag { int value; } __tm_ret_alias; "
      "__tm_ret_alias *__tm_ret_alias_fn(void); "
      "struct __tm_ret_tag *__tm_ret_tag_fn(void); "
      "int (*__tm_ret_nested(void))(double); "
      "__tm_ret_implicit(void);");
  (void)parsed_code;
  const psx_type_t *alias_return_function = ps_ctx_get_function_type(
      (char *)"__tm_ret_alias_fn",
      (int)(sizeof("__tm_ret_alias_fn") - 1));
  ASSERT_TRUE(alias_return_function != NULL);
  ASSERT_EQ(PSX_TYPE_FUNCTION, alias_return_function->kind);
  ASSERT_TRUE(alias_return_function->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, alias_return_function->base->kind);
  ASSERT_TRUE(alias_return_function->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_STRUCT, alias_return_function->base->base->kind);
  ASSERT_TRUE(alias_return_function->base->base->is_const_qualified);
  ASSERT_EQ((int)(sizeof("__tm_ret_tag") - 1),
            alias_return_function->base->base->tag_len);

  const psx_type_t *tag_return_function = ps_ctx_get_function_type(
      (char *)"__tm_ret_tag_fn",
      (int)(sizeof("__tm_ret_tag_fn") - 1));
  ASSERT_TRUE(tag_return_function != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, tag_return_function->base->kind);
  ASSERT_EQ(PSX_TYPE_STRUCT, tag_return_function->base->base->kind);
  ASSERT_EQ(alias_return_function->base->base->tag_scope_depth_p1,
            tag_return_function->base->base->tag_scope_depth_p1);

  const psx_type_t *nested_return_function = ps_ctx_get_function_type(
      (char *)"__tm_ret_nested",
      (int)(sizeof("__tm_ret_nested") - 1));
  ASSERT_TRUE(nested_return_function != NULL);
  const psx_type_t *returned_function =
      ps_type_find_function(nested_return_function->base);
  ASSERT_TRUE(returned_function != NULL);
  ASSERT_EQ(1, returned_function->param_count);
  ASSERT_EQ(PSX_TYPE_FLOAT, returned_function->param_types[0]->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            returned_function->param_types[0]->fp_kind);

  const psx_type_t *implicit_return_function = ps_ctx_get_function_type(
      (char *)"__tm_ret_implicit",
      (int)(sizeof("__tm_ret_implicit") - 1));
  ASSERT_TRUE(implicit_return_function != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, implicit_return_function->base->kind);
  ASSERT_EQ(TK_INT, implicit_return_function->base->scalar_kind);

  parsed_code = parse_program_input(
      "int (*(*__tm_sig_nested_gfp)(void))(double); "
      "int __tm_sig_nested_global(void) { return 0; }");
  (void)parsed_code;
  global_var_t *sig_nested_gvar =
      ps_find_global_var("__tm_sig_nested_gfp",
                          (int)(sizeof("__tm_sig_nested_gfp") - 1));
  ASSERT_TRUE(sig_nested_gvar != NULL);
  psx_type_t *sig_nested_gvar_type = ps_gvar_get_decl_type(sig_nested_gvar);
  ASSERT_TRUE(sig_nested_gvar_type != NULL);
  ASSERT_EQ(1, sig_nested_gvar_type->funcptr_sig.function.returned_funcptr.is_funcptr);
  ASSERT_EQ(0, sig_nested_gvar_type->funcptr_sig.function.callable.signature.param_int_mask);
  ASSERT_EQ(2, sig_nested_gvar_type->funcptr_sig.function.returned_funcptr.type->callable.signature.param_fp_mask);
  ASSERT_EQ(4, sig_nested_gvar_type->funcptr_sig.function.returned_funcptr.type->callable.return_shape.int_width);
  ASSERT_EQ(0, sig_nested_gvar_type->funcptr_sig.function.callable.return_shape.int_width);
  psx_decl_funcptr_sig_t sig_nested_gvar_canon = ps_gvar_funcptr_sig(sig_nested_gvar);
  ASSERT_EQ(1, sig_nested_gvar_canon.function.returned_funcptr.is_funcptr);
  ASSERT_EQ(4, sig_nested_gvar_canon.function.returned_funcptr.type->callable.return_shape.int_width);
  node_t *sig_nested_gvar_node = ps_node_new_gvar_for(sig_nested_gvar);
  ASSERT_EQ(4, ps_node_funcptr_sig(sig_nested_gvar_node)
                   .function.returned_funcptr.type->callable.return_shape.int_width);

  parsed_code = parse_program_input(
      "int **__tm_gpp; int *__tm_gptrs[2]; "
      "int __tm_gpp_use(void) { return *__tm_gpp[1]; }");
  (void)parsed_code;
  global_var_t *gpp = ps_find_global_var("__tm_gpp", 8);
  ASSERT_TRUE(gpp != NULL);
  psx_type_t *gpp_type = ps_gvar_get_decl_type(gpp);
  ASSERT_TRUE(gpp_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, gpp_type->kind);
  ASSERT_EQ(8, gpp_type->deref_size);
  ASSERT_TRUE(gpp_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, gpp_type->base->kind);
  ASSERT_EQ(4, gpp_type->base->deref_size);
  node_t *gpp_node = ps_node_new_gvar_for(gpp);
  ASSERT_EQ(8, ps_node_deref_size(gpp_node));

  parsed_code = parse_program_input(
      "double __tm_gda[2][3]; int __tm_gda_use(void) { return 0; }");
  (void)parsed_code;
  global_var_t *gda = ps_find_global_var("__tm_gda", 8);
  ASSERT_TRUE(gda != NULL);
  node_t *gda_node = ps_node_new_gvar_for(gda);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_node_pointee_fp_kind(gda_node));
  node_t *gda_stale_sidecar_node = ps_node_new_gvar_for(gda);
  ASSERT_EQ(8, ps_node_base_deref_size(gda_stale_sidecar_node));
  node_t *gda_base = ps_node_new_gvar_array_base_for(gda);
  ASSERT_TRUE(ps_node_get_type(gda_base) == ps_gvar_get_decl_type(gda));
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(gda_base)->kind);
  ASSERT_EQ(2, ps_node_get_type(gda_base)->array_len);
  ASSERT_TRUE(ps_node_get_type(gda_base)->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(gda_base)->base->kind);
  ASSERT_EQ(3, ps_node_get_type(gda_base)->base->array_len);

  parsed_code = parse_program_input(
      "unsigned char __tm_global_su_arr[2]; _Bool __tm_global_sb_arr[2]; "
      "int __tm_global_si_arr[2]; "
      "int __tm_global_array_addr_flags(void) { return 0; }");
  (void)parsed_code;
  global_var_t *global_su_arr = ps_find_global_var(
      "__tm_global_su_arr", (int)(sizeof("__tm_global_su_arr") - 1));
  ASSERT_TRUE(global_su_arr != NULL);
  ASSERT_TRUE(global_su_arr->decl_type != NULL);
  node_t *global_su_arr_addr = ps_node_new_gvar_array_addr_for(global_su_arr);
  ASSERT_TRUE(ps_node_get_type(global_su_arr_addr) != NULL);
  ASSERT_TRUE(ps_node_pointee_is_unsigned(global_su_arr_addr));
  node_t *global_su_arr_base =
      ps_node_new_gvar_array_base_for(global_su_arr);
  ASSERT_TRUE(ps_node_get_type(global_su_arr_base) ==
              global_su_arr->decl_type);
  ASSERT_TRUE(ps_node_pointee_is_unsigned(global_su_arr_base));

  global_var_t *global_sb_arr = ps_find_global_var(
      "__tm_global_sb_arr", (int)(sizeof("__tm_global_sb_arr") - 1));
  ASSERT_TRUE(global_sb_arr != NULL);
  ASSERT_TRUE(global_sb_arr->decl_type != NULL);
  node_t *global_sb_arr_addr = ps_node_new_gvar_array_addr_for(global_sb_arr);
  ASSERT_TRUE(ps_node_get_type(global_sb_arr_addr) != NULL);
  ASSERT_TRUE(ps_node_pointee_is_bool(global_sb_arr_addr));
  node_t *global_sb_arr_base =
      ps_node_new_gvar_array_base_for(global_sb_arr);
  ASSERT_TRUE(ps_node_get_type(global_sb_arr_base) ==
              global_sb_arr->decl_type);
  ASSERT_TRUE(ps_node_pointee_is_bool(global_sb_arr_base));

  global_var_t *global_si_arr = ps_find_global_var(
      "__tm_global_si_arr", (int)(sizeof("__tm_global_si_arr") - 1));
  ASSERT_TRUE(global_si_arr != NULL);
  ASSERT_TRUE(global_si_arr->decl_type != NULL);
  node_t *global_si_arr_addr = ps_node_new_gvar_array_addr_for(global_si_arr);
  ASSERT_TRUE(ps_node_get_type(global_si_arr_addr) != NULL);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, ps_node_pointee_fp_kind(global_si_arr_addr));
  ASSERT_TRUE(!ps_node_pointee_is_bool(global_si_arr_addr));
  ASSERT_TRUE(!ps_node_pointee_is_unsigned(global_si_arr_addr));
  ASSERT_EQ(4, ps_node_deref_size(global_si_arr_addr));
  ASSERT_EQ(0, ps_node_ptr_array_pointee_bytes(global_si_arr_addr));

  parsed_code = parse_program_input(
      "struct __tm_gsa_S { int x; int y; }; "
      "struct __tm_gsa_S __tm_gsa[2][2]; "
      "int __tm_gsa_use(void) { return 0; }");
  (void)parsed_code;
  global_var_t *gsa = ps_find_global_var("__tm_gsa", 8);
  ASSERT_TRUE(gsa != NULL);
  ASSERT_TRUE(ps_gvar_is_tag_aggregate(gsa));
  ASSERT_TRUE(ps_gvar_is_struct_aggregate(gsa));
  ASSERT_TRUE(!ps_gvar_is_union_aggregate(gsa));
  ASSERT_EQ(8, ps_gvar_initializer_element_size(gsa, 0));
  ASSERT_EQ(4, ps_gvar_initializer_element_count(gsa, 0));

  global_var_t *gptrs = ps_find_global_var("__tm_gptrs", 10);
  ASSERT_TRUE(gptrs != NULL);
  psx_type_t *gptrs_type = ps_gvar_get_decl_type(gptrs);
  ASSERT_TRUE(gptrs_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, gptrs_type->kind);
  ASSERT_TRUE(gptrs_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, gptrs_type->base->kind);
  ASSERT_EQ(8, ps_type_sizeof(gptrs_type->base));
  ASSERT_EQ(4, gptrs_type->base->deref_size);
  parsed_code = parse_program_input(
      "int __tm_rows_a[2][3]; typedef int (*__tm_RowPtr3)[3]; "
      "__tm_RowPtr3 __tm_rows[2];");
  (void)parsed_code;
  global_var_t *rows_array = ps_find_global_var("__tm_rows", 9);
  ASSERT_TRUE(rows_array != NULL);
  psx_type_t *rows_array_type = ps_gvar_get_decl_type(rows_array);
  ASSERT_TRUE(rows_array_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, rows_array_type->kind);
  ASSERT_TRUE(rows_array_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, rows_array_type->base->kind);
  ASSERT_TRUE(rows_array_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, rows_array_type->base->base->kind);
  node_t *rows_array_node = ps_node_new_gvar_for(rows_array);
  node_t *rows_array_elem = ps_node_new_subscript_deref_for(
      rows_array_node, rows_array_node, ps_node_new_num(0), 8, 0, 0, NULL, 0);
  ASSERT_TRUE(ps_node_is_pointer(rows_array_elem));
  ASSERT_EQ(12, ps_node_deref_size(rows_array_elem));
  int rows_inner = 0;
  int rows_next = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(rows_array_elem, &rows_inner,
                                               &rows_next, NULL, NULL));
  ASSERT_EQ(4, rows_inner);
  ASSERT_EQ(0, rows_next);

  parsed_code = parse_program_input(
      "typedef int (*__tm_RowPtrScalar)[3]; __tm_RowPtrScalar __tm_row_scalar; "
      "int __tm_row_scalar_use(void) { return __tm_row_scalar[1][0]; }");
  global_var_t *row_scalar = ps_find_global_var("__tm_row_scalar", 15);
  ASSERT_TRUE(row_scalar != NULL);
  psx_type_t *row_scalar_type = ps_gvar_get_decl_type(row_scalar);
  ASSERT_EQ(PSX_TYPE_POINTER, row_scalar_type->kind);
  ASSERT_EQ(PSX_TYPE_ARRAY, row_scalar_type->base->kind);
  ASSERT_EQ(PSX_TYPE_INTEGER, row_scalar_type->base->base->kind);
  fn = as_func(parsed_code[0]);
  body = as_block(fn->base.rhs);
  node_t *row_scalar_elem = NULL;
  for (int i = 0; body->body[i]; i++) {
    if (body->body[i]->kind == ND_RETURN) {
      row_scalar_elem = body->body[i]->lhs;
      break;
    }
  }
  ASSERT_TRUE(row_scalar_elem != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, ps_node_get_type(row_scalar_elem)->kind);
  ASSERT_EQ(4, ps_node_type_size(row_scalar_elem));
  ASSERT_TRUE(!ps_node_is_pointer(row_scalar_elem));

  parsed_code = parse_program_input(
      "int __tm_bool_matrix_use(void) { "
      "  _Bool m[2][3]; m[1][0] = 99; return 0; }");
  fn = as_func(parsed_code[0]);
  lvar_t *bool_matrix = find_func_lvar(fn, "m");
  ASSERT_TRUE(bool_matrix != NULL);
  psx_type_t *bool_matrix_type = ps_lvar_get_decl_type(bool_matrix);
  ASSERT_TRUE(bool_matrix_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, bool_matrix_type->kind);
  ASSERT_TRUE(bool_matrix_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, bool_matrix_type->base->kind);
  ASSERT_TRUE(bool_matrix_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_BOOL, bool_matrix_type->base->base->kind);
  node_t *bool_matrix_node = ps_node_new_lvar_identifier_ref_for(bool_matrix);
  node_t *bool_matrix_row = ps_node_new_subscript_deref_for(
      bool_matrix_node, bool_matrix_node, ps_node_new_num(0), 3, 1, 0, NULL, 0);
  psx_type_t *bool_matrix_row_type = ps_node_get_type(bool_matrix_row);
  ASSERT_TRUE(bool_matrix_row_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, bool_matrix_row_type->kind);
  ASSERT_TRUE(bool_matrix_row_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_BOOL, bool_matrix_row_type->base->kind);
  node_t *bool_matrix_elem = ps_node_new_subscript_deref_for(
      bool_matrix_row, bool_matrix_row, ps_node_new_num(0), 1, 0, 0, NULL, 0);
  psx_type_t *bool_matrix_elem_type = ps_node_get_type(bool_matrix_elem);
  ASSERT_TRUE(bool_matrix_elem_type != NULL);
  ASSERT_EQ(PSX_TYPE_BOOL, bool_matrix_elem_type->kind);

  parsed_code = parse_program_input(
      "int __tm_grid_a[2][3]; int __tm_grid_b[2][3]; "
      "typedef int (*__tm_RowPtr)[3]; "
      "int (*(*__tm_grid_ptrs)[2])[3] = &(int (*[2])[3]){__tm_grid_a, __tm_grid_b}; "
      "__tm_RowPtr *__tm_grid_ptr_list = (__tm_RowPtr[]){__tm_grid_a, __tm_grid_b}; "
      "int __tm_grid_use(void) { "
      "  return (*__tm_grid_ptrs)[0][0][1] + __tm_grid_ptr_list[1][0][2]; }");
  (void)parsed_code;
  global_var_t *grid_ptrs = ps_find_global_var("__tm_grid_ptrs", 14);
  ASSERT_TRUE(grid_ptrs != NULL);
  psx_type_t *grid_ptrs_type = ps_gvar_get_decl_type(grid_ptrs);
  ASSERT_TRUE(grid_ptrs_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, grid_ptrs_type->kind);
  ASSERT_TRUE(grid_ptrs_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, grid_ptrs_type->base->kind);
  ASSERT_TRUE(grid_ptrs_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, grid_ptrs_type->base->base->kind);
  node_t *grid_ptrs_node = ps_node_new_gvar_for(grid_ptrs);
  node_t *grid_rows = ps_node_new_unary_deref_for(grid_ptrs_node);
  psx_type_t *grid_rows_type = ps_node_get_type(grid_rows);
  ASSERT_TRUE(grid_rows_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, grid_rows_type->kind);
  ASSERT_TRUE(grid_rows_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, grid_rows_type->base->kind);
  node_t *grid_rowptr_elem = parse_expr_input("(*__tm_grid_ptrs)[0]");
  psx_type_t *grid_rowptr_elem_type = ps_node_get_type(grid_rowptr_elem);
  ASSERT_TRUE(grid_rowptr_elem_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, grid_rowptr_elem_type->kind);
  ASSERT_TRUE(grid_rowptr_elem_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, grid_rowptr_elem_type->base->kind);
  ASSERT_EQ(12, ps_node_deref_size(grid_rowptr_elem));

  parsed_code = parse_program_input(
      "int __tm_param_nested_rows(int (*(*rows)[2])[3]) { "
      "  return (*rows)[0][1][2]; }");
  fn = as_func(parsed_code[0]);
  lvar_t *nested_rows = find_func_lvar(fn, "rows");
  ASSERT_TRUE(nested_rows != NULL);
  psx_type_t *nested_rows_type = ps_lvar_get_decl_type(nested_rows);
  ASSERT_TRUE(nested_rows_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, nested_rows_type->kind);
  ASSERT_TRUE(nested_rows_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, nested_rows_type->base->kind);
  ASSERT_TRUE(nested_rows_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, nested_rows_type->base->base->kind);
  node_t *nested_rows_node = ps_node_new_lvar_identifier_ref_for(nested_rows);
  ASSERT_EQ(16, ps_node_deref_size(nested_rows_node));
  node_t *nested_rows_array = ps_node_new_unary_deref_for(nested_rows_node);
  psx_type_t *nested_rows_array_type = ps_node_get_type(nested_rows_array);
  ASSERT_TRUE(nested_rows_array_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, nested_rows_array_type->kind);
  ASSERT_TRUE(nested_rows_array_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, nested_rows_array_type->base->kind);
  node_t *nested_rowptr_elem = ps_node_new_subscript_deref_for(
      nested_rows_array,
      nested_rows_array->lhs ? nested_rows_array->lhs : nested_rows_array,
      ps_node_new_num(0), ps_node_deref_size(nested_rows_array),
      0, 0, NULL, 0);
  psx_type_t *nested_rowptr_elem_type = ps_node_get_type(nested_rowptr_elem);
  ASSERT_TRUE(nested_rowptr_elem_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, nested_rowptr_elem_type->kind);
  ASSERT_TRUE(nested_rowptr_elem_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, nested_rowptr_elem_type->base->kind);
  ASSERT_EQ(12, ps_node_deref_size(nested_rowptr_elem));

  parsed_code = parse_program_input(
      "int __tm_param_canonical(int a[2][3], int (*p)[3], "
      "int *q[3], int (*cb)(int)) { return a[0][0] + p[0][0] + *q[0] + cb(0); }");
  fn = as_func(parsed_code[0]);
  lvar_t *canonical_a = find_func_lvar(fn, "a");
  lvar_t *canonical_p = find_func_lvar(fn, "p");
  lvar_t *canonical_q = find_func_lvar(fn, "q");
  lvar_t *canonical_cb = find_func_lvar(fn, "cb");
  ASSERT_TRUE(canonical_a != NULL);
  ASSERT_TRUE(canonical_p != NULL);
  ASSERT_TRUE(canonical_q != NULL);
  ASSERT_TRUE(canonical_cb != NULL);
  psx_type_t *canonical_a_type = ps_lvar_get_decl_type(canonical_a);
  psx_type_t *canonical_p_type = ps_lvar_get_decl_type(canonical_p);
  psx_type_t *canonical_q_type = ps_lvar_get_decl_type(canonical_q);
  psx_type_t *canonical_cb_type = ps_lvar_get_decl_type(canonical_cb);
  ASSERT_EQ(PSX_TYPE_POINTER, canonical_a_type->kind);
  ASSERT_EQ(PSX_TYPE_ARRAY, canonical_a_type->base->kind);
  ASSERT_EQ(3, canonical_a_type->base->array_len);
  ASSERT_EQ(PSX_TYPE_INTEGER, canonical_a_type->base->base->kind);
  ASSERT_EQ(PSX_TYPE_POINTER, canonical_p_type->kind);
  ASSERT_EQ(PSX_TYPE_ARRAY, canonical_p_type->base->kind);
  ASSERT_EQ(3, canonical_p_type->base->array_len);
  ASSERT_EQ(PSX_TYPE_INTEGER, canonical_p_type->base->base->kind);
  ASSERT_EQ(PSX_TYPE_POINTER, canonical_q_type->kind);
  ASSERT_EQ(PSX_TYPE_POINTER, canonical_q_type->base->kind);
  ASSERT_EQ(PSX_TYPE_INTEGER, canonical_q_type->base->base->kind);
  ASSERT_EQ(PSX_TYPE_POINTER, canonical_cb_type->kind);
  ASSERT_EQ(PSX_TYPE_FUNCTION, canonical_cb_type->base->kind);
  ASSERT_EQ(PSX_TYPE_INTEGER, canonical_cb_type->base->base->kind);
  ASSERT_EQ(1, ps_type_funcptr_signature(canonical_cb_type)
                   .function.callable.signature.param_int_mask);

  parsed_code = parse_program_input(
      "struct __tm_member_canonical { int a[2][3]; int (*p)[3]; "
      "int *q[3]; int (**cb)(int); }; ");
  (void)parsed_code;
  tag_member_info_t canonical_member_a = {0};
  tag_member_info_t canonical_member_p = {0};
  tag_member_info_t canonical_member_q = {0};
  tag_member_info_t canonical_member_cb = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info(
      TK_STRUCT, "__tm_member_canonical", 21, "a", 1, &canonical_member_a));
  ASSERT_TRUE(ps_ctx_find_tag_member_info(
      TK_STRUCT, "__tm_member_canonical", 21, "p", 1, &canonical_member_p));
  ASSERT_TRUE(ps_ctx_find_tag_member_info(
      TK_STRUCT, "__tm_member_canonical", 21, "q", 1, &canonical_member_q));
  ASSERT_TRUE(ps_ctx_find_tag_member_info(
      TK_STRUCT, "__tm_member_canonical", 21, "cb", 2, &canonical_member_cb));
  ASSERT_EQ(PSX_TYPE_ARRAY, canonical_member_a.decl_type->kind);
  ASSERT_EQ(2, canonical_member_a.decl_type->array_len);
  ASSERT_EQ(PSX_TYPE_ARRAY, canonical_member_a.decl_type->base->kind);
  ASSERT_EQ(3, canonical_member_a.decl_type->base->array_len);
  ASSERT_EQ(PSX_TYPE_POINTER, canonical_member_p.decl_type->kind);
  ASSERT_EQ(PSX_TYPE_ARRAY, canonical_member_p.decl_type->base->kind);
  ASSERT_EQ(3, canonical_member_p.decl_type->base->array_len);
  ASSERT_EQ(PSX_TYPE_ARRAY, canonical_member_q.decl_type->kind);
  ASSERT_EQ(3, canonical_member_q.decl_type->array_len);
  ASSERT_EQ(PSX_TYPE_POINTER, canonical_member_q.decl_type->base->kind);
  ASSERT_EQ(PSX_TYPE_POINTER, canonical_member_cb.decl_type->kind);
  ASSERT_EQ(PSX_TYPE_POINTER, canonical_member_cb.decl_type->base->kind);
  ASSERT_EQ(PSX_TYPE_FUNCTION,
            canonical_member_cb.decl_type->base->base->kind);
  ASSERT_EQ(1, ps_type_funcptr_signature(canonical_member_cb.decl_type)
                   .function.callable.signature.param_int_mask);

  const char *qualified_member_tag = "__tm_member_qualified";
  parsed_code = parse_program_input(
      "struct __tm_member_qualified { const int c; "
      "volatile unsigned long long v; _Atomic int a; char pad; "
      "_Alignas(8) int y; }; ");
  (void)parsed_code;
  tag_member_info_t qualified_member_c = {0};
  tag_member_info_t qualified_member_v = {0};
  tag_member_info_t qualified_member_a = {0};
  tag_member_info_t qualified_member_y = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info(
      TK_STRUCT, (char *)qualified_member_tag,
      (int)strlen(qualified_member_tag), "c", 1, &qualified_member_c));
  ASSERT_TRUE(ps_ctx_find_tag_member_info(
      TK_STRUCT, (char *)qualified_member_tag,
      (int)strlen(qualified_member_tag), "v", 1, &qualified_member_v));
  ASSERT_TRUE(ps_ctx_find_tag_member_info(
      TK_STRUCT, (char *)qualified_member_tag,
      (int)strlen(qualified_member_tag), "a", 1, &qualified_member_a));
  ASSERT_TRUE(ps_ctx_find_tag_member_info(
      TK_STRUCT, (char *)qualified_member_tag,
      (int)strlen(qualified_member_tag), "y", 1, &qualified_member_y));
  ASSERT_TRUE(qualified_member_c.decl_type->is_const_qualified);
  ASSERT_TRUE(qualified_member_v.decl_type->is_volatile_qualified);
  ASSERT_TRUE(qualified_member_v.decl_type->is_unsigned);
  ASSERT_TRUE(qualified_member_v.decl_type->is_long_long);
  ASSERT_EQ(8, ps_type_sizeof(qualified_member_v.decl_type));
  ASSERT_TRUE(qualified_member_a.decl_type->is_atomic);
  ASSERT_EQ(0, qualified_member_y.offset % 8);

  parsed_code = parse_program_input(
      "int __tm_local_ptr_rows(void) { "
      "  int a[2][3]; int (*m[2][2])[3] = {{a, a}, {a, a}}; "
      "  return m[0][0][1][2]; }");
  fn = as_func(parsed_code[0]);
  lvar_t *local_ptr_rows = find_func_lvar(fn, "m");
  ASSERT_TRUE(local_ptr_rows != NULL);
  psx_type_t *local_ptr_rows_type = ps_lvar_get_decl_type(local_ptr_rows);
  ASSERT_TRUE(local_ptr_rows_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, local_ptr_rows_type->kind);
  ASSERT_TRUE(local_ptr_rows_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, local_ptr_rows_type->base->kind);
  ASSERT_TRUE(local_ptr_rows_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, local_ptr_rows_type->base->base->kind);
  ASSERT_TRUE(local_ptr_rows_type->base->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, local_ptr_rows_type->base->base->base->kind);
  node_t *local_ptr_rows_elem =
      ps_node_new_array_elem_lvar_for(local_ptr_rows, 0);
  psx_type_t *local_ptr_rows_elem_type = ps_node_get_type(local_ptr_rows_elem);
  ASSERT_TRUE(local_ptr_rows_elem_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, local_ptr_rows_elem_type->kind);
  ASSERT_TRUE(local_ptr_rows_elem_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, local_ptr_rows_elem_type->base->kind);
  ASSERT_TRUE(ps_node_is_pointer(local_ptr_rows_elem));
  ASSERT_EQ(12, ps_node_deref_size(local_ptr_rows_elem));
  node_t *local_ptr_rows_slot = ps_node_new_lvar_typed_at_for(
      local_ptr_rows, local_ptr_rows->offset, local_ptr_rows->elem_size);
  psx_type_t *local_ptr_rows_slot_type = ps_node_get_type(local_ptr_rows_slot);
  ASSERT_TRUE(local_ptr_rows_slot_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, local_ptr_rows_slot_type->kind);
  ASSERT_TRUE(local_ptr_rows_slot_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, local_ptr_rows_slot_type->base->kind);
  ASSERT_TRUE(ps_node_is_pointer(local_ptr_rows_slot));
  ASSERT_EQ(12, ps_node_deref_size(local_ptr_rows_slot));

  parsed_code = parse_program_input(
      "int __tm_local_scalar_array_flags(void) { "
      "  unsigned char u[2]; _Bool b[2]; int i[2]; return u[0] + b[0] + i[0]; }");
  fn = as_func(parsed_code[0]);
  lvar_t *local_scalar_u = find_func_lvar(fn, "u");
  ASSERT_TRUE(local_scalar_u != NULL);
  ASSERT_TRUE(ps_lvar_get_decl_type(local_scalar_u) != NULL);
  node_t *local_scalar_u_elem =
      ps_node_new_array_elem_lvar_for(local_scalar_u, 0);
  ASSERT_TRUE(ps_node_is_unsigned_type(local_scalar_u_elem));
  node_t *local_scalar_u_slot = ps_node_new_lvar_typed_at_for(
      local_scalar_u, local_scalar_u->offset, local_scalar_u->elem_size);
  ASSERT_TRUE(ps_node_is_unsigned_type(local_scalar_u_slot));

  lvar_t *local_scalar_b = find_func_lvar(fn, "b");
  ASSERT_TRUE(local_scalar_b != NULL);
  ASSERT_TRUE(ps_lvar_get_decl_type(local_scalar_b) != NULL);
  node_t *local_scalar_b_elem =
      ps_node_new_array_elem_lvar_for(local_scalar_b, 0);
  ASSERT_TRUE(ps_node_get_type(local_scalar_b_elem) != NULL);
  ASSERT_EQ(PSX_TYPE_BOOL, ps_node_get_type(local_scalar_b_elem)->kind);
  node_t *local_scalar_b_slot = ps_node_new_lvar_typed_at_for(
      local_scalar_b, local_scalar_b->offset, local_scalar_b->elem_size);
  ASSERT_TRUE(ps_node_get_type(local_scalar_b_slot) != NULL);
  ASSERT_EQ(PSX_TYPE_BOOL, ps_node_get_type(local_scalar_b_slot)->kind);

  lvar_t *local_scalar_i = find_func_lvar(fn, "i");
  ASSERT_TRUE(local_scalar_i != NULL);
  ASSERT_TRUE(ps_lvar_get_decl_type(local_scalar_i) != NULL);
  local_scalar_i->outer_stride = 32;
  local_scalar_i->mid_stride = 16;
  local_scalar_i->is_vla = 1;
  local_scalar_i->vla_row_stride_frame_off = 88;
  local_scalar_i->vla_strides_remaining = 2;
  node_t *local_scalar_i_addr =
      ps_node_new_lvar_array_addr_for(local_scalar_i, 0);
  ASSERT_TRUE(ps_node_get_type(local_scalar_i_addr) != NULL);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, ps_node_pointee_fp_kind(local_scalar_i_addr));
  ASSERT_TRUE(!ps_node_pointee_is_bool(local_scalar_i_addr));
  ASSERT_TRUE(!ps_node_pointee_is_unsigned(local_scalar_i_addr));
  ASSERT_EQ(4, ps_node_deref_size(local_scalar_i_addr));
  ASSERT_EQ(0, ps_node_ptr_array_pointee_bytes(local_scalar_i_addr));
  ASSERT_EQ(0, ps_node_vla_row_stride_frame_off(local_scalar_i_addr));

  parsed_code = parse_program_input(
      "int __tm_larr_base(void) { int a[2]; return a[0]; }");
  fn = as_func(parsed_code[0]);
  lvar_t *larr_base_a = find_func_lvar(fn, "a");
  ASSERT_TRUE(larr_base_a != NULL);
  ASSERT_TRUE(ps_lvar_get_decl_type(larr_base_a) != NULL);
  node_t *larr_base_node = ps_node_new_lvar_identifier_ref_for(larr_base_a);
  ASSERT_EQ(4, ps_node_base_deref_size(larr_base_node));

  parsed_code = parse_program_input(
      "struct __tm_byref_S { long a; long b; long c; }; "
      "long __tm_byref(struct __tm_byref_S s) { return s.b; }");
  fn = as_func(parsed_code[0]);
  lvar_t *byref_s = find_func_lvar(fn, "s");
  ASSERT_TRUE(byref_s != NULL);
  ASSERT_TRUE(byref_s->is_byref_param);
  psx_type_t *byref_s_type = ps_lvar_get_decl_type(byref_s);
  ASSERT_TRUE(byref_s_type != NULL);
  ASSERT_EQ(PSX_TYPE_STRUCT, byref_s_type->kind);
  ASSERT_EQ(24, ps_type_sizeof(byref_s_type));
  node_t *byref_s_node = ps_node_new_lvar_identifier_ref_for(byref_s);
  ASSERT_EQ(ND_LVAR, byref_s_node->kind);
  ASSERT_TRUE(ps_node_get_type(byref_s_node) == byref_s_type);
  ASSERT_EQ(24, ps_node_type_size(byref_s_node));
  ASSERT_TRUE(!ps_node_is_pointer(byref_s_node));
  ASSERT_EQ(PSX_TYPE_STRUCT, ps_node_get_type(byref_s_node)->kind);

  lvar_t byref_s_stale = *byref_s;
  byref_s_stale.elem_size = 99;
  node_t *byref_s_stale_node = ps_node_new_lvar_identifier_ref_for(&byref_s_stale);
  ASSERT_TRUE(ps_node_get_type(byref_s_stale_node) == byref_s_type);
  ASSERT_EQ(24, ps_node_type_size(byref_s_stale_node));
  ASSERT_EQ(PSX_TYPE_STRUCT, ps_node_get_type(byref_s_stale_node)->kind);

  parsed_code = parse_program_input(
      "int __tm_vla_sidecar(int m) { int a[2][3]; int (*p)[m] = a; return sizeof(p); }");
  fn = as_func(parsed_code[0]);
  lvar_t *vla_ptr_lvar = find_func_lvar(fn, "p");
  ASSERT_TRUE(vla_ptr_lvar != NULL);
  ASSERT_TRUE(vla_ptr_lvar->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, vla_ptr_lvar->decl_type->kind);
  ASSERT_EQ(8, ps_lvar_decl_sizeof(vla_ptr_lvar, 99));
  ASSERT_EQ(16, ps_lvar_storage_size(vla_ptr_lvar, 99));
  node_t *vla_ptr_node = ps_node_new_lvar_identifier_ref_for(vla_ptr_lvar);
  int vla_ptr_inner = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(vla_ptr_node, &vla_ptr_inner,
                                               NULL, NULL, NULL));
  ASSERT_EQ(4, vla_ptr_inner);
  ASSERT_EQ(vla_ptr_lvar->vla_row_stride_frame_off,
            ps_node_vla_row_stride_frame_off(vla_ptr_node));
  node_t *vla_ptr_row = ps_node_new_subscript_deref_for(
      vla_ptr_node, vla_ptr_node, ps_node_new_num(0), vla_ptr_inner,
      vla_ptr_inner, 0, NULL, 0);
  ASSERT_TRUE(ps_node_get_type(vla_ptr_row) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(vla_ptr_row)->kind);
  ASSERT_TRUE(ps_node_subscript_deref_uses_base_address(vla_ptr_row));

  parsed_code = parse_program_input(
      "int __tm_vla_ptr_arith(int m, int a[2][3]) { "
      "int (*p)[m] = a; return (*(p + 1))[2]; }");
  fn = as_func(parsed_code[0]);
  body = as_block(fn->base.rhs);
  node_t *vla_ptr_arith_elem = NULL;
  for (int i = 0; body->body[i]; i++) {
    if (body->body[i]->kind == ND_RETURN) {
      vla_ptr_arith_elem = body->body[i]->lhs;
      break;
    }
  }
  ASSERT_TRUE(vla_ptr_arith_elem != NULL);
  ASSERT_EQ(ND_DEREF, vla_ptr_arith_elem->kind);
  ASSERT_EQ(4, ps_node_type_size(vla_ptr_arith_elem));
  ASSERT_TRUE(vla_ptr_arith_elem->lhs != NULL);
  ASSERT_EQ(ND_ADD, vla_ptr_arith_elem->lhs->kind);
  ASSERT_TRUE(vla_ptr_arith_elem->lhs->lhs != NULL);
  ASSERT_TRUE(vla_ptr_arith_elem->lhs->lhs->kind != ND_DEREF);

  parsed_code = parse_program_input(
      "int __tm_vla_fp_sidecar(int m) { double a[2][3]; double (*p)[m] = a; "
      "p[0][1] = 9.5; return 0; }");
  fn = as_func(parsed_code[0]);
  lvar_t *vla_fp_array_lvar = find_func_lvar(fn, "a");
  ASSERT_TRUE(vla_fp_array_lvar != NULL);
  node_t *vla_fp_array_node =
      ps_node_new_lvar_identifier_ref_for(vla_fp_array_lvar);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            ps_node_pointee_fp_kind(vla_fp_array_node));
  lvar_t *vla_fp_ptr_lvar = find_func_lvar(fn, "p");
  ASSERT_TRUE(vla_fp_ptr_lvar != NULL);
  node_t *vla_fp_ptr_node =
      ps_node_new_lvar_identifier_ref_for(vla_fp_ptr_lvar);
  ASSERT_EQ(vla_fp_ptr_lvar->vla_row_stride_frame_off,
            ps_node_vla_row_stride_frame_off(vla_fp_ptr_node));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            ps_node_pointee_fp_kind(vla_fp_ptr_node));

  parsed_code = parse_program_input(
      "int __tm_vla_fp_leaf(int n) { double a[n]; return 0; }");
  fn = as_func(parsed_code[0]);
  lvar_t *vla_fp_leaf_lvar = find_func_lvar(fn, "a");
  ASSERT_TRUE(vla_fp_leaf_lvar != NULL);
  psx_type_t *vla_fp_leaf = vla_fp_leaf_lvar->decl_type;
  while (vla_fp_leaf && vla_fp_leaf->kind == PSX_TYPE_ARRAY)
    vla_fp_leaf = vla_fp_leaf->base;
  ASSERT_TRUE(vla_fp_leaf != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, vla_fp_leaf->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, vla_fp_leaf->fp_kind);

  parsed_code = parse_program_input(
      "struct __tm_ptrarr_S { int x; }; "
      "int __tm_ptrarr(void) { const struct __tm_ptrarr_S a[1] = {{1}}; "
      "const struct __tm_ptrarr_S (*p)[1] = &a; return 0; }");
  fn = as_func(parsed_code[0]);
  lvar_t *ptrarr_p = find_func_lvar(fn, "p");
  ASSERT_TRUE(ptrarr_p != NULL);
  ASSERT_TRUE(ptrarr_p->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ptrarr_p->decl_type->kind);
  ASSERT_EQ(4, ptrarr_p->decl_type->outer_stride);
  node_t *ptrarr_p_node = ps_node_new_lvar_identifier_ref_for(ptrarr_p);
  ASSERT_TRUE(ps_node_is_pointer(ptrarr_p_node));
  ASSERT_EQ(4, ps_node_deref_size(ptrarr_p_node));
  node_t *ptrarr_row = ps_node_new_unary_deref_for(ptrarr_p_node);
  ASSERT_TRUE(ps_node_is_pointer(ptrarr_row));
  ASSERT_EQ(4, ps_node_type_size(ptrarr_row));

  lvar_t tmp_fp_ptr_lvar = {0};
  tmp_fp_ptr_lvar.size = 8;
  tmp_fp_ptr_lvar.elem_size = 8;
  psx_decl_set_lvar_decl_type(
      &tmp_fp_ptr_lvar,
      ps_type_new_pointer(
          ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8), 8));
  psx_type_t *tmp_fp_ptr_double = tmp_fp_ptr_lvar.decl_type;
  ASSERT_TRUE(tmp_fp_ptr_double != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, tmp_fp_ptr_double->kind);
  ASSERT_TRUE(tmp_fp_ptr_double->base != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, tmp_fp_ptr_double->base->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, tmp_fp_ptr_double->base->fp_kind);
  ASSERT_TRUE(ps_lvar_value_is_pointer_like(&tmp_fp_ptr_lvar));

  lvar_t tmp_void_ptr_lvar = {0};
  tmp_void_ptr_lvar.size = 8;
  tmp_void_ptr_lvar.elem_size = 8;
  psx_type_t *tmp_void_type = ps_type_new(PSX_TYPE_VOID);
  tmp_void_type->scalar_kind = TK_VOID;
  psx_decl_set_lvar_decl_type(
      &tmp_void_ptr_lvar, ps_type_new_pointer(tmp_void_type, 0));
  ASSERT_TRUE(ps_lvar_value_is_pointer_like(&tmp_void_ptr_lvar));

  lvar_t tmp_complex_lvar = {0};
  tmp_complex_lvar.size = 16;
  tmp_complex_lvar.elem_size = 16;
  psx_type_t *canonical_complex = ps_type_new(PSX_TYPE_COMPLEX);
  canonical_complex->fp_kind = TK_FLOAT_KIND_DOUBLE;
  canonical_complex->size = 16;
  canonical_complex->align = 8;
  psx_decl_set_lvar_decl_type(&tmp_complex_lvar, canonical_complex);
  psx_type_t *tmp_complex_type = tmp_complex_lvar.decl_type;
  ASSERT_TRUE(tmp_complex_type != NULL);
  ASSERT_EQ(PSX_TYPE_COMPLEX, tmp_complex_type->kind);
  node_t *tmp_complex_node = ps_node_new_lvar_for(&tmp_complex_lvar);
  ASSERT_TRUE(ps_node_get_type(tmp_complex_node) == tmp_complex_type);
  ASSERT_EQ(PSX_TYPE_COMPLEX, ps_node_get_type(tmp_complex_node)->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_node_value_fp_kind(tmp_complex_node));

  node_t *tmp_complex_slot =
      ps_node_new_lvar_fp_slot_for(&tmp_complex_lvar, tmp_complex_lvar.offset, 8);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_node_value_fp_kind(tmp_complex_slot));
  ASSERT_TRUE(ps_node_get_type(tmp_complex_slot) != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, ps_node_get_type(tmp_complex_slot)->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_node_get_type(tmp_complex_slot)->fp_kind);

  node_t typed_complex_ptr_mem = {0};
  typed_complex_ptr_mem.kind = ND_DEREF;
  typed_complex_ptr_mem.type = ps_type_new_pointer(tmp_complex_type, 16);
  node_t *typed_complex_deref =
      ps_node_new_unary_deref_for(&typed_complex_ptr_mem);
  ASSERT_TRUE(ps_node_get_type(typed_complex_deref) == tmp_complex_type);
  ASSERT_TRUE(ps_node_value_is_complex(typed_complex_deref));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_node_value_fp_kind(typed_complex_deref));

  node_t typed_mem = {0};
  typed_mem.kind = ND_LVAR;
  typed_mem.type = ps_type_new_integer(TK_LONG, 8, 0);
  ASSERT_TRUE(ps_node_get_type(&typed_mem) == typed_mem.type);
  ASSERT_EQ(8, ps_node_type_size(&typed_mem));
  node_t typed_stale_scalar_size_mem = {0};
  typed_stale_scalar_size_mem.kind = ND_LVAR;
  typed_stale_scalar_size_mem.type = ps_type_new_integer(TK_UNSIGNED, 1, 1);
  ASSERT_EQ(1, ps_node_type_size(&typed_stale_scalar_size_mem));
  ASSERT_EQ(1, ps_node_storage_type_size(&typed_stale_scalar_size_mem));
  node_t typed_incomplete_size_mem = {0};
  typed_incomplete_size_mem.kind = ND_LVAR;
  typed_incomplete_size_mem.type =
      ps_type_new_tag(TK_STRUCT, "Incomplete", 10, 0, 0);
  ASSERT_EQ(0, ps_node_storage_type_size(&typed_incomplete_size_mem));
  typed_mem.is_unsigned = 1;
  typed_mem.is_long_long = 1;
  ASSERT_TRUE(!ps_node_is_pointer(&typed_mem));
  ASSERT_EQ(0, ps_node_deref_size(&typed_mem));
  ASSERT_TRUE(!ps_node_is_unsigned_type(&typed_mem));
  ASSERT_TRUE(!ps_node_conversion_value_is_unsigned(&typed_mem));
  ASSERT_TRUE(!ps_node_is_long_long_type(&typed_mem));
  ASSERT_TRUE(!ps_node_is_plain_char_type(&typed_mem));
  ASSERT_TRUE(!ps_node_is_long_double_type(&typed_mem));
  token_kind_t typed_scalar_tag_kind = TK_EOF;
  char *typed_scalar_tag_name = NULL;
  int typed_scalar_tag_len = 0;
  int typed_scalar_is_tag_pointer = 0;
  ps_node_get_tag_type(&typed_mem, &typed_scalar_tag_kind,
                        &typed_scalar_tag_name, &typed_scalar_tag_len,
                        &typed_scalar_is_tag_pointer);
  ASSERT_EQ(TK_EOF, typed_scalar_tag_kind);
  ASSERT_TRUE(typed_scalar_tag_name == NULL);
  ASSERT_EQ(0, typed_scalar_tag_len);
  ASSERT_EQ(0, typed_scalar_is_tag_pointer);
  ASSERT_EQ(-1, ps_node_get_tag_scope_depth(&typed_mem));
  int typed_scalar_inner_stride = 123;
  int typed_scalar_next_stride = 124;
  int typed_scalar_extra_strides[5] = {1, 2, 3, 4, 5};
  int typed_scalar_extra_count = 9;
  ASSERT_TRUE(!ps_node_pointer_stride_metadata(
      &typed_mem, &typed_scalar_inner_stride, &typed_scalar_next_stride,
      typed_scalar_extra_strides, &typed_scalar_extra_count));
  ASSERT_EQ(0, typed_scalar_inner_stride);
  ASSERT_EQ(0, typed_scalar_next_stride);
  ASSERT_EQ(0, typed_scalar_extra_count);
  ASSERT_EQ(0, typed_scalar_extra_strides[0]);
  ASSERT_EQ(0, ps_node_vla_row_stride_frame_off(&typed_mem));
  ASSERT_EQ(0, ps_node_base_deref_size(&typed_mem));
  ASSERT_EQ(0, ps_node_ptr_array_pointee_bytes(&typed_mem));

  node_t typed_stale_value_flags_mem = {0};
  typed_stale_value_flags_mem.kind = ND_LVAR;
  typed_stale_value_flags_mem.fp_kind = TK_FLOAT_KIND_DOUBLE;
  typed_stale_value_flags_mem.is_complex = 1;
  typed_stale_value_flags_mem.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 4);
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            ps_node_value_fp_kind(&typed_stale_value_flags_mem));
  ASSERT_TRUE(!ps_node_value_is_complex(&typed_stale_value_flags_mem));

  node_t typed_stale_pointee_flags_mem = {0};
  typed_stale_pointee_flags_mem.kind = ND_LVAR;
  typed_stale_pointee_flags_mem.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 4);
  ASSERT_TRUE(!ps_node_pointee_is_unsigned(&typed_stale_pointee_flags_mem));
  ASSERT_TRUE(!ps_node_pointee_is_bool(&typed_stale_pointee_flags_mem));
  ASSERT_TRUE(!ps_node_pointee_is_void(&typed_stale_pointee_flags_mem));

  node_t typed_missing_size_mem = {0};
  typed_missing_size_mem.kind = ND_LVAR;
  typed_missing_size_mem.type = ps_type_new(PSX_TYPE_INTEGER);
  ASSERT_EQ(0, ps_node_type_size(&typed_missing_size_mem));

  node_t typed_unsigned_mem = {0};
  typed_unsigned_mem.kind = ND_LVAR;
  typed_unsigned_mem.type = ps_type_new_integer(TK_UNSIGNED, 4, 1);
  ASSERT_TRUE(ps_node_is_unsigned_type(&typed_unsigned_mem));
  ASSERT_TRUE(ps_node_conversion_value_is_unsigned(&typed_unsigned_mem));

  node_t typed_bool_lhs_mem = {0};
  typed_bool_lhs_mem.kind = ND_LVAR;
  typed_bool_lhs_mem.type = ps_type_new(PSX_TYPE_BOOL);
  node_t *typed_bool_assign =
      ps_node_new_assign(&typed_bool_lhs_mem, ps_node_new_num(3));
  ASSERT_TRUE(typed_bool_assign->rhs != NULL);
  ASSERT_EQ(ND_NUM, typed_bool_assign->rhs->kind);
  psx_semantic_analyze_expression(typed_bool_assign, NULL);
  ASSERT_EQ(ND_NE, typed_bool_assign->rhs->kind);

  node_t typed_stale_bool_lhs_mem = {0};
  typed_stale_bool_lhs_mem.kind = ND_LVAR;
  typed_stale_bool_lhs_mem.type = ps_type_new_integer(TK_INT, 4, 0);
  node_t *typed_stale_bool_assign =
      ps_node_new_assign(&typed_stale_bool_lhs_mem, ps_node_new_num(3));
  ASSERT_TRUE(typed_stale_bool_assign->rhs != NULL);
  ASSERT_EQ(ND_NUM, typed_stale_bool_assign->rhs->kind);

  node_t typed_assign_ptr_lhs_mem = {0};
  typed_assign_ptr_lhs_mem.kind = ND_LVAR;
  typed_assign_ptr_lhs_mem.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 8);
  node_t *typed_assign_ptr =
      ps_node_new_assign(&typed_assign_ptr_lhs_mem, ps_node_new_num(0));
  ASSERT_TRUE(ps_node_is_pointer(typed_assign_ptr));
  ASSERT_EQ(8, ps_node_type_size(typed_assign_ptr));
  ASSERT_EQ(8, ps_node_deref_size(typed_assign_ptr));
  ASSERT_EQ(4, ps_node_base_deref_size(typed_assign_ptr));
  ASSERT_TRUE(ps_node_get_type(typed_assign_ptr) ==
              typed_assign_ptr_lhs_mem.type);

  node_t typed_assign_complex_lhs_mem = {0};
  typed_assign_complex_lhs_mem.kind = ND_LVAR;
  typed_assign_complex_lhs_mem.type = ps_type_new(PSX_TYPE_COMPLEX);
  typed_assign_complex_lhs_mem.type->fp_kind = TK_FLOAT_KIND_DOUBLE;
  typed_assign_complex_lhs_mem.type->size = 16;
  typed_assign_complex_lhs_mem.type->align = 8;
  typed_assign_complex_lhs_mem.fp_kind = TK_FLOAT_KIND_NONE;
  typed_assign_complex_lhs_mem.is_complex = 0;
  node_t *typed_assign_complex = ps_node_new_assign(
      &typed_assign_complex_lhs_mem, ps_node_new_num(0));
  ASSERT_TRUE(ps_node_get_type(typed_assign_complex) ==
              typed_assign_complex_lhs_mem.type);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            ps_node_value_fp_kind(typed_assign_complex));
  ASSERT_TRUE(ps_node_value_is_complex(typed_assign_complex));

  node_t typed_assign_atomic_lhs_mem = {0};
  typed_assign_atomic_lhs_mem.kind = ND_LVAR;
  typed_assign_atomic_lhs_mem.type = ps_type_new_integer(TK_INT, 4, 0);
  typed_assign_atomic_lhs_mem.type->is_atomic = 1;
  typed_assign_atomic_lhs_mem.is_atomic = 0;
  typed_assign_atomic_lhs_mem.is_atomic = 0;
  node_t *typed_assign_atomic = ps_node_new_assign(
      &typed_assign_atomic_lhs_mem, ps_node_new_num(0));
  ASSERT_TRUE(ps_node_get_type(typed_assign_atomic) ==
              typed_assign_atomic_lhs_mem.type);
  ASSERT_EQ(1, ps_node_get_type(typed_assign_atomic)->is_atomic);

  node_t typed_addr_ptr_operand_mem = {0};
  typed_addr_ptr_operand_mem.kind = ND_LVAR;
  typed_addr_ptr_operand_mem.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 8);
  typed_addr_ptr_operand_mem.type->pointer_qual_levels = 1;
  typed_addr_ptr_operand_mem.type->base_deref_size = 4;
  typed_addr_ptr_operand_mem.type->pointer_const_qual_mask = 1;
  node_t *typed_addr_ptr =
      ps_node_new_unary_addr_for(&typed_addr_ptr_operand_mem);
  ASSERT_TRUE(ps_node_is_pointer(typed_addr_ptr));
  ASSERT_EQ(8, ps_node_type_size(typed_addr_ptr));
  ASSERT_EQ(8, ps_node_deref_size(typed_addr_ptr));
  ASSERT_EQ(2, ps_node_pointer_qual_levels(typed_addr_ptr));
  ASSERT_EQ(4, ps_node_base_deref_size(typed_addr_ptr));
  ASSERT_EQ(2u, ps_node_pointer_const_qual_mask(typed_addr_ptr));
  ASSERT_TRUE(ps_node_get_type(typed_addr_ptr)->base ==
              typed_addr_ptr_operand_mem.type);

  node_t typed_addr_unsigned_operand_mem = {0};
  typed_addr_unsigned_operand_mem.kind = ND_LVAR;
  typed_addr_unsigned_operand_mem.type =
      ps_type_new_integer(TK_UNSIGNED, 1, 1);
  node_t *typed_addr_unsigned =
      ps_node_new_unary_addr_for(&typed_addr_unsigned_operand_mem);
  ASSERT_TRUE(ps_node_pointee_is_unsigned(typed_addr_unsigned));

  node_t typed_addr_bool_operand_mem = {0};
  typed_addr_bool_operand_mem.kind = ND_LVAR;
  typed_addr_bool_operand_mem.type = ps_type_new(PSX_TYPE_BOOL);
  typed_addr_bool_operand_mem.type->size = 1;
  node_t *typed_addr_bool =
      ps_node_new_addr_value_for(&typed_addr_bool_operand_mem);
  ASSERT_TRUE(ps_node_pointee_is_bool(typed_addr_bool));

  node_t typed_deref_ptrptr_operand_mem = {0};
  typed_deref_ptrptr_operand_mem.kind = ND_DEREF;
  psx_type_t *typed_deref_inner_ptr =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 4);
  typed_deref_inner_ptr->pointer_qual_levels = 1;
  typed_deref_inner_ptr->base_deref_size = 4;
  psx_type_t *typed_deref_outer_ptr =
      ps_type_new_pointer(typed_deref_inner_ptr, 8);
  typed_deref_outer_ptr->pointer_qual_levels = 2;
  typed_deref_outer_ptr->base_deref_size = 4;
  typed_deref_ptrptr_operand_mem.type = typed_deref_outer_ptr;
  node_t *typed_deref_ptr =
      ps_node_new_unary_deref_for(&typed_deref_ptrptr_operand_mem);
  ASSERT_TRUE(ps_node_is_pointer(typed_deref_ptr));
  ASSERT_EQ(8, ps_node_type_size(typed_deref_ptr));
  ASSERT_EQ(4, ps_node_deref_size(typed_deref_ptr));
  ASSERT_EQ(1, ps_node_pointer_qual_levels(typed_deref_ptr));
  ASSERT_EQ(4, ps_node_base_deref_size(typed_deref_ptr));
  ASSERT_TRUE(ps_node_get_type(typed_deref_ptr) == typed_deref_inner_ptr);

  node_t typed_deref_flat_ptrptr_operand_mem = {0};
  typed_deref_flat_ptrptr_operand_mem.kind = ND_DEREF;
  psx_type_t *typed_deref_flat_ptrptr =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 8);
  typed_deref_flat_ptrptr->pointer_qual_levels = 2;
  typed_deref_flat_ptrptr->base_deref_size = 4;
  typed_deref_flat_ptrptr->funcptr_sig.function.callable.signature.param_int_mask = 3;
  typed_deref_flat_ptrptr->funcptr_sig.function.callable.return_shape.is_data_pointer = 1;
  typed_deref_flat_ptrptr_operand_mem.type = typed_deref_flat_ptrptr;
  node_t *typed_deref_flat_ptr =
      ps_node_new_unary_deref_for(&typed_deref_flat_ptrptr_operand_mem);
  ASSERT_TRUE(!ps_node_is_pointer(typed_deref_flat_ptr));
  ASSERT_EQ(4, ps_node_type_size(typed_deref_flat_ptr));
  ASSERT_EQ(0, ps_node_deref_size(typed_deref_flat_ptr));
  ASSERT_EQ(0, ps_node_pointer_qual_levels(typed_deref_flat_ptr));
  ASSERT_TRUE(ps_node_get_type(typed_deref_flat_ptr) ==
              typed_deref_flat_ptrptr->base);

  node_t typed_nested_ptr_stale_quals_mem = {0};
  typed_nested_ptr_stale_quals_mem.kind = ND_LVAR;
  psx_type_t *typed_nested_ptr_inner =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 4);
  typed_nested_ptr_inner->pointer_const_qual_mask = 1u;
  typed_nested_ptr_inner->pointer_volatile_qual_mask = 1u;
  psx_type_t *typed_nested_ptr_outer =
      ps_type_new_pointer(typed_nested_ptr_inner, 8);
  typed_nested_ptr_outer->pointer_qual_levels = 1;
  typed_nested_ptr_outer->pointer_const_qual_mask = 1u;
  typed_nested_ptr_outer->pointer_volatile_qual_mask = 0u;
  typed_nested_ptr_stale_quals_mem.type = typed_nested_ptr_outer;
  ASSERT_EQ(2, ps_node_pointer_qual_levels(
                   &typed_nested_ptr_stale_quals_mem));
  ASSERT_EQ(3u, ps_node_pointer_const_qual_mask(
                    &typed_nested_ptr_stale_quals_mem));
  ASSERT_EQ(2u, ps_node_pointer_volatile_qual_mask(
                    &typed_nested_ptr_stale_quals_mem));

  node_t canonical_nested_ptr = {0};
  canonical_nested_ptr.kind = ND_DEREF;
  psx_type_t *raw_nested_ptr_type = ps_type_wrap_pointer_levels(
      ps_type_new_integer(TK_INT, 4, 0), 3, 8, 4, 5u, 6u);
  canonical_nested_ptr.type = raw_nested_ptr_type;
  ASSERT_TRUE(raw_nested_ptr_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, raw_nested_ptr_type->kind);
  ASSERT_EQ(3, ps_type_pointer_view_structural_qual_levels(raw_nested_ptr_type));
  ASSERT_EQ(5u, ps_type_pointer_view_structural_qual_mask(raw_nested_ptr_type, 0));
  ASSERT_EQ(6u, ps_type_pointer_view_structural_qual_mask(raw_nested_ptr_type, 1));
  ASSERT_TRUE(raw_nested_ptr_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, raw_nested_ptr_type->base->kind);
  ASSERT_EQ(2, ps_type_pointer_view_structural_qual_levels(raw_nested_ptr_type->base));
  ASSERT_EQ(2u, ps_type_pointer_view_structural_qual_mask(raw_nested_ptr_type->base, 0));
  ASSERT_EQ(3u, ps_type_pointer_view_structural_qual_mask(raw_nested_ptr_type->base, 1));
  ASSERT_TRUE(raw_nested_ptr_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, raw_nested_ptr_type->base->base->kind);
  ASSERT_EQ(4, ps_type_deref_size(raw_nested_ptr_type->base->base));
  ASSERT_TRUE(ps_node_get_type(&canonical_nested_ptr) ==
              raw_nested_ptr_type);
  ASSERT_EQ(3, ps_type_pointer_view_structural_qual_levels(
                   ps_node_get_type(&canonical_nested_ptr)));

  node_t typed_ptr_mem = {0};
  typed_ptr_mem.kind = ND_LVAR;
  typed_ptr_mem.is_unsigned = 1;
  psx_type_t *typed_ptr_base = ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8);
  typed_ptr_base->is_const_qualified = 1;
  psx_type_t *typed_ptr_row = ps_type_new_array(
      typed_ptr_base, 2, 16, 8, 0);
  typed_ptr_row->outer_stride = 8;
  typed_ptr_mem.type = ps_type_new_pointer(typed_ptr_row, 16);
  typed_ptr_mem.type->pointer_qual_levels = 1;
  typed_ptr_mem.type->pointer_const_qual_mask = 3;
  typed_ptr_mem.type->pointer_volatile_qual_mask = 1;
  typed_ptr_mem.type->base_deref_size = 8;
  typed_ptr_mem.type->ptr_array_pointee_bytes = 16;
  typed_ptr_mem.type->vla_row_stride_frame_off = 24;
  typed_ptr_mem.type->vla_strides_remaining = 3;
  typed_ptr_mem.type->outer_stride = 16;
  ASSERT_TRUE(ps_node_is_pointer(&typed_ptr_mem));
  ASSERT_TRUE(!ps_node_conversion_value_is_unsigned(&typed_ptr_mem));
  ASSERT_EQ(16, ps_node_deref_size(&typed_ptr_mem));
  ASSERT_EQ(1, ps_node_pointer_qual_levels(&typed_ptr_mem));
  ASSERT_EQ(1u, ps_node_pointer_const_qual_mask(&typed_ptr_mem));
  ASSERT_EQ(1u, ps_node_pointer_volatile_qual_mask(&typed_ptr_mem));
  ASSERT_EQ(8, ps_node_base_deref_size(&typed_ptr_mem));
  ASSERT_EQ(16, ps_node_ptr_array_pointee_bytes(&typed_ptr_mem));
  ASSERT_EQ(24, ps_node_vla_row_stride_frame_off(&typed_ptr_mem));
  int typed_inner_stride = 0;
  int typed_next_stride = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(&typed_ptr_mem,
                                               &typed_inner_stride,
                                               &typed_next_stride,
                                               NULL, NULL));
  ASSERT_EQ(8, typed_inner_stride);
  ASSERT_EQ(0, typed_next_stride);
  node_t *typed_vla_sub = ps_node_new_subscript_deref_for(
      &typed_ptr_mem, ps_node_new_num(0), ps_node_new_num(0),
      16, 8, 0, NULL, 0);
  ASSERT_EQ(32, ps_node_vla_row_stride_frame_off(typed_vla_sub));

  node_t typed_plain_ptr_stale_ptr_array = {0};
  typed_plain_ptr_stale_ptr_array.kind = ND_LVAR;
  typed_plain_ptr_stale_ptr_array.type = ps_type_new_pointer(
      ps_type_new_integer(TK_INT, 4, 0), 4);
  typed_plain_ptr_stale_ptr_array.type->outer_stride = 48;
  typed_plain_ptr_stale_ptr_array.type->mid_stride = 16;
  typed_plain_ptr_stale_ptr_array.type->extra_strides_count = 1;
  typed_plain_ptr_stale_ptr_array.type->extra_strides[0] = 8;
  ASSERT_TRUE(ps_node_is_pointer(&typed_plain_ptr_stale_ptr_array));
  ASSERT_EQ(0, ps_node_ptr_array_pointee_bytes(&typed_plain_ptr_stale_ptr_array));
  ASSERT_EQ(0, ps_node_vla_row_stride_frame_off(&typed_plain_ptr_stale_ptr_array));
  ASSERT_TRUE(!ps_node_pointer_stride_metadata(
      &typed_plain_ptr_stale_ptr_array, NULL, NULL, NULL, NULL));
  node_t typed_scalar_array_stale_stride = {0};
  typed_scalar_array_stale_stride.kind = ND_LVAR;
  typed_scalar_array_stale_stride.type = ps_type_new_array(
      ps_type_new_integer(TK_INT, 4, 0), 4, 16, 4, 0);
  typed_scalar_array_stale_stride.type->outer_stride = 48;
  typed_scalar_array_stale_stride.type->mid_stride = 16;
  typed_scalar_array_stale_stride.type->extra_strides_count = 1;
  typed_scalar_array_stale_stride.type->extra_strides[0] = 8;
  ASSERT_TRUE(!ps_node_pointer_stride_metadata(
      &typed_scalar_array_stale_stride, NULL, NULL, NULL, NULL));
  node_t typed_addr_stale_mem_stride = {0};
  typed_addr_stale_mem_stride.kind = ND_ADDR;
  typed_addr_stale_mem_stride.type = ps_type_new_pointer(
      ps_type_new_integer(TK_INT, 4, 0), 4);
  ASSERT_TRUE(!ps_node_pointer_stride_metadata(
      &typed_addr_stale_mem_stride, NULL, NULL, NULL, NULL));
  node_t typed_deref_stale_mem_stride = typed_addr_stale_mem_stride;
  typed_deref_stale_mem_stride.kind = ND_DEREF;
  typed_deref_stale_mem_stride.type = ps_type_new_array(
      ps_type_new_integer(TK_INT, 4, 0), 4, 16, 4, 0);
  ASSERT_TRUE(!ps_node_pointer_stride_metadata(
      &typed_deref_stale_mem_stride, NULL, NULL, NULL, NULL));
  ASSERT_EQ(2, ps_type_pointer_view_vla_strides_remaining(
                   ps_node_get_type(typed_vla_sub)));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_node_pointee_fp_kind(&typed_ptr_mem));
  ASSERT_TRUE(ps_node_pointee_is_const_qualified(&typed_ptr_mem));
  ASSERT_TRUE(!ps_node_pointee_is_volatile_qualified(&typed_ptr_mem));
  ASSERT_TRUE(!ps_node_pointee_is_unsigned(&typed_ptr_mem));
  ASSERT_TRUE(!ps_node_pointee_is_bool(&typed_ptr_mem));
  ASSERT_TRUE(!ps_node_pointee_is_void(&typed_ptr_mem));

  node_t typed_ptr_stale_base_deref_mem = {0};
  typed_ptr_stale_base_deref_mem.kind = ND_LVAR;
  typed_ptr_stale_base_deref_mem.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 8);
  typed_ptr_stale_base_deref_mem.type->base_deref_size = 99;
  ASSERT_EQ(4, ps_node_base_deref_size(&typed_ptr_stale_base_deref_mem));

  node_t typed_ptrarr_stale_base_deref_mem = {0};
  typed_ptrarr_stale_base_deref_mem.kind = ND_LVAR;
  psx_type_t *typed_ptrarr_stale_base =
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 3, 12, 4, 0);
  typed_ptrarr_stale_base_deref_mem.type =
      ps_type_new_pointer(typed_ptrarr_stale_base, 12);
  typed_ptrarr_stale_base_deref_mem.type->base_deref_size = 99;
  ASSERT_EQ(4, ps_node_base_deref_size(&typed_ptrarr_stale_base_deref_mem));
  typed_ptrarr_stale_base_deref_mem.type->ptr_array_pointee_bytes = 99;
  ASSERT_EQ(12, ps_node_ptr_array_pointee_bytes(
                    &typed_ptrarr_stale_base_deref_mem));

  node_t typed_signed_ptr_stale_unsigned_mem = {0};
  typed_signed_ptr_stale_unsigned_mem.kind = ND_LVAR;
  typed_signed_ptr_stale_unsigned_mem.is_unsigned = 1;
  typed_signed_ptr_stale_unsigned_mem.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 8);
  node_t *typed_signed_ptr_sub = ps_node_new_subscript_deref_for(
      &typed_signed_ptr_stale_unsigned_mem, ps_node_new_num(0),
      ps_node_new_num(0), 4, 0, 0, NULL, 0);
  ASSERT_TRUE(!ps_node_is_unsigned_type(typed_signed_ptr_sub));

  node_t typed_ptr_stale_pointee_scalar_ptr_mem = {0};
  typed_ptr_stale_pointee_scalar_ptr_mem.kind = ND_LVAR;
  typed_ptr_stale_pointee_scalar_ptr_mem.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 8);
  node_t *typed_ptr_stale_pointee_scalar_sub =
      ps_node_new_subscript_deref_for(
          &typed_ptr_stale_pointee_scalar_ptr_mem, ps_node_new_num(0),
          ps_node_new_num(0), 4, 0, 0, NULL, 0);
  ASSERT_TRUE(!ps_node_is_pointer(typed_ptr_stale_pointee_scalar_sub));
  ASSERT_EQ(0, ps_node_deref_size(typed_ptr_stale_pointee_scalar_sub));

  node_t typed_deref_stale_scalar_ptr_member = {0};
  typed_deref_stale_scalar_ptr_member.kind = ND_DEREF;
  typed_deref_stale_scalar_ptr_member.type = ps_type_new_pointer(
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 8), 8);
  typed_deref_stale_scalar_ptr_member.type->pointer_qual_levels = 2;
  typed_deref_stale_scalar_ptr_member.type->base_deref_size = 4;
  ASSERT_TRUE(!ps_node_scalar_ptr_member_lvalue(
      &typed_deref_stale_scalar_ptr_member));
  node_t *typed_deref_sub = ps_node_new_subscript_deref_for(
      &typed_deref_stale_scalar_ptr_member, ps_node_new_num(0),
      ps_node_new_num(0), 8, 0, 0, NULL, 0);
  ASSERT_EQ(1, ps_node_pointer_qual_levels(typed_deref_sub));
  ASSERT_EQ(4, ps_node_base_deref_size(typed_deref_sub));

  node_t subscript_row_lvalue = {0};
  subscript_row_lvalue.kind = ND_DEREF;
  subscript_row_lvalue.type = ps_type_new_pointer(
      ps_type_new_integer(TK_INT, 4, 0), 4);
  subscript_row_lvalue.type_state.subscript_uses_base_address = 1;
  ASSERT_TRUE(ps_node_subscript_deref_uses_base_address(
      &subscript_row_lvalue));

  node_t typed_row_array = {0};
  typed_row_array.kind = ND_DEREF;
  psx_type_t *typed_row_base = ps_type_new_integer(TK_INT, 4, 0);
  typed_row_array.type = ps_type_new_array(typed_row_base, 4, 16, 4, 0);
  psx_type_t *typed_row_decay =
      ps_node_row_decay_pointer_arith_type(&typed_row_array);
  ASSERT_TRUE(typed_row_decay != NULL);
  ASSERT_TRUE(typed_row_decay->kind == PSX_TYPE_POINTER);
  ASSERT_TRUE(typed_row_decay->base == typed_row_base);
  ASSERT_TRUE(!ps_type_is_unsigned(typed_row_decay->base));
  ASSERT_EQ(4, typed_row_decay->deref_size);
  ASSERT_EQ(4, typed_row_decay->base_deref_size);
  ASSERT_EQ(0, typed_row_decay->ptr_array_pointee_bytes);
  ASSERT_EQ(0, typed_row_decay->outer_stride);

  node_t typed_nonarray_stale_row = {0};
  typed_nonarray_stale_row.kind = ND_DEREF;
  typed_nonarray_stale_row.type = ps_type_new_integer(TK_INT, 16, 0);
  ASSERT_TRUE(ps_node_row_decay_pointer_arith_type(
                  &typed_nonarray_stale_row) == NULL);

  node_t typed_decay_array = {0};
  typed_decay_array.kind = ND_DEREF;
  typed_decay_array.type = ps_type_new_array(
      ps_type_new_integer(TK_INT, 4, 0), 2, 8, 4, 0);
  ASSERT_TRUE(ps_node_deref_decays_to_address(&typed_decay_array));

  node_t typed_ptr_stale_array_decay = {0};
  typed_ptr_stale_array_decay.kind = ND_DEREF;
  typed_ptr_stale_array_decay.type = ps_type_new_pointer(
      ps_type_new_integer(TK_INT, 4, 0), 8);
  ASSERT_TRUE(!ps_node_deref_decays_to_address(
      &typed_ptr_stale_array_decay));

  node_t canonical_decay_array = {0};
  canonical_decay_array.kind = ND_DEREF;
  canonical_decay_array.type = ps_type_new_array(
      ps_type_new_integer(TK_INT, 4, 0), 2, 8, 4, 0);
  ASSERT_TRUE(ps_node_deref_decays_to_address(&canonical_decay_array));

  node_t canonical_loaded_pointer = {0};
  canonical_loaded_pointer.kind = ND_DEREF;
  canonical_loaded_pointer.type = ps_type_new_pointer(
      ps_type_new_integer(TK_INT, 4, 0), 4);
  ASSERT_TRUE(!ps_node_deref_decays_to_address(&canonical_loaded_pointer));

  node_t canonical_struct_scalar = {0};
  canonical_struct_scalar.kind = ND_DEREF;
  canonical_struct_scalar.type = ps_type_new_tag(
      TK_STRUCT, "ScalarStruct", 12, 0, 32);
  ASSERT_TRUE(!ps_node_deref_decays_to_address(&canonical_struct_scalar));
  ASSERT_EQ(PSX_TYPE_STRUCT, ps_node_get_type(&canonical_struct_scalar)->kind);

  parsed_code = parse_program_input(
      "int __tm_ptrarr2d(void) { int a[2][3]; int (*p)[3] = a; return p[1][2]; }");
  fn = as_func(parsed_code[0]);
  lvar_t *ptrarr2d_p = find_func_lvar(fn, "p");
  ASSERT_TRUE(ptrarr2d_p != NULL);
  ASSERT_TRUE(ptrarr2d_p->decl_type != NULL);
  ASSERT_EQ(12, ptrarr2d_p->decl_type->ptr_array_pointee_bytes);
  ASSERT_EQ(12, ptrarr2d_p->decl_type->outer_stride);
  ASSERT_EQ(0, ptrarr2d_p->decl_type->mid_stride);
  ASSERT_EQ(4, ptrarr2d_p->decl_type->base_deref_size);
  node_t *ptrarr2d_p_node = ps_node_new_lvar_identifier_ref_for(ptrarr2d_p);
  ASSERT_EQ(12, ps_node_deref_size(ptrarr2d_p_node));
  ASSERT_EQ(12, ps_node_ptr_array_pointee_bytes(ptrarr2d_p_node));
  int ptrarr2d_inner = 0;
  int ptrarr2d_next = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(ptrarr2d_p_node,
                                               &ptrarr2d_inner,
                                               &ptrarr2d_next,
                                               NULL, NULL));
  ASSERT_EQ(4, ptrarr2d_inner);
  ASSERT_EQ(0, ptrarr2d_next);
  node_t *ptrarr2d_row = ps_node_new_subscript_deref_for(
      ptrarr2d_p_node, ptrarr2d_p_node, ps_node_new_num(0),
      ps_node_deref_size(ptrarr2d_p_node), ptrarr2d_inner, ptrarr2d_next, NULL, 0);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(ptrarr2d_row)->kind);
  ASSERT_EQ(12, ps_node_type_size(ptrarr2d_row));
  ASSERT_EQ(4, ps_node_deref_size(ptrarr2d_row));
  ASSERT_TRUE(ps_node_subscript_deref_uses_base_address(ptrarr2d_row));
  ASSERT_TRUE(ps_node_deref_decays_to_address(ptrarr2d_row));

  parsed_code = parse_program_input(
      "typedef int __tm_Row3[3]; typedef int (*__tm_PA3)[3]; "
      "int __tm_typedef_ptrarr(void) { __tm_Row3 *a; __tm_PA3 b; return 0; }");
  fn = as_func(parsed_code[0]);
  lvar_t *typedef_row_ptr = find_func_lvar(fn, "a");
  lvar_t *typedef_ptrarr = find_func_lvar(fn, "b");
  ASSERT_TRUE(typedef_row_ptr != NULL);
  ASSERT_TRUE(typedef_ptrarr != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, typedef_row_ptr->decl_type->kind);
  ASSERT_EQ(PSX_TYPE_ARRAY, typedef_row_ptr->decl_type->base->kind);
  ASSERT_EQ(3, typedef_row_ptr->decl_type->base->array_len);
  ASSERT_EQ(12, ps_type_deref_size(typedef_row_ptr->decl_type));
  ASSERT_EQ(PSX_TYPE_POINTER, typedef_ptrarr->decl_type->kind);
  ASSERT_EQ(PSX_TYPE_ARRAY, typedef_ptrarr->decl_type->base->kind);
  ASSERT_EQ(3, typedef_ptrarr->decl_type->base->array_len);
  ASSERT_EQ(12, ps_type_deref_size(typedef_ptrarr->decl_type));

  parsed_code = parse_program_input(
      "typedef int *TMDArrayPtr[3]; typedef int (*TMDPtrArray)[3]; "
      "int __tm_toplevel_decl_shape(void) { return 0; }");
  (void)parsed_code;
  psx_typedef_info_t tm_decl_array_ptr = {0};
  psx_typedef_info_t tm_decl_ptr_array = {0};
  ASSERT_TRUE(ps_ctx_find_typedef_name("TMDArrayPtr", 11,
                                        &tm_decl_array_ptr));
  ASSERT_TRUE(ps_ctx_find_typedef_name("TMDPtrArray", 11,
                                        &tm_decl_ptr_array));
  const psx_type_t *tm_array_ptr_type =
      ps_ctx_typedef_decl_type(&tm_decl_array_ptr);
  const psx_type_t *tm_ptr_array_type =
      ps_ctx_typedef_decl_type(&tm_decl_ptr_array);
  ASSERT_TRUE(tm_array_ptr_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, tm_array_ptr_type->kind);
  ASSERT_EQ(3, tm_array_ptr_type->array_len);
  ASSERT_TRUE(tm_array_ptr_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, tm_array_ptr_type->base->kind);
  ASSERT_TRUE(tm_array_ptr_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, tm_array_ptr_type->base->base->kind);
  ASSERT_TRUE(tm_ptr_array_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, tm_ptr_array_type->kind);
  ASSERT_TRUE(tm_ptr_array_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, tm_ptr_array_type->base->kind);
  ASSERT_EQ(3, tm_ptr_array_type->base->array_len);
  ASSERT_TRUE(tm_ptr_array_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, tm_ptr_array_type->base->base->kind);

  parsed_code = parse_program_input(
      "int __tm_ptrarr_leaf_flags(void) { "
      "  unsigned char uc[1][3]; unsigned char (*up)[3] = uc; "
      "  _Bool bb[1][2]; _Bool (*bp)[2] = bb; return 0; }");
  fn = as_func(parsed_code[0]);
  lvar_t *leaf_up = find_func_lvar(fn, "up");
  ASSERT_TRUE(leaf_up != NULL);
  node_t *leaf_up_node = ps_node_new_lvar_identifier_ref_for(leaf_up);
  ASSERT_TRUE(ps_node_pointee_is_unsigned(leaf_up_node));
  ASSERT_TRUE(!ps_node_is_unsigned_type(leaf_up_node));
  ASSERT_TRUE(!ps_node_pointee_is_bool(leaf_up_node));
  ASSERT_EQ(3, ps_node_deref_size(leaf_up_node));
  ASSERT_EQ(1, ps_node_base_deref_size(leaf_up_node));

  lvar_t *leaf_bp = find_func_lvar(fn, "bp");
  ASSERT_TRUE(leaf_bp != NULL);
  node_t *leaf_bp_node = ps_node_new_lvar_identifier_ref_for(leaf_bp);
  ASSERT_TRUE(ps_node_pointee_is_bool(leaf_bp_node));
  ASSERT_TRUE(!ps_node_pointee_is_unsigned(leaf_bp_node));
  ASSERT_EQ(2, ps_node_deref_size(leaf_bp_node));
  ASSERT_EQ(1, ps_node_base_deref_size(leaf_bp_node));

  parsed_code = parse_program_input(
      "int __tm_scalar_unsigned_not_pointee(void) { "
      "unsigned char u = 1; return u; }");
  fn = as_func(parsed_code[0]);
  lvar_t *leaf_scalar_u = find_func_lvar(fn, "u");
  ASSERT_TRUE(leaf_scalar_u != NULL);
  node_t *leaf_scalar_u_node = ps_node_new_lvar_identifier_ref_for(leaf_scalar_u);
  ASSERT_TRUE(ps_node_is_unsigned_type(leaf_scalar_u_node));
  ASSERT_TRUE(!ps_node_pointee_is_unsigned(leaf_scalar_u_node));

  parsed_code = parse_program_input(
      "int __tm_scalar_bool_not_pointee(void) { "
      "_Bool b = 1; return b; }");
  fn = as_func(parsed_code[0]);
  lvar_t *leaf_scalar_b = find_func_lvar(fn, "b");
  ASSERT_TRUE(leaf_scalar_b != NULL);
  node_t *leaf_scalar_b_node = ps_node_new_lvar_identifier_ref_for(leaf_scalar_b);
  ASSERT_EQ(PSX_TYPE_BOOL, ps_node_get_type(leaf_scalar_b_node)->kind);
  ASSERT_TRUE(!ps_node_pointee_is_bool(leaf_scalar_b_node));

  parsed_code = parse_program_input(
      "unsigned char __tm_guc[1][3]; unsigned char (*__tm_gup)[3] = __tm_guc; "
      "_Bool __tm_gb[1][2]; _Bool (*__tm_gbp)[2] = __tm_gb; "
      "int __tm_gptrarr_leaf_flags(void) { return 0; }");
  (void)parsed_code;
  global_var_t *leaf_gup = ps_find_global_var("__tm_gup", 8);
  ASSERT_TRUE(leaf_gup != NULL);
  node_t *leaf_gup_node = ps_node_new_gvar_for(leaf_gup);
  ASSERT_TRUE(ps_node_pointee_is_unsigned(leaf_gup_node));
  ASSERT_TRUE(!ps_node_pointee_is_bool(leaf_gup_node));

  global_var_t *leaf_gbp = ps_find_global_var("__tm_gbp", 8);
  ASSERT_TRUE(leaf_gbp != NULL);
  node_t *leaf_gbp_node = ps_node_new_gvar_for(leaf_gbp);
  ASSERT_TRUE(ps_node_pointee_is_bool(leaf_gbp_node));
  ASSERT_TRUE(!ps_node_pointee_is_unsigned(leaf_gbp_node));

  parsed_code = parse_program_input(
      "unsigned char *__tm_gucp; _Bool *__tm_gbp_scalar; "
      "int __tm_gptr_leaf_flags(void) { return 0; }");
  (void)parsed_code;
  global_var_t *leaf_gucp = ps_find_global_var("__tm_gucp",
                                                sizeof("__tm_gucp") - 1);
  ASSERT_TRUE(leaf_gucp != NULL);
  node_t *leaf_gucp_node = ps_node_new_gvar_for(leaf_gucp);
  ASSERT_TRUE(ps_node_pointee_is_unsigned(leaf_gucp_node));
  ASSERT_TRUE(!ps_node_pointee_is_bool(leaf_gucp_node));

  global_var_t *leaf_gbp_scalar = ps_find_global_var(
      "__tm_gbp_scalar", sizeof("__tm_gbp_scalar") - 1);
  ASSERT_TRUE(leaf_gbp_scalar != NULL);
  node_t *leaf_gbp_scalar_node = ps_node_new_gvar_for(leaf_gbp_scalar);
  ASSERT_TRUE(ps_node_pointee_is_bool(leaf_gbp_scalar_node));
  ASSERT_TRUE(!ps_node_pointee_is_unsigned(leaf_gbp_scalar_node));

  parsed_code = parse_program_input(
      "unsigned char __tm_gscalar_u; int __tm_gscalar_use(void) { return 0; }");
  (void)parsed_code;
  global_var_t *leaf_gscalar_u = ps_find_global_var(
      "__tm_gscalar_u", sizeof("__tm_gscalar_u") - 1);
  ASSERT_TRUE(leaf_gscalar_u != NULL);
  node_t *leaf_gscalar_u_node = ps_node_new_gvar_for(leaf_gscalar_u);
  ASSERT_TRUE(ps_node_is_unsigned_type(leaf_gscalar_u_node));
  leaf_gscalar_u_node->type = NULL;
  ASSERT_TRUE(!ps_node_pointee_is_unsigned(leaf_gscalar_u_node));

  parsed_code = parse_program_input(
      "_Bool __tm_gscalar_b; int __tm_gscalar_bool_use(void) { return 0; }");
  (void)parsed_code;
  global_var_t *leaf_gscalar_b = ps_find_global_var(
      "__tm_gscalar_b", sizeof("__tm_gscalar_b") - 1);
  ASSERT_TRUE(leaf_gscalar_b != NULL);
  node_t *leaf_gscalar_b_node = ps_node_new_gvar_for(leaf_gscalar_b);
  ASSERT_EQ(PSX_TYPE_BOOL, ps_node_get_type(leaf_gscalar_b_node)->kind);
  leaf_gscalar_b_node->type = NULL;
  ASSERT_TRUE(!ps_node_pointee_is_bool(leaf_gscalar_b_node));

  psx_type_t *stale_bool_pointer_type = ps_type_new_pointer(
      ps_type_new_integer(TK_INT, 4, 0), 4);
  ASSERT_TRUE(stale_bool_pointer_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, stale_bool_pointer_type->kind);
  ASSERT_TRUE(stale_bool_pointer_type->base != NULL);
  ASSERT_TRUE(stale_bool_pointer_type->base->kind != PSX_TYPE_BOOL);

  psx_type_t *legacy_bool_array_type = ps_type_new_array(
      ps_type_new_integer(TK_BOOL, 1, 1), 2, 2, 1, 0);
  ASSERT_TRUE(legacy_bool_array_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, legacy_bool_array_type->kind);
  ASSERT_TRUE(legacy_bool_array_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_BOOL, legacy_bool_array_type->base->kind);

  psx_type_t *legacy_unsigned_array_type = ps_type_new_array(
      ps_type_new_integer(TK_UNSIGNED, 1, 1), 3, 3, 1, 0);
  ASSERT_TRUE(legacy_unsigned_array_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, legacy_unsigned_array_type->kind);
  ASSERT_TRUE(legacy_unsigned_array_type->base != NULL);
  ASSERT_TRUE(ps_type_is_unsigned(legacy_unsigned_array_type->base));

  psx_type_t *legacy_bool_2d_row = ps_type_new_array(
      ps_type_new_integer(TK_BOOL, 1, 1), 3, 3, 1, 0);
  psx_type_t *legacy_bool_2d_array_type = ps_type_new_array(
      legacy_bool_2d_row, 2, 6, 3, 0);
  ASSERT_TRUE(legacy_bool_2d_array_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, legacy_bool_2d_array_type->kind);
  ASSERT_TRUE(legacy_bool_2d_array_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, legacy_bool_2d_array_type->base->kind);
  ASSERT_TRUE(legacy_bool_2d_array_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_BOOL, legacy_bool_2d_array_type->base->base->kind);

  psx_type_t *legacy_unsigned_2d_row = ps_type_new_array(
      ps_type_new_integer(TK_UNSIGNED, 1, 1), 3, 3, 1, 0);
  psx_type_t *legacy_unsigned_2d_array_type = ps_type_new_array(
      legacy_unsigned_2d_row, 2, 6, 3, 0);
  ASSERT_TRUE(legacy_unsigned_2d_array_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, legacy_unsigned_2d_array_type->kind);
  ASSERT_TRUE(legacy_unsigned_2d_array_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, legacy_unsigned_2d_array_type->base->kind);
  ASSERT_TRUE(legacy_unsigned_2d_array_type->base->base != NULL);
  ASSERT_TRUE(ps_type_is_unsigned(legacy_unsigned_2d_array_type->base->base));

  parsed_code = parse_program_input(
      "typedef int __tm_M[2][3][4]; "
      "int __tm_ptrarr3d(__tm_M *p) { return (*p)[1][2][3]; }");
  fn = as_func(parsed_code[0]);
  lvar_t *ptrarr3d_p = find_func_lvar(fn, "p");
  ASSERT_TRUE(ptrarr3d_p != NULL);
  ASSERT_TRUE(ptrarr3d_p->decl_type != NULL);
  ASSERT_EQ(96, ptrarr3d_p->decl_type->ptr_array_pointee_bytes);
  ASSERT_EQ(96, ptrarr3d_p->decl_type->outer_stride);
  ASSERT_EQ(48, ptrarr3d_p->decl_type->mid_stride);
  ASSERT_EQ(16, ptrarr3d_p->decl_type->extra_strides[0]);
  ASSERT_EQ(1, ptrarr3d_p->decl_type->extra_strides_count);
  ASSERT_TRUE(ptrarr3d_p->decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ptrarr3d_p->decl_type->base->kind);
  ASSERT_EQ(2, ptrarr3d_p->decl_type->base->array_len);
  ASSERT_TRUE(ptrarr3d_p->decl_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ptrarr3d_p->decl_type->base->base->kind);
  ASSERT_EQ(3, ptrarr3d_p->decl_type->base->base->array_len);
  ASSERT_TRUE(ptrarr3d_p->decl_type->base->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ptrarr3d_p->decl_type->base->base->base->kind);
  ASSERT_EQ(4, ptrarr3d_p->decl_type->base->base->base->array_len);
  ASSERT_TRUE(ptrarr3d_p->decl_type->base->base->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER,
            ptrarr3d_p->decl_type->base->base->base->base->kind);
  node_t *ptrarr3d_p_node = ps_node_new_lvar_identifier_ref_for(ptrarr3d_p);
  ASSERT_EQ(96, ps_node_deref_size(ptrarr3d_p_node));
  int ptrarr3d_inner = 0;
  int ptrarr3d_next = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(ptrarr3d_p_node,
                                               &ptrarr3d_inner,
                                               &ptrarr3d_next,
                                               NULL, NULL));
  ASSERT_EQ(48, ptrarr3d_inner);
  ASSERT_EQ(16, ptrarr3d_next);
  node_t *ptrarr3d_array = ps_node_new_unary_deref_for(ptrarr3d_p_node);
  ASSERT_EQ(96, ps_node_type_size(ptrarr3d_array));
  ASSERT_EQ(48, ps_node_deref_size(ptrarr3d_array));
  ptrarr3d_inner = 0;
  ptrarr3d_next = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(ptrarr3d_array,
                                               &ptrarr3d_inner,
                                               &ptrarr3d_next,
                                               NULL, NULL));
  ASSERT_EQ(16, ptrarr3d_inner);
  ASSERT_EQ(4, ptrarr3d_next);

  parsed_code = parse_program_input(
      "struct __tm_ptrarr2d_S { int a; int b; }; "
      "int __tm_ptrarr2d_struct(void) { "
      "struct __tm_ptrarr2d_S a[2][3]; "
      "struct __tm_ptrarr2d_S (*p)[2][3] = &a; "
      "return (*p)[0][0].a; }");
  fn = as_func(parsed_code[0]);
  lvar_t *ptrarr2d_struct_a = find_func_lvar(fn, "a");
  ASSERT_TRUE(ptrarr2d_struct_a != NULL);
  ASSERT_TRUE(ps_lvar_is_tag_aggregate(ptrarr2d_struct_a));
  ASSERT_TRUE(ps_lvar_is_struct_aggregate(ptrarr2d_struct_a));
  ASSERT_TRUE(!ps_lvar_is_union_aggregate(ptrarr2d_struct_a));
  lvar_t *ptrarr2d_struct_p = find_func_lvar(fn, "p");
  ASSERT_TRUE(ptrarr2d_struct_p != NULL);
  node_t *ptrarr2d_struct_p_node =
      ps_node_new_lvar_identifier_ref_for(ptrarr2d_struct_p);
  node_t *ptrarr2d_struct_array =
      ps_node_new_unary_deref_for(ptrarr2d_struct_p_node);
  ASSERT_TRUE(ps_node_get_type(ptrarr2d_struct_array) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(ptrarr2d_struct_array)->kind);
  ASSERT_EQ(48, ps_node_type_size(ptrarr2d_struct_array));
  ASSERT_EQ(24, ps_node_deref_size(ptrarr2d_struct_array));
  ASSERT_TRUE(ps_node_deref_decays_to_address(ptrarr2d_struct_array));
  int ptrarr2d_struct_inner = 0;
  int ptrarr2d_struct_next = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(ptrarr2d_struct_array,
                                               &ptrarr2d_struct_inner,
                                               &ptrarr2d_struct_next,
                                               NULL, NULL));
  ASSERT_EQ(8, ptrarr2d_struct_inner);
  ASSERT_EQ(0, ptrarr2d_struct_next);
  node_t *ptrarr2d_struct_row = ps_node_new_subscript_deref_for(
      ptrarr2d_struct_array,
      ptrarr2d_struct_array->lhs ? ptrarr2d_struct_array->lhs
                                 : ptrarr2d_struct_array,
      ps_node_new_num(0), ps_node_deref_size(ptrarr2d_struct_array),
      ptrarr2d_struct_inner, ptrarr2d_struct_next, NULL, 0);
  ASSERT_TRUE(ps_node_get_type(ptrarr2d_struct_row) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(ptrarr2d_struct_row)->kind);
  ASSERT_EQ(24, ps_node_type_size(ptrarr2d_struct_row));
  ASSERT_EQ(8, ps_node_deref_size(ptrarr2d_struct_row));
  ASSERT_TRUE(ps_node_subscript_deref_uses_base_address(ptrarr2d_struct_row));
  node_t *ptrarr2d_struct_elem = ps_node_new_subscript_deref_for(
      ptrarr2d_struct_row,
      ptrarr2d_struct_row->lhs ? ptrarr2d_struct_row->lhs
                               : ptrarr2d_struct_row,
      ps_node_new_num(0), ps_node_deref_size(ptrarr2d_struct_row),
      0, 0, NULL, 0);
  ASSERT_TRUE(ps_node_get_type(ptrarr2d_struct_elem) != NULL);
  ASSERT_EQ(PSX_TYPE_STRUCT, ps_node_get_type(ptrarr2d_struct_elem)->kind);
  ASSERT_EQ(8, ps_node_type_size(ptrarr2d_struct_elem));
  ASSERT_EQ(0, ps_node_deref_size(ptrarr2d_struct_elem));
  ASSERT_TRUE(!ps_node_deref_decays_to_address(ptrarr2d_struct_elem));

  parsed_code = parse_program_input(
      "int __tm_array3d(void) { int a[2][2][3]; return a[0][1][0]; }");
  fn = as_func(parsed_code[0]);
  lvar_t *array3d_a = find_func_lvar(fn, "a");
  ASSERT_TRUE(array3d_a != NULL);
  ASSERT_TRUE(array3d_a->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, array3d_a->decl_type->kind);
  ASSERT_EQ(24, array3d_a->decl_type->outer_stride);
  ASSERT_EQ(12, array3d_a->decl_type->mid_stride);
  node_t *array3d_addr = ps_node_new_lvar_array_addr_for(array3d_a, 0);
  ASSERT_TRUE(ps_node_is_pointer(array3d_addr));
  ASSERT_EQ(24, ps_node_deref_size(array3d_addr));
  int array3d_inner = 0;
  int array3d_next = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(array3d_addr,
                                               &array3d_inner,
                                               &array3d_next,
                                               NULL, NULL));
  ASSERT_EQ(12, array3d_inner);
  ASSERT_EQ(4, array3d_next);
  node_t *array3d_row = ps_node_new_subscript_deref_for(
      array3d_addr, array3d_addr, ps_node_new_num(0),
      ps_node_deref_size(array3d_addr), array3d_inner, array3d_next,
      NULL, 0);
  ASSERT_EQ(24, ps_node_type_size(array3d_row));
  ASSERT_EQ(12, ps_node_deref_size(array3d_row));
  ASSERT_TRUE(ps_node_subscript_deref_uses_base_address(array3d_row));
  array3d_inner = 0;
  array3d_next = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(array3d_row,
                                               &array3d_inner,
                                               &array3d_next,
                                               NULL, NULL));
  ASSERT_EQ(4, array3d_inner);
  ASSERT_EQ(0, array3d_next);
  node_t *array3d_explicit_row = ps_node_new_unary_deref_for(array3d_addr);
  ASSERT_EQ(24, ps_node_type_size(array3d_explicit_row));
  ASSERT_EQ(12, ps_node_deref_size(array3d_explicit_row));
  ASSERT_TRUE(ps_node_deref_decays_to_address(array3d_explicit_row));
  node_t *array3d_explicit_cell = ps_node_new_unary_deref_for(array3d_explicit_row);
  ASSERT_EQ(12, ps_node_type_size(array3d_explicit_cell));
  ASSERT_EQ(4, ps_node_deref_size(array3d_explicit_cell));
  ASSERT_TRUE(ps_node_deref_decays_to_address(array3d_explicit_cell));
  node_t *array3d_explicit_scalar = ps_node_new_unary_deref_for(array3d_explicit_cell);
  ASSERT_EQ(4, ps_node_type_size(array3d_explicit_scalar));
  ASSERT_EQ(0, ps_node_deref_size(array3d_explicit_scalar));
  ASSERT_TRUE(!ps_node_deref_decays_to_address(array3d_explicit_scalar));

  parsed_code = parse_program_input(
      "int __tm_array2d_identifier(void) { int a[2][3]; return a[1][1]; }");
  fn = as_func(parsed_code[0]);
  lvar_t *array2d_a = find_func_lvar(fn, "a");
  ASSERT_TRUE(array2d_a != NULL);
  node_t *array2d_node = ps_node_new_lvar_identifier_ref_for(array2d_a);
  ASSERT_EQ(12, ps_node_deref_size(array2d_node));
  int array2d_inner = 0;
  int array2d_next = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(array2d_node,
                                               &array2d_inner,
                                               &array2d_next, NULL, NULL));
  ASSERT_EQ(12, array2d_inner);
  ASSERT_EQ(4, array2d_next);
  node_t *array2d_row = ps_node_new_subscript_deref_for(
      array2d_node, array2d_node, ps_node_new_num(0),
      ps_node_deref_size(array2d_node), array2d_next, 0, NULL, 0);
  ASSERT_EQ(12, ps_node_type_size(array2d_row));
  ASSERT_EQ(4, ps_node_deref_size(array2d_row));
  ASSERT_TRUE(ps_node_subscript_deref_uses_base_address(array2d_row));
  node_t *array2d_cell = ps_node_new_subscript_deref_for(
      array2d_row,
      ps_node_subscript_deref_uses_base_address(array2d_row)
          ? array2d_row->lhs
          : array2d_row,
      ps_node_new_num(0), ps_node_deref_size(array2d_row),
      0, 0, NULL, 0);
  ASSERT_EQ(4, ps_node_type_size(array2d_cell));
  ASSERT_EQ(0, ps_node_deref_size(array2d_cell));
  ASSERT_TRUE(!ps_node_is_pointer(array2d_cell));

  parsed_code = parse_program_input(
      "typedef int __tm_M2[3][4]; "
      "int __tm_param_ptrarr3d(__tm_M2 *a, int i, int j, int k) { return a[i][j][k]; }");
  fn = as_func(parsed_code[0]);
  lvar_t *param_ptrarr3d_a = find_func_lvar(fn, "a");
  ASSERT_TRUE(param_ptrarr3d_a != NULL);
  node_t *param_ptrarr3d_node = ps_node_new_lvar_identifier_ref_for(param_ptrarr3d_a);
  ASSERT_EQ(48, ps_node_deref_size(param_ptrarr3d_node));
  int param_ptrarr3d_inner = 0;
  int param_ptrarr3d_next = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(param_ptrarr3d_node,
                                               &param_ptrarr3d_inner,
                                               &param_ptrarr3d_next,
                                               NULL, NULL));
  ASSERT_EQ(16, param_ptrarr3d_inner);
  ASSERT_EQ(4, param_ptrarr3d_next);
  node_t *param_ptrarr3d_arg_node = ps_node_new_param_lvar_for(
      param_ptrarr3d_a);
  ASSERT_TRUE(ps_node_get_type(param_ptrarr3d_arg_node) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(param_ptrarr3d_arg_node)->kind);
  ASSERT_TRUE(ps_node_is_pointer(param_ptrarr3d_arg_node));
  ASSERT_EQ(8, ps_node_type_size(param_ptrarr3d_arg_node));
  ASSERT_EQ(48, ps_node_deref_size(param_ptrarr3d_arg_node));
  ASSERT_TRUE(ps_node_get_type(param_ptrarr3d_arg_node)->base_deref_size > 0);
  ASSERT_EQ(4, ps_node_get_type(param_ptrarr3d_arg_node)->base_deref_size);
  ASSERT_EQ(4, ps_node_base_deref_size(param_ptrarr3d_arg_node));

  parsed_code = parse_program_input(
      "int __tm_param_decl_type(unsigned char u, double d, int i) { return u + i; }");
  fn = as_func(parsed_code[0]);
  lvar_t *param_decl_u = find_func_lvar(fn, "u");
  lvar_t *param_decl_d = find_func_lvar(fn, "d");
  lvar_t *param_decl_i = find_func_lvar(fn, "i");
  ASSERT_TRUE(param_decl_u != NULL);
  ASSERT_TRUE(param_decl_d != NULL);
  ASSERT_TRUE(param_decl_i != NULL);
  node_t *param_decl_u_node = ps_node_new_param_lvar_for(
      param_decl_u);
  ASSERT_TRUE(ps_node_get_type(param_decl_u_node) == param_decl_u->decl_type);
  ASSERT_EQ(1, ps_node_type_size(param_decl_u_node));
  ASSERT_TRUE(ps_node_is_unsigned_type(param_decl_u_node));
  ASSERT_TRUE(ps_node_conversion_value_is_unsigned(param_decl_u_node));
  ASSERT_EQ(TK_FLOAT_KIND_NONE, ps_node_value_fp_kind(param_decl_u_node));
  ASSERT_TRUE(ps_node_get_type(param_decl_u_node)->kind != PSX_TYPE_COMPLEX);
  node_t *param_decl_d_node = ps_node_new_param_lvar_for(
      param_decl_d);
  ASSERT_TRUE(ps_node_get_type(param_decl_d_node) == param_decl_d->decl_type);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_node_value_fp_kind(param_decl_d_node));
  ASSERT_EQ(8, ps_node_type_size(param_decl_d_node));
  node_t *param_decl_i_node = ps_node_new_param_lvar_for(
      param_decl_i);
  ASSERT_TRUE(ps_node_get_type(param_decl_i_node) == param_decl_i->decl_type);
  ASSERT_EQ(4, ps_node_type_size(param_decl_i_node));
  ASSERT_EQ(TK_FLOAT_KIND_NONE, ps_node_value_fp_kind(param_decl_i_node));
  ASSERT_TRUE(ps_node_get_type(param_decl_i_node)->kind != PSX_TYPE_COMPLEX);

  node_t *param_ptrarr3d_row = ps_node_new_subscript_deref_for(
      param_ptrarr3d_node, param_ptrarr3d_node, ps_node_new_num(0),
      ps_node_deref_size(param_ptrarr3d_node), param_ptrarr3d_inner,
      param_ptrarr3d_next, NULL, 0);
  ASSERT_EQ(48, ps_node_type_size(param_ptrarr3d_row));
  ASSERT_EQ(16, ps_node_deref_size(param_ptrarr3d_row));
  ASSERT_TRUE(ps_node_subscript_deref_uses_base_address(param_ptrarr3d_row));
  param_ptrarr3d_inner = 0;
  param_ptrarr3d_next = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(param_ptrarr3d_row,
                                               &param_ptrarr3d_inner,
                                               &param_ptrarr3d_next,
                                               NULL, NULL));
  ASSERT_EQ(4, param_ptrarr3d_inner);
  ASSERT_EQ(0, param_ptrarr3d_next);
  node_t *param_ptrarr3d_cell_row = ps_node_new_subscript_deref_for(
      param_ptrarr3d_row,
      ps_node_subscript_deref_uses_base_address(param_ptrarr3d_row)
          ? param_ptrarr3d_row->lhs
          : param_ptrarr3d_row,
      ps_node_new_num(0), ps_node_deref_size(param_ptrarr3d_row),
      param_ptrarr3d_inner, param_ptrarr3d_next, NULL, 0);
  ASSERT_EQ(16, ps_node_type_size(param_ptrarr3d_cell_row));
  ASSERT_EQ(4, ps_node_deref_size(param_ptrarr3d_cell_row));
  param_ptrarr3d_inner = 0;
  param_ptrarr3d_next = 0;
  ASSERT_TRUE(!ps_node_pointer_stride_metadata(param_ptrarr3d_cell_row,
                                                &param_ptrarr3d_inner,
                                                &param_ptrarr3d_next,
                                                NULL, NULL));

  parsed_code = parse_program_input(
      "int (*__tm_ret_ptrarr3d(void))[3][4]; "
      "int __tm_local_ptrarr3d(void) { "
      "  int (*p)[3][4] = __tm_ret_ptrarr3d(); return p[1][2][0]; }");
  fn = as_func(parsed_code[0]);
  lvar_t *local_ptrarr3d_p = find_func_lvar(fn, "p");
  ASSERT_TRUE(local_ptrarr3d_p != NULL);
  ASSERT_TRUE(local_ptrarr3d_p->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, local_ptrarr3d_p->decl_type->kind);
  ASSERT_EQ(48, local_ptrarr3d_p->decl_type->outer_stride);
  ASSERT_EQ(16, local_ptrarr3d_p->decl_type->mid_stride);
  node_t *local_ptrarr3d_node = ps_node_new_lvar_identifier_ref_for(local_ptrarr3d_p);
  ASSERT_EQ(48, ps_node_deref_size(local_ptrarr3d_node));
  int local_ptrarr3d_inner = 0;
  int local_ptrarr3d_next = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(local_ptrarr3d_node,
                                               &local_ptrarr3d_inner,
                                               &local_ptrarr3d_next,
                                               NULL, NULL));
  ASSERT_EQ(16, local_ptrarr3d_inner);
  ASSERT_EQ(4, local_ptrarr3d_next);
  node_t *local_ptrarr3d_row = ps_node_new_subscript_deref_for(
      local_ptrarr3d_node, local_ptrarr3d_node, ps_node_new_num(0),
      ps_node_deref_size(local_ptrarr3d_node), local_ptrarr3d_inner,
      local_ptrarr3d_next, NULL, 0);
  ASSERT_EQ(48, ps_node_type_size(local_ptrarr3d_row));
  ASSERT_EQ(16, ps_node_deref_size(local_ptrarr3d_row));
  ASSERT_TRUE(ps_node_subscript_deref_uses_base_address(local_ptrarr3d_row));
  local_ptrarr3d_inner = 0;
  local_ptrarr3d_next = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(local_ptrarr3d_row,
                                               &local_ptrarr3d_inner,
                                               &local_ptrarr3d_next,
                                               NULL, NULL));
  ASSERT_EQ(4, local_ptrarr3d_inner);
  ASSERT_EQ(0, local_ptrarr3d_next);

  parsed_code = parse_program_input(
      "int __tm_vla3d(int n, int m, int k) { "
      "  int a[n][m][k]; return a[0][0][0]; }");
  fn = as_func(parsed_code[0]);
  lvar_t *vla3d_a = find_func_lvar(fn, "a");
  ASSERT_TRUE(vla3d_a != NULL);
  ASSERT_TRUE(vla3d_a->is_vla);
  ASSERT_TRUE(vla3d_a->vla_row_stride_frame_off > 0);
  ASSERT_EQ(1, vla3d_a->vla_strides_remaining);
  node_t *vla3d_node = ps_node_new_lvar_identifier_ref_for(vla3d_a);
  node_t *vla3d_decay = ps_node_new_vla_decay_ref_for(vla3d_a);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(vla3d_decay)->kind);
  ASSERT_TRUE(ps_node_is_pointer(vla3d_decay));
  ASSERT_EQ(0, ps_node_aggregate_value_size(vla3d_decay));
  ASSERT_EQ(vla3d_a->vla_row_stride_frame_off,
            ps_node_vla_row_stride_frame_off(vla3d_decay));
  int vla3d_inner = 0;
  int vla3d_next = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(vla3d_node,
                                               &vla3d_inner,
                                               &vla3d_next,
                                               NULL, NULL));
  ASSERT_EQ(4, vla3d_inner);
  node_t *vla3d_row = ps_node_new_subscript_deref_for(
      vla3d_node, vla3d_node, ps_node_new_num(0),
      vla3d_inner, vla3d_inner, vla3d_next, NULL, 0);
  ASSERT_EQ(vla3d_a->vla_row_stride_frame_off + 8,
            ps_node_vla_row_stride_frame_off(vla3d_row));
  ASSERT_TRUE(ps_node_get_type(vla3d_row) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(vla3d_row)->kind);
  ASSERT_TRUE(ps_node_subscript_deref_uses_base_address(vla3d_row));
  vla3d_inner = 0;
  vla3d_next = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(vla3d_row,
                                               &vla3d_inner,
                                               &vla3d_next,
                                               NULL, NULL));
  ASSERT_EQ(4, vla3d_inner);
  node_t *vla3d_row_base =
      ps_node_subscript_deref_uses_base_address(vla3d_row)
          ? vla3d_row->lhs
          : vla3d_row;
  node_t *vla3d_cell_row = ps_node_new_subscript_deref_for(
      vla3d_row, vla3d_row_base, ps_node_new_num(0),
      vla3d_inner, vla3d_inner, vla3d_next, NULL, 0);
  ASSERT_TRUE(ps_node_get_type(vla3d_cell_row) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(vla3d_cell_row)->kind);
  ASSERT_TRUE(ps_node_subscript_deref_uses_base_address(vla3d_cell_row));

  parsed_code = parse_program_input(
      "int __tm_vla_param_shape(int m, int k, int t[][m][3][k]) { "
      "  return t[0][0][0][0]; }");
  fn = as_func(parsed_code[0]);
  lvar_t *vla_param_m = find_func_lvar(fn, "m");
  lvar_t *vla_param_k = find_func_lvar(fn, "k");
  lvar_t *vla_param_t = find_func_lvar(fn, "t");
  ASSERT_TRUE(vla_param_m != NULL);
  ASSERT_TRUE(vla_param_k != NULL);
  ASSERT_TRUE(vla_param_t != NULL);
  ASSERT_TRUE(vla_param_t->decl_type != NULL);
  ASSERT_TRUE(vla_param_t->decl_type->is_vla);
  ASSERT_EQ(3, ps_type_vla_param_inner_dim_count(vla_param_t->decl_type));
  ASSERT_EQ(0, ps_type_vla_param_inner_dim_const(vla_param_t->decl_type, 0));
  ASSERT_EQ(3, ps_type_vla_param_inner_dim_const(vla_param_t->decl_type, 1));
  ASSERT_EQ(0, ps_type_vla_param_inner_dim_const(vla_param_t->decl_type, 2));
  ASSERT_EQ(vla_param_m->offset,
            ps_type_vla_param_inner_dim_src_offset(vla_param_t->decl_type, 0));
  ASSERT_EQ(vla_param_k->offset,
            ps_type_vla_param_inner_dim_src_offset(vla_param_t->decl_type, 2));
  ASSERT_EQ(4, ps_type_vla_row_stride_elem_size(vla_param_t->decl_type));
  vla_param_t->vla_param_inner_dim_count = 1;
  vla_param_t->vla_param_inner_dim_consts[1] = 9;
  vla_param_t->vla_param_inner_dim_src_offsets[0] = 777;
  vla_param_t->vla_row_stride_elem_size = 8;
  ASSERT_EQ(3, ps_lvar_vla_param_inner_dim_count(vla_param_t));
  ASSERT_EQ(3, ps_lvar_vla_param_inner_dim_const(vla_param_t, 1));
  ASSERT_EQ(vla_param_m->offset,
            ps_lvar_vla_param_inner_dim_src_offset(vla_param_t, 0));
  ASSERT_EQ(4, ps_lvar_vla_row_stride_elem_size(vla_param_t));

  int canonical_stride_dims[4] = {2, 3, 4, 5};
  psx_type_t *canonical_stride_array = ps_type_wrap_array_dims(
      ps_type_new_integer(TK_INT, 4, 0), canonical_stride_dims, 4);
  canonical_stride_array->outer_stride = 999;
  canonical_stride_array->mid_stride = 998;
  int canonical_outer_stride = 0;
  int canonical_mid_stride = 0;
  int canonical_extra_strides[5] = {0};
  int canonical_extra_stride_count = 0;
  ASSERT_TRUE(ps_type_decl_array_stride_metadata(
      canonical_stride_array, &canonical_outer_stride,
      &canonical_mid_stride, canonical_extra_strides,
      &canonical_extra_stride_count));
  ASSERT_EQ(240, canonical_outer_stride);
  ASSERT_EQ(80, canonical_mid_stride);
  ASSERT_EQ(1, canonical_extra_stride_count);
  ASSERT_EQ(20, canonical_extra_strides[0]);
  psx_type_t *canonical_stride_pointer = ps_type_new_pointer(
      ps_type_clone(canonical_stride_array->base), 240);
  canonical_stride_pointer->outer_stride = 997;
  ASSERT_TRUE(ps_type_decl_array_stride_metadata(
      canonical_stride_pointer, &canonical_outer_stride,
      &canonical_mid_stride, canonical_extra_strides,
      &canonical_extra_stride_count));
  ASSERT_EQ(240, canonical_outer_stride);
  ASSERT_EQ(80, canonical_mid_stride);
  ASSERT_EQ(1, canonical_extra_stride_count);
  ASSERT_EQ(20, canonical_extra_strides[0]);

  psx_declarator_shape_t array_of_pointer_shape;
  ps_declarator_shape_init(&array_of_pointer_shape);
  ASSERT_TRUE(ps_declarator_shape_append_array(
      &array_of_pointer_shape, 3));
  ASSERT_TRUE(ps_declarator_shape_append_pointer(
      &array_of_pointer_shape, 0, 0));
  psx_type_t *array_of_pointer_type = ps_type_apply_declarator_shape(
      ps_type_new_integer(TK_INT, 4, 0), &array_of_pointer_shape);
  ASSERT_EQ(PSX_TYPE_ARRAY, array_of_pointer_type->kind);
  ASSERT_EQ(PSX_TYPE_POINTER, array_of_pointer_type->base->kind);
  ASSERT_EQ(PSX_TYPE_INTEGER, array_of_pointer_type->base->base->kind);

  psx_declarator_shape_t incomplete_array_shape;
  ps_declarator_shape_init(&incomplete_array_shape);
  ASSERT_TRUE(ps_declarator_shape_append_array_ex(
      &incomplete_array_shape, 0, 1));
  ASSERT_EQ(1, incomplete_array_shape.count);
  ASSERT_TRUE(incomplete_array_shape.ops[0].is_incomplete_array);
  ASSERT_TRUE(!incomplete_array_shape.ops[0].is_vla_array);
  ASSERT_EQ(0, incomplete_array_shape.ops[0].array_len);

  psx_declarator_shape_t vla_array_shape;
  ps_declarator_shape_init(&vla_array_shape);
  ASSERT_TRUE(ps_declarator_shape_append_vla_array(&vla_array_shape));
  ASSERT_EQ(1, vla_array_shape.count);
  ASSERT_TRUE(vla_array_shape.ops[0].is_vla_array);
  ASSERT_TRUE(!vla_array_shape.ops[0].is_incomplete_array);
  psx_type_t *vla_array_type = ps_type_apply_declarator_shape(
      ps_type_new_integer(TK_INT, 4, 0), &vla_array_shape);
  ASSERT_EQ(PSX_TYPE_ARRAY, vla_array_type->kind);
  ASSERT_TRUE(vla_array_type->is_vla);

  psx_declarator_shape_t pointer_to_array_shape;
  ps_declarator_shape_init(&pointer_to_array_shape);
  ASSERT_TRUE(ps_declarator_shape_append_pointer(
      &pointer_to_array_shape, 0, 0));
  ASSERT_TRUE(ps_declarator_shape_append_array(
      &pointer_to_array_shape, 3));
  psx_type_t *pointer_to_array_type = ps_type_apply_declarator_shape(
      ps_type_new_integer(TK_INT, 4, 0), &pointer_to_array_shape);
  ASSERT_EQ(PSX_TYPE_POINTER, pointer_to_array_type->kind);
  ASSERT_EQ(PSX_TYPE_ARRAY, pointer_to_array_type->base->kind);
  ASSERT_EQ(PSX_TYPE_INTEGER, pointer_to_array_type->base->base->kind);

  psx_decl_funcptr_sig_t declarator_func_sig = {0};
  declarator_func_sig.function.callable.signature.param_int_mask = 1u;
  declarator_func_sig.function.callable.return_shape.int_width = 4;
  psx_declarator_shape_t array_of_funcptr_shape;
  ps_declarator_shape_init(&array_of_funcptr_shape);
  ASSERT_TRUE(ps_declarator_shape_append_array(
      &array_of_funcptr_shape, 2));
  ASSERT_TRUE(ps_declarator_shape_append_pointer(
      &array_of_funcptr_shape, 0, 0));
  ASSERT_TRUE(ps_declarator_shape_append_function(
      &array_of_funcptr_shape, declarator_func_sig));
  psx_type_t *array_of_funcptr_type = ps_type_apply_declarator_shape(
      ps_type_new_funcptr_return_type(declarator_func_sig),
      &array_of_funcptr_shape);
  ASSERT_EQ(PSX_TYPE_ARRAY, array_of_funcptr_type->kind);
  ASSERT_EQ(PSX_TYPE_POINTER, array_of_funcptr_type->base->kind);
  ASSERT_EQ(PSX_TYPE_FUNCTION, array_of_funcptr_type->base->base->kind);
  ASSERT_EQ(PSX_TYPE_INTEGER,
            array_of_funcptr_type->base->base->base->kind);

  parsed_code = parse_program_input(
      "int __tm_vla_const_inner(int n) { int a[n][4]; return a[0][1]; }");
  fn = as_func(parsed_code[0]);
  lvar_t *vla_const_inner_a = find_func_lvar(fn, "a");
  ASSERT_TRUE(vla_const_inner_a != NULL);
  ASSERT_TRUE(vla_const_inner_a->is_vla);
  node_t *vla_const_inner_node = ps_node_new_lvar_identifier_ref_for(vla_const_inner_a);
  ASSERT_EQ(16, ps_node_deref_size(vla_const_inner_node));
  ASSERT_EQ(16, ps_node_deref_size(vla_const_inner_node));
  int vla_const_inner_stride = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(vla_const_inner_node,
                                               &vla_const_inner_stride,
                                               NULL, NULL, NULL));
  ASSERT_EQ(16, vla_const_inner_stride);
  node_t *vla_const_inner_row = ps_node_new_subscript_deref_for(
      vla_const_inner_node, vla_const_inner_node, ps_node_new_num(0),
      ps_node_deref_size(vla_const_inner_node), vla_const_inner_stride,
      0, NULL, 0);
  ASSERT_EQ(16, ps_node_type_size(vla_const_inner_row));
  ASSERT_EQ(4, ps_node_deref_size(vla_const_inner_row));
  ASSERT_TRUE(!ps_node_pointer_stride_metadata(vla_const_inner_row,
                                                NULL, NULL, NULL, NULL));

  parsed_code = parse_program_input(
      "typedef int *__tm_IP; "
      "int __tm_ptrarr_ptr_elem(void) { "
      "  int a, b, c; __tm_IP row[3] = { &a, &b, &c }; "
      "  __tm_IP (*pia)[3] = &row; return 0; }");
  fn = as_func(parsed_code[0]);
  lvar_t *ptrarr_ip_pia = find_func_lvar(fn, "pia");
  ASSERT_TRUE(ptrarr_ip_pia != NULL);
  ASSERT_TRUE(ptrarr_ip_pia->decl_type != NULL);
  ASSERT_EQ(24, ptrarr_ip_pia->decl_type->ptr_array_pointee_bytes);
  ASSERT_EQ(24, ptrarr_ip_pia->decl_type->outer_stride);
  ASSERT_EQ(4, ptrarr_ip_pia->decl_type->base_deref_size);
  node_t *ptrarr_ip_pia_node = ps_node_new_lvar_identifier_ref_for(ptrarr_ip_pia);
  ASSERT_EQ(24, ps_node_deref_size(ptrarr_ip_pia_node));
  int ptrarr_ip_inner = 0;
  int ptrarr_ip_next = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(ptrarr_ip_pia_node,
                                               &ptrarr_ip_inner,
                                               &ptrarr_ip_next,
                                               NULL, NULL));
  ASSERT_EQ(8, ptrarr_ip_inner);
  ASSERT_EQ(0, ptrarr_ip_next);
  node_t *ptrarr_ip_row = ps_node_new_unary_deref_for(ptrarr_ip_pia_node);
  ASSERT_EQ(24, ps_node_type_size(ptrarr_ip_row));
  ASSERT_EQ(8, ps_node_deref_size(ptrarr_ip_row));
  ASSERT_TRUE(ps_node_is_pointer(ptrarr_ip_row));
  ASSERT_TRUE(ps_node_get_type(ptrarr_ip_row) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(ptrarr_ip_row)->kind);
  ASSERT_TRUE(ps_node_get_type(ptrarr_ip_row)->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(ptrarr_ip_row)->base->kind);
  node_t *ptrarr_ip_elem = ps_node_new_subscript_deref_for(
      ptrarr_ip_row, ptrarr_ip_row->lhs ? ptrarr_ip_row->lhs : ptrarr_ip_row,
      ps_node_new_num(0), ps_node_deref_size(ptrarr_ip_row), 0, 0, NULL, 0);
  ASSERT_EQ(8, ps_node_type_size(ptrarr_ip_elem));
  ASSERT_EQ(4, ps_node_deref_size(ptrarr_ip_elem));
  ASSERT_TRUE(ps_node_is_pointer(ptrarr_ip_elem));
  ASSERT_EQ(1, ps_node_pointer_qual_levels(ptrarr_ip_elem));
  node_t *ptrarr_ip_value = ps_node_new_unary_deref_for(ptrarr_ip_elem);
  ASSERT_EQ(4, ps_node_type_size(ptrarr_ip_value));
  ASSERT_EQ(0, ps_node_deref_size(ptrarr_ip_value));
  ASSERT_TRUE(!ps_node_is_pointer(ptrarr_ip_value));

  parsed_code = parse_program_input(
      "typedef int *__tm_param_IP; "
      "int __tm_param_ptrarr_ptr_elem(__tm_param_IP (*p)[3]) { "
      "  return *(*p)[0]; }");
  fn = as_func(parsed_code[0]);
  lvar_t *param_ptrarr_ip_p = find_func_lvar(fn, "p");
  ASSERT_TRUE(param_ptrarr_ip_p != NULL);
  ASSERT_TRUE(param_ptrarr_ip_p->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, param_ptrarr_ip_p->decl_type->kind);
  ASSERT_TRUE(param_ptrarr_ip_p->decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, param_ptrarr_ip_p->decl_type->base->kind);
  ASSERT_TRUE(param_ptrarr_ip_p->decl_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, param_ptrarr_ip_p->decl_type->base->base->kind);
  ASSERT_EQ(24, param_ptrarr_ip_p->decl_type->ptr_array_pointee_bytes);
  ASSERT_EQ(24, param_ptrarr_ip_p->decl_type->outer_stride);
  ASSERT_EQ(4, param_ptrarr_ip_p->decl_type->base_deref_size);
  node_t *param_ptrarr_ip_p_node =
      ps_node_new_lvar_identifier_ref_for(param_ptrarr_ip_p);
  ASSERT_EQ(24, ps_node_deref_size(param_ptrarr_ip_p_node));
  int param_ptrarr_ip_inner = 0;
  int param_ptrarr_ip_next = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(param_ptrarr_ip_p_node,
                                               &param_ptrarr_ip_inner,
                                               &param_ptrarr_ip_next,
                                               NULL, NULL));
  ASSERT_EQ(8, param_ptrarr_ip_inner);
  ASSERT_EQ(0, param_ptrarr_ip_next);
  node_t *param_ptrarr_ip_row =
      ps_node_new_unary_deref_for(param_ptrarr_ip_p_node);
  ASSERT_EQ(24, ps_node_type_size(param_ptrarr_ip_row));
  ASSERT_EQ(8, ps_node_deref_size(param_ptrarr_ip_row));
  ASSERT_TRUE(ps_node_is_pointer(param_ptrarr_ip_row));
  ASSERT_TRUE(ps_node_get_type(param_ptrarr_ip_row) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(param_ptrarr_ip_row)->kind);
  ASSERT_TRUE(ps_node_get_type(param_ptrarr_ip_row)->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(param_ptrarr_ip_row)->base->kind);
  node_t *param_ptrarr_ip_elem = ps_node_new_subscript_deref_for(
      param_ptrarr_ip_row,
      param_ptrarr_ip_row->lhs ? param_ptrarr_ip_row->lhs : param_ptrarr_ip_row,
      ps_node_new_num(0), ps_node_deref_size(param_ptrarr_ip_row),
      0, 0, NULL, 0);
  ASSERT_EQ(8, ps_node_type_size(param_ptrarr_ip_elem));
  ASSERT_EQ(4, ps_node_deref_size(param_ptrarr_ip_elem));
  ASSERT_TRUE(ps_node_is_pointer(param_ptrarr_ip_elem));
  ASSERT_EQ(1, ps_node_pointer_qual_levels(param_ptrarr_ip_elem));
  node_t *param_ptrarr_ip_value =
      ps_node_new_unary_deref_for(param_ptrarr_ip_elem);
  ASSERT_EQ(4, ps_node_type_size(param_ptrarr_ip_value));
  ASSERT_EQ(0, ps_node_deref_size(param_ptrarr_ip_value));
  ASSERT_TRUE(!ps_node_is_pointer(param_ptrarr_ip_value));

  parsed_code = parse_program_input(
      "typedef int *__tm_param_IP2; "
      "int __tm_param_ptrarr_ptr_elem_2d(__tm_param_IP2 (*p)[2][3]) { "
      "  return *(*p)[1][0]; }");
  fn = as_func(parsed_code[0]);
  lvar_t *param_ptrarr_ip2_p = find_func_lvar(fn, "p");
  ASSERT_TRUE(param_ptrarr_ip2_p != NULL);
  ASSERT_TRUE(param_ptrarr_ip2_p->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, param_ptrarr_ip2_p->decl_type->kind);
  ASSERT_TRUE(param_ptrarr_ip2_p->decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, param_ptrarr_ip2_p->decl_type->base->kind);
  ASSERT_EQ(2, param_ptrarr_ip2_p->decl_type->base->array_len);
  ASSERT_EQ(24, param_ptrarr_ip2_p->decl_type->base->elem_size);
  ASSERT_TRUE(param_ptrarr_ip2_p->decl_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, param_ptrarr_ip2_p->decl_type->base->base->kind);
  ASSERT_EQ(3, param_ptrarr_ip2_p->decl_type->base->base->array_len);
  ASSERT_EQ(8, param_ptrarr_ip2_p->decl_type->base->base->elem_size);
  ASSERT_TRUE(param_ptrarr_ip2_p->decl_type->base->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER,
            param_ptrarr_ip2_p->decl_type->base->base->base->kind);
  ASSERT_EQ(48, param_ptrarr_ip2_p->decl_type->ptr_array_pointee_bytes);
  ASSERT_EQ(48, param_ptrarr_ip2_p->decl_type->outer_stride);
  ASSERT_EQ(24, param_ptrarr_ip2_p->decl_type->mid_stride);
  ASSERT_EQ(4, param_ptrarr_ip2_p->decl_type->base_deref_size);
  node_t *param_ptrarr_ip2_p_node =
      ps_node_new_lvar_identifier_ref_for(param_ptrarr_ip2_p);
  ASSERT_EQ(48, ps_node_deref_size(param_ptrarr_ip2_p_node));
  int param_ptrarr_ip2_inner = 0;
  int param_ptrarr_ip2_next = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(param_ptrarr_ip2_p_node,
                                               &param_ptrarr_ip2_inner,
                                               &param_ptrarr_ip2_next,
                                               NULL, NULL));
  ASSERT_EQ(24, param_ptrarr_ip2_inner);
  ASSERT_EQ(8, param_ptrarr_ip2_next);
  node_t *param_ptrarr_ip2_row =
      ps_node_new_unary_deref_for(param_ptrarr_ip2_p_node);
  ASSERT_EQ(48, ps_node_type_size(param_ptrarr_ip2_row));
  ASSERT_EQ(24, ps_node_deref_size(param_ptrarr_ip2_row));
  ASSERT_TRUE(ps_node_get_type(param_ptrarr_ip2_row) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(param_ptrarr_ip2_row)->kind);
  node_t *param_ptrarr_ip2_row1 = ps_node_new_subscript_deref_for(
      param_ptrarr_ip2_row,
      param_ptrarr_ip2_row->lhs ? param_ptrarr_ip2_row->lhs
                                : param_ptrarr_ip2_row,
      ps_node_new_num(1), ps_node_deref_size(param_ptrarr_ip2_row),
      param_ptrarr_ip2_inner, param_ptrarr_ip2_next, NULL, 0);
  ASSERT_EQ(24, ps_node_type_size(param_ptrarr_ip2_row1));
  ASSERT_EQ(8, ps_node_deref_size(param_ptrarr_ip2_row1));
  ASSERT_TRUE(ps_node_get_type(param_ptrarr_ip2_row1) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(param_ptrarr_ip2_row1)->kind);
  param_ptrarr_ip2_inner = 0;
  param_ptrarr_ip2_next = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(param_ptrarr_ip2_row1,
                                               &param_ptrarr_ip2_inner,
                                               &param_ptrarr_ip2_next,
                                               NULL, NULL));
  ASSERT_EQ(8, param_ptrarr_ip2_inner);
  ASSERT_EQ(0, param_ptrarr_ip2_next);
  node_t *param_ptrarr_ip2_elem = ps_node_new_subscript_deref_for(
      param_ptrarr_ip2_row1,
      ps_node_subscript_deref_uses_base_address(param_ptrarr_ip2_row1)
          ? param_ptrarr_ip2_row1->lhs
          : param_ptrarr_ip2_row1,
      ps_node_new_num(0), ps_node_deref_size(param_ptrarr_ip2_row1),
      param_ptrarr_ip2_inner, param_ptrarr_ip2_next, NULL, 0);
  ASSERT_EQ(8, ps_node_type_size(param_ptrarr_ip2_elem));
  ASSERT_EQ(4, ps_node_deref_size(param_ptrarr_ip2_elem));
  ASSERT_TRUE(ps_node_is_pointer(param_ptrarr_ip2_elem));
  node_t *param_ptrarr_ip2_value =
      ps_node_new_unary_deref_for(param_ptrarr_ip2_elem);
  ASSERT_EQ(4, ps_node_type_size(param_ptrarr_ip2_value));
  ASSERT_EQ(0, ps_node_deref_size(param_ptrarr_ip2_value));
  ASSERT_TRUE(!ps_node_is_pointer(param_ptrarr_ip2_value));

  parsed_code = parse_program_input(
      "int __tm_nested_ptrarr(int (*(*rows)[2])[3]) { "
      "  return (*rows)[0][1][2]; }");
  fn = as_func(parsed_code[0]);
  lvar_t *nested2_rows = find_func_lvar(fn, "rows");
  ASSERT_TRUE(nested2_rows != NULL);
  node_t *nested2_rows_node = ps_node_new_lvar_identifier_ref_for(nested2_rows);
  node_t *nested2_rows_array = ps_node_new_unary_deref_for(nested2_rows_node);
  ASSERT_TRUE(ps_node_get_type(nested2_rows_array) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(nested2_rows_array)->kind);
  int nested2_inner = 0;
  int nested2_next = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(nested2_rows_array,
                                               &nested2_inner,
                                               &nested2_next,
                                               NULL, NULL));
  ASSERT_EQ(8, nested2_inner);
  node_t *nested2_rowptr = ps_node_new_subscript_deref_for(
      nested2_rows_array,
      ps_node_subscript_deref_uses_base_address(nested2_rows_array)
          ? nested2_rows_array->lhs
          : nested2_rows_array,
      ps_node_new_num(0), ps_node_deref_size(nested2_rows_array),
      nested2_inner, nested2_next, NULL, 0);
  ASSERT_TRUE(ps_node_is_pointer(nested2_rowptr));
  ASSERT_TRUE(ps_node_get_type(nested2_rowptr) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(nested2_rowptr)->kind);
  ASSERT_TRUE(ps_node_get_type(nested2_rowptr)->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(nested2_rowptr)->base->kind);
  ASSERT_TRUE(ps_node_get_type(nested2_rowptr)->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, ps_node_get_type(nested2_rowptr)->base->base->kind);
  nested2_inner = 0;
  nested2_next = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(nested2_rowptr,
                                               &nested2_inner,
                                               &nested2_next,
                                               NULL, NULL));
  ASSERT_EQ(4, nested2_inner);
  node_t *nested2_int_row = ps_node_new_subscript_deref_for(
      nested2_rowptr, nested2_rowptr, ps_node_new_num(0),
      ps_node_deref_size(nested2_rowptr), nested2_inner, nested2_next,
      NULL, 0);
  ASSERT_TRUE(ps_node_get_type(nested2_int_row) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(nested2_int_row)->kind);
  ASSERT_TRUE(ps_node_get_type(nested2_int_row)->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, ps_node_get_type(nested2_int_row)->base->kind);
  ASSERT_TRUE(ps_node_subscript_deref_uses_base_address(nested2_int_row));
  node_t *nested2_int_cell = ps_node_new_subscript_deref_for(
      nested2_int_row,
      ps_node_subscript_deref_uses_base_address(nested2_int_row)
          ? nested2_int_row->lhs
          : nested2_int_row,
      ps_node_new_num(0), ps_node_deref_size(nested2_int_row),
      0, 0, NULL, 0);
  ASSERT_TRUE(ps_node_get_type(nested2_int_cell) != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, ps_node_get_type(nested2_int_cell)->kind);
  ASSERT_EQ(4, ps_node_type_size(nested2_int_cell));
  ASSERT_EQ(0, ps_node_deref_size(nested2_int_cell));
  ASSERT_TRUE(!ps_node_is_pointer(nested2_int_cell));

  parsed_code = parse_program_input(
      "int __tm_local_nested_ptrarr(void) { "
      "  int x[2][3]; int (*(*ptrs)[2])[3] = &(int (*[2])[3]){x, x}; "
      "  return (*ptrs)[0][1][2]; }");
  fn = as_func(parsed_code[0]);
  lvar_t *local_nested_ptrs = find_func_lvar(fn, "ptrs");
  ASSERT_TRUE(local_nested_ptrs != NULL);
  node_t *local_nested_ptrs_node =
      ps_node_new_lvar_identifier_ref_for(local_nested_ptrs);
  node_t *local_nested_rows_array =
      ps_node_new_unary_deref_for(local_nested_ptrs_node);
  ASSERT_TRUE(ps_node_get_type(local_nested_rows_array) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(local_nested_rows_array)->kind);
  int local_nested_inner = 0;
  int local_nested_next = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(local_nested_rows_array,
                                               &local_nested_inner,
                                               &local_nested_next,
                                               NULL, NULL));
  ASSERT_EQ(8, local_nested_inner);

  parsed_code = parse_program_input(
      "typedef int (*__tm_RowPtr3_local)[3]; "
      "int __tm_local_rowptr_typedef_subscript(void) { "
      "  int x[2][3]; __tm_RowPtr3_local *typedef_ptrs = "
      "      (__tm_RowPtr3_local[]){x, x}; "
      "  return typedef_ptrs[0][0][2]; }");
  fn = as_func(parsed_code[0]);
  lvar_t *local_typedef_ptrs = find_func_lvar(fn, "typedef_ptrs");
  ASSERT_TRUE(local_typedef_ptrs != NULL);
  ASSERT_TRUE(local_typedef_ptrs->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, local_typedef_ptrs->decl_type->kind);
  ASSERT_TRUE(local_typedef_ptrs->decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, local_typedef_ptrs->decl_type->base->kind);
  ASSERT_TRUE(local_typedef_ptrs->decl_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, local_typedef_ptrs->decl_type->base->base->kind);
  node_t *local_typedef_ptrs_node =
      ps_node_new_lvar_identifier_ref_for(local_typedef_ptrs);
  node_t *local_typedef_rowptr = ps_node_new_subscript_deref_for(
      local_typedef_ptrs_node, local_typedef_ptrs_node, ps_node_new_num(0),
      ps_node_deref_size(local_typedef_ptrs_node), 0, 0, NULL, 0);
  ASSERT_TRUE(ps_node_get_type(local_typedef_rowptr) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(local_typedef_rowptr)->kind);
  ASSERT_TRUE(ps_node_get_type(local_typedef_rowptr)->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(local_typedef_rowptr)->base->kind);
  int local_typedef_row_inner = 0;
  int local_typedef_row_next = 0;
  ps_node_pointer_stride_metadata(local_typedef_rowptr,
                                   &local_typedef_row_inner,
                                   &local_typedef_row_next,
                                   NULL, NULL);
  node_t *local_typedef_int_row = ps_node_new_subscript_deref_for(
      local_typedef_rowptr, local_typedef_rowptr, ps_node_new_num(0),
      ps_node_deref_size(local_typedef_rowptr), local_typedef_row_inner,
      local_typedef_row_next, NULL, 0);
  ASSERT_TRUE(ps_node_get_type(local_typedef_int_row) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(local_typedef_int_row)->kind);
  ASSERT_TRUE(ps_node_subscript_deref_uses_base_address(local_typedef_int_row));
  node_t *local_typedef_cell = ps_node_new_subscript_deref_for(
      local_typedef_int_row,
      ps_node_subscript_deref_uses_base_address(local_typedef_int_row)
          ? local_typedef_int_row->lhs
          : local_typedef_int_row,
      ps_node_new_num(2), ps_node_deref_size(local_typedef_int_row),
      0, 0, NULL, 0);
  ASSERT_TRUE(ps_node_get_type(local_typedef_cell) != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, ps_node_get_type(local_typedef_cell)->kind);
  ASSERT_EQ(4, ps_node_type_size(local_typedef_cell));
  ASSERT_TRUE(!ps_node_is_pointer(local_typedef_cell));

  parsed_code = parse_program_input(
      "int __tm_flat_rowptr_param(int (*rows[2])[3]) { "
      "  return rows[0][0][2]; }");
  fn = as_func(parsed_code[0]);
  lvar_t *flat_rows_param = find_func_lvar(fn, "rows");
  ASSERT_TRUE(flat_rows_param != NULL);
  ASSERT_TRUE(flat_rows_param->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, flat_rows_param->decl_type->kind);
  ASSERT_TRUE(flat_rows_param->decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, flat_rows_param->decl_type->base->kind);
  ASSERT_TRUE(flat_rows_param->decl_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, flat_rows_param->decl_type->base->base->kind);
  node_t *flat_rows_param_node =
      ps_node_new_lvar_identifier_ref_for(flat_rows_param);
  node_t *flat_rows_rowptr = ps_node_new_subscript_deref_for(
      flat_rows_param_node, flat_rows_param_node, ps_node_new_num(0),
      ps_node_deref_size(flat_rows_param_node), 0, 0, NULL, 0);
  ASSERT_TRUE(ps_node_get_type(flat_rows_rowptr) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(flat_rows_rowptr)->kind);
  ASSERT_TRUE(ps_node_get_type(flat_rows_rowptr)->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(flat_rows_rowptr)->base->kind);
  int flat_rows_inner = 0;
  int flat_rows_next = 0;
  ps_node_pointer_stride_metadata(flat_rows_rowptr,
                                   &flat_rows_inner,
                                   &flat_rows_next,
                                   NULL, NULL);
  node_t *flat_rows_int_row = ps_node_new_subscript_deref_for(
      flat_rows_rowptr, flat_rows_rowptr, ps_node_new_num(0),
      ps_node_deref_size(flat_rows_rowptr), flat_rows_inner,
      flat_rows_next, NULL, 0);
  ASSERT_TRUE(ps_node_get_type(flat_rows_int_row) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(flat_rows_int_row)->kind);
  ASSERT_TRUE(ps_node_subscript_deref_uses_base_address(flat_rows_int_row));

  node_t *vla_alloc = ps_node_new_vla_alloc(64, 80, ps_node_new_num(3), ps_node_new_num(12));
  int vla_desc_off = 0;
  int vla_row_off = 0;
  ASSERT_TRUE(ps_node_vla_alloc_descriptor_info(vla_alloc, &vla_desc_off, &vla_row_off));
  ASSERT_EQ(64, vla_desc_off);
  ASSERT_EQ(80, vla_row_off);
  ASSERT_TRUE(!ps_node_vla_alloc_descriptor_info(&canonical_struct_scalar,
                                                  &vla_desc_off, &vla_row_off));
  ASSERT_EQ(0, vla_desc_off);
  ASSERT_EQ(0, vla_row_off);

  node_t typed_funcptr_sig = {0};
  typed_funcptr_sig.kind = ND_LVAR;
  typed_funcptr_sig.type = ps_type_new_pointer(
      ps_type_new_integer(TK_INT, 4, 0), 8);
  typed_funcptr_sig.type->funcptr_sig.function.callable.return_shape.int_width = 8;
  ASSERT_EQ(8, ps_node_funcptr_sig(&typed_funcptr_sig).function.callable.return_shape.int_width);

  node_t typed_data_ptr_stale_funcptr_sig = {0};
  typed_data_ptr_stale_funcptr_sig.kind = ND_LVAR;
  typed_data_ptr_stale_funcptr_sig.type = ps_type_new_pointer(
      ps_type_new_integer(TK_INT, 4, 0), 8);
  psx_decl_funcptr_sig_t typed_data_ptr_sig =
      ps_node_funcptr_sig(&typed_data_ptr_stale_funcptr_sig);
  ASSERT_TRUE(!ps_decl_funcptr_sig_has_payload(typed_data_ptr_sig));
  ASSERT_TRUE(!ps_node_has_funcptr_signature(&typed_data_ptr_stale_funcptr_sig));

  node_t canonical_funcptr = {0};
  canonical_funcptr.kind = ND_DEREF;
  psx_decl_funcptr_sig_t canonical_funcptr_sig = {0};
  canonical_funcptr_sig.function.callable.return_shape.int_width = 4;
  canonical_funcptr.type = ps_type_new_funcptr(canonical_funcptr_sig, 1);
  canonical_funcptr.type->funcptr_sig.function.callable.return_shape.int_width = 8;
  ASSERT_EQ(4, ps_node_funcptr_sig(&canonical_funcptr).function.callable.return_shape.int_width);

  node_t compound_lit_object = {0};
  compound_lit_object.kind = ND_LVAR;
  compound_lit_object.type = ps_type_new_array(
      ps_type_new_integer(TK_INT, 4, 0), 3, 12, 4, 0);
  node_t compound_lit_addr = {0};
  compound_lit_addr.kind = ND_ADDR;
  compound_lit_addr.lhs = &compound_lit_object;
  ASSERT_EQ(12, ps_node_compound_literal_array_size(&compound_lit_addr));
  node_t compound_lit_comma = {0};
  compound_lit_comma.kind = ND_COMMA;
  compound_lit_comma.rhs = &compound_lit_addr;
  ASSERT_EQ(12, ps_node_compound_literal_array_size(&compound_lit_comma));
  node_t compound_lit_nonaddr = {0};
  compound_lit_nonaddr.kind = ND_DEREF;
  compound_lit_nonaddr.type = compound_lit_object.type;
  ASSERT_EQ(0, ps_node_compound_literal_array_size(&compound_lit_nonaddr));
  node_t typed_noncompound_addr = {0};
  typed_noncompound_addr.kind = ND_ADDR;
  typed_noncompound_addr.type = ps_type_new_pointer(
      ps_type_new_integer(TK_INT, 4, 0), 4);
  ASSERT_EQ(0, ps_node_compound_literal_array_size(
      &typed_noncompound_addr));

  node_t bitfield_deref = {0};
  bitfield_deref.kind = ND_DEREF;
  bitfield_deref.type_state.bit_width = 3;
  bitfield_deref.type_state.bit_offset = 5;
  bitfield_deref.type_state.bit_is_signed = 1;
  ASSERT_EQ(3, ps_node_bitfield_width(&bitfield_deref));
  int bf_width = 0;
  int bf_offset = 0;
  int bf_is_signed = 0;
  ASSERT_TRUE(ps_node_bitfield_info(&bitfield_deref,
                                     &bf_width, &bf_offset, &bf_is_signed));
  ASSERT_EQ(3, bf_width);
  ASSERT_EQ(5, bf_offset);
  ASSERT_EQ(1, bf_is_signed);
  node_t typed_nonbitfield = {0};
  typed_nonbitfield.kind = ND_DEREF;
  typed_nonbitfield.type = ps_type_new_integer(TK_INT, 4, 0);
  ASSERT_EQ(0, ps_node_bitfield_width(&typed_nonbitfield));
  ASSERT_TRUE(!ps_node_bitfield_info(&typed_nonbitfield,
                                      NULL, NULL, NULL));
  node_t non_mem_num = {0};
  non_mem_num.kind = ND_NUM;
  ASSERT_EQ(0, ps_node_bitfield_width(&non_mem_num));
  ASSERT_TRUE(!ps_node_bitfield_info(&non_mem_num, NULL, NULL, NULL));

  node_t typed_nonptr_stale_pointer_like = {0};
  typed_nonptr_stale_pointer_like.kind = ND_DEREF;
  typed_nonptr_stale_pointer_like.type = ps_type_new_integer(TK_INT, 8, 0);
  ASSERT_TRUE(!ps_node_value_is_pointer_like(
      &typed_nonptr_stale_pointer_like));
  ASSERT_EQ(8, ps_node_storage_type_size(&typed_nonptr_stale_pointer_like));

  node_t typed_ptr_no_mem_pointer_like = {0};
  typed_ptr_no_mem_pointer_like.kind = ND_DEREF;
  typed_ptr_no_mem_pointer_like.type = ps_type_new_pointer(
      ps_type_new_integer(TK_INT, 4, 0), 4);
  ASSERT_TRUE(ps_node_value_is_pointer_like(&typed_ptr_no_mem_pointer_like));

  parsed_code = parse_program_input(
      "int __tm_ptr_array_unary_deref(void) { "
      "  int w = 1; int *p1[2] = { &w, &w }; return *p1[0]; }");
  fn = as_func(parsed_code[0]);
  body = as_block(fn->base.rhs);
  node_t *ptrarr_ret = body->body[2];
  ASSERT_EQ(ND_RETURN, ptrarr_ret->kind);
  ASSERT_EQ(4, ps_node_type_size(ptrarr_ret->lhs));
  ASSERT_EQ(4, ps_node_storage_type_size(ptrarr_ret->lhs));
  ASSERT_TRUE(!ps_node_value_is_pointer_like(ptrarr_ret->lhs));
  ASSERT_EQ(0, ps_node_pointer_qual_levels(ptrarr_ret->lhs));

  node_t typed_deref_stale_tag_mem = {0};
  typed_deref_stale_tag_mem.kind = ND_DEREF;
  psx_type_t *typed_deref_stale_tag_inner =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 16);
  typed_deref_stale_tag_mem.type =
      ps_type_new_pointer(typed_deref_stale_tag_inner, 8);
  typed_deref_stale_tag_mem.type->pointer_qual_levels = 2;
  typed_deref_stale_tag_mem.type->base_deref_size = 16;
  typed_deref_stale_tag_mem.type->outer_stride = 4;
  node_t *typed_deref_unary =
      ps_node_new_unary_deref_for(&typed_deref_stale_tag_mem);
  ASSERT_EQ(16, ps_node_deref_size(typed_deref_unary));

  node_t typed_flat_tag_array_ptr = {0};
  typed_flat_tag_array_ptr.kind = ND_LVAR;
  psx_type_t *typed_flat_tag =
      ps_type_new_tag(TK_STRUCT, "TMFlatTag", 9, 0, 4);
  typed_flat_tag_array_ptr.type =
      ps_type_new_pointer(typed_flat_tag, 16);
  typed_flat_tag_array_ptr.type->base_deref_size = 4;
  typed_flat_tag_array_ptr.type->outer_stride = 16;
  typed_flat_tag_array_ptr.type->mid_stride = 99;
  typed_flat_tag_array_ptr.type->extra_strides_count = 1;
  typed_flat_tag_array_ptr.type->extra_strides[0] = 77;
  node_t *typed_flat_tag_array_deref =
      ps_node_new_unary_deref_for(&typed_flat_tag_array_ptr);
  psx_type_t *typed_flat_tag_array_deref_type =
      ps_node_get_type(typed_flat_tag_array_deref);
  ASSERT_TRUE(typed_flat_tag_array_deref_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, typed_flat_tag_array_deref_type->kind);
  ASSERT_EQ(16, ps_type_sizeof(typed_flat_tag_array_deref_type));
  ASSERT_EQ(4, ps_type_deref_size(typed_flat_tag_array_deref_type));
  ASSERT_EQ(0, typed_flat_tag_array_deref_type->outer_stride);
  ASSERT_EQ(0, typed_flat_tag_array_deref_type->mid_stride);
  ASSERT_EQ(0, typed_flat_tag_array_deref_type->extra_strides_count);

  node_t typed_missing_ptr_mem = {0};
  typed_missing_ptr_mem.kind = ND_LVAR;
  typed_missing_ptr_mem.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 8);
  typed_missing_ptr_mem.type->base_deref_size = 0;
  typed_missing_ptr_mem.type->ptr_array_pointee_bytes = 0;
  typed_missing_ptr_mem.type->vla_row_stride_frame_off = 0;
  typed_missing_ptr_mem.type->vla_strides_remaining = 0;
  ASSERT_EQ(4, ps_node_base_deref_size(&typed_missing_ptr_mem));
  ASSERT_EQ(0, ps_node_ptr_array_pointee_bytes(&typed_missing_ptr_mem));
  ASSERT_EQ(0, ps_node_vla_row_stride_frame_off(&typed_missing_ptr_mem));

  node_t typed_unsigned_ptr_mem = {0};
  typed_unsigned_ptr_mem.kind = ND_LVAR;
  typed_unsigned_ptr_mem.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 1), 8);
  ASSERT_TRUE(ps_node_pointee_is_unsigned(&typed_unsigned_ptr_mem));
  int atomic_width = 0;
  int atomic_is_unsigned = 0;
  ASSERT_TRUE(ps_node_atomic_pointer_info(&typed_unsigned_ptr_mem,
                                           &atomic_width, &atomic_is_unsigned));
  ASSERT_EQ(8, atomic_width);
  ASSERT_EQ(1, atomic_is_unsigned);

  node_t typed_unsigned_ptrptr_mem = {0};
  typed_unsigned_ptrptr_mem.kind = ND_DEREF;
  psx_type_t *typed_unsigned_char =
      ps_type_new_integer(TK_UNSIGNED, 1, 1);
  typed_unsigned_ptrptr_mem.type = ps_type_new_pointer(
      ps_type_new_pointer(typed_unsigned_char, 1), 8);
  node_t *typed_unsigned_ptrptr_elem = ps_node_new_subscript_deref_for(
      &typed_unsigned_ptrptr_mem,
      &typed_unsigned_ptrptr_mem, ps_node_new_num(0),
      8, 0, 0, NULL, 0);
  ASSERT_TRUE(ps_node_get_type(typed_unsigned_ptrptr_elem) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER,
            ps_node_get_type(typed_unsigned_ptrptr_elem)->kind);
  ASSERT_TRUE(ps_node_pointee_is_unsigned(typed_unsigned_ptrptr_elem));
  node_t *typed_unsigned_ptrptr_cell = ps_node_new_subscript_deref_for(
      typed_unsigned_ptrptr_elem, typed_unsigned_ptrptr_elem,
      ps_node_new_num(1), 1, 0, 0, NULL, 0);
  ASSERT_TRUE(ps_node_integer_value_is_unsigned(typed_unsigned_ptrptr_cell));

  parsed_code = parse_program_input(
      "int __tm_unsigned_ptrptr(void) { "
      "  unsigned char a[2]; unsigned char *lp = a; "
      "  unsigned char **pp = &lp; return pp[0][1]; }");
  fn = as_func(parsed_code[0]);
  lvar_t *unsigned_pp = find_func_lvar(fn, "pp");
  ASSERT_TRUE(unsigned_pp != NULL);
  ASSERT_TRUE(unsigned_pp->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, unsigned_pp->decl_type->kind);
  ASSERT_TRUE(unsigned_pp->decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, unsigned_pp->decl_type->base->kind);
  ASSERT_TRUE(unsigned_pp->decl_type->base->base != NULL);
  ASSERT_TRUE(ps_type_is_unsigned(unsigned_pp->decl_type->base->base));
  node_t *unsigned_pp_node = ps_node_new_lvar_identifier_ref_for(unsigned_pp);
  node_t *unsigned_pp_elem = ps_node_new_subscript_deref_for(
      unsigned_pp_node, unsigned_pp_node, ps_node_new_num(0),
      8, 0, 0, NULL, 0);
  ASSERT_TRUE(ps_node_get_type(unsigned_pp_elem) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(unsigned_pp_elem)->kind);
  ASSERT_TRUE(ps_node_get_type(unsigned_pp_elem)->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, ps_node_get_type(unsigned_pp_elem)->base->kind);
  ASSERT_TRUE(!ps_node_scalar_ptr_member_lvalue(unsigned_pp_elem));
  ASSERT_TRUE(ps_node_pointee_is_unsigned(unsigned_pp_elem));
  node_t *unsigned_pp_cell = ps_node_new_subscript_deref_for(
      unsigned_pp_elem, unsigned_pp_elem, ps_node_new_num(1),
      1, 0, 0, NULL, 0);
  ASSERT_TRUE(ps_node_get_type(unsigned_pp_cell) != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, ps_node_get_type(unsigned_pp_cell)->kind);
  ASSERT_TRUE(!ps_node_is_pointer(unsigned_pp_cell));
  ASSERT_TRUE(ps_node_integer_value_is_unsigned(unsigned_pp_cell));

  node_t canonical_atomic_ptr = {0};
  canonical_atomic_ptr.kind = ND_DEREF;
  canonical_atomic_ptr.type = ps_type_new_pointer(
      ps_type_new_integer(TK_UNSIGNED, 4, 1), 3);
  ASSERT_TRUE(ps_node_atomic_pointer_info(&canonical_atomic_ptr,
                                           &atomic_width, &atomic_is_unsigned));
  ASSERT_EQ(4, atomic_width);
  ASSERT_EQ(1, atomic_is_unsigned);

  node_t unsigned_atomic_target_mem = {0};
  unsigned_atomic_target_mem.kind = ND_LVAR;
  unsigned_atomic_target_mem.is_unsigned = 1;
  unsigned_atomic_target_mem.type =
      ps_type_new_integer(TK_UNSIGNED, 1, 1);
  node_t *unsigned_atomic_addr =
      ps_node_new_unary_addr_for(&unsigned_atomic_target_mem);
  ASSERT_TRUE(ps_node_atomic_pointer_info(unsigned_atomic_addr,
                                           &atomic_width, &atomic_is_unsigned));
  ASSERT_EQ(1, atomic_width);
  ASSERT_EQ(1, atomic_is_unsigned);

  node_t typed_bool_ptr_mem = {0};
  typed_bool_ptr_mem.kind = ND_LVAR;
  psx_type_t *typed_bool_type = ps_type_new(PSX_TYPE_BOOL);
  typed_bool_type->size = 1;
  typed_bool_ptr_mem.type = ps_type_new_pointer(typed_bool_type, 8);
  ASSERT_TRUE(ps_node_pointee_is_bool(&typed_bool_ptr_mem));

  node_t typed_void_ptr_mem = {0};
  typed_void_ptr_mem.kind = ND_LVAR;
  typed_void_ptr_mem.type = ps_type_new_pointer(ps_type_new(PSX_TYPE_VOID), 8);
  ASSERT_TRUE(ps_node_pointee_is_void(&typed_void_ptr_mem));

  node_t typed_void_ptr_ptr_mem = {0};
  typed_void_ptr_ptr_mem.kind = ND_LVAR;
  typed_void_ptr_ptr_mem.type = ps_type_new_pointer(
      ps_type_new_pointer(ps_type_new(PSX_TYPE_VOID), 8), 8);
  ASSERT_TRUE(!ps_node_pointee_is_void(&typed_void_ptr_ptr_mem));
  expect_parse_ok(
      "int void_pp(void) { void *value = 0; void **out = &value; "
      "*out = (void *)0; return 0; }");

  node_t typed_const_view_mem = {0};
  typed_const_view_mem.kind = ND_LVAR;
  typed_const_view_mem.type = ps_type_new_tag(TK_STRUCT, "TypedView", 9, 1, 4);
  ASSERT_EQ(4, ps_node_aggregate_value_size(&typed_const_view_mem));
  ASSERT_EQ(0, ps_node_pointer_qual_levels(&typed_const_view_mem));
  ASSERT_EQ(0u, ps_node_pointer_const_qual_mask(&typed_const_view_mem));
  ASSERT_EQ(0u, ps_node_pointer_volatile_qual_mask(&typed_const_view_mem));
  ASSERT_EQ(0, ps_node_base_deref_size(&typed_const_view_mem));
  ASSERT_EQ(0, ps_node_ptr_array_pointee_bytes(&typed_const_view_mem));
  ASSERT_EQ(TK_FLOAT_KIND_NONE, ps_node_pointee_fp_kind(&typed_const_view_mem));
  ASSERT_TRUE(!ps_node_pointee_is_const_qualified(&typed_const_view_mem));
  ASSERT_TRUE(!ps_node_pointee_is_volatile_qualified(&typed_const_view_mem));
  ASSERT_TRUE(!ps_node_pointee_is_unsigned(&typed_const_view_mem));
  ASSERT_TRUE(!ps_node_pointee_is_bool(&typed_const_view_mem));
  ASSERT_TRUE(!ps_node_pointee_is_void(&typed_const_view_mem));

  node_t typed_stale_self_const_mem = {0};
  typed_stale_self_const_mem.kind = ND_LVAR;
  typed_stale_self_const_mem.type = ps_type_new_integer(TK_INT, 4, 0);
  expect_const_assign_ok_for_node(&typed_stale_self_const_mem);

  node_t typed_scalar_canonical_const_mem = {0};
  typed_scalar_canonical_const_mem.kind = ND_LVAR;
  typed_scalar_canonical_const_mem.type =
      ps_type_new_integer(TK_INT, 4, 0);
  typed_scalar_canonical_const_mem.type->is_const_qualified = 1;
  expect_const_assign_fail_for_node(
      &typed_scalar_canonical_const_mem);

  node_t typed_ptr_canonical_self_const_mem = {0};
  typed_ptr_canonical_self_const_mem.kind = ND_LVAR;
  typed_ptr_canonical_self_const_mem.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 8);
  typed_ptr_canonical_self_const_mem.type->pointer_const_qual_mask = 1u;
  expect_const_assign_fail_for_node(
      &typed_ptr_canonical_self_const_mem);

  node_t typed_nonconst_ptr_lhs_mem = {0};
  typed_nonconst_ptr_lhs_mem.kind = ND_LVAR;
  typed_nonconst_ptr_lhs_mem.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 8);
  node_t typed_const_ptr_rhs_mem = {0};
  typed_const_ptr_rhs_mem.kind = ND_LVAR;
  psx_type_t *typed_const_rhs_base = ps_type_new_integer(TK_INT, 4, 0);
  typed_const_rhs_base->is_const_qualified = 1;
  typed_const_ptr_rhs_mem.type = ps_type_new_pointer(typed_const_rhs_base, 8);
  expect_const_qual_discard_fail_for_nodes(&typed_nonconst_ptr_lhs_mem,
                                           &typed_const_ptr_rhs_mem);

  node_t typed_funcptr_view_mem = {0};
  typed_funcptr_view_mem.kind = ND_LVAR;
  typed_funcptr_view_mem.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 8);
  typed_funcptr_view_mem.type->funcptr_sig.function.callable.signature.param_fp_mask = 1;
  typed_funcptr_view_mem.type->funcptr_sig.function.callable.return_shape.fp_kind = TK_FLOAT_KIND_DOUBLE;
  ASSERT_EQ(1u, ps_node_funcptr_param_fp_mask(&typed_funcptr_view_mem));
  ASSERT_EQ(0u, ps_node_funcptr_param_int_mask(&typed_funcptr_view_mem));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            ps_node_funcptr_ret_fp_kind(&typed_funcptr_view_mem));
  ASSERT_TRUE(!ps_node_funcptr_returns_complex(&typed_funcptr_view_mem));
  ASSERT_TRUE(!ps_node_funcptr_returns_pointee_array(&typed_funcptr_view_mem));

  node_t typed_funcptr_array_mem = {0};
  typed_funcptr_array_mem.kind = ND_DEREF;
  psx_type_t *typed_funcptr_array_elem =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 8);
  typed_funcptr_array_mem.type =
      ps_type_new_array(typed_funcptr_array_elem, 2, 16, 8, 0);
  typed_funcptr_array_mem.type->funcptr_sig.function.callable.signature.param_fp_mask = 1;
  node_t *typed_funcptr_array_elem_node = ps_node_new_subscript_deref_for(
      &typed_funcptr_array_mem, &typed_funcptr_array_mem,
      ps_node_new_num(0), 8, 0, 0, NULL, 0);
  ASSERT_EQ(1u, ps_node_funcptr_param_fp_mask(typed_funcptr_array_elem_node));

  node_t typed_funcptr_callee_mem = {0};
  typed_funcptr_callee_mem.kind = ND_LVAR;
  typed_funcptr_callee_mem.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 8);
  typed_funcptr_callee_mem.type->funcptr_sig.function.callable.return_shape.int_width = 8;
  node_func_t typed_indirect_call = {0};
  typed_indirect_call.base.kind = ND_FUNCALL;
  typed_indirect_call.callee = &typed_funcptr_callee_mem;
  psx_type_t *typed_indirect_ty = ps_node_get_type((node_t *)&typed_indirect_call);
  ASSERT_TRUE(typed_indirect_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, typed_indirect_ty->kind);
  ASSERT_EQ(8, ps_type_sizeof(typed_indirect_ty));

  node_t typed_stale_funcptr_callee_mem = {0};
  typed_stale_funcptr_callee_mem.kind = ND_LVAR;
  typed_stale_funcptr_callee_mem.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 8);
  node_func_t typed_stale_indirect_call = {0};
  typed_stale_indirect_call.base.kind = ND_FUNCALL;
  typed_stale_indirect_call.callee = &typed_stale_funcptr_callee_mem;
  ASSERT_TRUE(ps_node_get_type((node_t *)&typed_stale_indirect_call) == NULL);

  node_t typed_tag_ret_funcptr_callee_mem = {0};
  typed_tag_ret_funcptr_callee_mem.kind = ND_LVAR;
  psx_type_t *typed_tag_ret = ps_type_new_tag(TK_STRUCT, "Ret", 3, 7, 4);
  typed_tag_ret_funcptr_callee_mem.type = ps_type_new_pointer(typed_tag_ret, 4);
  typed_tag_ret_funcptr_callee_mem.type->funcptr_sig.function.callable.signature.param_int_mask = 1u;
  node_func_t typed_tag_ret_indirect_call = {0};
  typed_tag_ret_indirect_call.base.kind = ND_FUNCALL;
  typed_tag_ret_indirect_call.callee = &typed_tag_ret_funcptr_callee_mem;
  psx_type_t *typed_tag_ret_ty =
      ps_node_get_type((node_t *)&typed_tag_ret_indirect_call);
  ASSERT_TRUE(typed_tag_ret_ty != NULL);
  ASSERT_EQ(PSX_TYPE_STRUCT, typed_tag_ret_ty->kind);
  ASSERT_EQ(4, ps_type_sizeof(typed_tag_ret_ty));
  ASSERT_EQ(TK_STRUCT, typed_tag_ret_ty->tag_kind);
  ASSERT_EQ(3, typed_tag_ret_ty->tag_len);
  ASSERT_TRUE(strncmp(typed_tag_ret_ty->tag_name, "Ret", 3) == 0);

  node_func_t typed_cached_ptr_call = {0};
  typed_cached_ptr_call.base.kind = ND_FUNCALL;
  typed_cached_ptr_call.base.fp_kind = TK_FLOAT_KIND_DOUBLE;
  typed_cached_ptr_call.base.is_complex = 1;
  typed_cached_ptr_call.base.is_unsigned = 1;
  typed_cached_ptr_call.base.is_long_long = 1;
  typed_cached_ptr_call.base.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 4);
  ASSERT_TRUE(ps_node_get_type((node_t *)&typed_cached_ptr_call) ==
              typed_cached_ptr_call.base.type);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, typed_cached_ptr_call.base.fp_kind);
  ASSERT_EQ(0, typed_cached_ptr_call.base.is_complex);
  ASSERT_EQ(0, typed_cached_ptr_call.base.is_unsigned);
  ASSERT_EQ(0, typed_cached_ptr_call.base.is_long_long);
  ASSERT_EQ(0, ps_node_aggregate_value_size((node_t *)&typed_cached_ptr_call));
  ASSERT_TRUE(!ps_node_value_is_void((node_t *)&typed_cached_ptr_call));

  node_func_t typed_cached_complex_call = {0};
  typed_cached_complex_call.base.kind = ND_FUNCALL;
  typed_cached_complex_call.base.type = ps_type_new(PSX_TYPE_COMPLEX);
  typed_cached_complex_call.base.type->fp_kind = TK_FLOAT_KIND_FLOAT;
  typed_cached_complex_call.base.type->size = 8;
  ASSERT_TRUE(ps_node_get_type((node_t *)&typed_cached_complex_call) ==
              typed_cached_complex_call.base.type);
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT, typed_cached_complex_call.base.fp_kind);
  ASSERT_EQ(1, typed_cached_complex_call.base.is_complex);
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT,
            ps_node_value_fp_kind((node_t *)&typed_cached_complex_call));
  ASSERT_TRUE(ps_node_value_is_complex((node_t *)&typed_cached_complex_call));

  node_func_t typed_cached_struct_call = {0};
  typed_cached_struct_call.base.kind = ND_FUNCALL;
  typed_cached_struct_call.base.type =
      ps_type_new_tag(TK_STRUCT, "CallRet", 7, 0, 12);
  ASSERT_TRUE(ps_node_get_type((node_t *)&typed_cached_struct_call) ==
              typed_cached_struct_call.base.type);
  ASSERT_EQ(12, ps_node_aggregate_value_size((node_t *)&typed_cached_struct_call));

  node_func_t typed_cached_void_call = {0};
  typed_cached_void_call.base.kind = ND_FUNCALL;
  typed_cached_void_call.base.type = ps_type_new(PSX_TYPE_VOID);
  ASSERT_TRUE(ps_node_value_is_void((node_t *)&typed_cached_void_call));

  node_func_t typed_cached_unsigned_call = {0};
  typed_cached_unsigned_call.base.kind = ND_FUNCALL;
  typed_cached_unsigned_call.base.type = ps_type_new_integer(TK_UNSIGNED, 8, 1);
  typed_cached_unsigned_call.base.type->is_long_long = 1;
  ASSERT_TRUE(ps_node_get_type((node_t *)&typed_cached_unsigned_call) ==
              typed_cached_unsigned_call.base.type);
  ASSERT_EQ(1, typed_cached_unsigned_call.base.is_unsigned);
  ASSERT_EQ(1, typed_cached_unsigned_call.base.is_long_long);
  ASSERT_TRUE(ps_node_conversion_value_is_unsigned(
      (node_t *)&typed_cached_unsigned_call));
  ASSERT_TRUE(ps_node_i64_widen_source_is_unsigned(
      (node_t *)&typed_cached_unsigned_call));

  parsed_code = parse_program_input("long __tm_funcdef_long(void) { return 1; }");
  node_func_t *typed_long_funcdef = as_func(parsed_code[0]);
  psx_type_t *typed_long_funcdef_ty =
      ps_node_get_type((node_t *)typed_long_funcdef);
  ASSERT_TRUE(typed_long_funcdef_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, typed_long_funcdef_ty->kind);
  ASSERT_EQ(8, ps_type_sizeof(typed_long_funcdef_ty));
  ASSERT_EQ(8, ps_node_type_size((node_t *)typed_long_funcdef));

  parsed_code =
      parse_program_input("int *__tm_funcdef_ptr(int *p) { return p; }");
  node_func_t *typed_ptr_funcdef = as_func(parsed_code[0]);
  psx_type_t *typed_ptr_funcdef_ty =
      ps_node_get_type((node_t *)typed_ptr_funcdef);
  ASSERT_TRUE(typed_ptr_funcdef_ty != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, typed_ptr_funcdef_ty->kind);
  ASSERT_TRUE(ps_node_value_is_pointer_like((node_t *)typed_ptr_funcdef));

  parsed_code = parse_program_input("void __tm_funcdef_void(void) { }");
  node_func_t *typed_void_funcdef = as_func(parsed_code[0]);
  psx_type_t *typed_void_funcdef_ty =
      ps_node_get_type((node_t *)typed_void_funcdef);
  ASSERT_TRUE(typed_void_funcdef_ty != NULL);
  ASSERT_EQ(PSX_TYPE_VOID, typed_void_funcdef_ty->kind);
  ASSERT_TRUE(ps_node_value_is_void((node_t *)typed_void_funcdef));

  parsed_code = parse_program_input(
      "struct __tm_fdr { int a; int b; } __tm_funcdef_struct(void) { "
      "struct __tm_fdr r; return r; }");
  node_func_t *typed_struct_funcdef = as_func(parsed_code[0]);
  psx_type_t *typed_struct_funcdef_ty =
      ps_node_get_type((node_t *)typed_struct_funcdef);
  ASSERT_TRUE(typed_struct_funcdef_ty != NULL);
  ASSERT_EQ(PSX_TYPE_STRUCT, typed_struct_funcdef_ty->kind);
  ASSERT_EQ(8, ps_type_sizeof(typed_struct_funcdef_ty));
  ASSERT_EQ(8, ps_node_aggregate_value_size((node_t *)typed_struct_funcdef));

  parsed_code =
      parse_program_input("_Complex double __tm_funcdef_complex(void) { return 1; }");
  node_func_t *typed_complex_funcdef = as_func(parsed_code[0]);
  psx_type_t *typed_complex_funcdef_ty =
      ps_node_get_type((node_t *)typed_complex_funcdef);
  ASSERT_TRUE(typed_complex_funcdef_ty != NULL);
  ASSERT_EQ(PSX_TYPE_COMPLEX, typed_complex_funcdef_ty->kind);
  ASSERT_TRUE(ps_node_value_is_complex((node_t *)typed_complex_funcdef));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            ps_node_value_fp_kind((node_t *)typed_complex_funcdef));

  node_t typed_cached_pointer_stale_complex = {0};
  typed_cached_pointer_stale_complex.kind = ND_ADD;
  typed_cached_pointer_stale_complex.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 4);
  typed_cached_pointer_stale_complex.fp_kind = TK_FLOAT_KIND_DOUBLE;
  typed_cached_pointer_stale_complex.is_complex = 1;
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            ps_node_value_fp_kind(&typed_cached_pointer_stale_complex));
  ASSERT_TRUE(!ps_node_value_is_complex(&typed_cached_pointer_stale_complex));

  node_t typed_double_ret_callee = {0};
  typed_double_ret_callee.kind = ND_LVAR;
  typed_double_ret_callee.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 8);
  typed_double_ret_callee.type->funcptr_sig.function.callable.return_shape.fp_kind =
      TK_FLOAT_KIND_DOUBLE;
  node_func_t typed_double_ret_call = {0};
  typed_double_ret_call.base.kind = ND_FUNCALL;
  typed_double_ret_call.callee = &typed_double_ret_callee;
  psx_type_t *typed_double_ret_ty =
      ps_node_get_type((node_t *)&typed_double_ret_call);
  ASSERT_TRUE(typed_double_ret_ty != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, typed_double_ret_ty->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, typed_double_ret_call.base.fp_kind);

  node_t typed_complex_operand = {0};
  typed_complex_operand.kind = ND_NUM;
  typed_complex_operand.type = ps_type_new(PSX_TYPE_COMPLEX);
  typed_complex_operand.type->fp_kind = TK_FLOAT_KIND_FLOAT;
  typed_complex_operand.type->size = 8;
  node_t *typed_complex_binary =
      ps_node_new_binary(ND_ADD, &typed_complex_operand, ps_node_new_num(1));
  typed_complex_binary->fp_kind = TK_FLOAT_KIND_NONE;
  typed_complex_binary->is_complex = 0;
  ASSERT_EQ(0, typed_complex_binary->is_complex);
  psx_type_t *typed_complex_binary_ty = ps_node_get_type(typed_complex_binary);
  ASSERT_TRUE(typed_complex_binary_ty != NULL);
  ASSERT_EQ(PSX_TYPE_COMPLEX, typed_complex_binary_ty->kind);
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT, typed_complex_binary->fp_kind);
  ASSERT_EQ(1, typed_complex_binary->is_complex);

  node_t typed_double_lhs = {0};
  typed_double_lhs.kind = ND_NUM;
  typed_double_lhs.type = ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8);
  typed_double_lhs.fp_kind = TK_FLOAT_KIND_NONE;
  node_t *typed_constructed_double =
      ps_node_new_binary(ND_ADD, &typed_double_lhs, ps_node_new_num(1));
  ASSERT_TRUE(typed_constructed_double->type != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, typed_constructed_double->type->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, typed_constructed_double->fp_kind);

  node_t typed_unsigned_ll_lhs = {0};
  typed_unsigned_ll_lhs.kind = ND_NUM;
  typed_unsigned_ll_lhs.type = ps_type_new_integer(TK_UNSIGNED, 8, 1);
  typed_unsigned_ll_lhs.type->is_long_long = 1;
  typed_unsigned_ll_lhs.is_unsigned = 0;
  typed_unsigned_ll_lhs.is_long_long = 0;
  node_t *typed_constructed_unsigned_ll =
      ps_node_new_binary(ND_MUL, &typed_unsigned_ll_lhs, ps_node_new_num(2));
  ASSERT_TRUE(typed_constructed_unsigned_ll->type != NULL);
  ASSERT_TRUE(ps_type_is_unsigned(typed_constructed_unsigned_ll->type));
  ASSERT_EQ(1, typed_constructed_unsigned_ll->is_unsigned);
  ASSERT_EQ(1, typed_constructed_unsigned_ll->is_long_long);

  node_t typed_int_lhs_with_stale_fp = {0};
  typed_int_lhs_with_stale_fp.kind = ND_NUM;
  typed_int_lhs_with_stale_fp.type = ps_type_new_integer(TK_INT, 4, 0);
  typed_int_lhs_with_stale_fp.fp_kind = TK_FLOAT_KIND_DOUBLE;
  typed_int_lhs_with_stale_fp.is_complex = 1;
  node_t typed_int_rhs_with_stale_fp = {0};
  typed_int_rhs_with_stale_fp.kind = ND_NUM;
  typed_int_rhs_with_stale_fp.type = ps_type_new_integer(TK_INT, 4, 0);
  typed_int_rhs_with_stale_fp.fp_kind = TK_FLOAT_KIND_DOUBLE;
  typed_int_rhs_with_stale_fp.is_complex = 1;
  node_t typed_binary_stale_result = {0};
  typed_binary_stale_result.kind = ND_MUL;
  typed_binary_stale_result.lhs = &typed_int_lhs_with_stale_fp;
  typed_binary_stale_result.rhs = &typed_int_rhs_with_stale_fp;
  typed_binary_stale_result.fp_kind = TK_FLOAT_KIND_DOUBLE;
  typed_binary_stale_result.is_complex = 1;
  psx_type_t *typed_binary_stale_result_ty =
      ps_node_get_type(&typed_binary_stale_result);
  ASSERT_TRUE(typed_binary_stale_result_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, typed_binary_stale_result_ty->kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, typed_binary_stale_result.fp_kind);
  ASSERT_EQ(0, typed_binary_stale_result.is_complex);
  node_t *typed_constructed_stale_int_binary = ps_node_new_binary(
      ND_ADD, &typed_int_lhs_with_stale_fp, &typed_int_rhs_with_stale_fp);
  ASSERT_TRUE(typed_constructed_stale_int_binary->type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, typed_constructed_stale_int_binary->type->kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, typed_constructed_stale_int_binary->fp_kind);
  ASSERT_EQ(0, typed_constructed_stale_int_binary->is_complex);

  node_ctrl_t typed_cached_ternary = {0};
  typed_cached_ternary.base.kind = ND_TERNARY;
  typed_cached_ternary.base.fp_kind = TK_FLOAT_KIND_DOUBLE;
  typed_cached_ternary.base.is_complex = 1;
  typed_cached_ternary.base.type = ps_type_new_integer(TK_UNSIGNED, 8, 1);
  typed_cached_ternary.base.type->is_long_long = 1;
  ASSERT_TRUE(ps_node_get_type((node_t *)&typed_cached_ternary) ==
              typed_cached_ternary.base.type);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, typed_cached_ternary.base.fp_kind);
  ASSERT_EQ(0, typed_cached_ternary.base.is_complex);
  ASSERT_EQ(1, typed_cached_ternary.base.is_unsigned);
  ASSERT_EQ(1, typed_cached_ternary.base.is_long_long);
  ASSERT_TRUE(ps_node_is_long_long_type((node_t *)&typed_cached_ternary));

  node_t typed_stale_scalar_flags_branch = {0};
  typed_stale_scalar_flags_branch.kind = ND_DEREF;
  typed_stale_scalar_flags_branch.is_long_long = 1;
  node_ctrl_t typed_cached_int_ternary = {0};
  typed_cached_int_ternary.base.kind = ND_TERNARY;
  typed_cached_int_ternary.base.rhs = &typed_stale_scalar_flags_branch;
  typed_cached_int_ternary.els = &typed_stale_scalar_flags_branch;
  typed_cached_int_ternary.base.type = ps_type_new_integer(TK_INT, 4, 0);
  ASSERT_TRUE(ps_node_get_type((node_t *)&typed_cached_int_ternary) ==
              typed_cached_int_ternary.base.type);
  ASSERT_TRUE(!ps_node_is_long_long_type((node_t *)&typed_cached_int_ternary));
  ASSERT_TRUE(!ps_node_is_plain_char_type((node_t *)&typed_cached_int_ternary));
  ASSERT_TRUE(!ps_node_is_long_double_type((node_t *)&typed_cached_int_ternary));

  node_ctrl_t typed_uncached_int_ternary = {0};
  typed_uncached_int_ternary.base.kind = ND_TERNARY;
  typed_uncached_int_ternary.base.rhs = &typed_int_lhs_with_stale_fp;
  typed_uncached_int_ternary.els = &typed_int_rhs_with_stale_fp;
  typed_uncached_int_ternary.base.fp_kind = TK_FLOAT_KIND_DOUBLE;
  typed_uncached_int_ternary.base.is_complex = 1;
  psx_type_t *typed_uncached_int_ternary_ty =
      ps_node_get_type((node_t *)&typed_uncached_int_ternary);
  ASSERT_TRUE(typed_uncached_int_ternary_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, typed_uncached_int_ternary_ty->kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, typed_uncached_int_ternary.base.fp_kind);
  ASSERT_EQ(0, typed_uncached_int_ternary.base.is_complex);

  node_ctrl_t typed_cached_long_double_ternary = {0};
  typed_cached_long_double_ternary.base.kind = ND_TERNARY;
  typed_cached_long_double_ternary.base.type =
      ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8);
  typed_cached_long_double_ternary.base.type->is_long_double = 1;
  ASSERT_TRUE(ps_node_is_long_double_type(
      (node_t *)&typed_cached_long_double_ternary));

  node_t typed_float_rhs = {0};
  typed_float_rhs.kind = ND_NUM;
  typed_float_rhs.type = ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8);
  typed_float_rhs.fp_kind = TK_FLOAT_KIND_NONE;
  node_t *typed_comma = ps_node_new_binary(
      ND_COMMA, ps_node_new_num(0), &typed_float_rhs);
  typed_comma->fp_kind = TK_FLOAT_KIND_NONE;
  ASSERT_TRUE(ps_node_get_type(typed_comma) == typed_float_rhs.type);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, typed_comma->fp_kind);

  node_t typed_stmt_expr = {0};
  typed_stmt_expr.kind = ND_STMT_EXPR;
  typed_stmt_expr.rhs = &typed_float_rhs;
  typed_stmt_expr.fp_kind = TK_FLOAT_KIND_NONE;
  ASSERT_TRUE(ps_node_get_type(&typed_stmt_expr) == typed_float_rhs.type);
  ASSERT_TRUE(typed_stmt_expr.type == typed_float_rhs.type);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, typed_stmt_expr.fp_kind);

  node_t typed_stmt_tag_ptr_rhs = {0};
  typed_stmt_tag_ptr_rhs.kind = ND_LVAR;
  psx_type_t *typed_stmt_tag =
      ps_type_new_tag(TK_STRUCT, "StmtTag", 7, 4, 16);
  typed_stmt_tag_ptr_rhs.type = ps_type_new_pointer(typed_stmt_tag, 16);
  typed_stmt_tag_ptr_rhs.type->pointer_qual_levels = 1;
  typed_stmt_tag_ptr_rhs.type->base_deref_size = 16;
  node_t typed_stmt_tag_ptr = {0};
  typed_stmt_tag_ptr.kind = ND_STMT_EXPR;
  typed_stmt_tag_ptr.rhs = &typed_stmt_tag_ptr_rhs;
  ASSERT_EQ(1, ps_node_is_pointer(&typed_stmt_tag_ptr));
  ASSERT_EQ(16, ps_node_deref_size(&typed_stmt_tag_ptr));
  ASSERT_EQ(1, ps_node_pointer_qual_levels(&typed_stmt_tag_ptr));
  ASSERT_EQ(16, ps_node_base_deref_size(&typed_stmt_tag_ptr));
  token_kind_t typed_stmt_tag_kind = TK_EOF;
  char *typed_stmt_tag_name = NULL;
  int typed_stmt_tag_len = 0;
  int typed_stmt_is_tag_pointer = 0;
  ps_node_get_tag_type(&typed_stmt_tag_ptr, &typed_stmt_tag_kind,
                        &typed_stmt_tag_name, &typed_stmt_tag_len,
                        &typed_stmt_is_tag_pointer);
  ASSERT_EQ(TK_STRUCT, typed_stmt_tag_kind);
  ASSERT_EQ(7, typed_stmt_tag_len);
  ASSERT_TRUE(strncmp(typed_stmt_tag_name, "StmtTag", 7) == 0);
  ASSERT_EQ(1, typed_stmt_is_tag_pointer);
  ASSERT_EQ(3, ps_node_get_tag_scope_depth(&typed_stmt_tag_ptr));

  node_t typed_stmt_fp_ptr_rhs = {0};
  typed_stmt_fp_ptr_rhs.kind = ND_LVAR;
  typed_stmt_fp_ptr_rhs.type =
      ps_type_new_pointer(ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8), 8);
  typed_stmt_fp_ptr_rhs.type->pointee_fp_kind = TK_FLOAT_KIND_DOUBLE;
  node_t typed_stmt_fp_ptr = {0};
  typed_stmt_fp_ptr.kind = ND_STMT_EXPR;
  typed_stmt_fp_ptr.rhs = &typed_stmt_fp_ptr_rhs;
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            ps_node_pointee_fp_kind(&typed_stmt_fp_ptr));

  node_t cached_scalar_rhs = {0};
  cached_scalar_rhs.kind = ND_NUM;
  cached_scalar_rhs.type = ps_type_new_integer(TK_INT, 4, 0);
  node_t cached_unsigned_comma = {0};
  cached_unsigned_comma.kind = ND_COMMA;
  cached_unsigned_comma.rhs = &cached_scalar_rhs;
  cached_unsigned_comma.type = ps_type_new_integer(TK_UNSIGNED, 8, 1);
  cached_unsigned_comma.type->is_long_long = 1;
  ASSERT_TRUE(ps_node_is_unsigned_type(&cached_unsigned_comma));
  ASSERT_TRUE(ps_node_is_long_long_type(&cached_unsigned_comma));

  node_t cached_plain_char_stmt = {0};
  cached_plain_char_stmt.kind = ND_STMT_EXPR;
  cached_plain_char_stmt.rhs = &cached_scalar_rhs;
  cached_plain_char_stmt.type = ps_type_new_integer(TK_CHAR, 1, 0);
  cached_plain_char_stmt.type->is_plain_char = 1;
  ASSERT_TRUE(ps_node_is_plain_char_type(&cached_plain_char_stmt));

  node_t cached_long_double_stmt = {0};
  cached_long_double_stmt.kind = ND_STMT_EXPR;
  cached_long_double_stmt.rhs = &cached_scalar_rhs;
  cached_long_double_stmt.type = ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8);
  cached_long_double_stmt.type->is_long_double = 1;
  ASSERT_TRUE(ps_node_is_long_double_type(&cached_long_double_stmt));

  node_t cached_add_ptr = {0};
  cached_add_ptr.kind = ND_ADD;
  cached_add_ptr.lhs = &cached_scalar_rhs;
  cached_add_ptr.rhs = ps_node_new_num(1);
  cached_add_ptr.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 4);
  cached_add_ptr.type->pointer_qual_levels = 3;
  cached_add_ptr.type->base_deref_size = 4;
  cached_add_ptr.type->pointer_const_qual_mask = 0x5;
  cached_add_ptr.type->pointer_volatile_qual_mask = 0x2;
  ASSERT_EQ(1, ps_node_pointer_qual_levels(&cached_add_ptr));
  ASSERT_EQ(4, ps_node_base_deref_size(&cached_add_ptr));
  ASSERT_EQ(1u, ps_node_pointer_const_qual_mask(&cached_add_ptr));
  ASSERT_EQ(0u, ps_node_pointer_volatile_qual_mask(&cached_add_ptr));

  node_t cached_nested_ptr = {0};
  cached_nested_ptr.kind = ND_ADD;
  cached_nested_ptr.lhs = &cached_scalar_rhs;
  cached_nested_ptr.rhs = ps_node_new_num(1);
  psx_type_t *cached_nested_inner =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 4);
  cached_nested_inner->pointer_const_qual_mask = 1u;
  cached_nested_inner->pointer_volatile_qual_mask = 1u;
  cached_nested_ptr.type = ps_type_new_pointer(cached_nested_inner, 8);
  cached_nested_ptr.type->pointer_qual_levels = 1;
  cached_nested_ptr.type->pointer_const_qual_mask = 1u;
  cached_nested_ptr.type->pointer_volatile_qual_mask = 0u;
  ASSERT_EQ(2, ps_node_pointer_qual_levels(&cached_nested_ptr));
  ASSERT_EQ(3u, ps_node_pointer_const_qual_mask(&cached_nested_ptr));
  ASSERT_EQ(2u, ps_node_pointer_volatile_qual_mask(&cached_nested_ptr));

  node_t cached_ptr_array_add = {0};
  cached_ptr_array_add.kind = ND_ADD;
  cached_ptr_array_add.lhs = &cached_scalar_rhs;
  cached_ptr_array_add.rhs = ps_node_new_num(1);
  cached_ptr_array_add.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 4);
  cached_ptr_array_add.type->ptr_array_pointee_bytes = 24;
  ASSERT_EQ(0, ps_node_ptr_array_pointee_bytes(&cached_ptr_array_add));

  node_t cached_real_ptr_array_add = {0};
  cached_real_ptr_array_add.kind = ND_ADD;
  cached_real_ptr_array_add.lhs = &cached_scalar_rhs;
  cached_real_ptr_array_add.rhs = ps_node_new_num(1);
  cached_real_ptr_array_add.type = ps_type_new_pointer(
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 3, 12, 4, 0),
      12);
  cached_real_ptr_array_add.type->ptr_array_pointee_bytes = 24;
  ASSERT_EQ(12, ps_node_ptr_array_pointee_bytes(&cached_real_ptr_array_add));

  node_t cached_ptr_to_array_stride = {0};
  cached_ptr_to_array_stride.kind = ND_ADD;
  cached_ptr_to_array_stride.lhs = &cached_scalar_rhs;
  cached_ptr_to_array_stride.rhs = ps_node_new_num(1);
  psx_type_t *cached_ptr_to_array_stride_inner =
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 4, 16, 4, 0);
  psx_type_t *cached_ptr_to_array_stride_array =
      ps_type_new_array(cached_ptr_to_array_stride_inner, 3, 48, 16, 0);
  cached_ptr_to_array_stride.type =
      ps_type_new_pointer(cached_ptr_to_array_stride_array, 48);
  cached_ptr_to_array_stride.type->outer_stride = 999;
  cached_ptr_to_array_stride.type->mid_stride = 777;
  cached_ptr_to_array_stride.type->extra_strides_count = 1;
  cached_ptr_to_array_stride.type->extra_strides[0] = 333;
  int cached_ptr_to_array_inner = 0;
  int cached_ptr_to_array_next = 0;
  int cached_ptr_to_array_extra[5] = {0};
  int cached_ptr_to_array_extra_count = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(
      &cached_ptr_to_array_stride, &cached_ptr_to_array_inner,
      &cached_ptr_to_array_next, cached_ptr_to_array_extra,
      &cached_ptr_to_array_extra_count));
  ASSERT_EQ(16, cached_ptr_to_array_inner);
  ASSERT_EQ(4, cached_ptr_to_array_next);
	  ASSERT_EQ(0, cached_ptr_to_array_extra_count);
	  ASSERT_EQ(4, ps_type_pointer_view_mid_stride(
	                   cached_ptr_to_array_stride.type));

  node_t cached_stale_array_subscript_base = {0};
  cached_stale_array_subscript_base.kind = ND_LVAR;
  psx_type_t *cached_stale_subscript_leaf =
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 4, 16, 4, 0);
  psx_type_t *cached_stale_subscript_row =
      ps_type_new_array(cached_stale_subscript_leaf, 3, 48, 16, 0);
  cached_stale_subscript_row->outer_stride = 999;
  cached_stale_subscript_row->mid_stride = 777;
  cached_stale_subscript_row->extra_strides_count = 1;
  cached_stale_subscript_row->extra_strides[0] = 333;
  cached_stale_array_subscript_base.type =
      ps_type_new_array(cached_stale_subscript_row, 2, 96, 48, 0);
  int cached_stale_subscript_inner = 0;
  int cached_stale_subscript_next = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(
      &cached_stale_array_subscript_base,
      &cached_stale_subscript_inner, &cached_stale_subscript_next, NULL, NULL));
  ASSERT_EQ(48, cached_stale_subscript_inner);
  ASSERT_EQ(16, cached_stale_subscript_next);
  node_t *cached_stale_array_row = ps_node_new_subscript_deref_for(
      &cached_stale_array_subscript_base,
      &cached_stale_array_subscript_base, ps_node_new_num(0),
      ps_node_deref_size(&cached_stale_array_subscript_base),
      cached_stale_subscript_next, 0, NULL, 0);
  cached_stale_subscript_inner = 0;
  cached_stale_subscript_next = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(
      cached_stale_array_row, &cached_stale_subscript_inner,
      &cached_stale_subscript_next, NULL, NULL));
  ASSERT_EQ(16, ps_node_deref_size(cached_stale_array_row));
  ASSERT_EQ(16, cached_stale_subscript_inner);
  ASSERT_EQ(4, cached_stale_subscript_next);

  node_t cached_vla_add = {0};
  cached_vla_add.kind = ND_ADD;
  cached_vla_add.lhs = &cached_scalar_rhs;
  cached_vla_add.rhs = ps_node_new_num(1);
  cached_vla_add.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 4);
  cached_vla_add.type->vla_row_stride_frame_off = 123;
  cached_vla_add.type->vla_strides_remaining = 2;
  ASSERT_EQ(123, ps_node_vla_row_stride_frame_off(&cached_vla_add));

  node_t cached_stale_plain_stride_add = {0};
  cached_stale_plain_stride_add.kind = ND_ADD;
  cached_stale_plain_stride_add.lhs = &cached_scalar_rhs;
  cached_stale_plain_stride_add.rhs = ps_node_new_num(1);
  cached_stale_plain_stride_add.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 4);
  cached_stale_plain_stride_add.type->outer_stride = 48;
  cached_stale_plain_stride_add.type->mid_stride = 16;
  cached_stale_plain_stride_add.type->extra_strides_count = 1;
  cached_stale_plain_stride_add.type->extra_strides[0] = 8;
  ASSERT_TRUE(!ps_node_pointer_stride_metadata(
      &cached_stale_plain_stride_add, NULL, NULL, NULL, NULL));

  node_t cached_pointee_comma = {0};
  cached_pointee_comma.kind = ND_COMMA;
  cached_pointee_comma.rhs = &cached_scalar_rhs;
  psx_type_t *cached_unsigned_char = ps_type_new_integer(TK_UNSIGNED, 1, 1);
  cached_pointee_comma.type = ps_type_new_pointer(cached_unsigned_char, 1);
  ASSERT_TRUE(ps_node_pointee_is_unsigned(&cached_pointee_comma));

  node_t cached_tag_add = {0};
  cached_tag_add.kind = ND_ADD;
  cached_tag_add.lhs = &cached_scalar_rhs;
  cached_tag_add.rhs = ps_node_new_num(1);
  psx_type_t *cached_own_tag =
      ps_type_new_tag(TK_STRUCT, "OwnTag", 6, 5, 12);
  cached_tag_add.type = ps_type_new_pointer(cached_own_tag, 12);
  token_kind_t cached_tag_kind = TK_EOF;
  char *cached_tag_name = NULL;
  int cached_tag_len = 0;
  int cached_tag_is_pointer = 0;
  ps_node_get_tag_type(&cached_tag_add, &cached_tag_kind, &cached_tag_name,
                        &cached_tag_len, &cached_tag_is_pointer);
  ASSERT_EQ(TK_STRUCT, cached_tag_kind);
  ASSERT_EQ(6, cached_tag_len);
  ASSERT_TRUE(strncmp(cached_tag_name, "OwnTag", 6) == 0);
  ASSERT_EQ(1, cached_tag_is_pointer);
  ASSERT_EQ(4, ps_node_get_tag_scope_depth(&cached_tag_add));

  node_t typed_tag_mem = {0};
  typed_tag_mem.kind = ND_LVAR;
  psx_type_t *typed_tag = ps_type_new_tag(TK_STRUCT, "Typed", 5, 3, 4);
  typed_tag_mem.type = ps_type_new_pointer(typed_tag, 4);
  token_kind_t typed_tag_kind = TK_EOF;
  char *typed_tag_name = NULL;
  int typed_tag_len = 0;
  int typed_is_tag_pointer = 0;
  ps_node_get_tag_type(&typed_tag_mem, &typed_tag_kind, &typed_tag_name,
                        &typed_tag_len, &typed_is_tag_pointer);
  ASSERT_EQ(TK_STRUCT, typed_tag_kind);
  ASSERT_EQ(5, typed_tag_len);
  ASSERT_TRUE(strncmp(typed_tag_name, "Typed", 5) == 0);
  ASSERT_EQ(1, typed_is_tag_pointer);
  ASSERT_EQ(2, ps_node_get_tag_scope_depth(&typed_tag_mem));
  ASSERT_EQ(0, ps_node_aggregate_value_size(&typed_tag_mem));

  node_t canonical_aggregate = {0};
  canonical_aggregate.kind = ND_DEREF;
  canonical_aggregate.type = ps_type_new_tag(
      TK_STRUCT, "CanonicalAgg", 12, 0, 6);
  ASSERT_EQ(6, ps_node_aggregate_value_size(&canonical_aggregate));

  node_t typed_cast_long = {0};
  typed_cast_long.kind = ND_CAST;
  typed_cast_long.type = ps_type_new_integer(TK_LONG, 8, 0);
  int cast_target_size = 0;
  int cast_widen_zext = 0;
  int cast_needs_i64 = 0;
  ASSERT_TRUE(ps_node_cast_i64_extension_info(
      &typed_cast_long, &cast_target_size, &cast_widen_zext,
      &cast_needs_i64));
  ASSERT_EQ(8, cast_target_size);
  ASSERT_EQ(0, cast_widen_zext);
  ASSERT_EQ(1, cast_needs_i64);

  node_t *inferred_unsigned_cast = ps_node_new_integer_cast_result(
      ps_node_new_num(1), NULL, 8, 1, 0);
  ASSERT_TRUE(inferred_unsigned_cast->type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, inferred_unsigned_cast->type->kind);
  ASSERT_TRUE(ps_type_is_unsigned(inferred_unsigned_cast->type));

  node_t *canonical_zext_cast = ps_node_new_integer_cast_result_ex(
      ps_node_new_num(1), NULL, 8, 1, 0, 0, 1);
  ASSERT_TRUE(ps_node_cast_i64_extension_info(
      canonical_zext_cast, &cast_target_size, &cast_widen_zext,
      &cast_needs_i64));
  ASSERT_EQ(1, cast_widen_zext);

  node_t *typed_internal_slot = ps_node_new_lvar_typed(1234, 8);
  ASSERT_TRUE(typed_internal_slot->type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, typed_internal_slot->type->kind);
  ASSERT_EQ(8, ps_type_sizeof(typed_internal_slot->type));

  psx_type_t *typed_unsigned_cast_type = ps_type_new_integer(TK_UNSIGNED, 4, 1);
  node_t *typed_unsigned_cast_node = ps_node_new_integer_cast_result_ex(
      ps_node_new_num(1), typed_unsigned_cast_type, 4, 0, 0, 0, 0);
  ASSERT_TRUE(ps_node_get_type(typed_unsigned_cast_node) == typed_unsigned_cast_type);
  ASSERT_TRUE(ps_node_is_unsigned_type(typed_unsigned_cast_node));

  psx_type_t *typed_bool_cast_type = ps_type_new(PSX_TYPE_BOOL);
  typed_bool_cast_type->size = 1;
  node_t *typed_bool_cast_node = ps_node_new_integer_cast_result_ex(
      ps_node_new_num(1), typed_bool_cast_type, 4, 0, 0, 0, 0);
  ASSERT_TRUE(ps_node_get_type(typed_bool_cast_node) == typed_bool_cast_type);
  ASSERT_EQ(PSX_TYPE_BOOL, ps_node_get_type(typed_bool_cast_node)->kind);

  psx_type_t *typed_atomic_cast_type = ps_type_new_integer(TK_INT, 4, 0);
  typed_atomic_cast_type->is_atomic = 1;
  node_t *typed_atomic_cast_node = ps_node_new_integer_cast_result_ex(
      ps_node_new_num(1), typed_atomic_cast_type, 4, 0, 0, 0, 0);
  ASSERT_TRUE(ps_node_get_type(typed_atomic_cast_node) == typed_atomic_cast_type);
  ASSERT_EQ(1, ps_node_get_type(typed_atomic_cast_node)->is_atomic);

  psx_type_t *typed_fp_to_unsigned_type = ps_type_new_integer(TK_UNSIGNED, 4, 1);
  node_t *typed_fp_to_unsigned = ps_node_new_fp_to_int_cast(
      ps_node_new_num(1), 4, typed_fp_to_unsigned_type);
  ASSERT_TRUE(ps_node_get_type(typed_fp_to_unsigned) == typed_fp_to_unsigned_type);
  ASSERT_TRUE(ps_node_is_unsigned_type(typed_fp_to_unsigned));
  ASSERT_EQ(4, ps_node_storage_type_size(typed_fp_to_unsigned));

  psx_type_t *typed_fp_to_bool_type = ps_type_new(PSX_TYPE_BOOL);
  typed_fp_to_bool_type->size = 1;
  node_t *typed_fp_to_bool = ps_node_new_fp_to_int_cast(
      ps_node_new_num(1), 4, typed_fp_to_bool_type);
  ASSERT_TRUE(ps_node_get_type(typed_fp_to_bool) == typed_fp_to_bool_type);
  ASSERT_EQ(PSX_TYPE_BOOL, ps_node_get_type(typed_fp_to_bool)->kind);

  psx_type_t *typed_fp_to_atomic_type = ps_type_new_integer(TK_INT, 4, 0);
  typed_fp_to_atomic_type->is_atomic = 1;
  node_t *typed_fp_to_atomic = ps_node_new_fp_to_int_cast(
      ps_node_new_num(1), 4, typed_fp_to_atomic_type);
  ASSERT_TRUE(ps_node_get_type(typed_fp_to_atomic) == typed_fp_to_atomic_type);
  ASSERT_EQ(1, ps_node_get_type(typed_fp_to_atomic)->is_atomic);

  parsed_code = parse_program_input(
      "unsigned __tm_fp_to_unsigned_expr(double d){ return (unsigned)d; }");
  fn = as_func(parsed_code[0]);
  body = as_block(fn->base.rhs);
  node_t *fp_to_unsigned_ret = body->body[0];
  ASSERT_EQ(ND_RETURN, fp_to_unsigned_ret->kind);
  node_t *fp_to_unsigned_result = fp_to_unsigned_ret->lhs;
  ASSERT_TRUE(ps_node_is_unsigned_type(fp_to_unsigned_result));
  node_t *fp_to_unsigned_inner =
      fp_to_unsigned_result->kind == ND_CAST
          ? fp_to_unsigned_result->lhs
          : fp_to_unsigned_result;
  ASSERT_EQ(ND_FP_TO_INT, fp_to_unsigned_inner->kind);
  ASSERT_TRUE(ps_node_get_type(fp_to_unsigned_inner) != NULL);
  ASSERT_TRUE(ps_type_is_unsigned(ps_node_get_type(fp_to_unsigned_inner)));
  ASSERT_TRUE(ps_node_conversion_value_is_unsigned(fp_to_unsigned_inner));

  node_t typed_cast_ptr = {0};
  typed_cast_ptr.kind = ND_CAST;
  typed_cast_ptr.type = ps_type_new_pointer(
      ps_type_new_integer(TK_INT, 4, 0), 8);
  ASSERT_TRUE(ps_node_cast_i64_extension_info(
      &typed_cast_ptr, &cast_target_size, &cast_widen_zext,
      &cast_needs_i64));
  ASSERT_EQ(8, cast_target_size);
  ASSERT_EQ(0, cast_widen_zext);
  ASSERT_EQ(0, cast_needs_i64);

  node_t typed_incomplete_cast = {0};
  typed_incomplete_cast.kind = ND_CAST;
  typed_incomplete_cast.type =
      ps_type_new_tag(TK_STRUCT, "Incomplete", 10, 0, 0);
  ASSERT_TRUE(ps_node_cast_i64_extension_info(
      &typed_incomplete_cast, &cast_target_size, &cast_widen_zext,
      &cast_needs_i64));
  ASSERT_EQ(0, cast_target_size);
  ASSERT_EQ(0, cast_widen_zext);
  ASSERT_EQ(0, cast_needs_i64);

  psx_type_t *typed_signed_ptr_cast_type = ps_type_new_pointer(
      ps_type_new_integer(TK_INT, 4, 0), 4);
  node_t *typed_signed_ptr_cast = ps_node_new_pointer_cast_result(
      ps_node_new_num(0), typed_signed_ptr_cast_type,
      TK_UNSIGNED, TK_EOF, NULL, 0, 4, 1);
  ASSERT_TRUE(ps_node_get_type(typed_signed_ptr_cast) == typed_signed_ptr_cast_type);
  ASSERT_TRUE(!ps_node_pointee_is_unsigned(typed_signed_ptr_cast));

  psx_type_t *typed_flat_ptr_stale_cast_type = ps_type_new_pointer(NULL, 8);
  typed_flat_ptr_stale_cast_type->base_deref_size = 4;
  typed_flat_ptr_stale_cast_type->outer_stride = 32;
  typed_flat_ptr_stale_cast_type->mid_stride = 16;
  typed_flat_ptr_stale_cast_type->ptr_array_pointee_bytes = 32;
  node_t *typed_flat_ptr_stale_cast = ps_node_new_pointer_cast_result(
      ps_node_new_num(0), typed_flat_ptr_stale_cast_type,
      TK_INT, TK_EOF, NULL, 0, 4, 0);
  ASSERT_TRUE(ps_node_get_type(typed_flat_ptr_stale_cast) ==
              typed_flat_ptr_stale_cast_type);
  ASSERT_TRUE(ps_node_is_pointer(typed_flat_ptr_stale_cast));
  ASSERT_EQ(0, ps_node_ptr_array_pointee_bytes(typed_flat_ptr_stale_cast));
  ASSERT_TRUE(!ps_node_pointer_stride_metadata(
      typed_flat_ptr_stale_cast, NULL, NULL, NULL, NULL));

  psx_type_t *typed_bool_ptr_cast_type =
      ps_type_new_pointer(ps_type_new(PSX_TYPE_BOOL), 1);
  node_t *typed_bool_ptr_cast = ps_node_new_pointer_cast_result(
      ps_node_new_num(0), typed_bool_ptr_cast_type,
      TK_INT, TK_EOF, NULL, 0, 4, 0);
  ASSERT_TRUE(ps_node_get_type(typed_bool_ptr_cast) == typed_bool_ptr_cast_type);
  ASSERT_TRUE(ps_node_pointee_is_bool(typed_bool_ptr_cast));

  psx_type_t *typed_tag_ptr_cast_type = ps_type_new_pointer(
      ps_type_new_tag(TK_STRUCT, "PCast", 5, 0, 4), 4);
  node_t *typed_tag_ptr_cast = ps_node_new_pointer_cast_result(
      ps_node_new_num(0), typed_tag_ptr_cast_type,
      TK_INT, TK_EOF, NULL, 0, 4, 0);
  token_kind_t ptr_cast_tag_kind = TK_EOF;
  char *ptr_cast_tag_name = NULL;
  int ptr_cast_tag_len = 0;
  int ptr_cast_is_tag_pointer = 0;
  ps_node_get_tag_type(typed_tag_ptr_cast, &ptr_cast_tag_kind,
                        &ptr_cast_tag_name, &ptr_cast_tag_len,
                        &ptr_cast_is_tag_pointer);
  ASSERT_EQ(TK_STRUCT, ptr_cast_tag_kind);
  ASSERT_EQ(5, ptr_cast_tag_len);
  ASSERT_TRUE(strncmp(ptr_cast_tag_name, "PCast", 5) == 0);
  ASSERT_EQ(1, ptr_cast_is_tag_pointer);

  node_t *plain_widen_cast = ps_node_new_integer_cast_result_ex(
      ps_node_new_num(1), NULL, 8, 1, 0, 0, 1);
  ASSERT_TRUE(ps_node_cast_i64_extension_info(
      plain_widen_cast, &cast_target_size, &cast_widen_zext,
      &cast_needs_i64));
  ASSERT_EQ(8, cast_target_size);
  ASSERT_EQ(1, cast_widen_zext);
  ASSERT_EQ(1, cast_needs_i64);

  parsed_code = parse_program_input("main() { struct S { int x; } *p; p=0; return p==0; }");
  fn = as_func(parsed_code[0]);
  body = as_block(fn->base.rhs);
  assign = body->body[1];
  ASSERT_EQ(ND_ASSIGN, assign->kind);
  psx_type_t *ptr_ty = ps_node_get_type(assign->lhs);
  ASSERT_TRUE(ptr_ty != NULL);
  ASSERT_TRUE(assign->lhs->type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ptr_ty->kind);
  ASSERT_TRUE(ps_type_is_pointer(ptr_ty));
  ASSERT_TRUE(ptr_ty->base != NULL);
  ASSERT_EQ(TK_STRUCT, ptr_ty->base->tag_kind);
  ASSERT_EQ(1, ptr_ty->base->tag_len);
  ASSERT_TRUE(strncmp(ptr_ty->base->tag_name, "S", 1) == 0);
  lvar_t *p_lvar = find_func_lvar(fn, "p");
  ASSERT_TRUE(p_lvar != NULL);
  ASSERT_TRUE(ps_node_lvar_symbol(assign->lhs) == p_lvar);
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
  psx_type_t *array_ty = ps_node_get_type(member);
  ASSERT_TRUE(array_ty != NULL);
  ASSERT_TRUE(member->type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, array_ty->kind);
  ASSERT_EQ(16, ps_type_sizeof(array_ty));
  ASSERT_TRUE(ps_type_is_pointer(array_ty));
  lvar_t *r1_lvar = find_func_lvar(fn, "r1");
  ASSERT_TRUE(r1_lvar != NULL);
  ASSERT_TRUE(r1_lvar->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_STRUCT, r1_lvar->decl_type->kind);
  ASSERT_EQ(16, ps_type_sizeof(r1_lvar->decl_type));
  ASSERT_TRUE(ps_lvar_is_tag_aggregate(r1_lvar));
  ASSERT_TRUE(ps_lvar_is_struct_aggregate(r1_lvar));
  ASSERT_TRUE(!ps_lvar_is_union_aggregate(r1_lvar));

  lvar_t tmp_tag_lvar = {0};
  tmp_tag_lvar.size = 4;
  tmp_tag_lvar.elem_size = 4;
  psx_decl_set_lvar_decl_type(
      &tmp_tag_lvar, ps_type_new_tag(TK_UNION, NULL, 0, 0, 4));
  ASSERT_TRUE(ps_lvar_is_tag_aggregate(&tmp_tag_lvar));
  ASSERT_TRUE(!ps_lvar_is_struct_aggregate(&tmp_tag_lvar));
  ASSERT_TRUE(ps_lvar_is_union_aggregate(&tmp_tag_lvar));
  psx_decl_set_lvar_decl_type(
      &tmp_tag_lvar,
      ps_type_new_pointer(
          ps_type_new_tag(TK_UNION, NULL, 0, 0, 4), 4));
  ASSERT_TRUE(!ps_lvar_is_tag_aggregate(&tmp_tag_lvar));
  ASSERT_TRUE(!ps_lvar_is_union_aggregate(&tmp_tag_lvar));
  ASSERT_TRUE(ps_ctx_is_tag_aggregate_kind(TK_STRUCT));
  ASSERT_TRUE(ps_ctx_is_tag_aggregate_kind(TK_UNION));
  ASSERT_TRUE(!ps_ctx_is_tag_aggregate_kind(TK_ENUM));
  ASSERT_EQ(PSX_TYPE_STRUCT, ps_type_kind_from_tag_kind(TK_STRUCT));
  ASSERT_EQ(PSX_TYPE_UNION, ps_type_kind_from_tag_kind(TK_UNION));
  ASSERT_EQ(PSX_TYPE_INVALID, ps_type_kind_from_tag_kind(TK_ENUM));
  psx_type_t *tmp_struct_type = ps_type_new_tag(TK_STRUCT, "TS", 2, 1, 4);
  ASSERT_TRUE(ps_type_is_tag_aggregate(tmp_struct_type));
  ASSERT_EQ(PSX_TYPE_STRUCT, tmp_struct_type->kind);
  psx_type_t *tmp_union_type = ps_type_new_tag(TK_UNION, "TU", 2, 1, 4);
  ASSERT_TRUE(ps_type_is_tag_aggregate(tmp_union_type));
  ASSERT_EQ(PSX_TYPE_UNION, tmp_union_type->kind);
  psx_type_t *tmp_invalid_tag_type = ps_type_new_tag(TK_ENUM, "TE", 2, 1, 4);
  ASSERT_TRUE(!ps_type_is_tag_aggregate(tmp_invalid_tag_type));
  ASSERT_EQ(PSX_TYPE_INVALID, tmp_invalid_tag_type->kind);
  parsed_code = parse_program_input(
      "struct FlatIn { int a; int b; };"
      "struct FlatOut { int x; struct FlatIn in; union { int u; int v; }; int y; };"
      "union FlatU { int i; struct FlatIn in; };"
      "union FlatFpU { int i; float f; double d; };"
      "main(){ return 0; }");
  (void)parsed_code;
  ASSERT_EQ(2, ps_tag_flat_slot_count(TK_STRUCT, "FlatIn", 6));
  ASSERT_EQ(5, ps_tag_flat_slot_count(TK_STRUCT, "FlatOut", 7));
  ASSERT_EQ(2, ps_tag_flat_slot_count(TK_UNION, "FlatU", 5));
  tag_member_info_t flat_member = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info(TK_STRUCT, "FlatOut", 7, "in", 2, &flat_member));
  ASSERT_EQ(2, ps_tag_member_flat_slots(&flat_member));
  ASSERT_EQ(2, ps_tag_member_elem_flat_slots(&flat_member));
  int named_ordinal = -1;
  tag_member_info_t named_member = {0};
  ASSERT_TRUE(ps_tag_find_named_member(TK_STRUCT, "FlatOut", 7, "y", 1,
                                        &named_member, &named_ordinal));
  ASSERT_EQ(5, named_ordinal);
  ASSERT_TRUE(named_member.name != NULL);
  ASSERT_EQ(0, strncmp(named_member.name, "y", (size_t)named_member.len));
  named_ordinal = -1;
  ASSERT_TRUE(!ps_tag_find_named_member(TK_STRUCT, "FlatOut", 7, "missing", 7,
                                         &named_member, &named_ordinal));
  ASSERT_EQ(-1, named_ordinal);
  global_var_t tmp_no_init = {0};
  psx_gvar_initializer_class_t no_init_cls =
      ps_gvar_initializer_class(&tmp_no_init, 0);
  ASSERT_TRUE(!no_init_cls.has_explicit_initializer);
  ASSERT_TRUE(!no_init_cls.has_payload);
  global_var_t tmp_zero_init = {0};
  tmp_zero_init.has_init = 1;
  psx_gvar_initializer_class_t zero_init_cls =
      ps_gvar_initializer_class(&tmp_zero_init, 0);
  ASSERT_TRUE(zero_init_cls.has_explicit_initializer);
  ASSERT_TRUE(zero_init_cls.has_payload);
  global_var_t tmp_empty_aggregate_init = {0};
  tmp_empty_aggregate_init.has_init = 1;
  tmp_empty_aggregate_init.decl_type =
      ps_type_new_tag(TK_STRUCT, NULL, 0, 0, 0);
  psx_gvar_initializer_class_t empty_aggregate_cls =
      ps_gvar_initializer_class(&tmp_empty_aggregate_init, 1);
  ASSERT_TRUE(empty_aggregate_cls.has_explicit_initializer);
  ASSERT_TRUE(!empty_aggregate_cls.has_payload);
  ASSERT_EQ(PSX_GVAR_INIT_KIND_AGGREGATE, empty_aggregate_cls.kind);
  char *init_syms[1] = {NULL};
  int init_sym_lens[1] = {-2};
  global_var_t tmp_union_init = {0};
  tmp_union_init.init_count = 1;
  tmp_union_init.init_value_symbols = init_syms;
  tmp_union_init.init_value_symbol_lens = init_sym_lens;
  psx_gvar_init_slot_t sentinel_slot = ps_gvar_init_slot_view(&tmp_union_init, 0);
  ASSERT_TRUE(sentinel_slot.in_range);
  ASSERT_TRUE(sentinel_slot.symbol == NULL);
  ASSERT_EQ(-2, sentinel_slot.symbol_len);
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT, sentinel_slot.fp_sentinel_kind);
  ASSERT_EQ(0, ps_gvar_union_init_slot_ordinal(&tmp_union_init, 0));
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT, ps_gvar_init_slot_fp_kind(&tmp_union_init, 0));
  ASSERT_TRUE(!ps_gvar_init_slot_is_plain_zero(&tmp_union_init, 0));
  ASSERT_EQ(4, ps_gvar_union_init_slot_fp_size(&tmp_union_init, 0));
  tag_member_info_t selected_union_member = {0};
  ASSERT_TRUE(ps_ctx_get_tag_member_info(TK_UNION, "FlatFpU", 7, 0,
                                          &selected_union_member));
  ASSERT_EQ(TK_FLOAT_KIND_NONE, selected_union_member.fp_kind);
  ASSERT_TRUE(ps_tag_select_union_member_for_init_slot(TK_UNION, "FlatFpU", 7,
                                                        &tmp_union_init, 0,
                                                        &selected_union_member));
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT, selected_union_member.fp_kind);
  selected_union_member = (tag_member_info_t){0};
  selected_union_member.fp_kind = TK_FLOAT_KIND_FLOAT;
  selected_union_member.decl_type = ps_type_new_integer(TK_INT, 4, 0);
  ASSERT_TRUE(ps_tag_select_union_member_for_init_slot(TK_UNION, "FlatFpU", 7,
                                                        &tmp_union_init, 0,
                                                        &selected_union_member));
  ASSERT_TRUE(selected_union_member.name != NULL);
  ASSERT_EQ(0, strncmp(selected_union_member.name, "f",
                       (size_t)selected_union_member.len));
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT, selected_union_member.fp_kind);
  global_var_t tmp_member_value_gv = {0};
  ps_gvar_init_slots_alloc(&tmp_member_value_gv, 1, 1);
  tmp_member_value_gv.init_count = 1;
  ps_gvar_init_slot_write(&tmp_member_value_gv, 0, 42, 3.5, NULL, 0);
  tag_member_info_t tmp_member_value_double = {0};
  tmp_member_value_double.type_size = 4;
  tmp_member_value_double.fp_kind = TK_FLOAT_KIND_NONE;
  tmp_member_value_double.decl_type =
      ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8);
  psx_gvar_init_member_value_t tmp_member_value =
      ps_gvar_init_member_value(&tmp_member_value_gv, 0,
                                 &tmp_member_value_double);
  ASSERT_EQ(PSX_GVAR_INIT_VALUE_FLOAT, tmp_member_value.kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, tmp_member_value.fp_kind);
  ASSERT_EQ(8, tmp_member_value.size);
  global_var_t tmp_member_bool_gv = {0};
  ps_gvar_init_slots_alloc(&tmp_member_bool_gv, 1, 0);
  tmp_member_bool_gv.init_count = 1;
  ps_gvar_init_slot_write(&tmp_member_bool_gv, 0, 7, 0.0, NULL, 0);
  tag_member_info_t tmp_member_value_bool = {0};
  tmp_member_value_bool.type_size = 4;
  tmp_member_value_bool.is_bool = 0;
  tmp_member_value_bool.decl_type = ps_type_new_integer(TK_BOOL, 1, 1);
  tmp_member_value = ps_gvar_init_member_value(&tmp_member_bool_gv, 0,
                                                &tmp_member_value_bool);
  ASSERT_EQ(PSX_GVAR_INIT_VALUE_INTEGER, tmp_member_value.kind);
  ASSERT_EQ(1, tmp_member_value.value);
  ASSERT_EQ(1, tmp_member_value.size);
  selected_union_member = (tag_member_info_t){0};
  ASSERT_TRUE(ps_tag_union_init_member_for_slot(TK_UNION, "FlatFpU", 7,
                                                 &tmp_union_init, 0,
                                                 &selected_union_member));
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT, selected_union_member.fp_kind);
  int flatu_in_ordinal = -1;
  tag_member_info_t flatu_in_member = {0};
  ASSERT_TRUE(ps_tag_find_named_member(TK_UNION, "FlatU", 5, "in", 2,
                                        &flatu_in_member, &flatu_in_ordinal));
  int init_ordinals[1] = {flatu_in_ordinal};
  global_var_t tmp_union_ord = {0};
  tmp_union_ord.init_count = 1;
  tmp_union_ord.init_union_ordinals = init_ordinals;
  psx_gvar_init_slot_t zero_slot = ps_gvar_init_slot_view(&tmp_union_ord, 0);
  ASSERT_TRUE(zero_slot.in_range);
  ASSERT_EQ(0, zero_slot.symbol_len);
  ASSERT_EQ(0, zero_slot.value);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, zero_slot.fp_sentinel_kind);
  ASSERT_EQ(flatu_in_ordinal, ps_gvar_union_init_slot_ordinal(&tmp_union_ord, 0));
  ASSERT_EQ(TK_FLOAT_KIND_NONE, ps_gvar_init_slot_fp_kind(&tmp_union_ord, 0));
  ASSERT_TRUE(ps_gvar_init_slot_is_plain_zero(&tmp_union_ord, 0));
  tag_member_info_t overridden_union_member = {0};
  ASSERT_TRUE(ps_tag_union_init_member_for_slot(TK_UNION, "FlatU", 5,
                                                 &tmp_union_ord, 0,
                                                 &overridden_union_member));
  ASSERT_TRUE(overridden_union_member.name != NULL);
  ASSERT_EQ(0, strncmp(overridden_union_member.name, "in",
                       (size_t)overridden_union_member.len));
  int first_named_ordinal = -1;
  tag_member_info_t first_named_member = {0};
  ASSERT_TRUE(ps_tag_first_named_member(TK_STRUCT, "FlatOut", 7,
                                         &first_named_member, &first_named_ordinal));
  ASSERT_EQ(0, first_named_ordinal);
  ASSERT_TRUE(first_named_member.name != NULL);
  ASSERT_EQ(0, strncmp(first_named_member.name, "x", (size_t)first_named_member.len));
  first_named_ordinal = -1;
  ASSERT_TRUE(ps_tag_first_named_member(TK_UNION, "FlatU", 5,
                                         &first_named_member, &first_named_ordinal));
  ASSERT_EQ(0, first_named_ordinal);
  ASSERT_TRUE(first_named_member.name != NULL);
  ASSERT_EQ(0, strncmp(first_named_member.name, "i", (size_t)first_named_member.len));
  int next_named_ordinal = 0;
  tag_member_info_t next_named_member = {0};
  ASSERT_TRUE(ps_tag_next_named_member(TK_STRUCT, "FlatOut", 7,
                                        &next_named_ordinal, &next_named_member));
  ASSERT_EQ(1, next_named_ordinal);
  ASSERT_TRUE(next_named_member.name != NULL);
  ASSERT_EQ(0, strncmp(next_named_member.name, "x", (size_t)next_named_member.len));
  ASSERT_TRUE(ps_tag_next_named_member(TK_STRUCT, "FlatOut", 7,
                                        &next_named_ordinal, &next_named_member));
  ASSERT_EQ(2, next_named_ordinal);
  ASSERT_TRUE(next_named_member.name != NULL);
  ASSERT_EQ(0, strncmp(next_named_member.name, "in", (size_t)next_named_member.len));
  int flat_slot_ordinal = -1;
  tag_member_info_t flat_slot_member = {0};
  ASSERT_TRUE(ps_tag_member_at_flat_slot(TK_STRUCT, "FlatOut", 7, 0,
                                          &flat_slot_member, &flat_slot_ordinal));
  ASSERT_EQ(0, flat_slot_ordinal);
  ASSERT_TRUE(flat_slot_member.name != NULL);
  ASSERT_EQ(0, strncmp(flat_slot_member.name, "x", (size_t)flat_slot_member.len));
  ASSERT_TRUE(ps_tag_member_at_flat_slot(TK_STRUCT, "FlatOut", 7, 1,
                                          &flat_slot_member, &flat_slot_ordinal));
  ASSERT_EQ(1, flat_slot_ordinal);
  ASSERT_TRUE(flat_slot_member.name != NULL);
  ASSERT_EQ(0, strncmp(flat_slot_member.name, "in", (size_t)flat_slot_member.len));
  ASSERT_TRUE(ps_tag_member_at_flat_slot(TK_STRUCT, "FlatOut", 7, 2,
                                          &flat_slot_member, &flat_slot_ordinal));
  ASSERT_EQ(1, flat_slot_ordinal);
  ASSERT_TRUE(ps_tag_member_at_flat_slot(TK_STRUCT, "FlatOut", 7, 3,
                                          &flat_slot_member, &flat_slot_ordinal));
  ASSERT_EQ(TK_UNION, flat_slot_member.tag_kind);
  ASSERT_TRUE(ps_tag_member_at_flat_slot(TK_STRUCT, "FlatOut", 7, 4,
                                          &flat_slot_member, &flat_slot_ordinal));
  ASSERT_EQ(5, flat_slot_ordinal);
  ASSERT_TRUE(flat_slot_member.name != NULL);
  ASSERT_EQ(0, strncmp(flat_slot_member.name, "y", (size_t)flat_slot_member.len));
  ASSERT_TRUE(!ps_tag_member_at_flat_slot(TK_STRUCT, "FlatOut", 7, 5,
                                           &flat_slot_member, &flat_slot_ordinal));
  ASSERT_TRUE(ps_tag_member_at_flat_slot(TK_STRUCT, "FlatOut", 7, 3,
                                          &flat_slot_member, &flat_slot_ordinal));
  psx_tag_flat_cover_state_t flat_cover;
  ps_tag_flat_cover_state_init(&flat_cover);
  ASSERT_TRUE(!ps_tag_flat_cover_state_covers(&flat_cover, &flat_slot_member));
  ps_tag_flat_cover_state_note(&flat_cover, TK_STRUCT, "FlatOut", 7, &flat_slot_member);
  tag_member_info_t flat_promoted_union_member = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info(TK_STRUCT, "FlatOut", 7, "u", 1,
                                           &flat_promoted_union_member));
  ASSERT_TRUE(ps_tag_flat_cover_state_covers(&flat_cover, &flat_promoted_union_member));
  ASSERT_TRUE(ps_ctx_find_tag_member_info(TK_STRUCT, "FlatOut", 7, "y", 1,
                                           &flat_slot_member));
  ASSERT_TRUE(!ps_tag_flat_cover_state_covers(&flat_cover, &flat_slot_member));
  int flat_ordinal = -1;
  ASSERT_EQ(4, ps_tag_member_designator_slot(TK_STRUCT, "FlatOut", 7, "y", 1,
                                              &flat_ordinal));
  ASSERT_EQ(5, flat_ordinal);
  flat_ordinal = -1;
  ASSERT_EQ(0, ps_tag_member_designator_slot(TK_UNION, "FlatU", 5, "in", 2,
                                              &flat_ordinal));
  ASSERT_EQ(1, flat_ordinal);

  parsed_code = parse_program_input("union LongMemberU { int a; long b; }; "
                                    "static union LongMemberU __tm_lu = {.b = 0x1122334455L}; "
                                    "main(){ return 0; }");
  (void)parsed_code;
  global_var_t *__tm_lu = ps_find_global_var("__tm_lu", 7);
  ASSERT_TRUE(__tm_lu != NULL);
  ASSERT_TRUE(ps_gvar_is_union_aggregate(__tm_lu));
  psx_gvar_initializer_class_t long_union_cls =
      ps_gvar_initializer_class(__tm_lu, 0);
  ASSERT_EQ(PSX_GVAR_INIT_KIND_AGGREGATE, long_union_cls.kind);
  ASSERT_TRUE(long_union_cls.has_aggregate_initializer);
  tag_member_info_t long_union_member = {0};
  ASSERT_TRUE(ps_tag_union_init_member_for_slot(TK_UNION, "LongMemberU", 11,
                                                 __tm_lu, 0, &long_union_member));
  ASSERT_TRUE(long_union_member.name != NULL);
  ASSERT_EQ(0, strncmp(long_union_member.name, "b",
                       (size_t)long_union_member.len));
  ASSERT_EQ(8, long_union_member.type_size);

  parsed_code = parse_program_input("struct P { int x, y; }; "
                                    "union U { int a; long b; }; "
                                    "enum E { E0, E1, E9 = 9 }; "
                                    "static struct P sp = {3, 4}; "
                                    "static union U su = {.b = 0x1122334455L}; "
                                    "static enum E se = E9; "
                                    "static struct P *get_p(void) { return &sp; } "
                                    "static union U *get_u(void) { return &su; } "
                                    "static enum E *get_e(void) { return &se; } "
                                    "static struct P *get_p_arg(int d) { sp.x += d; return &sp; } "
                                    "static struct P make_p(int a, int b) { struct P p = {a, b}; return p; } "
                                    "static struct P arr[3] = {{1, 2}, {3, 4}, {5, 6}}; "
                                    "static struct P *get_arr(void) { return arr; } "
                                    "main(){ return get_u()->b == 0x1122334455L ? 0 : 1; }");
  (void)parsed_code;
  global_var_t *short_union_su = ps_find_global_var("su", 2);
  ASSERT_TRUE(short_union_su != NULL);
  ASSERT_TRUE(ps_gvar_is_union_aggregate(short_union_su));
  psx_gvar_initializer_class_t short_union_cls =
      ps_gvar_initializer_class(short_union_su, 0);
  ASSERT_EQ(PSX_GVAR_INIT_KIND_AGGREGATE, short_union_cls.kind);
  tag_member_info_t short_union_member = {0};
  ASSERT_TRUE(ps_tag_union_init_member_for_slot(TK_UNION, "U", 1,
                                                 short_union_su, 0,
                                                 &short_union_member));
  ASSERT_TRUE(short_union_member.name != NULL);
  ASSERT_EQ(0, strncmp(short_union_member.name, "b",
                       (size_t)short_union_member.len));
  ASSERT_EQ(8, short_union_member.type_size);

  parsed_code = parse_program_input("unsigned int __tm_gu; int *__tm_gp; int __tm_ga[3]; main(){ return 0; }");
  (void)parsed_code;
  global_var_t *gu = ps_find_global_var("__tm_gu", 7);
  ASSERT_TRUE(gu != NULL);
  ASSERT_TRUE(gu->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, gu->decl_type->kind);
  ASSERT_EQ(4, ps_type_sizeof(gu->decl_type));
  ASSERT_TRUE(ps_type_is_unsigned(gu->decl_type));
  psx_type_t *gu_decl_a = ps_gvar_get_decl_type(gu);
  ASSERT_TRUE(gu_decl_a != NULL);
  gu->decl_type = NULL;
  ASSERT_TRUE(ps_gvar_get_decl_type(gu) == NULL);
  gu->decl_type = gu_decl_a;

  global_var_t tmp_gv = {0};
  tmp_gv.type_size = 4;
  ps_decl_set_gvar_decl_type(
      &tmp_gv, ps_type_new_integer(TK_INT, 4, 0));
  psx_type_t *tmp_gv_int = ps_gvar_get_decl_type(&tmp_gv);
  ASSERT_TRUE(tmp_gv_int != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, tmp_gv_int->kind);
  ASSERT_EQ(4, ps_gvar_storage_size(&tmp_gv, 99));
  ASSERT_TRUE(!ps_gvar_is_array(&tmp_gv));
  ASSERT_TRUE(!ps_gvar_is_tag_aggregate(&tmp_gv));
  ASSERT_TRUE(!ps_gvar_is_struct_aggregate(&tmp_gv));
  ASSERT_TRUE(!ps_gvar_is_union_aggregate(&tmp_gv));
  tmp_gv.type_size = 8;
  ps_decl_set_gvar_decl_type(
      &tmp_gv,
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 4));
  psx_type_t *tmp_gv_ptr = tmp_gv.decl_type;
  ASSERT_TRUE(tmp_gv_ptr != NULL);
  ASSERT_TRUE(tmp_gv_ptr != tmp_gv_int);
  ASSERT_TRUE(tmp_gv.decl_type == tmp_gv_ptr);
  ASSERT_EQ(PSX_TYPE_POINTER, tmp_gv_ptr->kind);
  ps_decl_set_gvar_decl_type(
      &tmp_gv,
      ps_type_new_pointer(
          ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8), 8));
  psx_type_t *tmp_gv_double_ptr = tmp_gv.decl_type;
  ASSERT_TRUE(tmp_gv_double_ptr != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, tmp_gv_double_ptr->kind);
  ASSERT_TRUE(tmp_gv_double_ptr->base != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, tmp_gv_double_ptr->base->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, tmp_gv_double_ptr->base->fp_kind);

  global_var_t tmp_arr_gv = {0};
  tmp_arr_gv.type_size = 0;
  ps_decl_set_gvar_decl_type(
      &tmp_arr_gv,
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0),
                         0, 0, 4, 0));
  ASSERT_EQ(99, ps_gvar_storage_size(&tmp_arr_gv, 99));
  psx_type_t *tmp_arr_empty = ps_gvar_get_decl_type(&tmp_arr_gv);
  ASSERT_TRUE(tmp_arr_empty != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, tmp_arr_empty->kind);
  ASSERT_TRUE(ps_gvar_is_array(&tmp_arr_gv));
  ps_decl_set_gvar_type_size(&tmp_arr_gv, 12);
  psx_type_t *tmp_arr_sized = tmp_arr_gv.decl_type;
  ASSERT_TRUE(tmp_arr_sized != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, tmp_arr_sized->kind);
  ASSERT_EQ(12, ps_type_sizeof(tmp_arr_sized));
  ASSERT_EQ(4, ps_gvar_array_element_size(&tmp_arr_gv));
  ASSERT_EQ(3, ps_gvar_array_element_count(&tmp_arr_gv));
  ASSERT_EQ(4, ps_gvar_initializer_element_size(&tmp_arr_gv, 12));
  ASSERT_EQ(3, ps_gvar_initializer_element_count(&tmp_arr_gv, 12));
  ASSERT_EQ(12, ps_gvar_initializer_element_size(gu, 12));

  global_var_t tmp_arr_stale_size_gv = {0};
  tmp_arr_stale_size_gv.type_size = 4;
  tmp_arr_stale_size_gv.decl_type =
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 0, 16, 4, 0);
  ASSERT_TRUE(ps_gvar_is_array(&tmp_arr_stale_size_gv));
  ASSERT_EQ(4, ps_gvar_array_element_size(&tmp_arr_stale_size_gv));
  ASSERT_EQ(4, ps_gvar_array_element_count(&tmp_arr_stale_size_gv));
  ASSERT_EQ(4, ps_gvar_initializer_element_size(&tmp_arr_stale_size_gv, 4));
  ASSERT_EQ(4, ps_gvar_initializer_element_count(&tmp_arr_stale_size_gv, 4));

  global_var_t tmp_bool_scalar_decl_type_wins = {0};
  tmp_bool_scalar_decl_type_wins.decl_type =
      ps_type_new_integer(TK_BOOL, 1, 0);
  ASSERT_TRUE(ps_gvar_is_bool_scalar(&tmp_bool_scalar_decl_type_wins));

  global_var_t tmp_int_scalar_decl_type_wins = {0};
  tmp_int_scalar_decl_type_wins.decl_type =
      ps_type_new_integer(TK_INT, 4, 0);
  ASSERT_TRUE(!ps_gvar_is_bool_scalar(&tmp_int_scalar_decl_type_wins));

  global_var_t tmp_bool_array_decl_type_wins = {0};
  tmp_bool_array_decl_type_wins.decl_type =
      ps_type_new_array(ps_type_new_integer(TK_BOOL, 1, 0), 3, 3, 1, 0);
  ASSERT_TRUE(ps_gvar_array_element_is_bool(&tmp_bool_array_decl_type_wins));

  global_var_t tmp_int_array_decl_type_wins = {0};
  tmp_int_array_decl_type_wins.decl_type =
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 3, 12, 4, 0);
  ASSERT_TRUE(!ps_gvar_array_element_is_bool(&tmp_int_array_decl_type_wins));

  global_var_t tmp_gv_scalar_decl_type_wins = {0};
  psx_type_t *tmp_gv_scalar_canonical =
      ps_type_new_integer(TK_INT, 4, 0);
  tmp_gv_scalar_decl_type_wins.name = "__tm_gv_scalar_decl_type_wins";
  tmp_gv_scalar_decl_type_wins.name_len =
      (int)strlen(tmp_gv_scalar_decl_type_wins.name);
  tmp_gv_scalar_decl_type_wins.type_size = 8;
  tmp_gv_scalar_decl_type_wins.decl_type = tmp_gv_scalar_canonical;
  ASSERT_TRUE(ps_gvar_get_decl_type(&tmp_gv_scalar_decl_type_wins) ==
              tmp_gv_scalar_canonical);
  ASSERT_TRUE(!ps_gvar_is_array(&tmp_gv_scalar_decl_type_wins));
  ASSERT_TRUE(!ps_gvar_is_tag_aggregate(&tmp_gv_scalar_decl_type_wins));
  node_t *tmp_gv_scalar_decl_type_ref =
      ps_node_new_gvar_for(&tmp_gv_scalar_decl_type_wins);
  ASSERT_TRUE(ps_node_get_type(tmp_gv_scalar_decl_type_ref) ==
              tmp_gv_scalar_canonical);
  ASSERT_EQ(4, ps_node_type_size(tmp_gv_scalar_decl_type_ref));
  ASSERT_TRUE(!ps_node_is_pointer(tmp_gv_scalar_decl_type_ref));
  ASSERT_EQ(0, ps_node_deref_size(tmp_gv_scalar_decl_type_ref));
  ASSERT_EQ(PSX_TYPE_INTEGER,
            ps_node_get_type(tmp_gv_scalar_decl_type_ref)->kind);
  psx_decl_funcptr_sig_t tmp_gv_scalar_funcptr =
      ps_gvar_funcptr_sig(&tmp_gv_scalar_decl_type_wins);
  ASSERT_TRUE(!ps_decl_funcptr_sig_has_payload(tmp_gv_scalar_funcptr));
  node_t *tmp_gv_scalar_funcptr_ref =
      ps_node_new_gvar_for(&tmp_gv_scalar_decl_type_wins);
  ASSERT_TRUE(!ps_node_has_funcptr_signature(tmp_gv_scalar_funcptr_ref));

  global_var_t tmp_gv_ptr_array_cache_decl_type_wins = {0};
  psx_type_t *tmp_gv_ptr_array_cache_canonical =
      ps_type_new_integer(TK_INT, 4, 0);
  tmp_gv_ptr_array_cache_decl_type_wins.name =
      "__tm_gv_ptr_array_cache_decl_type_wins";
  tmp_gv_ptr_array_cache_decl_type_wins.name_len =
      (int)strlen(tmp_gv_ptr_array_cache_decl_type_wins.name);
  tmp_gv_ptr_array_cache_decl_type_wins.type_size = 8;
  tmp_gv_ptr_array_cache_decl_type_wins.decl_type =
      tmp_gv_ptr_array_cache_canonical;
  ASSERT_TRUE(ps_gvar_get_decl_type(
                  &tmp_gv_ptr_array_cache_decl_type_wins) ==
              tmp_gv_ptr_array_cache_canonical);
  ASSERT_TRUE(!ps_gvar_is_array(&tmp_gv_ptr_array_cache_decl_type_wins));
  node_t *tmp_gv_ptr_array_cache_ref =
      ps_node_new_gvar_for(&tmp_gv_ptr_array_cache_decl_type_wins);
  ASSERT_TRUE(ps_node_get_type(tmp_gv_ptr_array_cache_ref) ==
              tmp_gv_ptr_array_cache_canonical);
  ASSERT_EQ(4, ps_node_type_size(tmp_gv_ptr_array_cache_ref));
  ASSERT_TRUE(!ps_node_is_pointer(tmp_gv_ptr_array_cache_ref));
  ASSERT_EQ(0, ps_node_deref_size(tmp_gv_ptr_array_cache_ref));
  ASSERT_EQ(0, ps_node_ptr_array_pointee_bytes(
                   tmp_gv_ptr_array_cache_ref));

  global_var_t tmp_gv_ptr_decl_type_wins = {0};
  tmp_gv_ptr_decl_type_wins.name = "__tm_gv_ptr_decl_type_wins";
  tmp_gv_ptr_decl_type_wins.name_len =
      (int)strlen(tmp_gv_ptr_decl_type_wins.name);
  tmp_gv_ptr_decl_type_wins.type_size = 8;
  tmp_gv_ptr_decl_type_wins.decl_type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 4);
  tmp_gv_ptr_decl_type_wins.decl_type->outer_stride = 96;
  tmp_gv_ptr_decl_type_wins.decl_type->mid_stride = 48;
  tmp_gv_ptr_decl_type_wins.decl_type->ptr_array_pointee_bytes = 96;
  node_t *tmp_gv_ptr_decl_type_ref =
      ps_node_new_gvar_for(&tmp_gv_ptr_decl_type_wins);
  ASSERT_TRUE(ps_node_get_type(tmp_gv_ptr_decl_type_ref) ==
              tmp_gv_ptr_decl_type_wins.decl_type);
  ASSERT_TRUE(ps_node_is_pointer(tmp_gv_ptr_decl_type_ref));
  ASSERT_EQ(4, ps_node_deref_size(tmp_gv_ptr_decl_type_ref));
  ASSERT_EQ(0, ps_node_ptr_array_pointee_bytes(
                   tmp_gv_ptr_decl_type_ref));
  ASSERT_TRUE(!ps_node_pointer_stride_metadata(
      tmp_gv_ptr_decl_type_ref, NULL, NULL, NULL, NULL));
  node_t *tmp_gv_ptr_decl_type_deref =
      ps_node_new_unary_deref_for(tmp_gv_ptr_decl_type_ref);
  ASSERT_TRUE(ps_node_get_type(tmp_gv_ptr_decl_type_deref) ==
              tmp_gv_ptr_decl_type_wins.decl_type->base);
  ASSERT_TRUE(!ps_node_is_pointer(tmp_gv_ptr_decl_type_deref));
  ASSERT_EQ(4, ps_node_type_size(tmp_gv_ptr_decl_type_deref));
  ASSERT_EQ(0, ps_node_deref_size(tmp_gv_ptr_decl_type_deref));
  ASSERT_EQ(0, ps_node_ptr_array_pointee_bytes(
                   tmp_gv_ptr_decl_type_deref));
  ASSERT_TRUE(!ps_node_pointer_stride_metadata(
      tmp_gv_ptr_decl_type_deref, NULL, NULL, NULL, NULL));

  global_var_t tmp_gv_flat_ptr_mismatch_decl_type = {0};
  tmp_gv_flat_ptr_mismatch_decl_type.name =
      "__tm_gv_flat_ptr_mismatch_decl_type";
  tmp_gv_flat_ptr_mismatch_decl_type.name_len =
      (int)strlen(tmp_gv_flat_ptr_mismatch_decl_type.name);
  tmp_gv_flat_ptr_mismatch_decl_type.type_size = 8;
  tmp_gv_flat_ptr_mismatch_decl_type.decl_type =
      ps_type_new_pointer(NULL, 8);
  tmp_gv_flat_ptr_mismatch_decl_type.decl_type->pointer_const_qual_mask = 1u;
  tmp_gv_flat_ptr_mismatch_decl_type.decl_type->pointer_volatile_qual_mask = 1u;
  tmp_gv_flat_ptr_mismatch_decl_type.decl_type->base_deref_size = 4;
  tmp_gv_flat_ptr_mismatch_decl_type.decl_type->outer_stride = 32;
  tmp_gv_flat_ptr_mismatch_decl_type.decl_type->ptr_array_pointee_bytes = 32;
  node_t *tmp_gv_flat_ptr_mismatch_ref =
      ps_node_new_gvar_for(&tmp_gv_flat_ptr_mismatch_decl_type);
  ASSERT_TRUE(ps_node_get_type(tmp_gv_flat_ptr_mismatch_ref) ==
              tmp_gv_flat_ptr_mismatch_decl_type.decl_type);
  ASSERT_TRUE(ps_node_is_pointer(tmp_gv_flat_ptr_mismatch_ref));
  ASSERT_EQ(8, ps_node_deref_size(tmp_gv_flat_ptr_mismatch_ref));
  ASSERT_EQ(0, ps_node_pointer_qual_levels(tmp_gv_flat_ptr_mismatch_ref));
  ASSERT_EQ(0u, ps_node_pointer_const_qual_mask(tmp_gv_flat_ptr_mismatch_ref));
  ASSERT_EQ(0u, ps_node_pointer_volatile_qual_mask(tmp_gv_flat_ptr_mismatch_ref));
  ASSERT_EQ(0, ps_node_base_deref_size(tmp_gv_flat_ptr_mismatch_ref));
  ASSERT_EQ(0, ps_node_ptr_array_pointee_bytes(
                   tmp_gv_flat_ptr_mismatch_ref));
  ASSERT_TRUE(!ps_node_pointer_stride_metadata(
      tmp_gv_flat_ptr_mismatch_ref, NULL, NULL, NULL, NULL));

  global_var_t tmp_gv_cached_runtime_shape = {0};
  tmp_gv_cached_runtime_shape.name = "__tm_gv_cached_runtime_shape";
  tmp_gv_cached_runtime_shape.name_len =
      (int)strlen(tmp_gv_cached_runtime_shape.name);
  tmp_gv_cached_runtime_shape.decl_type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 4);
  ASSERT_EQ(0, tmp_gv_cached_runtime_shape.decl_type->outer_stride);
  ASSERT_TRUE(ps_gvar_get_decl_type(&tmp_gv_cached_runtime_shape) ==
              tmp_gv_cached_runtime_shape.decl_type);
  ASSERT_EQ(0, tmp_gv_cached_runtime_shape.decl_type->outer_stride);
  ASSERT_EQ(0, tmp_gv_cached_runtime_shape.decl_type->mid_stride);
  ASSERT_EQ(0, tmp_gv_cached_runtime_shape.decl_type->extra_strides_count);

  global_var_t tmp_gv_cached_stale_ptr_array_shape = {0};
  tmp_gv_cached_stale_ptr_array_shape.name =
      "__tm_gv_cached_stale_ptr_array_shape";
  tmp_gv_cached_stale_ptr_array_shape.name_len =
      (int)strlen(tmp_gv_cached_stale_ptr_array_shape.name);
  tmp_gv_cached_stale_ptr_array_shape.decl_type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 4);
  ASSERT_TRUE(ps_gvar_get_decl_type(
                  &tmp_gv_cached_stale_ptr_array_shape) ==
              tmp_gv_cached_stale_ptr_array_shape.decl_type);
  ASSERT_EQ(0, tmp_gv_cached_stale_ptr_array_shape.decl_type->outer_stride);
  ASSERT_EQ(0, tmp_gv_cached_stale_ptr_array_shape.decl_type->mid_stride);
  ASSERT_EQ(0, tmp_gv_cached_stale_ptr_array_shape.decl_type->ptr_array_pointee_bytes);

  global_var_t tmp_gv_cached_stale_array_shape = {0};
  tmp_gv_cached_stale_array_shape.name =
      "__tm_gv_cached_stale_array_shape";
  tmp_gv_cached_stale_array_shape.name_len =
      (int)strlen(tmp_gv_cached_stale_array_shape.name);
  tmp_gv_cached_stale_array_shape.type_size = 77;
  psx_type_t *tmp_gv_stale_array_d2 =
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 4, 16, 4, 0);
  psx_type_t *tmp_gv_stale_array_d1 =
      ps_type_new_array(tmp_gv_stale_array_d2, 3, 48, 16, 0);
  tmp_gv_cached_stale_array_shape.decl_type =
      ps_type_new_array(tmp_gv_stale_array_d1, 2, 96, 48, 0);
  ASSERT_TRUE(ps_gvar_get_decl_type(
                  &tmp_gv_cached_stale_array_shape) ==
              tmp_gv_cached_stale_array_shape.decl_type);
  ASSERT_EQ(0, tmp_gv_cached_stale_array_shape.decl_type->outer_stride);
  ASSERT_EQ(0, tmp_gv_cached_stale_array_shape.decl_type->mid_stride);
  node_t *tmp_gv_stale_array_addr =
      ps_node_new_gvar_array_addr_for(&tmp_gv_cached_stale_array_shape);
  ASSERT_TRUE(ps_node_is_pointer(tmp_gv_stale_array_addr));
  psx_type_t *tmp_gv_stale_array_addr_type =
      ps_node_get_type(tmp_gv_stale_array_addr);
  ASSERT_TRUE(tmp_gv_stale_array_addr_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, tmp_gv_stale_array_addr_type->kind);
  ASSERT_EQ(16, tmp_gv_stale_array_addr_type->outer_stride);
  ASSERT_EQ(4, tmp_gv_stale_array_addr_type->mid_stride);
  ASSERT_EQ(8, ps_node_type_size(tmp_gv_stale_array_addr));
  ASSERT_EQ(48, ps_node_deref_size(tmp_gv_stale_array_addr));
  int tmp_gv_stale_array_inner = 0;
  int tmp_gv_stale_array_next = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(tmp_gv_stale_array_addr,
                                               &tmp_gv_stale_array_inner,
                                               &tmp_gv_stale_array_next,
                                               NULL, NULL));
  ASSERT_EQ(16, tmp_gv_stale_array_inner);
  ASSERT_EQ(4, tmp_gv_stale_array_next);

  global_var_t *gp = ps_find_global_var("__tm_gp", 7);
  ASSERT_TRUE(gp != NULL);
  ASSERT_TRUE(gp->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, gp->decl_type->kind);
  ASSERT_TRUE(ps_type_is_pointer(gp->decl_type));
  global_var_t *ga = ps_find_global_var("__tm_ga", 7);
  ASSERT_TRUE(ga != NULL);
  ASSERT_TRUE(ga->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ga->decl_type->kind);
  ASSERT_EQ(12, ps_type_sizeof(ga->decl_type));
  ASSERT_TRUE(ps_gvar_is_array(ga));
  ASSERT_EQ(4, ps_gvar_array_element_size(ga));
  ASSERT_EQ(3, ps_gvar_array_element_count(ga));

  parsed_code = parse_program_input("static short __tm_sha[2][2] = {{10,20},{30,40}}; "
                                    "int main(void){ return __tm_sha[1][0]; }");
  (void)parsed_code;
  global_var_t *sha = ps_find_global_var("__tm_sha", 8);
  ASSERT_TRUE(sha != NULL);
  ASSERT_TRUE(ps_gvar_is_array(sha));
  ASSERT_EQ(4, ps_gvar_array_element_size(sha));
  ASSERT_EQ(2, ps_gvar_array_element_count(sha));
  ASSERT_EQ(2, ps_gvar_initializer_element_size(sha, 4));
  ASSERT_EQ(4, ps_gvar_initializer_element_count(sha, 4));

  parsed_code = parse_program_input(
      "struct __tm817_S { char tag[3]; int n; }; "
      "struct __tm817_S __tm817_gs[2] = {{{1,2,3},4},{{5,6,7},8}}; "
      "int main(void){ return 0; }");
  (void)parsed_code;
  global_var_t *tm817_gs = ps_find_global_var("__tm817_gs", 10);
  ASSERT_TRUE(tm817_gs != NULL);
  ASSERT_TRUE(ps_gvar_is_tag_aggregate(tm817_gs));
  ASSERT_TRUE(ps_gvar_is_struct_aggregate(tm817_gs));
  ASSERT_TRUE(!ps_gvar_is_union_aggregate(tm817_gs));
  ASSERT_EQ(8, ps_gvar_array_element_size(tm817_gs));
  ASSERT_EQ(2, ps_gvar_array_element_count(tm817_gs));
  global_var_t tmp_tag_arr_gv = {0};
  tmp_tag_arr_gv.type_size = 16;
  const char tmp_tag_arr_name[] = "__tm_tmp_tag_arr";
  tmp_tag_arr_gv.decl_type = ps_type_new_array(
      ps_type_new_tag(TK_STRUCT, (char *)tmp_tag_arr_name,
                       (int)sizeof(tmp_tag_arr_name) - 1, 0, 8),
      2, 16, 8, 0);
  ASSERT_TRUE(ps_gvar_is_tag_aggregate(&tmp_tag_arr_gv));
  ASSERT_TRUE(ps_gvar_is_struct_aggregate(&tmp_tag_arr_gv));
  ASSERT_TRUE(!ps_gvar_is_union_aggregate(&tmp_tag_arr_gv));
  ASSERT_EQ(8, ps_gvar_array_element_size(&tmp_tag_arr_gv));
  ASSERT_EQ(2, ps_gvar_array_element_count(&tmp_tag_arr_gv));
  ps_decl_set_gvar_decl_type(
      &tmp_tag_arr_gv,
      ps_type_new_array(
          ps_type_new_tag(TK_UNION, (char *)tmp_tag_arr_name,
                           (int)sizeof(tmp_tag_arr_name) - 1, 0, 8),
          2, 16, 8, 0));
  ASSERT_TRUE(ps_gvar_is_tag_aggregate(&tmp_tag_arr_gv));
  ASSERT_TRUE(!ps_gvar_is_struct_aggregate(&tmp_tag_arr_gv));
  ASSERT_TRUE(ps_gvar_is_union_aggregate(&tmp_tag_arr_gv));
  ps_decl_set_gvar_decl_type(
      &tmp_tag_arr_gv,
      ps_type_new_pointer(
          ps_type_new_tag(TK_UNION, (char *)tmp_tag_arr_name,
                           (int)sizeof(tmp_tag_arr_name) - 1, 0, 8),
          8));
  ASSERT_TRUE(!ps_gvar_is_tag_aggregate(&tmp_tag_arr_gv));
  ASSERT_TRUE(!ps_gvar_is_union_aggregate(&tmp_tag_arr_gv));

  tag_member_info_t tmp_member = {0};
  tmp_member.tag_kind = TK_STRUCT;
  tmp_member.decl_type = ps_type_new_tag(TK_STRUCT, NULL, 0, 0, 8);
  ASSERT_TRUE(ps_tag_member_is_tag_aggregate(&tmp_member));
  ASSERT_TRUE(ps_tag_member_is_struct_aggregate(&tmp_member));
  ASSERT_TRUE(!ps_tag_member_is_union_aggregate(&tmp_member));
  ASSERT_TRUE(ps_tag_member_is_unnamed_struct(&tmp_member));
  ASSERT_TRUE(!ps_tag_member_is_unnamed_union(&tmp_member));
  ASSERT_TRUE(ps_tag_member_is_unnamed_aggregate(&tmp_member));
  tmp_member.len = 1;
  ASSERT_TRUE(!ps_tag_member_is_unnamed_struct(&tmp_member));
  ASSERT_TRUE(!ps_tag_member_is_unnamed_aggregate(&tmp_member));
  tmp_member.len = 0;
  tmp_member.tag_kind = TK_UNION;
  tmp_member.decl_type = ps_type_new_tag(TK_UNION, NULL, 0, 0, 8);
  ASSERT_TRUE(ps_tag_member_is_union_aggregate(&tmp_member));
  ASSERT_TRUE(ps_tag_member_is_unnamed_union(&tmp_member));
  ASSERT_TRUE(ps_tag_member_is_unnamed_aggregate(&tmp_member));
  tmp_member.is_tag_pointer = 1;
  tmp_member.decl_type = ps_type_new_pointer(tmp_member.decl_type, 8);
  ASSERT_TRUE(!ps_tag_member_is_tag_aggregate(&tmp_member));
  ASSERT_TRUE(!ps_tag_member_is_unnamed_union(&tmp_member));
  ASSERT_TRUE(!ps_tag_member_is_unnamed_aggregate(&tmp_member));
  tag_member_info_t tmp_member_decl_array = {0};
  tmp_member_decl_array.decl_type = ps_type_new_array(
      ps_type_new_tag(TK_STRUCT, "__tm_member_decl_tag", 20, 0, 8),
      2, 16, 8, 0);
  ASSERT_TRUE(ps_tag_member_is_tag_aggregate(&tmp_member_decl_array));
  ASSERT_TRUE(ps_tag_member_is_struct_aggregate(&tmp_member_decl_array));
  ASSERT_TRUE(!ps_tag_member_is_union_aggregate(&tmp_member_decl_array));
  ASSERT_TRUE(ps_tag_member_is_unnamed_struct(&tmp_member_decl_array));
  tag_member_info_t tmp_member_decl_ptr = {0};
  tmp_member_decl_ptr.tag_kind = TK_STRUCT;
  tmp_member_decl_ptr.is_tag_pointer = 0;
  tmp_member_decl_ptr.decl_type = ps_type_new_pointer(
      ps_type_new_tag(TK_STRUCT, "__tm_member_decl_ptr_tag", 24, 0, 8), 8);
  ASSERT_TRUE(!ps_tag_member_is_tag_aggregate(&tmp_member_decl_ptr));
  ASSERT_TRUE(!ps_tag_member_is_unnamed_aggregate(&tmp_member_decl_ptr));
  const char flat_decl_inner_tag[] = "__tm_flat_decl_inner";
  ps_ctx_define_tag_type_with_layout(TK_STRUCT, (char *)flat_decl_inner_tag,
                                      (int)sizeof(flat_decl_inner_tag) - 1,
                                      2, 8, 4);
  tag_member_info_t flat_decl_inner_a = {0};
  flat_decl_inner_a.name = "a";
  flat_decl_inner_a.len = 1;
  flat_decl_inner_a.type_size = 4;
  flat_decl_inner_a.decl_type = ps_type_new_integer(TK_INT, 4, 0);
  ps_ctx_add_tag_member(TK_STRUCT, (char *)flat_decl_inner_tag,
                         (int)sizeof(flat_decl_inner_tag) - 1,
                         &flat_decl_inner_a);
  tag_member_info_t flat_decl_inner_b = {0};
  flat_decl_inner_b.name = "b";
  flat_decl_inner_b.len = 1;
  flat_decl_inner_b.offset = 4;
  flat_decl_inner_b.type_size = 4;
  flat_decl_inner_b.decl_type = ps_type_new_integer(TK_INT, 4, 0);
  ps_ctx_add_tag_member(TK_STRUCT, (char *)flat_decl_inner_tag,
                         (int)sizeof(flat_decl_inner_tag) - 1,
                         &flat_decl_inner_b);
  tag_member_info_t flat_decl_array_member = {0};
  flat_decl_array_member.name = "arr";
  flat_decl_array_member.len = 3;
  flat_decl_array_member.type_size = 4;
  flat_decl_array_member.array_len = 0;
  flat_decl_array_member.tag_kind = TK_EOF;
  flat_decl_array_member.decl_type = ps_type_new_array(
      ps_type_new_tag(TK_STRUCT, (char *)flat_decl_inner_tag,
                       (int)sizeof(flat_decl_inner_tag) - 1, 0, 8),
      3, 24, 8, 0);
  ASSERT_EQ(24, ps_type_sizeof(flat_decl_array_member.decl_type));
  ASSERT_EQ(8, ps_type_sizeof(flat_decl_array_member.decl_type->base));
  ASSERT_EQ(2, ps_tag_flat_slot_count(TK_STRUCT, (char *)flat_decl_inner_tag,
                                       (int)sizeof(flat_decl_inner_tag) - 1));
  ASSERT_EQ(6, ps_tag_member_flat_slots(&flat_decl_array_member));
  ASSERT_EQ(2, ps_tag_member_elem_flat_slots(&flat_decl_array_member));
  const char flat_decl_union_tag[] = "__tm_flat_decl_union";
  ps_ctx_define_tag_type_with_layout(TK_UNION, (char *)flat_decl_union_tag,
                                      (int)sizeof(flat_decl_union_tag) - 1,
                                      2, 24, 8);
  tag_member_info_t flat_decl_union_large = {0};
  flat_decl_union_large.name = "large";
  flat_decl_union_large.len = 5;
  flat_decl_union_large.type_size = 20;
  flat_decl_union_large.decl_type =
      ps_type_new_array(ps_type_new_integer(TK_CHAR, 1, 0),
                         20, 20, 1, 0);
  ps_ctx_add_tag_member(TK_UNION, (char *)flat_decl_union_tag,
                         (int)sizeof(flat_decl_union_tag) - 1,
                         &flat_decl_union_large);
  ps_ctx_add_tag_member(TK_UNION, (char *)flat_decl_union_tag,
                         (int)sizeof(flat_decl_union_tag) - 1,
                         &flat_decl_array_member);
  ASSERT_EQ(6, ps_tag_flat_slot_count(TK_UNION, (char *)flat_decl_union_tag,
                                       (int)sizeof(flat_decl_union_tag) - 1));

  parsed_code = parse_program_input(
      "struct __tm_bf_decl_type { unsigned long wide:40; _Bool flag:1; }; "
      "int main(void){ return 0; }");
  (void)parsed_code;
  tag_member_info_t bf_wide_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info(TK_STRUCT, "__tm_bf_decl_type", 17,
                                           "wide", 4, &bf_wide_info));
  ASSERT_TRUE(bf_wide_info.decl_type != NULL);
  ASSERT_EQ(40, bf_wide_info.bit_width);
  ASSERT_EQ(8, ps_tag_member_decl_value_size(&bf_wide_info));
  ASSERT_EQ(8, ps_tag_member_decl_storage_size(&bf_wide_info));
  tag_member_info_t bf_bool_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info(TK_STRUCT, "__tm_bf_decl_type", 17,
                                           "flag", 4, &bf_bool_info));
  ASSERT_TRUE(bf_bool_info.decl_type != NULL);
  ASSERT_EQ(1, bf_bool_info.bit_width);
  ASSERT_TRUE(ps_tag_member_decl_is_bool(&bf_bool_info));
  ASSERT_EQ(1, ps_tag_member_decl_value_size(&bf_bool_info));

  const char member_sync_tag[] = "__tm_member_sync_clean";
  int member_sync_len = (int)sizeof(member_sync_tag) - 1;
  ps_ctx_define_tag_type_with_layout(TK_STRUCT, (char *)member_sync_tag,
                                      member_sync_len, 1, 4, 4);
  tag_member_info_t member_sync_stale = {0};
  member_sync_stale.name = "x";
  member_sync_stale.len = 1;
  member_sync_stale.type_size = 99;
  member_sync_stale.tag_kind = TK_UNION;
  member_sync_stale.tag_name = "Stale";
  member_sync_stale.tag_len = 5;
  member_sync_stale.is_tag_pointer = 1;
  member_sync_stale.pointer_qual_levels = 2;
  member_sync_stale.fp_kind = TK_FLOAT_KIND_DOUBLE;
  member_sync_stale.is_bool = 1;
  member_sync_stale.is_unsigned = 1;
  member_sync_stale.decl_type = ps_type_new_integer(TK_INT, 4, 0);
  ps_ctx_add_tag_member(TK_STRUCT, (char *)member_sync_tag, member_sync_len,
                         &member_sync_stale);
  tag_member_info_t member_sync_out = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info(TK_STRUCT, (char *)member_sync_tag,
                                           member_sync_len, "x", 1,
                                           &member_sync_out));
  ASSERT_TRUE(member_sync_out.decl_type != NULL);
  ASSERT_EQ(TK_EOF, member_sync_out.tag_kind);
  ASSERT_TRUE(member_sync_out.tag_name == NULL);
  ASSERT_EQ(0, member_sync_out.tag_len);
  ASSERT_EQ(0, member_sync_out.is_tag_pointer);
  ASSERT_EQ(0, member_sync_out.pointer_qual_levels);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, member_sync_out.fp_kind);
  ASSERT_EQ(0, member_sync_out.is_bool);
  ASSERT_EQ(0, member_sync_out.is_unsigned);
  ASSERT_EQ(4, ps_tag_member_decl_value_size(&member_sync_out));

  const char canonical_member_tag[] = "__tm_canonical_member_desc";
  int canonical_member_tag_len = (int)sizeof(canonical_member_tag) - 1;
  ps_ctx_define_tag_type_with_layout(TK_STRUCT, (char *)canonical_member_tag,
                                      canonical_member_tag_len, 1, 8, 8);
  tag_member_info_t canonical_member_desc = {0};
  canonical_member_desc.name = "p";
  canonical_member_desc.len = 1;
  canonical_member_desc.type_size = 8;
  canonical_member_desc.deref_size = 4;
  canonical_member_desc.is_tag_pointer = 1;
  canonical_member_desc.pointer_qual_levels = 1;
  canonical_member_desc.is_unsigned = 1;
  canonical_member_desc.decl_type = ps_type_new_pointer(
      ps_type_new_integer(TK_INT, 4, 1), 4);
  ps_ctx_add_tag_member(TK_STRUCT, (char *)canonical_member_tag,
                         canonical_member_tag_len, &canonical_member_desc);
  tag_member_info_t canonical_member_out = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info(
      TK_STRUCT, (char *)canonical_member_tag, canonical_member_tag_len,
      "p", 1, &canonical_member_out));
  ASSERT_TRUE(canonical_member_out.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, canonical_member_out.decl_type->kind);
  ASSERT_TRUE(canonical_member_out.decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, canonical_member_out.decl_type->base->kind);
  ASSERT_TRUE(canonical_member_out.decl_type->base->is_unsigned);

  const char walk_inner_tag[] = "__tm_walk_inner";
  int walk_inner_len = (int)sizeof(walk_inner_tag) - 1;
  ps_ctx_define_tag_type_with_layout(TK_STRUCT, (char *)walk_inner_tag,
                                      walk_inner_len, 2, 8, 4);
  tag_member_info_t walk_inner_a = {0};
  walk_inner_a.name = "a";
  walk_inner_a.len = 1;
  walk_inner_a.type_size = 4;
  walk_inner_a.decl_type = ps_type_new_integer(TK_INT, 4, 0);
  ps_ctx_add_tag_member(TK_STRUCT, (char *)walk_inner_tag, walk_inner_len,
                         &walk_inner_a);
  tag_member_info_t walk_inner_b = {0};
  walk_inner_b.name = "b";
  walk_inner_b.len = 1;
  walk_inner_b.offset = 4;
  walk_inner_b.type_size = 4;
  walk_inner_b.decl_type = ps_type_new_integer(TK_INT, 4, 0);
  ps_ctx_add_tag_member(TK_STRUCT, (char *)walk_inner_tag, walk_inner_len,
                         &walk_inner_b);

  const char walk_outer_tag[] = "__tm_walk_outer";
  int walk_outer_len = (int)sizeof(walk_outer_tag) - 1;
  ps_ctx_define_tag_type_with_layout(TK_STRUCT, (char *)walk_outer_tag,
                                      walk_outer_len, 2, 20, 4);
  tag_member_info_t walk_outer_arr = {0};
  walk_outer_arr.name = "arr";
  walk_outer_arr.len = 3;
  walk_outer_arr.type_size = 4;
  walk_outer_arr.array_len = 0;
  walk_outer_arr.tag_kind = TK_EOF;
  walk_outer_arr.decl_type = ps_type_new_array(
      ps_type_new_tag(TK_STRUCT, (char *)walk_inner_tag, walk_inner_len, 0, 8),
      2, 16, 8, 0);
  ps_ctx_add_tag_member(TK_STRUCT, (char *)walk_outer_tag, walk_outer_len,
                         &walk_outer_arr);
  tag_member_info_t walk_outer_tail = {0};
  walk_outer_tail.name = "tail";
  walk_outer_tail.len = 4;
  walk_outer_tail.offset = 16;
  walk_outer_tail.type_size = 4;
  walk_outer_tail.decl_type = ps_type_new_integer(TK_INT, 4, 0);
  ps_ctx_add_tag_member(TK_STRUCT, (char *)walk_outer_tag, walk_outer_len,
                         &walk_outer_tail);

  global_var_t walk_gv = {0};
  walk_gv.type_size = 4;
  walk_gv.decl_type =
      ps_type_new_tag(TK_STRUCT, (char *)walk_outer_tag, walk_outer_len, 0, 20);
  ps_gvar_init_slots_alloc(&walk_gv, 5, 0);
  walk_gv.init_count = 5;
  for (int i = 0; i < 5; i++)
    ps_gvar_init_slot_write(&walk_gv, i, i + 1, 0.0, NULL, 0);
  aggregate_walk_trace_t walk_trace = {.gv = &walk_gv};
  const psx_gvar_aggregate_walk_ops_t walk_ops = {
      .scalar = aggregate_walk_trace_scalar,
      .padding = aggregate_walk_trace_padding,
  };
  ASSERT_TRUE(ps_gvar_walk_aggregate_initializer(&walk_gv, 0, &walk_ops,
                                                  &walk_trace));
  ASSERT_EQ(5, walk_trace.scalar_count);
  ASSERT_EQ(0, walk_trace.scalar_offsets[0]);
  ASSERT_EQ(4, walk_trace.scalar_offsets[1]);
  ASSERT_EQ(8, walk_trace.scalar_offsets[2]);
  ASSERT_EQ(12, walk_trace.scalar_offsets[3]);
  ASSERT_EQ(16, walk_trace.scalar_offsets[4]);
  for (int i = 0; i < 5; i++) {
    ASSERT_EQ(i + 1, walk_trace.scalar_values[i]);
    ASSERT_EQ(4, walk_trace.scalar_sizes[i]);
  }
  ASSERT_EQ(0, walk_trace.padding_count);

  const char walk_array_tag[] = "__tm_walk_array_elem";
  int walk_array_len = (int)sizeof(walk_array_tag) - 1;
  ps_ctx_define_tag_type_with_layout(TK_STRUCT, (char *)walk_array_tag,
                                      walk_array_len, 2, 8, 4);
  tag_member_info_t walk_array_a = {0};
  walk_array_a.name = "a";
  walk_array_a.len = 1;
  walk_array_a.type_size = 4;
  walk_array_a.decl_type = ps_type_new_integer(TK_INT, 4, 0);
  ps_ctx_add_tag_member(TK_STRUCT, (char *)walk_array_tag, walk_array_len,
                         &walk_array_a);
  tag_member_info_t walk_array_b = {0};
  walk_array_b.name = "b";
  walk_array_b.len = 1;
  walk_array_b.offset = 4;
  walk_array_b.type_size = 4;
  walk_array_b.decl_type = ps_type_new_integer(TK_INT, 4, 0);
  ps_ctx_add_tag_member(TK_STRUCT, (char *)walk_array_tag, walk_array_len,
                         &walk_array_b);

  global_var_t walk_array_gv = {0};
  walk_array_gv.type_size = 4;
  walk_array_gv.decl_type = ps_type_new_array(
      ps_type_new_tag(TK_STRUCT, (char *)walk_array_tag, walk_array_len, 0, 8),
      2, 16, 8, 0);
  ps_gvar_init_slots_alloc(&walk_array_gv, 4, 0);
  walk_array_gv.init_count = 4;
  for (int i = 0; i < 4; i++)
    ps_gvar_init_slot_write(&walk_array_gv, i, i + 11, 0.0, NULL, 0);
  aggregate_walk_trace_t walk_array_trace = {.gv = &walk_array_gv};
  ASSERT_TRUE(ps_gvar_walk_aggregate_initializer(&walk_array_gv, 0, &walk_ops,
                                                  &walk_array_trace));
  ASSERT_EQ(4, walk_array_trace.scalar_count);
  ASSERT_EQ(0, walk_array_trace.scalar_offsets[0]);
  ASSERT_EQ(4, walk_array_trace.scalar_offsets[1]);
  ASSERT_EQ(8, walk_array_trace.scalar_offsets[2]);
  ASSERT_EQ(12, walk_array_trace.scalar_offsets[3]);
  for (int i = 0; i < 4; i++) {
    ASSERT_EQ(i + 11, walk_array_trace.scalar_values[i]);
    ASSERT_EQ(4, walk_array_trace.scalar_sizes[i]);
  }
  ASSERT_EQ(0, walk_array_trace.padding_count);

  parsed_code = parse_program_input("extern int __tm_extern_arr[]; int __tm_extern_arr[3]; main(){ return 0; }");
  (void)parsed_code;
  global_var_t *gext = ps_find_global_var("__tm_extern_arr", 15);
  ASSERT_TRUE(gext != NULL);
  ASSERT_TRUE(gext->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, gext->decl_type->kind);
  ASSERT_EQ(12, ps_type_sizeof(gext->decl_type));

  parsed_code = parse_program_input("int __tm_static_decl_type(void) { static double sd = 1.0; return 0; }");
  fn = as_func(parsed_code[0]);
  lvar_t *sd = find_func_lvar(fn, "sd");
  ASSERT_TRUE(sd != NULL);
  ASSERT_TRUE(sd->is_static_local);
  ASSERT_TRUE(sd->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, sd->decl_type->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, sd->decl_type->fp_kind);
  global_var_t *sd_gv = ps_find_global_var(sd->static_global_name, sd->static_global_name_len);
  ASSERT_TRUE(sd_gv != NULL);
  ASSERT_TRUE(sd_gv->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, sd_gv->decl_type->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, sd_gv->decl_type->fp_kind);
  node_t *sd_node = ps_node_new_static_local_gvar_for(sd, sd->size);
  ASSERT_TRUE(ps_node_get_type(sd_node) == sd_gv->decl_type);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_node_get_type(sd_node)->fp_kind);

  parsed_code = parse_program_input(
      "int __tm_static_scalar_flags(void) { "
      "static _Bool sb = 1; static unsigned int su = 2; return sb + su; }");
  fn = as_func(parsed_code[0]);
  lvar_t *static_scalar_sb = find_func_lvar(fn, "sb");
  ASSERT_TRUE(static_scalar_sb != NULL);
  ASSERT_TRUE(static_scalar_sb->is_static_local);
  global_var_t *static_scalar_sb_gv = ps_find_global_var(
      static_scalar_sb->static_global_name, static_scalar_sb->static_global_name_len);
  ASSERT_TRUE(static_scalar_sb_gv != NULL);
  ASSERT_TRUE(static_scalar_sb_gv->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_BOOL, static_scalar_sb_gv->decl_type->kind);
  node_t *static_scalar_sb_node =
      ps_node_new_static_local_gvar_for(static_scalar_sb, static_scalar_sb->size);
  ASSERT_TRUE(ps_node_get_type(static_scalar_sb_node) == static_scalar_sb_gv->decl_type);
  ASSERT_EQ(PSX_TYPE_BOOL, ps_node_get_type(static_scalar_sb_node)->kind);

  lvar_t *static_scalar_su = find_func_lvar(fn, "su");
  ASSERT_TRUE(static_scalar_su != NULL);
  ASSERT_TRUE(static_scalar_su->is_static_local);
  global_var_t *static_scalar_su_gv = ps_find_global_var(
      static_scalar_su->static_global_name, static_scalar_su->static_global_name_len);
  ASSERT_TRUE(static_scalar_su_gv != NULL);
  ASSERT_TRUE(static_scalar_su_gv->decl_type != NULL);
  ASSERT_TRUE(ps_type_is_unsigned(static_scalar_su_gv->decl_type));
  node_t *static_scalar_su_node =
      ps_node_new_static_local_gvar_for(static_scalar_su, static_scalar_su->size);
  ASSERT_TRUE(ps_node_get_type(static_scalar_su_node) == static_scalar_su_gv->decl_type);
  ASSERT_TRUE(ps_node_is_unsigned_type(static_scalar_su_node));

  parsed_code = parse_program_input("int __tm_static_arr_decl_type(void) { static int sa[2] = {1,2}; return sa[0]; }");
  fn = as_func(parsed_code[0]);
  lvar_t *sa = find_func_lvar(fn, "sa");
  ASSERT_TRUE(sa != NULL);
  ASSERT_TRUE(sa->is_static_local);
  ASSERT_TRUE(sa->decl_type != NULL);
  global_var_t *sa_gv = ps_find_global_var(sa->static_global_name, sa->static_global_name_len);
  ASSERT_TRUE(sa_gv != NULL);
  ASSERT_TRUE(sa_gv->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, sa_gv->decl_type->kind);
  ASSERT_EQ(8, ps_type_sizeof(sa_gv->decl_type));
  node_t *sa_node = ps_node_new_static_local_gvar_for(sa, sa->size);
  ASSERT_TRUE(ps_node_get_type(sa_node) == sa_gv->decl_type);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(sa_node)->kind);
  node_t *sa_addr = ps_node_new_static_local_array_addr_for(sa, sa->size);
  psx_type_t *sa_addr_type = ps_node_get_type(sa_addr);
  ASSERT_TRUE(sa_addr_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, sa_addr_type->kind);
  ASSERT_TRUE(sa_addr_type->base == sa_gv->decl_type->base);

  parsed_code = parse_program_input(
      "int __tm_static_inferred_arr(void) { "
      "static int inferred[] = {1,2,3}; return inferred[2]; }");
  fn = as_func(parsed_code[0]);
  lvar_t *static_inferred = find_func_lvar(fn, "inferred");
  ASSERT_TRUE(static_inferred != NULL);
  global_var_t *static_inferred_gv = ps_find_global_var(
      static_inferred->static_global_name,
      static_inferred->static_global_name_len);
  ASSERT_TRUE(static_inferred_gv != NULL);
  ASSERT_EQ(3, ps_lvar_get_decl_type(static_inferred)->array_len);
  ASSERT_EQ(3, ps_gvar_get_decl_type(static_inferred_gv)->array_len);
  ASSERT_EQ(12, ps_type_sizeof(ps_lvar_get_decl_type(static_inferred)));
  ASSERT_EQ(12, ps_type_sizeof(ps_gvar_get_decl_type(static_inferred_gv)));

  parsed_code = parse_program_input(
      "int __tm_static_2d_arr_decl_type(void) { "
      "static int sm[2][3] = {{1,2,3},{4,5,6}}; return sm[1][2]; }");
  fn = as_func(parsed_code[0]);
  lvar_t *sm = find_func_lvar(fn, "sm");
  ASSERT_TRUE(sm != NULL);
  ASSERT_TRUE(sm->is_static_local);
  global_var_t *sm_gv = ps_find_global_var(sm->static_global_name, sm->static_global_name_len);
  ASSERT_TRUE(sm_gv != NULL);
  ASSERT_TRUE(sm_gv->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, sm_gv->decl_type->kind);
  node_t *sm_addr = ps_node_new_static_local_array_addr_for(sm, sm->size);
  psx_type_t *sm_addr_type = ps_node_get_type(sm_addr);
  ASSERT_TRUE(sm_addr_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, sm_addr_type->kind);
  ASSERT_TRUE(sm_addr_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, sm_addr_type->base->kind);
  ASSERT_EQ(3, sm_addr_type->base->array_len);

  parsed_code = parse_program_input(
      "int __tm_static_array_addr_flags(void) { "
      "static unsigned char su[2] = {1,2}; static _Bool sb[2] = {0,1}; "
      "static int si[2] = {3,4}; return su[0] + sb[1] + si[0]; }");
  fn = as_func(parsed_code[0]);
  lvar_t *static_su = find_func_lvar(fn, "su");
  ASSERT_TRUE(static_su != NULL);
  ASSERT_TRUE(static_su->is_static_local);
  global_var_t *static_su_gv = ps_find_global_var(
      static_su->static_global_name, static_su->static_global_name_len);
  ASSERT_TRUE(static_su_gv != NULL);
  ASSERT_TRUE(static_su_gv->decl_type != NULL);
  node_t *static_su_addr =
      ps_node_new_static_local_array_addr_for(static_su, static_su->size);
  ASSERT_TRUE(ps_node_get_type(static_su_addr) != NULL);
  ASSERT_TRUE(ps_node_get_type(static_su_addr)->base ==
              static_su_gv->decl_type->base);
  ASSERT_TRUE(ps_node_pointee_is_unsigned(static_su_addr));

  lvar_t *static_sb = find_func_lvar(fn, "sb");
  ASSERT_TRUE(static_sb != NULL);
  ASSERT_TRUE(static_sb->is_static_local);
  global_var_t *static_sb_gv = ps_find_global_var(
      static_sb->static_global_name, static_sb->static_global_name_len);
  ASSERT_TRUE(static_sb_gv != NULL);
  ASSERT_TRUE(static_sb_gv->decl_type != NULL);
  node_t *static_sb_addr =
      ps_node_new_static_local_array_addr_for(static_sb, static_sb->size);
  ASSERT_TRUE(ps_node_get_type(static_sb_addr) != NULL);
  ASSERT_TRUE(ps_node_get_type(static_sb_addr)->base ==
              static_sb_gv->decl_type->base);
  ASSERT_TRUE(ps_node_pointee_is_bool(static_sb_addr));

  lvar_t *static_si = find_func_lvar(fn, "si");
  ASSERT_TRUE(static_si != NULL);
  ASSERT_TRUE(static_si->is_static_local);
  global_var_t *static_si_gv = ps_find_global_var(
      static_si->static_global_name, static_si->static_global_name_len);
  ASSERT_TRUE(static_si_gv != NULL);
  ASSERT_TRUE(static_si_gv->decl_type != NULL);
  static_si->outer_stride = 32;
  static_si->mid_stride = 16;
  node_t *static_si_addr =
      ps_node_new_static_local_array_addr_for(static_si, static_si->size);
  ASSERT_TRUE(ps_node_get_type(static_si_addr) != NULL);
  ASSERT_TRUE(ps_node_get_type(static_si_addr)->base ==
              static_si_gv->decl_type->base);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, ps_node_pointee_fp_kind(static_si_addr));
  ASSERT_TRUE(!ps_node_pointee_is_bool(static_si_addr));
  ASSERT_TRUE(!ps_node_pointee_is_unsigned(static_si_addr));
  ASSERT_EQ(4, ps_node_deref_size(static_si_addr));
  ASSERT_EQ(0, ps_node_ptr_array_pointee_bytes(static_si_addr));

  parsed_code = parse_program_input(
      "int __tm_static_pointer_pointee_flags(void) { "
      "static _Bool *bp = 0; static unsigned int *up = 0; "
      "return bp == 0 && up == 0; }");
  fn = as_func(parsed_code[0]);
  lvar_t *static_bp = find_func_lvar(fn, "bp");
  ASSERT_TRUE(static_bp != NULL);
  ASSERT_TRUE(static_bp->is_static_local);
  global_var_t *static_bp_gv = ps_find_global_var(
      static_bp->static_global_name, static_bp->static_global_name_len);
  ASSERT_TRUE(static_bp_gv != NULL);
  ASSERT_TRUE(static_bp_gv->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, static_bp_gv->decl_type->kind);
  ASSERT_EQ(PSX_TYPE_BOOL, static_bp_gv->decl_type->base->kind);
  node_t *static_bp_node =
      ps_node_new_static_local_gvar_for(static_bp, static_bp->size);
  ASSERT_TRUE(ps_node_get_type(static_bp_node) == static_bp_gv->decl_type);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(static_bp_node)->kind);
  ASSERT_TRUE(ps_node_pointee_is_bool(static_bp_node));

  lvar_t *static_up = find_func_lvar(fn, "up");
  ASSERT_TRUE(static_up != NULL);
  ASSERT_TRUE(static_up->is_static_local);
  global_var_t *static_up_gv = ps_find_global_var(
      static_up->static_global_name, static_up->static_global_name_len);
  ASSERT_TRUE(static_up_gv != NULL);
  ASSERT_TRUE(static_up_gv->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, static_up_gv->decl_type->kind);
  ASSERT_TRUE(ps_type_is_unsigned(static_up_gv->decl_type->base));
  node_t *static_up_node =
      ps_node_new_static_local_gvar_for(static_up, static_up->size);
  ASSERT_TRUE(ps_node_get_type(static_up_node) == static_up_gv->decl_type);
  ASSERT_TRUE(ps_node_pointee_is_unsigned(static_up_node));

  parsed_code = parse_program_input(
      "int __tm_static_self_reference(void) { "
      "static int *self = &self; return self != 0; }");
  fn = as_func(parsed_code[0]);
  lvar_t *static_self = find_func_lvar(fn, "self");
  ASSERT_TRUE(static_self != NULL);
  ASSERT_TRUE(static_self->is_static_local);
  global_var_t *static_self_gv = ps_find_global_var(
      static_self->static_global_name,
      static_self->static_global_name_len);
  ASSERT_TRUE(static_self_gv != NULL);
  ASSERT_TRUE(static_self_gv->init_symbol == static_self_gv->name);
  ASSERT_EQ(static_self_gv->name_len, static_self_gv->init_symbol_len);

  parsed_code = parse_program_input("int __tm_local_extern_decl_type(void) { extern double __tm_local_extern_dp; return 0; }");
  (void)parsed_code;
  const char *local_extern_name = "__tm_local_extern_dp";
  global_var_t *local_extern_dp =
      ps_find_global_var((char *)local_extern_name, (int)(sizeof("__tm_local_extern_dp") - 1));
  ASSERT_TRUE(local_extern_dp != NULL);
  ASSERT_TRUE(local_extern_dp->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, local_extern_dp->decl_type->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, local_extern_dp->decl_type->fp_kind);

  parsed_code = parse_program_input(
      "int __tm_block_extern_proto(void) { "
      "extern double __tm_block_declared_fn(int); return 0; }");
  (void)parsed_code;
  const psx_type_t *block_declared_function = ps_ctx_get_function_type(
      (char *)"__tm_block_declared_fn", 22);
  ASSERT_TRUE(block_declared_function != NULL);
  ASSERT_EQ(PSX_TYPE_FUNCTION, block_declared_function->kind);
  ASSERT_TRUE(block_declared_function->base != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, block_declared_function->base->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            block_declared_function->base->fp_kind);
  ASSERT_EQ(1, block_declared_function->param_count);
  ASSERT_EQ(PSX_TYPE_INTEGER,
            block_declared_function->param_types[0]->kind);

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
  ASSERT_EQ(12, ps_type_sizeof(compound_lit_local->decl_type));
  node_t *compound_lit_stale_addr =
      ps_node_new_lvar_array_addr_for(compound_lit_local, 0);
  ASSERT_TRUE(ps_node_get_type(compound_lit_stale_addr) != NULL);
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            ps_node_pointee_fp_kind(compound_lit_stale_addr));
  ASSERT_TRUE(!ps_node_pointee_is_bool(compound_lit_stale_addr));
  ASSERT_TRUE(!ps_node_pointee_is_unsigned(compound_lit_stale_addr));

  node_t *compound_unsigned_expr = parse_expr_input("(unsigned char[2]){1,2}");
  ASSERT_EQ(ND_COMMA, compound_unsigned_expr->kind);
  ASSERT_TRUE(compound_unsigned_expr->rhs != NULL);
  ASSERT_EQ(ND_ADDR, compound_unsigned_expr->rhs->kind);
  lvar_t *compound_unsigned_local =
      as_lvar(compound_unsigned_expr->rhs->lhs)->var;
  ASSERT_TRUE(compound_unsigned_local != NULL);
  ASSERT_TRUE(compound_unsigned_local->decl_type != NULL);
  node_t *compound_unsigned_addr =
      ps_node_new_lvar_array_addr_for(compound_unsigned_local, 0);
  ASSERT_TRUE(ps_node_pointee_is_unsigned(compound_unsigned_addr));

  node_t *compound_bool_expr = parse_expr_input("(_Bool[2]){0,1}");
  ASSERT_EQ(ND_COMMA, compound_bool_expr->kind);
  ASSERT_TRUE(compound_bool_expr->rhs != NULL);
  ASSERT_EQ(ND_ADDR, compound_bool_expr->rhs->kind);
  lvar_t *compound_bool_local = as_lvar(compound_bool_expr->rhs->lhs)->var;
  ASSERT_TRUE(compound_bool_local != NULL);
  ASSERT_TRUE(compound_bool_local->decl_type != NULL);
  node_t *compound_bool_addr =
      ps_node_new_lvar_array_addr_for(compound_bool_local, 0);
  ASSERT_TRUE(ps_node_pointee_is_bool(compound_bool_addr));

  ps_reset_translation_unit_state();
  parsed_code = parse_program_input("int *__tm_compound_lit_global = (int[]){1,2,3}; int main(void) { return 0; }");
  (void)parsed_code;
  global_var_t *compound_lit_global =
      ps_find_global_var("__compound_lit_0", (int)(sizeof("__compound_lit_0") - 1));
  ASSERT_TRUE(compound_lit_global != NULL);
  ASSERT_TRUE(compound_lit_global->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, compound_lit_global->decl_type->kind);
  ASSERT_EQ(12, ps_type_sizeof(compound_lit_global->decl_type));
  node_t *compound_global_stale_addr =
      ps_node_new_gvar_array_addr_for(compound_lit_global);
  ASSERT_TRUE(ps_node_get_type(compound_global_stale_addr) != NULL);
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            ps_node_pointee_fp_kind(compound_global_stale_addr));
  ASSERT_TRUE(!ps_node_pointee_is_bool(compound_global_stale_addr));
  ASSERT_TRUE(!ps_node_pointee_is_unsigned(compound_global_stale_addr));

  ps_reset_translation_unit_state();
  parsed_code = parse_program_input(
      "unsigned char *__tm_compound_u = (unsigned char[]){1,2}; "
      "_Bool *__tm_compound_b = (_Bool[]){0,1}; int main(void) { return 0; }");
  (void)parsed_code;
  global_var_t *compound_global_u =
      ps_find_global_var("__compound_lit_0", (int)(sizeof("__compound_lit_0") - 1));
  ASSERT_TRUE(compound_global_u != NULL);
  ASSERT_TRUE(compound_global_u->decl_type != NULL);
  node_t *compound_global_u_addr =
      ps_node_new_gvar_array_addr_for(compound_global_u);
  ASSERT_TRUE(ps_node_pointee_is_unsigned(compound_global_u_addr));

  global_var_t *compound_global_b =
      ps_find_global_var("__compound_lit_1", (int)(sizeof("__compound_lit_1") - 1));
  ASSERT_TRUE(compound_global_b != NULL);
  ASSERT_TRUE(compound_global_b->decl_type != NULL);
  node_t *compound_global_b_addr =
      ps_node_new_gvar_array_addr_for(compound_global_b);
  ASSERT_TRUE(ps_node_pointee_is_bool(compound_global_b_addr));

  parsed_code = parse_program_input("int __tm_mix_f(int a), __tm_mix_g(int a), __tm_mix_a; "
                                    "int __tm_mix_f(int a) { return a; } "
                                    "int __tm_mix_g(int a) { return a; } "
                                    "int main(void) { __tm_mix_a = 5; return __tm_mix_a; }");
  (void)parsed_code;
  global_var_t *mix_a = ps_find_global_var("__tm_mix_a", 10);
  ASSERT_TRUE(mix_a != NULL);
  ASSERT_EQ(4, ps_gvar_storage_size(mix_a, 99));
  ASSERT_TRUE(mix_a->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, mix_a->decl_type->kind);
  ASSERT_EQ(4, ps_type_sizeof(mix_a->decl_type));

  parsed_code = parse_program_input("char *__tm_ptr_s = (char[6]){\"hi\"}; "
                                    "int main(void) { return __tm_ptr_s[0]; }");
  (void)parsed_code;
  global_var_t *ptr_s = ps_find_global_var("__tm_ptr_s", 10);
  ASSERT_TRUE(ptr_s != NULL);
  ASSERT_TRUE(!ps_gvar_is_array(ptr_s));
  ASSERT_EQ(8, ps_gvar_decl_sizeof(ptr_s, 99));
  ASSERT_EQ(8, ps_gvar_storage_size(ptr_s, 99));
  ASSERT_EQ(1, ps_gvar_initializer_element_count(ptr_s, 4));

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
        lvar_t *callee_lvar = ps_node_lvar_symbol(call->callee);
        if (callee_lvar && callee_lvar->len == 3 &&
            strncmp(callee_lvar->name, "dpa", 3) == 0) {
          indirect_double_ptr_to_array_call = n;
        }
      }
    }
  }
  psx_type_t *long_add_ty = ps_node_get_type(long_add);
  ASSERT_TRUE(long_add_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, long_add_ty->kind);
  ASSERT_EQ(8, ps_type_sizeof(long_add_ty));
  psx_type_t *ptr_add_ty = ps_node_get_type(ptr_add);
  ASSERT_TRUE(ptr_add_ty != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ptr_add_ty->kind);
  ASSERT_EQ(4, ps_type_deref_size(ptr_add_ty));
  ASSERT_TRUE(long_call->type != NULL);
  psx_type_t *long_call_ty = ps_node_get_type(long_call);
  ASSERT_TRUE(long_call_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, long_call_ty->kind);
  ASSERT_EQ(8, ps_type_sizeof(long_call_ty));
  ASSERT_EQ(8, ps_node_type_size(long_call));
  ASSERT_TRUE(ptr_call->type != NULL);
  psx_type_t *ptr_call_ty = ps_node_get_type(ptr_call);
  ASSERT_TRUE(ptr_call_ty != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ptr_call_ty->kind);
  ASSERT_EQ(4, ps_type_deref_size(ptr_call_ty));
  ASSERT_TRUE(ps_node_is_pointer(ptr_call));
  ASSERT_EQ(4, ps_node_deref_size(ptr_call));
  ASSERT_TRUE(ptrptr_call->type != NULL);
  psx_type_t *ptrptr_call_ty = ps_node_get_type(ptrptr_call);
  ASSERT_TRUE(ptrptr_call_ty != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ptrptr_call_ty->kind);
  ASSERT_EQ(8, ps_type_deref_size(ptrptr_call_ty));
  ASSERT_TRUE(ptrptr_call_ty->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ptrptr_call_ty->base->kind);
  ASSERT_EQ(4, ps_type_deref_size(ptrptr_call_ty->base));
  ASSERT_EQ(8, ps_node_deref_size(ptrptr_call));
  ASSERT_EQ(2, ps_node_pointer_qual_levels(ptrptr_call));
  psx_function_ret_info_t ptrptr_ret_info =
      ps_ctx_get_function_ret_info("__tm_pp", 7);
  ASSERT_TRUE(ptrptr_ret_info.is_pointer);
  ASSERT_EQ(2, ptrptr_ret_info.pointer_levels);
  ASSERT_EQ(TK_INT, ptrptr_ret_info.token_kind);
  ASSERT_TRUE(double_ptr_to_array_call->type != NULL);
  psx_type_t *double_ptr_to_array_ty = ps_node_get_type(double_ptr_to_array_call);
  ASSERT_TRUE(double_ptr_to_array_ty != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, double_ptr_to_array_ty->kind);
  ASSERT_EQ(16, ps_type_deref_size(double_ptr_to_array_ty));
  ASSERT_TRUE(double_ptr_to_array_ty->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, double_ptr_to_array_ty->base->kind);
  ASSERT_EQ(16, ps_type_sizeof(double_ptr_to_array_ty->base));
  ASSERT_EQ(16, ps_type_pointer_view_structural_ptr_array_pointee_bytes(
                    double_ptr_to_array_ty));
  ASSERT_EQ(16, double_ptr_to_array_ty->outer_stride);
  ASSERT_EQ(0, double_ptr_to_array_ty->mid_stride);
  ASSERT_EQ(16, ps_node_deref_size(double_ptr_to_array_call));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_node_pointee_fp_kind(double_ptr_to_array_call));
  ASSERT_EQ(2, double_ptr_to_array_ty->funcptr_sig.function.callable.return_shape.pointee_array.first_dim);
  ASSERT_EQ(8, double_ptr_to_array_ty->funcptr_sig.function.callable.return_shape.pointee_array.elem_size);
  psx_function_ret_info_t dpa_ret_info =
      ps_ctx_get_function_ret_info("__tm_dp", 7);
  ASSERT_TRUE(dpa_ret_info.is_pointer);
  ASSERT_EQ(0, dpa_ret_info.is_funcptr);
  ASSERT_EQ(1, dpa_ret_info.pointer_levels);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, dpa_ret_info.fp_kind);
  ASSERT_EQ(2, dpa_ret_info.pointee_array.first_dim);
  ASSERT_EQ(8, dpa_ret_info.pointee_array.elem_size);
  ps_ctx_define_function_name_with_ret("__tm_manual_type", 16, 0);
  psx_type_t *manual_ret_type =
      ps_type_new_pointer(ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8), 8);
  psx_type_t *manual_function_type = ps_type_new_function(
      manual_ret_type, (psx_decl_funcptr_sig_t){0});
  ASSERT_TRUE(ps_ctx_track_function_type("__tm_manual_type", 16,
                                          manual_function_type));
  psx_function_ret_info_t manual_ret_info =
      ps_ctx_get_function_ret_info("__tm_manual_type", 16);
  ASSERT_TRUE(manual_ret_info.is_pointer);
  ASSERT_EQ(TK_DOUBLE, manual_ret_info.token_kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, manual_ret_info.fp_kind);
  ASSERT_EQ(1, manual_ret_info.pointer_levels);

  const char manual_ptrarr_name[] = "__tm_manual_ptrarr_type";
  ps_ctx_define_function_name_with_ret(
      (char *)manual_ptrarr_name, (int)sizeof(manual_ptrarr_name) - 1, 0);
  psx_type_t *manual_ptrarr_ret =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 8);
  manual_ptrarr_ret->funcptr_sig.function.callable.return_shape.pointee_array =
      ps_ret_pointee_array_make(3, 4, 4);
  manual_ptrarr_ret->ptr_array_pointee_bytes = 999;
  manual_ptrarr_ret->outer_stride = 777;
  manual_ptrarr_ret->mid_stride = 555;
  manual_ptrarr_ret->extra_strides_count = 1;
  manual_ptrarr_ret->extra_strides[0] = 333;
  psx_type_t *manual_ptrarr_function_type = ps_type_new_function(
      manual_ptrarr_ret, (psx_decl_funcptr_sig_t){0});
  ASSERT_TRUE(ps_ctx_track_function_type(
      (char *)manual_ptrarr_name, (int)sizeof(manual_ptrarr_name) - 1,
      manual_ptrarr_function_type));
  const psx_type_t *manual_ptrarr_stored = ps_ctx_get_function_ret_type(
      (char *)manual_ptrarr_name, (int)sizeof(manual_ptrarr_name) - 1);
  ASSERT_TRUE(manual_ptrarr_stored != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, manual_ptrarr_stored->kind);
  ASSERT_TRUE(manual_ptrarr_stored->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, manual_ptrarr_stored->base->kind);
  ASSERT_EQ(48, ps_type_deref_size(manual_ptrarr_stored));
  ASSERT_EQ(48, ps_type_pointer_view_structural_ptr_array_pointee_bytes(
                    manual_ptrarr_stored));
  ASSERT_EQ(48, manual_ptrarr_stored->outer_stride);
  ASSERT_EQ(16, manual_ptrarr_stored->mid_stride);
  ASSERT_EQ(0, manual_ptrarr_stored->extra_strides_count);
  psx_function_ret_info_t manual_ptrarr_info = ps_ctx_get_function_ret_info(
      (char *)manual_ptrarr_name, (int)sizeof(manual_ptrarr_name) - 1);
  ASSERT_TRUE(manual_ptrarr_info.is_pointer);
  ASSERT_EQ(3, manual_ptrarr_info.pointee_array.first_dim);
  ASSERT_EQ(4, manual_ptrarr_info.pointee_array.second_dim);
  ASSERT_EQ(4, manual_ptrarr_info.pointee_array.elem_size);

  const char many_param_name[] = "__tm_many_param_type";
  ps_ctx_define_function_name_with_ret(
      (char *)many_param_name, (int)sizeof(many_param_name) - 1, 0);
  psx_type_t *many_param_function = ps_type_new_function(
      ps_type_new_integer(TK_INT, 4, 0), (psx_decl_funcptr_sig_t){0});
  psx_type_t *many_param_types[17] = {0};
  for (int i = 0; i < 17; i++)
    many_param_types[i] = ps_type_new_integer(TK_INT, 4, 0);
  ps_type_set_function_params(many_param_function, many_param_types, 17, 0);
  ASSERT_TRUE(ps_ctx_track_function_type(
      (char *)many_param_name, (int)sizeof(many_param_name) - 1,
      many_param_function));
  ASSERT_EQ(17, ps_ctx_get_function_nargs_fixed(
                    (char *)many_param_name,
                    (int)sizeof(many_param_name) - 1));

  const char typedef_shape_name[] = "__tm_typedef_shape_cmp";
  psx_type_t *typedef_shape_int_ptr =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 4);
  psx_typedef_info_t typedef_shape_a = {0};
  typedef_shape_a.base_kind = TK_INT;
  typedef_shape_a.elem_size = 4;
  typedef_shape_a.is_pointer = 1;
  typedef_shape_a.sizeof_size = 8;
  typedef_shape_a.decl_type = typedef_shape_int_ptr;
  ASSERT_TRUE(ps_ctx_define_typedef_name((char *)typedef_shape_name,
                                          (int)sizeof(typedef_shape_name) - 1,
                                          &typedef_shape_a));
  psx_typedef_info_t typedef_shape_same_type_stale_legacy = typedef_shape_a;
  typedef_shape_same_type_stale_legacy.base_kind = TK_DOUBLE;
  typedef_shape_same_type_stale_legacy.elem_size = 8;
  typedef_shape_same_type_stale_legacy.fp_kind = TK_FLOAT_KIND_DOUBLE;
  ASSERT_TRUE(ps_ctx_define_typedef_name(
      (char *)typedef_shape_name, (int)sizeof(typedef_shape_name) - 1,
      &typedef_shape_same_type_stale_legacy));
  psx_typedef_info_t typedef_shape_different = typedef_shape_a;
  typedef_shape_different.decl_type =
      ps_type_new_pointer(ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8), 8);
  ASSERT_TRUE(!ps_ctx_define_typedef_name((char *)typedef_shape_name,
                                           (int)sizeof(typedef_shape_name) - 1,
                                           &typedef_shape_different));
  const char typedef_sync_name[] = "__tm_typedef_sync_from_type";
  psx_typedef_info_t typedef_sync = {0};
  typedef_sync.base_kind = TK_DOUBLE;
  typedef_sync.elem_size = 8;
  typedef_sync.fp_kind = TK_FLOAT_KIND_DOUBLE;
  typedef_sync.is_pointer = 0;
  typedef_sync.decl_type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 4);
  ASSERT_TRUE(ps_ctx_define_typedef_name((char *)typedef_sync_name,
                                          (int)sizeof(typedef_sync_name) - 1,
                                          &typedef_sync));
  ASSERT_EQ(1, ps_ctx_get_typedef_pointer_levels(
                   (char *)typedef_sync_name, (int)sizeof(typedef_sync_name) - 1));
  psx_typedef_info_t typedef_sync_out = {0};
  ASSERT_TRUE(ps_ctx_find_typedef_name((char *)typedef_sync_name,
                                        (int)sizeof(typedef_sync_name) - 1,
                                        &typedef_sync_out));
  ASSERT_EQ(TK_INT, typedef_sync_out.base_kind);
  ASSERT_TRUE(typedef_sync_out.is_pointer);

  const char tag_member_desc_tag[] = "__tm_tag_member_desc_sync";
  const char tag_member_desc_name[] = "rows";
  ps_ctx_define_tag_type_with_layout(TK_STRUCT, (char *)tag_member_desc_tag,
                                      (int)sizeof(tag_member_desc_tag) - 1,
                                      1, 6, 1);
  tag_member_info_t tag_member_desc = {0};
  tag_member_desc.name = (char *)tag_member_desc_name;
  tag_member_desc.len = (int)sizeof(tag_member_desc_name) - 1;
  tag_member_desc.type_size = 6;
  tag_member_desc.deref_size = 1;
  tag_member_desc.array_len = 6;
  tag_member_desc.outer_stride = 3;
  tag_member_desc.arr_ndim = 2;
  tag_member_desc.arr_dims[0] = 2;
  tag_member_desc.arr_dims[1] = 3;
  psx_type_t *tag_member_desc_leaf =
      ps_type_new_integer(TK_UNSIGNED, 1, 1);
  psx_type_t *tag_member_desc_inner =
      ps_type_new_array(tag_member_desc_leaf, 3, 3, 1, 0);
  tag_member_desc_inner->outer_stride = 1;
  tag_member_desc.decl_type =
      ps_type_new_array(tag_member_desc_inner, 2, 6, 3, 0);
  tag_member_desc.decl_type->outer_stride = 3;
  ps_ctx_add_tag_member(TK_STRUCT, (char *)tag_member_desc_tag,
                         (int)sizeof(tag_member_desc_tag) - 1,
                         &tag_member_desc);
  tag_member_info_t tag_member_desc_out = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info(
      TK_STRUCT, (char *)tag_member_desc_tag,
      (int)sizeof(tag_member_desc_tag) - 1,
      (char *)tag_member_desc_name, (int)sizeof(tag_member_desc_name) - 1,
      &tag_member_desc_out));
  ASSERT_EQ(1, tag_member_desc_out.type_size);
  ASSERT_EQ(3, tag_member_desc_out.outer_stride);
  ASSERT_EQ(2, tag_member_desc_out.arr_ndim);
  ASSERT_EQ(2, tag_member_desc_out.arr_dims[0]);
  ASSERT_EQ(3, tag_member_desc_out.arr_dims[1]);
  ASSERT_TRUE(tag_member_desc_out.is_unsigned);
  tag_member_info_t tag_member_desc_stale_stride = tag_member_desc_out;
  tag_member_desc_stale_stride.decl_type->outer_stride = 99;
  tag_member_desc_stale_stride.decl_type->mid_stride = 77;
  ASSERT_EQ(3, ps_tag_member_decl_outer_stride(
                   &tag_member_desc_stale_stride));
  tag_member_desc_stale_stride.decl_type->outer_stride = 0;
  tag_member_desc_stale_stride.decl_type->mid_stride = 0;
  tag_member_info_t tag_member_desc_stale_dims = tag_member_desc_out;
  tag_member_desc_stale_dims.arr_ndim = 3;
  tag_member_desc_stale_dims.arr_dims[0] = 9;
  tag_member_desc_stale_dims.arr_dims[1] = 8;
  tag_member_desc_stale_dims.arr_dims[2] = 7;
  ASSERT_EQ(2, ps_tag_member_decl_array_dim_count(
                   &tag_member_desc_stale_dims));
  ASSERT_EQ(2, ps_tag_member_decl_array_dim(&tag_member_desc_stale_dims, 0));
  ASSERT_EQ(3, ps_tag_member_decl_array_dim(&tag_member_desc_stale_dims, 1));
  ASSERT_EQ(0, ps_tag_member_decl_array_dim(&tag_member_desc_stale_dims, 2));

  global_var_t gvar_view_sync = {0};
  gvar_view_sync.name = "__tm_gvar_view_sync";
  gvar_view_sync.name_len = (int)strlen(gvar_view_sync.name);
  gvar_view_sync.type_size = 4;
  gvar_view_sync.decl_type =
      ps_type_new_array(ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8), 2, 16, 8, 0);
  psx_gvar_view_t gvar_view_sync_out = ps_gvar_view(&gvar_view_sync);
  ASSERT_EQ(16, gvar_view_sync_out.type_size);
  ASSERT_TRUE(gvar_view_sync_out.is_array);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, gvar_view_sync_out.fp_kind);
  ASSERT_EQ(8, gvar_view_sync_out.deref_size);

  global_var_t gvar_view_array_shape = {0};
  gvar_view_array_shape.name = "__tm_gvar_view_array_shape";
  gvar_view_array_shape.name_len = (int)strlen(gvar_view_array_shape.name);
  gvar_view_array_shape.type_size = 4;
  psx_type_t *gvar_view_array_row =
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 5, 20, 4, 0);
  gvar_view_array_shape.decl_type =
      ps_type_new_array(gvar_view_array_row, 3, 60, 20, 0);
  psx_gvar_view_t gvar_view_array_shape_out =
      ps_gvar_view(&gvar_view_array_shape);
  ASSERT_TRUE(gvar_view_array_shape_out.is_array);
  ASSERT_EQ(60, gvar_view_array_shape_out.type_size);
  ASSERT_EQ(4, gvar_view_array_shape_out.deref_size);
  ASSERT_EQ(20, gvar_view_array_shape_out.outer_stride);
  ASSERT_EQ(0, gvar_view_array_shape_out.mid_stride);
  ASSERT_EQ(0, gvar_view_array_shape_out.extra_strides_count);

  global_var_t gvar_view_deep_array_shape = {0};
  gvar_view_deep_array_shape.name = "__tm_gvar_view_deep_array_shape";
  gvar_view_deep_array_shape.name_len =
      (int)strlen(gvar_view_deep_array_shape.name);
  psx_type_t *gvar_view_deep_d3 =
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 5, 20, 4, 0);
  psx_type_t *gvar_view_deep_d2 =
      ps_type_new_array(gvar_view_deep_d3, 4, 80, 20, 0);
  psx_type_t *gvar_view_deep_d1 =
      ps_type_new_array(gvar_view_deep_d2, 3, 240, 80, 0);
  gvar_view_deep_array_shape.decl_type =
      ps_type_new_array(gvar_view_deep_d1, 2, 480, 240, 0);
  psx_gvar_view_t gvar_view_deep_array_shape_out =
      ps_gvar_view(&gvar_view_deep_array_shape);
  ASSERT_TRUE(gvar_view_deep_array_shape_out.is_array);
  ASSERT_EQ(480, gvar_view_deep_array_shape_out.type_size);
  ASSERT_EQ(4, gvar_view_deep_array_shape_out.deref_size);
  ASSERT_EQ(240, gvar_view_deep_array_shape_out.outer_stride);
  ASSERT_EQ(80, gvar_view_deep_array_shape_out.mid_stride);
  ASSERT_EQ(1, gvar_view_deep_array_shape_out.extra_strides_count);
  ASSERT_EQ(20, gvar_view_deep_array_shape_out.extra_strides[0]);

  global_var_t gvar_view_scalar_stale_tag = {0};
  gvar_view_scalar_stale_tag.name = "__tm_gvar_view_scalar_stale_tag";
  gvar_view_scalar_stale_tag.name_len =
      (int)strlen(gvar_view_scalar_stale_tag.name);
  gvar_view_scalar_stale_tag.decl_type =
      ps_type_new_integer(TK_INT, 4, 0);
  psx_gvar_view_t gvar_view_scalar_stale_tag_out =
      ps_gvar_view(&gvar_view_scalar_stale_tag);
  ASSERT_EQ(4, gvar_view_scalar_stale_tag_out.type_size);
  ASSERT_TRUE(!gvar_view_scalar_stale_tag_out.is_array);
  ASSERT_EQ(0, gvar_view_scalar_stale_tag_out.deref_size);
  ASSERT_EQ(0, gvar_view_scalar_stale_tag_out.outer_stride);
  ASSERT_EQ(0, gvar_view_scalar_stale_tag_out.mid_stride);
  ASSERT_EQ(0, gvar_view_scalar_stale_tag_out.extra_strides_count);
  ASSERT_EQ(TK_EOF, gvar_view_scalar_stale_tag_out.tag_kind);
  ASSERT_TRUE(gvar_view_scalar_stale_tag_out.tag_name == NULL);
  ASSERT_EQ(0, gvar_view_scalar_stale_tag_out.tag_len);
  ASSERT_TRUE(!gvar_view_scalar_stale_tag_out.is_tag_pointer);

  const char gvar_view_tag_name[] = "__tm_gvar_view_tag";
  global_var_t gvar_view_tag_ptr = {0};
  gvar_view_tag_ptr.name = "__tm_gvar_view_tag_ptr";
  gvar_view_tag_ptr.name_len = (int)strlen(gvar_view_tag_ptr.name);
  gvar_view_tag_ptr.type_size = 4;
  gvar_view_tag_ptr.decl_type = ps_type_new_pointer(
      ps_type_new_tag(TK_STRUCT, (char *)gvar_view_tag_name,
                       (int)sizeof(gvar_view_tag_name) - 1, 0, 12),
      12);
  psx_gvar_view_t gvar_view_tag_ptr_out = ps_gvar_view(&gvar_view_tag_ptr);
  ASSERT_EQ(8, gvar_view_tag_ptr_out.type_size);
  ASSERT_EQ(TK_STRUCT, gvar_view_tag_ptr_out.tag_kind);
  ASSERT_EQ((int)sizeof(gvar_view_tag_name) - 1, gvar_view_tag_ptr_out.tag_len);
  ASSERT_TRUE(gvar_view_tag_ptr_out.is_tag_pointer);

  const char stale_node_tag_name[] = "__tm_stale_node_tag";
  global_var_t gvar_node_scalar_sync = {0};
  gvar_node_scalar_sync.name = "__tm_gvar_node_scalar";
  gvar_node_scalar_sync.name_len = (int)strlen(gvar_node_scalar_sync.name);
  gvar_node_scalar_sync.type_size = 4;
  gvar_node_scalar_sync.decl_type =
      ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8);
  node_t *gvar_node_scalar_sync_node =
      ps_node_new_gvar_for(&gvar_node_scalar_sync);
  ASSERT_EQ(8, ps_node_type_size(gvar_node_scalar_sync_node));
  ASSERT_EQ(PSX_TYPE_FLOAT,
            ps_node_get_type(gvar_node_scalar_sync_node)->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            ps_node_get_type(gvar_node_scalar_sync_node)->fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            ps_node_pointee_fp_kind(gvar_node_scalar_sync_node));
  ASSERT_TRUE(!ps_node_pointee_is_bool(gvar_node_scalar_sync_node));
  ASSERT_TRUE(!ps_node_pointee_is_unsigned(gvar_node_scalar_sync_node));
  global_var_t gvar_materialized_scalar_stale_tag = gvar_node_scalar_sync;
  gvar_materialized_scalar_stale_tag.decl_type = NULL;
  psx_type_t *gvar_materialized_scalar_type =
      ps_gvar_get_decl_type(&gvar_materialized_scalar_stale_tag);
  ASSERT_TRUE(gvar_materialized_scalar_type == NULL);

  const char gvar_node_tag_name[] = "__tm_gvar_node_tag";
  global_var_t gvar_node_tag_sync = {0};
  gvar_node_tag_sync.name = "__tm_gvar_node_tag_obj";
  gvar_node_tag_sync.name_len = (int)strlen(gvar_node_tag_sync.name);
  gvar_node_tag_sync.type_size = 4;
  gvar_node_tag_sync.decl_type =
      ps_type_new_tag(TK_STRUCT, (char *)gvar_node_tag_name,
                       (int)sizeof(gvar_node_tag_name) - 1, 0, 12);
  node_t *gvar_node_tag_sync_node =
      ps_node_new_gvar_for(&gvar_node_tag_sync);
  ASSERT_EQ(12, ps_node_type_size(gvar_node_tag_sync_node));
  ASSERT_EQ(PSX_TYPE_STRUCT,
            ps_node_get_type(gvar_node_tag_sync_node)->kind);
  ASSERT_EQ(TK_STRUCT,
            ps_node_get_type(gvar_node_tag_sync_node)->tag_kind);

  global_var_t gvar_node_ptr_scalar_sync = {0};
  gvar_node_ptr_scalar_sync.name = "__tm_gvar_node_ptr_scalar";
  gvar_node_ptr_scalar_sync.name_len = (int)strlen(gvar_node_ptr_scalar_sync.name);
  gvar_node_ptr_scalar_sync.type_size = 8;
  gvar_node_ptr_scalar_sync.decl_type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 4);
  node_t *gvar_node_ptr_scalar_sync_node =
      ps_node_new_gvar_for(&gvar_node_ptr_scalar_sync);
  ASSERT_TRUE(ps_node_is_pointer(gvar_node_ptr_scalar_sync_node));
  ASSERT_EQ(PSX_TYPE_INTEGER,
            ps_node_get_type(gvar_node_ptr_scalar_sync_node)->base->kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            ps_node_pointee_fp_kind(gvar_node_ptr_scalar_sync_node));
  ASSERT_TRUE(!ps_node_pointee_is_bool(gvar_node_ptr_scalar_sync_node));
  ASSERT_TRUE(!ps_node_pointee_is_unsigned(
      gvar_node_ptr_scalar_sync_node));

  global_var_t gvar_node_ptr_bool_sync = {0};
  gvar_node_ptr_bool_sync.name = "__tm_gvar_node_ptr_bool";
  gvar_node_ptr_bool_sync.name_len = (int)strlen(gvar_node_ptr_bool_sync.name);
  gvar_node_ptr_bool_sync.type_size = 8;
  gvar_node_ptr_bool_sync.decl_type =
      ps_type_new_pointer(ps_type_new_integer(TK_BOOL, 1, 0), 1);
  node_t *gvar_node_ptr_bool_sync_node =
      ps_node_new_gvar_for(&gvar_node_ptr_bool_sync);
  ASSERT_TRUE(ps_node_is_pointer(gvar_node_ptr_bool_sync_node));
  ASSERT_TRUE(ps_node_pointee_is_bool(gvar_node_ptr_bool_sync_node));
  ASSERT_TRUE(!ps_node_pointee_is_unsigned(gvar_node_ptr_bool_sync_node));

  global_var_t gvar_node_ptr_unsigned_sync = {0};
  gvar_node_ptr_unsigned_sync.name = "__tm_gvar_node_ptr_unsigned";
  gvar_node_ptr_unsigned_sync.name_len =
      (int)strlen(gvar_node_ptr_unsigned_sync.name);
  gvar_node_ptr_unsigned_sync.type_size = 8;
  gvar_node_ptr_unsigned_sync.decl_type =
      ps_type_new_pointer(ps_type_new_integer(TK_UNSIGNED, 4, 1), 4);
  node_t *gvar_node_ptr_unsigned_sync_node =
      ps_node_new_gvar_for(&gvar_node_ptr_unsigned_sync);
  ASSERT_TRUE(ps_node_is_pointer(gvar_node_ptr_unsigned_sync_node));
  ASSERT_TRUE(!ps_node_pointee_is_bool(gvar_node_ptr_unsigned_sync_node));
  ASSERT_TRUE(ps_node_pointee_is_unsigned(
      gvar_node_ptr_unsigned_sync_node));

  lvar_t lvar_view_fp_array = {0};
  lvar_view_fp_array.decl_type = ps_type_new_array(
      ps_type_new_array(ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8), 3, 24, 8, 0),
      2, 48, 24, 0);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_lvar_fp_kind(&lvar_view_fp_array));

  lvar_t lvar_view_complex_array = {0};
  psx_type_t *lvar_view_complex_leaf = ps_type_new(PSX_TYPE_COMPLEX);
  lvar_view_complex_leaf->size = 16;
  lvar_view_complex_leaf->fp_kind = TK_FLOAT_KIND_DOUBLE;
  lvar_view_complex_array.decl_type =
      ps_type_new_array(lvar_view_complex_leaf, 2, 32, 16, 0);
  ASSERT_TRUE(ps_lvar_is_complex(&lvar_view_complex_array));

  lvar_t lvar_view_array_shape = {0};
  lvar_view_array_shape.size = 77;
  lvar_view_array_shape.elem_size = 99;
  lvar_view_array_shape.is_array = 0;
  lvar_view_array_shape.outer_stride = 55;
  lvar_view_array_shape.mid_stride = 33;
  psx_type_t *lvar_view_array_d2 =
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 4, 16, 4, 0);
  psx_type_t *lvar_view_array_d1 =
      ps_type_new_array(lvar_view_array_d2, 3, 48, 16, 0);
  lvar_view_array_shape.decl_type =
      ps_type_new_array(lvar_view_array_d1, 2, 96, 48, 0);
  ASSERT_TRUE(ps_lvar_is_array(&lvar_view_array_shape));
  ASSERT_EQ(24, ps_lvar_array_flat_element_count(&lvar_view_array_shape));
  ASSERT_EQ(12, ps_lvar_array_designator_stride_elements(
                    &lvar_view_array_shape, 0));
  ASSERT_EQ(4, ps_lvar_array_designator_stride_elements(
                   &lvar_view_array_shape, 1));
  ASSERT_EQ(1, ps_lvar_array_designator_stride_elements(
                   &lvar_view_array_shape, 2));
  node_t *lvar_view_array_shape_addr =
      ps_node_new_lvar_array_addr_for(&lvar_view_array_shape, 0);
  ASSERT_TRUE(ps_node_is_pointer(lvar_view_array_shape_addr));
  ASSERT_EQ(8, ps_node_type_size(lvar_view_array_shape_addr));
  ASSERT_EQ(48, ps_node_deref_size(lvar_view_array_shape_addr));
  int lvar_view_array_shape_inner = 0;
  int lvar_view_array_shape_next = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(lvar_view_array_shape_addr,
                                               &lvar_view_array_shape_inner,
                                               &lvar_view_array_shape_next,
                                               NULL, NULL));
  ASSERT_EQ(16, lvar_view_array_shape_inner);
  ASSERT_EQ(4, lvar_view_array_shape_next);

  const char lvar_view_struct_array_tag_name[] = "__tm_lvar_view_struct_array";
  lvar_t lvar_view_struct_array_shape = {0};
  lvar_view_struct_array_shape.offset = 100;
  lvar_view_struct_array_shape.size = 77;
  lvar_view_struct_array_shape.elem_size = 99;
  lvar_view_struct_array_shape.is_array = 0;
  lvar_view_struct_array_shape.decl_type = ps_type_new_array(
      ps_type_new_tag(TK_STRUCT, (char *)lvar_view_struct_array_tag_name,
                       (int)sizeof(lvar_view_struct_array_tag_name) - 1, 0, 12),
      2, 24, 12, 0);
  ASSERT_TRUE(ps_lvar_is_array(&lvar_view_struct_array_shape));
  ASSERT_TRUE(ps_lvar_is_tag_aggregate(&lvar_view_struct_array_shape));
  ASSERT_EQ(24, ps_lvar_decl_sizeof(&lvar_view_struct_array_shape, 0));
  ASSERT_EQ(12,
            ps_lvar_array_scalar_element_size(&lvar_view_struct_array_shape));
  node_lvar_t *lvar_view_struct_array_elem =
      as_lvar(ps_node_new_array_elem_lvar_for(
          &lvar_view_struct_array_shape, 1));
  ASSERT_EQ(112, lvar_view_struct_array_elem->offset);
  ASSERT_EQ(12, ps_node_type_size((node_t *)lvar_view_struct_array_elem));
  ASSERT_EQ(PSX_TYPE_STRUCT,
            ps_node_get_type((node_t *)lvar_view_struct_array_elem)->kind);

  lvar_t lvar_view_struct_object_size = {0};
  lvar_view_struct_object_size.offset = 200;
  lvar_view_struct_object_size.size = 77;
  lvar_view_struct_object_size.elem_size = 99;
  lvar_view_struct_object_size.decl_type =
      ps_type_new_tag(TK_STRUCT, (char *)lvar_view_struct_array_tag_name,
                       (int)sizeof(lvar_view_struct_array_tag_name) - 1, 0, 12);
  ASSERT_EQ(12, ps_lvar_decl_sizeof(&lvar_view_struct_object_size, 0));
  node_lvar_t *lvar_view_struct_object_ref =
      as_lvar(ps_node_new_lvar_object_ref_for(&lvar_view_struct_object_size));
  ASSERT_EQ(12, ps_node_type_size((node_t *)lvar_view_struct_object_ref));
  ASSERT_EQ(PSX_TYPE_STRUCT,
            ps_node_get_type((node_t *)lvar_view_struct_object_ref)->kind);

  lvar_t lvar_view_const_array_addr = {0};
  lvar_view_const_array_addr.size = 8;
  lvar_view_const_array_addr.elem_size = 4;
  psx_type_t *lvar_view_const_array_leaf =
      ps_type_new_integer(TK_INT, 4, 0);
  lvar_view_const_array_leaf->is_const_qualified = 1;
  lvar_view_const_array_leaf->is_volatile_qualified = 1;
  lvar_view_const_array_addr.decl_type =
      ps_type_new_array(lvar_view_const_array_leaf, 2, 8, 4, 0);
  node_t *lvar_view_const_array_addr_node =
      ps_node_new_lvar_array_addr_for(&lvar_view_const_array_addr, 0);
  ASSERT_TRUE(ps_node_pointee_is_const_qualified(
      lvar_view_const_array_addr_node));
  ASSERT_TRUE(ps_node_pointee_is_volatile_qualified(
      lvar_view_const_array_addr_node));

  const char lvar_view_tag_name[] = "__tm_lvar_view_tag";
  lvar_t lvar_view_tag_ptr = {0};
  lvar_view_tag_ptr.decl_type = ps_type_new_pointer(
      ps_type_new_array(
          ps_type_new_tag(TK_STRUCT, (char *)lvar_view_tag_name,
                           (int)sizeof(lvar_view_tag_name) - 1, 0, 12),
          2, 24, 12, 0),
      24);
  ASSERT_TRUE(ps_lvar_is_tag_pointer(&lvar_view_tag_ptr));
  ASSERT_EQ(TK_STRUCT, ps_lvar_tag_kind(&lvar_view_tag_ptr));

  lvar_t lvar_node_scalar_sync = {0};
  lvar_node_scalar_sync.size = 4;
  lvar_node_scalar_sync.elem_size = 4;
  lvar_node_scalar_sync.decl_type =
      ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8);
  node_lvar_t *lvar_node_scalar_sync_node =
      as_lvar(ps_node_new_lvar_for(&lvar_node_scalar_sync));
  ASSERT_EQ(8, ps_node_type_size((node_t *)lvar_node_scalar_sync_node));
  ASSERT_EQ(PSX_TYPE_FLOAT,
            ps_node_get_type((node_t *)lvar_node_scalar_sync_node)->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            ps_node_get_type((node_t *)lvar_node_scalar_sync_node)->fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            ps_node_pointee_fp_kind((node_t *)lvar_node_scalar_sync_node));
  ASSERT_TRUE(!ps_node_pointee_is_bool((node_t *)lvar_node_scalar_sync_node));
  ASSERT_TRUE(!ps_node_pointee_is_unsigned(
      (node_t *)lvar_node_scalar_sync_node));
  lvar_t lvar_materialized_scalar_stale_tag = lvar_node_scalar_sync;
  lvar_materialized_scalar_stale_tag.decl_type = NULL;
  psx_type_t *lvar_materialized_scalar_type =
      ps_lvar_get_decl_type(&lvar_materialized_scalar_stale_tag);
  ASSERT_TRUE(lvar_materialized_scalar_type == NULL);

  const char lvar_node_tag_name[] = "__tm_lvar_node_tag";
  lvar_t lvar_node_tag_sync = {0};
  lvar_node_tag_sync.size = 4;
  lvar_node_tag_sync.elem_size = 4;
  lvar_node_tag_sync.decl_type =
      ps_type_new_tag(TK_STRUCT, (char *)lvar_node_tag_name,
                       (int)sizeof(lvar_node_tag_name) - 1, 0, 12);
  node_lvar_t *lvar_node_tag_sync_node =
      as_lvar(ps_node_new_lvar_for(&lvar_node_tag_sync));
  ASSERT_EQ(12, ps_node_type_size((node_t *)lvar_node_tag_sync_node));
  ASSERT_EQ(PSX_TYPE_STRUCT,
            ps_node_get_type((node_t *)lvar_node_tag_sync_node)->kind);

  lvar_t lvar_array_elem_scalar_sync = {0};
  lvar_array_elem_scalar_sync.size = 16;
  lvar_array_elem_scalar_sync.elem_size = 8;
  lvar_array_elem_scalar_sync.decl_type =
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 2, 8, 4, 0);
  node_lvar_t *lvar_array_elem_scalar_sync_node =
      as_lvar(ps_node_new_array_elem_lvar_for(&lvar_array_elem_scalar_sync, 1));
  ASSERT_EQ(4, ps_node_type_size((node_t *)lvar_array_elem_scalar_sync_node));
  ASSERT_EQ(4, lvar_array_elem_scalar_sync_node->offset);
  ASSERT_EQ(PSX_TYPE_INTEGER,
            ps_node_get_type((node_t *)lvar_array_elem_scalar_sync_node)->kind);

  lvar_t lvar_array_elem_pointer_sync = {0};
  lvar_array_elem_pointer_sync.size = 16;
  lvar_array_elem_pointer_sync.elem_size = 8;
  lvar_array_elem_pointer_sync.decl_type = ps_type_new_array(
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 4), 2, 16, 8, 0);
  node_lvar_t *lvar_array_elem_pointer_sync_node =
      as_lvar(ps_node_new_array_elem_lvar_for(
          &lvar_array_elem_pointer_sync, 1));
  ASSERT_EQ(8, ps_node_type_size((node_t *)lvar_array_elem_pointer_sync_node));
  ASSERT_EQ(8, lvar_array_elem_pointer_sync_node->offset);
  ASSERT_TRUE(ps_node_is_pointer((node_t *)lvar_array_elem_pointer_sync_node));
  ASSERT_EQ(4, ps_node_deref_size((node_t *)lvar_array_elem_pointer_sync_node));
  ASSERT_EQ(PSX_TYPE_INTEGER,
            ps_node_get_type((node_t *)lvar_array_elem_pointer_sync_node)
                ->base->kind);

  tag_member_info_t member_scalar_decl_type_wins = {0};
  member_scalar_decl_type_wins.offset = 4;
  member_scalar_decl_type_wins.type_size = 8;
  member_scalar_decl_type_wins.tag_kind = TK_STRUCT;
  member_scalar_decl_type_wins.tag_name = (char *)stale_node_tag_name;
  member_scalar_decl_type_wins.tag_len = (int)sizeof(stale_node_tag_name) - 1;
  member_scalar_decl_type_wins.is_tag_pointer = 1;
  member_scalar_decl_type_wins.decl_type = ps_type_new_integer(TK_INT, 4, 0);
  lvar_t member_scalar_owner = {0};
  member_scalar_owner.offset = 32;
  node_lvar_t *member_scalar_decl_type_node =
      as_lvar(ps_node_new_tag_member_lvar_ref_for(
          &member_scalar_owner, member_scalar_decl_type_wins.offset,
          &member_scalar_decl_type_wins));
  ASSERT_EQ(4, ps_node_type_size((node_t *)member_scalar_decl_type_node));
  ASSERT_EQ(PSX_TYPE_INTEGER,
            ps_node_get_type((node_t *)member_scalar_decl_type_node)->kind);

  tag_member_info_t member_const_owner_info = {0};
  member_const_owner_info.offset = 0;
  member_const_owner_info.type_size = 4;
  member_const_owner_info.decl_type = ps_type_new_integer(TK_INT, 4, 0);
  lvar_t member_const_owner = {0};
  member_const_owner.offset = 48;
  member_const_owner.decl_type =
      ps_type_new_tag(TK_STRUCT, (char *)stale_node_tag_name,
                       (int)sizeof(stale_node_tag_name) - 1, 0, 4);
  member_const_owner.decl_type->is_const_qualified = 1;
  member_const_owner.decl_type->is_volatile_qualified = 1;
  node_lvar_t *member_const_owner_node =
      as_lvar(ps_node_new_tag_member_lvar_ref_for(
          &member_const_owner, member_const_owner_info.offset,
          &member_const_owner_info));
  ASSERT_TRUE(ps_node_get_type((node_t *)member_const_owner_node) != NULL);
  ASSERT_TRUE(ps_node_get_type((node_t *)member_const_owner_node)
                  ->is_const_qualified);
  ASSERT_TRUE(ps_node_get_type((node_t *)member_const_owner_node)
                  ->is_volatile_qualified);
  expect_const_assign_fail_for_node((node_t *)member_const_owner_node);

  tag_member_info_t member_pointer_decl_type_wins = {0};
  member_pointer_decl_type_wins.offset = 8;
  member_pointer_decl_type_wins.type_size = 8;
  member_pointer_decl_type_wins.deref_size = 8;
  member_pointer_decl_type_wins.fp_kind = TK_FLOAT_KIND_DOUBLE;
  member_pointer_decl_type_wins.is_bool = 1;
  member_pointer_decl_type_wins.is_unsigned = 1;
  member_pointer_decl_type_wins.tag_kind = TK_STRUCT;
  member_pointer_decl_type_wins.tag_name = (char *)stale_node_tag_name;
  member_pointer_decl_type_wins.tag_len = (int)sizeof(stale_node_tag_name) - 1;
  member_pointer_decl_type_wins.is_tag_pointer = 1;
  member_pointer_decl_type_wins.decl_type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 4);
  node_t *member_pointer_decl_type_node =
      ps_node_new_tag_member_deref_for(
          ps_node_new_num(0), ps_node_new_lvar_for(&member_scalar_owner),
          &member_pointer_decl_type_wins);
  ASSERT_TRUE(ps_node_is_pointer(member_pointer_decl_type_node));
  ASSERT_EQ(8, ps_node_type_size(member_pointer_decl_type_node));
  ASSERT_EQ(4, ps_node_deref_size(member_pointer_decl_type_node));
  ASSERT_EQ(TK_FLOAT_KIND_NONE, member_pointer_decl_type_node->fp_kind);
  ASSERT_EQ(0, ps_node_integer_value_is_unsigned(
                   member_pointer_decl_type_node));
  ASSERT_EQ(PSX_TYPE_POINTER,
            ps_node_get_type(member_pointer_decl_type_node)->kind);
  ASSERT_EQ(PSX_TYPE_INTEGER,
            ps_node_get_type(member_pointer_decl_type_node)->base->kind);

  lvar_t lvar_node_ptr_scalar_sync = {0};
  lvar_node_ptr_scalar_sync.size = 8;
  lvar_node_ptr_scalar_sync.elem_size = 8;
  lvar_node_ptr_scalar_sync.decl_type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 4);
  node_lvar_t *lvar_node_ptr_scalar_sync_node =
      as_lvar(ps_node_new_lvar_for(&lvar_node_ptr_scalar_sync));
  ASSERT_TRUE(ps_node_is_pointer((node_t *)lvar_node_ptr_scalar_sync_node));
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            ps_node_pointee_fp_kind((node_t *)lvar_node_ptr_scalar_sync_node));
  ASSERT_TRUE(!ps_node_pointee_is_bool(
      (node_t *)lvar_node_ptr_scalar_sync_node));
  ASSERT_TRUE(!ps_node_pointee_is_unsigned(
      (node_t *)lvar_node_ptr_scalar_sync_node));

  lvar_t lvar_node_ptr_bool_sync = {0};
  lvar_node_ptr_bool_sync.size = 8;
  lvar_node_ptr_bool_sync.elem_size = 4;
  lvar_node_ptr_bool_sync.decl_type =
      ps_type_new_pointer(ps_type_new_integer(TK_BOOL, 1, 0), 1);
  node_lvar_t *lvar_node_ptr_bool_sync_node =
      as_lvar(ps_node_new_lvar_for(&lvar_node_ptr_bool_sync));
  ASSERT_TRUE(ps_node_is_pointer((node_t *)lvar_node_ptr_bool_sync_node));
  ASSERT_TRUE(ps_node_pointee_is_bool((node_t *)lvar_node_ptr_bool_sync_node));
  ASSERT_TRUE(!ps_node_pointee_is_unsigned(
      (node_t *)lvar_node_ptr_bool_sync_node));

  lvar_t lvar_node_ptr_unsigned_sync = {0};
  lvar_node_ptr_unsigned_sync.size = 8;
  lvar_node_ptr_unsigned_sync.elem_size = 4;
  lvar_node_ptr_unsigned_sync.decl_type =
      ps_type_new_pointer(ps_type_new_integer(TK_UNSIGNED, 4, 1), 4);
  node_lvar_t *lvar_node_ptr_unsigned_sync_node =
      as_lvar(ps_node_new_lvar_for(&lvar_node_ptr_unsigned_sync));
  ASSERT_TRUE(ps_node_is_pointer((node_t *)lvar_node_ptr_unsigned_sync_node));
  ASSERT_TRUE(!ps_node_pointee_is_bool(
      (node_t *)lvar_node_ptr_unsigned_sync_node));
  ASSERT_TRUE(ps_node_pointee_is_unsigned(
      (node_t *)lvar_node_ptr_unsigned_sync_node));

  ASSERT_TRUE(indirect_double_ptr_to_array_call->type != NULL);
  psx_type_t *indirect_double_ptr_to_array_ty =
      ps_node_get_type(indirect_double_ptr_to_array_call);
  ASSERT_TRUE(indirect_double_ptr_to_array_ty != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, indirect_double_ptr_to_array_ty->kind);
  ASSERT_EQ(16, ps_type_deref_size(indirect_double_ptr_to_array_ty));
  ASSERT_TRUE(indirect_double_ptr_to_array_ty->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, indirect_double_ptr_to_array_ty->base->kind);
  ASSERT_EQ(16, ps_type_sizeof(indirect_double_ptr_to_array_ty->base));
  ASSERT_EQ(16, ps_type_pointer_view_structural_ptr_array_pointee_bytes(
                    indirect_double_ptr_to_array_ty));
  ASSERT_EQ(16, indirect_double_ptr_to_array_ty->outer_stride);
  ASSERT_EQ(0, indirect_double_ptr_to_array_ty->mid_stride);
  ASSERT_EQ(16, ps_node_deref_size(indirect_double_ptr_to_array_call));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            ps_node_pointee_fp_kind(indirect_double_ptr_to_array_call));
  ASSERT_EQ(2, indirect_double_ptr_to_array_ty->funcptr_sig.function.callable.return_shape.pointee_array.first_dim);
  ASSERT_EQ(8, indirect_double_ptr_to_array_ty->funcptr_sig.function.callable.return_shape.pointee_array.elem_size);
  lvar_t *dpa_lvar = find_func_lvar(fn, "dpa");
  ASSERT_TRUE(dpa_lvar != NULL);
  const psx_type_t *dpa_function_type =
      ps_type_find_function(dpa_lvar->decl_type);
  ASSERT_TRUE(dpa_function_type != NULL);
  ASSERT_EQ(PSX_TYPE_FUNCTION, dpa_function_type->kind);
  ASSERT_TRUE(dpa_function_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, dpa_function_type->base->kind);
  ASSERT_TRUE(dpa_function_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, dpa_function_type->base->base->kind);
  ASSERT_EQ(PSX_TYPE_FLOAT, dpa_function_type->base->base->base->kind);
  psx_decl_funcptr_sig_t dpa_lvar_sig = ps_lvar_funcptr_sig(dpa_lvar);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, dpa_lvar_sig.function.callable.return_shape.fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            dpa_lvar_sig.function.callable.return_shape.pointee_fp_kind);

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
      ps_node_get_type(indirect_explicit_row_elem);
  ASSERT_TRUE(indirect_explicit_row_elem_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, indirect_explicit_row_elem_ty->kind);
  ASSERT_EQ(4, ps_type_sizeof(indirect_explicit_row_elem_ty));

  parsed_code = parse_program_input(
      "int add(int a,int b){return a+b;} int (*ops[3])(int,int)={add,add,add}; "
      "int main(void){ int (*(*pb)[3])(int,int)=&ops; return (*pb)[0](1,2); }");
  fn = as_func(parsed_code[1]);
  lvar_t *pb_lvar = find_func_lvar(fn, "pb");
  ASSERT_TRUE(pb_lvar != NULL);
  psx_type_t *pb_type = pb_lvar->decl_type;
  ASSERT_TRUE(pb_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, pb_type->kind);
  ASSERT_TRUE(pb_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, pb_type->base->kind);
  ASSERT_TRUE(pb_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, pb_type->base->base->kind);
  ASSERT_TRUE(pb_type->base->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_FUNCTION, pb_type->base->base->base->kind);

  parsed_code = parse_program_input(
      "static int data; static void *get(void){return &data;} "
      "int main(void){int *p=(int *)get(); return p[0];}");
  fn = as_func(parsed_code[1]);
  body = as_block(fn->base.rhs);
  node_t *void_ptr_call = NULL;
  for (int i = 0; body->body[i]; i++) {
    node_t *candidate = body->body[i];
    if (candidate->kind == ND_ASSIGN && candidate->rhs &&
        candidate->rhs->kind == ND_CAST) {
      void_ptr_call = candidate->rhs->lhs;
      break;
    }
  }
  ASSERT_TRUE(void_ptr_call != NULL);
  ASSERT_EQ(ND_FUNCALL, void_ptr_call->kind);
  psx_type_t *void_ptr_call_type = ps_node_get_type(void_ptr_call);
  ASSERT_TRUE(void_ptr_call_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, void_ptr_call_type->kind);
  ASSERT_TRUE(void_ptr_call_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_VOID, void_ptr_call_type->base->kind);
  ASSERT_TRUE(!ps_node_value_is_void(void_ptr_call));
  psx_function_ret_info_t void_ptr_ret_info =
      ps_ctx_get_function_ret_info("get", 3);
  ASSERT_TRUE(void_ptr_ret_info.is_pointer);
  ASSERT_TRUE(!void_ptr_ret_info.is_void);

  parsed_code = parse_program_input(
      "typedef int (*TMFunc)(int,int); typedef TMFunc TMFuncs[3]; "
      "int tm_add(int a,int b){return a+b;} "
      "int main(void){TMFuncs ops={tm_add,tm_add,tm_add}; TMFuncs *pa=&ops; "
      "return (*pa)[0](1,2);}");
  psx_typedef_info_t tm_func_info = {0};
  ASSERT_TRUE(ps_ctx_find_typedef_name("TMFunc", 6, &tm_func_info));
  ASSERT_TRUE(ps_decl_funcptr_sig_has_payload(
      ps_ctx_typedef_funcptr_sig(&tm_func_info)));
  psx_typedef_info_t tm_funcs_info = {0};
  ASSERT_TRUE(ps_ctx_find_typedef_name("TMFuncs", 7, &tm_funcs_info));
  ASSERT_TRUE(ps_decl_funcptr_sig_has_payload(
      ps_ctx_typedef_funcptr_sig(&tm_funcs_info)));
  ASSERT_TRUE(ps_type_find_function(
      ps_ctx_typedef_decl_type(&tm_funcs_info)) != NULL);
  fn = as_func(parsed_code[1]);
  lvar_t *tm_ops = find_func_lvar(fn, "ops");
  lvar_t *tm_pa = find_func_lvar(fn, "pa");
  ASSERT_TRUE(tm_ops != NULL);
  ASSERT_TRUE(tm_pa != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, tm_ops->decl_type->kind);
  ASSERT_TRUE(tm_ops->decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, tm_ops->decl_type->base->kind);
  ASSERT_TRUE(ps_decl_funcptr_sig_has_payload(ps_lvar_funcptr_sig(tm_ops)));
  ASSERT_TRUE(ps_type_find_function(tm_ops->decl_type) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, tm_pa->decl_type->kind);
  ASSERT_TRUE(tm_pa->decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, tm_pa->decl_type->base->kind);
  ASSERT_TRUE(tm_pa->decl_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, tm_pa->decl_type->base->base->kind);
  ASSERT_TRUE(ps_type_find_function(tm_pa->decl_type) != NULL);

  parsed_code = parse_program_input(
      "int tm_local_add(int a,int b){return a+b;} "
      "int main(void){typedef int (*TMLocalFunc)(int,int); "
      "typedef TMLocalFunc TMLocalFuncs[2]; "
      "typedef int *TMLocalArrayPtr[3]; typedef int (*TMLocalPtrArray)[3]; "
      "TMLocalFuncs ops={tm_local_add,tm_local_add}; "
      "TMLocalFuncs matrix[2]; "
      "TMLocalFuncs *pa=&ops; TMLocalArrayPtr array_ptrs; "
      "TMLocalPtrArray ptr_array; return (*pa)[0](1,2);}");
  fn = as_func(parsed_code[1]);
  lvar_t *tm_local_ops = find_func_lvar(fn, "ops");
  lvar_t *tm_local_matrix = find_func_lvar(fn, "matrix");
  lvar_t *tm_local_pa = find_func_lvar(fn, "pa");
  lvar_t *tm_local_array_ptrs = find_func_lvar(fn, "array_ptrs");
  lvar_t *tm_local_ptr_array = find_func_lvar(fn, "ptr_array");
  ASSERT_TRUE(tm_local_ops != NULL);
  ASSERT_TRUE(tm_local_matrix != NULL);
  ASSERT_TRUE(tm_local_pa != NULL);
  ASSERT_TRUE(tm_local_array_ptrs != NULL);
  ASSERT_TRUE(tm_local_ptr_array != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, tm_local_ops->decl_type->kind);
  ASSERT_EQ(PSX_TYPE_POINTER, tm_local_ops->decl_type->base->kind);
  ASSERT_TRUE(ps_type_find_function(tm_local_ops->decl_type) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, tm_local_pa->decl_type->kind);
  ASSERT_EQ(PSX_TYPE_ARRAY, tm_local_pa->decl_type->base->kind);
  ASSERT_EQ(PSX_TYPE_POINTER, tm_local_pa->decl_type->base->base->kind);
  ASSERT_TRUE(ps_type_find_function(tm_local_pa->decl_type) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, tm_local_matrix->decl_type->kind);
  ASSERT_EQ(2, tm_local_matrix->decl_type->array_len);
  ASSERT_EQ(PSX_TYPE_ARRAY, tm_local_matrix->decl_type->base->kind);
  ASSERT_EQ(2, tm_local_matrix->decl_type->base->array_len);
  ASSERT_EQ(PSX_TYPE_POINTER,
            tm_local_matrix->decl_type->base->base->kind);
  ASSERT_TRUE(ps_type_find_function(tm_local_matrix->decl_type) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, tm_local_array_ptrs->decl_type->kind);
  ASSERT_EQ(3, tm_local_array_ptrs->decl_type->array_len);
  ASSERT_EQ(PSX_TYPE_POINTER, tm_local_array_ptrs->decl_type->base->kind);
  ASSERT_EQ(PSX_TYPE_INTEGER,
            tm_local_array_ptrs->decl_type->base->base->kind);
  ASSERT_EQ(PSX_TYPE_POINTER, tm_local_ptr_array->decl_type->kind);
  ASSERT_EQ(PSX_TYPE_ARRAY, tm_local_ptr_array->decl_type->base->kind);
  ASSERT_EQ(3, tm_local_ptr_array->decl_type->base->array_len);
  ASSERT_EQ(PSX_TYPE_INTEGER,
            tm_local_ptr_array->decl_type->base->base->kind);

  parsed_code = parse_program_input(
      "struct TM695 { double *dp; double (*fp)(void); }; int main(void){ return 0; }");
  (void)parsed_code;
  tag_member_info_t dp_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info(TK_STRUCT, "TM695", 5, "dp", 2, &dp_info));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, dp_info.fp_kind);
  ASSERT_EQ(0, dp_info.is_funcptr);
  ASSERT_TRUE(!ps_decl_funcptr_sig_has_payload(dp_info.funcptr_sig));
  psx_decl_funcptr_sig_t dp_sig = ps_ctx_tag_member_funcptr_sig(&dp_info);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, dp_sig.function.callable.return_shape.fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, dp_sig.function.callable.return_shape.pointee_fp_kind);
  ASSERT_TRUE(!ps_decl_funcptr_sig_has_payload(dp_sig));
  dp_info.funcptr_sig.function.callable.return_shape.int_width = 8;
  dp_info.is_funcptr = 1;
  dp_sig = ps_ctx_tag_member_funcptr_sig(&dp_info);
  ASSERT_TRUE(!ps_decl_funcptr_sig_has_payload(dp_sig));

  node_t canonical_int_ptr = {0};
  canonical_int_ptr.kind = ND_LVAR;
  canonical_int_ptr.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 4);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, ps_node_pointee_fp_kind(&canonical_int_ptr));

  tag_member_info_t fp_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info(TK_STRUCT, "TM695", 5, "fp", 2, &fp_info));
  ASSERT_EQ(TK_FLOAT_KIND_NONE, fp_info.fp_kind);
  ASSERT_EQ(1, fp_info.is_funcptr);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, fp_info.funcptr_sig.function.callable.return_shape.fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, fp_info.funcptr_sig.function.callable.return_shape.pointee_fp_kind);
  psx_decl_funcptr_sig_t fp_sig = ps_ctx_tag_member_funcptr_sig(&fp_info);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, fp_sig.function.callable.return_shape.fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, fp_sig.function.callable.return_shape.pointee_fp_kind);
  ASSERT_TRUE(ps_decl_funcptr_sig_has_payload(fp_sig));
  ASSERT_TRUE(fp_info.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, fp_info.decl_type->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, fp_info.decl_type->funcptr_sig.function.callable.return_shape.fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, fp_info.decl_type->funcptr_sig.function.callable.return_shape.pointee_fp_kind);
  fp_info.funcptr_sig.function.callable.return_shape.fp_kind = TK_FLOAT_KIND_NONE;
  fp_info.funcptr_sig.function.callable.return_shape.int_width = 4;
  psx_decl_funcptr_sig_t fp_info_canon_sig = ps_ctx_tag_member_funcptr_sig(&fp_info);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, fp_info_canon_sig.function.callable.return_shape.fp_kind);
  ASSERT_EQ(0, fp_info_canon_sig.function.callable.return_shape.int_width);

  tag_member_info_t partial_sig_member = {0};
  partial_sig_member.type_size = 8;
  partial_sig_member.deref_size = 8;
  partial_sig_member.is_tag_pointer = 1;
  psx_decl_funcptr_sig_t partial_member_sig = {0};
  partial_member_sig.function.callable.signature.param_fp_mask = 1u;
  partial_member_sig.function.callable.return_shape.fp_kind = TK_FLOAT_KIND_DOUBLE;
  partial_sig_member.decl_type = ps_type_new_funcptr(partial_member_sig, 1);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            ps_type_funcptr_signature(partial_sig_member.decl_type)
                .function.callable.return_shape.fp_kind);
  psx_type_t *partial_sig_function = (psx_type_t *)ps_type_find_function(
      partial_sig_member.decl_type);
  ASSERT_TRUE(partial_sig_function != NULL);
  partial_sig_function->funcptr_sig.function.callable.return_shape.fp_kind =
      TK_FLOAT_KIND_NONE;
  partial_sig_function->funcptr_sig.function.callable.return_shape.int_width = 4;
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            ps_type_funcptr_signature(partial_sig_member.decl_type)
                .function.callable.return_shape.fp_kind);
  ASSERT_EQ(0, ps_type_funcptr_signature(partial_sig_member.decl_type)
                   .function.callable.return_shape.int_width);
  partial_member_sig.function.callable.return_shape.fp_kind = TK_FLOAT_KIND_NONE;
  partial_member_sig.function.callable.return_shape.int_width = 4;
  psx_type_t *partial_sig_member_decl_type = partial_sig_member.decl_type;
  partial_sig_member.funcptr_sig = ps_decl_funcptr_sig_clone(partial_member_sig);
  partial_sig_member.is_funcptr = 1;
  ASSERT_TRUE(partial_sig_member.decl_type == partial_sig_member_decl_type);
  ASSERT_EQ(0, ps_type_funcptr_signature(partial_sig_member.decl_type)
                   .function.callable.return_shape.int_width);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            ps_type_funcptr_signature(partial_sig_member.decl_type)
                .function.callable.return_shape.fp_kind);
  node_t *partial_sig_node = ps_node_new_tag_member_deref_for(
      ps_node_new_num(0), ps_node_new_num(0), &partial_sig_member);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            ps_node_funcptr_sig(partial_sig_node)
                .function.callable.return_shape.fp_kind);
  ASSERT_EQ(1u, ps_node_funcptr_param_fp_mask(partial_sig_node));

  parsed_code = parse_program_input(
      "struct TM695F { int (*fns[2])(int, int); }; int main(void){ return 0; }");
  (void)parsed_code;
  tag_member_info_t fns_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info(TK_STRUCT, "TM695F", 6, "fns", 3, &fns_info));
  ASSERT_TRUE(fns_info.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, fns_info.decl_type->kind);
  ASSERT_EQ(2, fns_info.decl_type->array_len);
  ASSERT_EQ(8, fns_info.decl_type->elem_size);
  ASSERT_TRUE(fns_info.decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, fns_info.decl_type->base->kind);
  ASSERT_TRUE(fns_info.decl_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_FUNCTION, fns_info.decl_type->base->base->kind);
  ASSERT_TRUE(fns_info.decl_type->base->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, fns_info.decl_type->base->base->base->kind);
  ASSERT_EQ(4, fns_info.decl_type->base->funcptr_sig.function.callable.return_shape.int_width);
  ASSERT_TRUE(fns_info.decl_type->base->funcptr_sig.function.callable.signature.param_int_mask != 0);

  parsed_code = parse_program_input(
      "struct TM695Ops { double (*d)(double); }; "
      "struct TM695Holder { struct TM695Ops ops[2]; }; "
      "double __tm695_d(double x){ return x; } "
      "int main(void){ struct TM695Holder h; h.ops[0].d = __tm695_d; return 0; }");
  (void)parsed_code;
  tag_member_info_t ops_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info(TK_STRUCT, "TM695Holder", 11,
                                           "ops", 3, &ops_info));
  ASSERT_TRUE(ops_info.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ops_info.decl_type->kind);
  ASSERT_TRUE(ops_info.decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_STRUCT, ops_info.decl_type->base->kind);
  ASSERT_EQ(TK_STRUCT, ops_info.decl_type->base->tag_kind);
  ASSERT_EQ(8, ops_info.decl_type->base->tag_len);

  parsed_code = parse_program_input(
      "typedef int (*__tm695RowPtr)[3]; "
      "struct TM695RowHolder { struct { __tm695RowPtr rows[2]; }; int z; }; "
      "int main(void){ int a[2][3]; struct TM695RowHolder h = {.rows = {a, a}}; "
      "return h.rows[0][1][2]; }");
  (void)parsed_code;
  tag_member_info_t rows_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info(TK_STRUCT, "TM695RowHolder", 14,
                                           "rows", 4, &rows_info));
  ASSERT_TRUE(rows_info.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, rows_info.decl_type->kind);
  ASSERT_EQ(12, ps_type_pointer_view_structural_ptr_array_pointee_bytes(
                    rows_info.decl_type));
  ASSERT_TRUE(rows_info.decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, rows_info.decl_type->base->kind);
  ASSERT_EQ(12, rows_info.decl_type->base->deref_size);
  ASSERT_EQ(12, rows_info.decl_type->base->ptr_array_pointee_bytes);
  ASSERT_TRUE(rows_info.decl_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, rows_info.decl_type->base->base->kind);
  ASSERT_EQ(3, rows_info.decl_type->base->base->array_len);
  ASSERT_EQ(4, rows_info.decl_type->base->base->elem_size);

  parsed_code = parse_program_input(
      "struct TMPtrRows { int (*p[2])[3]; }; int main(void){return 0;}");
  (void)parsed_code;
  tag_member_info_t ptr_rows_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info(
      TK_STRUCT, "TMPtrRows", 9, "p", 1, &ptr_rows_info));
  ASSERT_TRUE(ptr_rows_info.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ptr_rows_info.decl_type->kind);
  ASSERT_EQ(2, ptr_rows_info.decl_type->array_len);
  ASSERT_TRUE(ptr_rows_info.decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ptr_rows_info.decl_type->base->kind);
  ASSERT_TRUE(ptr_rows_info.decl_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ptr_rows_info.decl_type->base->base->kind);
  ASSERT_EQ(3, ptr_rows_info.decl_type->base->base->array_len);
  ASSERT_TRUE(ptr_rows_info.decl_type->base->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER,
            ptr_rows_info.decl_type->base->base->base->kind);
  ASSERT_EQ(12, ps_type_sizeof(ptr_rows_info.decl_type->base->base));

  parsed_code = parse_program_input(
      "struct __tm_member_ptrarr { int (*p)[3]; }; "
      "int main(void){ struct __tm_member_ptrarr h; return 0; }");
  fn = as_func(parsed_code[0]);
  lvar_t *member_ptrarr_h = find_func_lvar(fn, "h");
  ASSERT_TRUE(member_ptrarr_h != NULL);
  tag_member_info_t member_ptrarr_p_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info(TK_STRUCT, "__tm_member_ptrarr", 18,
                                           "p", 1, &member_ptrarr_p_info));
  node_t *member_ptrarr_p_node = ps_node_new_tag_member_lvar_ref_for(
      member_ptrarr_h, member_ptrarr_p_info.offset, &member_ptrarr_p_info);
  ASSERT_TRUE(ps_node_is_pointer(member_ptrarr_p_node));
  ASSERT_EQ(12, ps_node_deref_size(member_ptrarr_p_node));
  ASSERT_EQ(4, ps_node_base_deref_size(member_ptrarr_p_node));
  ASSERT_EQ(12, ps_node_ptr_array_pointee_bytes(member_ptrarr_p_node));
  int member_ptrarr_inner = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(
      member_ptrarr_p_node, &member_ptrarr_inner, NULL, NULL, NULL));
  ASSERT_EQ(4, member_ptrarr_inner);
  ASSERT_TRUE(ps_node_get_type(member_ptrarr_p_node) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(member_ptrarr_p_node)->kind);
  node_t *member_ptrarr_p_deref = ps_node_new_tag_member_deref_for(
      ps_node_new_num(0), ps_node_new_lvar_for(member_ptrarr_h),
      &member_ptrarr_p_info);
  ASSERT_TRUE(ps_node_is_pointer(member_ptrarr_p_deref));
  ASSERT_EQ(12, ps_node_deref_size(member_ptrarr_p_deref));
  ASSERT_EQ(4, ps_node_base_deref_size(member_ptrarr_p_deref));
  ASSERT_EQ(12, ps_node_ptr_array_pointee_bytes(member_ptrarr_p_deref));
  ASSERT_TRUE(ps_node_get_type(member_ptrarr_p_deref) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(member_ptrarr_p_deref)->kind);
  ASSERT_EQ(12, ps_tag_member_decl_ptr_array_pointee_bytes(&member_ptrarr_p_info));
  ASSERT_EQ(4, ps_tag_member_decl_ptr_array_pointee_elem_size(&member_ptrarr_p_info));

  parsed_code = parse_program_input(
      "typedef int *__tm_member_IP; "
      "struct __tm_member_ip_ptrarr { __tm_member_IP (*p)[3]; }; "
      "int main(void){ struct __tm_member_ip_ptrarr h; return 0; }");
  fn = as_func(parsed_code[0]);
  lvar_t *member_ip_ptrarr_h = find_func_lvar(fn, "h");
  ASSERT_TRUE(member_ip_ptrarr_h != NULL);
  tag_member_info_t member_ip_ptrarr_p_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info(
      TK_STRUCT, "__tm_member_ip_ptrarr",
      (int)sizeof("__tm_member_ip_ptrarr") - 1, "p", 1,
      &member_ip_ptrarr_p_info));
  ASSERT_TRUE(member_ip_ptrarr_p_info.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, member_ip_ptrarr_p_info.decl_type->kind);
  ASSERT_EQ(24, member_ip_ptrarr_p_info.decl_type->deref_size);
  ASSERT_EQ(24, member_ip_ptrarr_p_info.decl_type->ptr_array_pointee_bytes);
  ASSERT_TRUE(member_ip_ptrarr_p_info.decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, member_ip_ptrarr_p_info.decl_type->base->kind);
  ASSERT_EQ(3, member_ip_ptrarr_p_info.decl_type->base->array_len);
  ASSERT_EQ(8, member_ip_ptrarr_p_info.decl_type->base->elem_size);
  ASSERT_TRUE(member_ip_ptrarr_p_info.decl_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, member_ip_ptrarr_p_info.decl_type->base->base->kind);
  ASSERT_EQ(4, member_ip_ptrarr_p_info.decl_type->base->base->deref_size);
  ASSERT_EQ(24, ps_tag_member_decl_ptr_array_pointee_bytes(
                    &member_ip_ptrarr_p_info));
  node_t *member_ip_ptrarr_p_node = ps_node_new_tag_member_lvar_ref_for(
      member_ip_ptrarr_h, member_ip_ptrarr_p_info.offset,
      &member_ip_ptrarr_p_info);
  ASSERT_TRUE(ps_node_is_pointer(member_ip_ptrarr_p_node));
  ASSERT_EQ(24, ps_node_deref_size(member_ip_ptrarr_p_node));
  ASSERT_EQ(4, ps_node_base_deref_size(member_ip_ptrarr_p_node));
  int member_ip_ptrarr_inner = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(
      member_ip_ptrarr_p_node, &member_ip_ptrarr_inner, NULL, NULL, NULL));
  ASSERT_EQ(8, member_ip_ptrarr_inner);
  node_t *member_ip_ptrarr_row =
      ps_node_new_unary_deref_for(member_ip_ptrarr_p_node);
  ASSERT_EQ(24, ps_node_type_size(member_ip_ptrarr_row));
  ASSERT_EQ(8, ps_node_deref_size(member_ip_ptrarr_row));
  node_t *member_ip_ptrarr_elem = ps_node_new_subscript_deref_for(
      member_ip_ptrarr_row,
      member_ip_ptrarr_row->lhs ? member_ip_ptrarr_row->lhs
                                : member_ip_ptrarr_row,
      ps_node_new_num(0), ps_node_deref_size(member_ip_ptrarr_row),
      0, 0, NULL, 0);
  ASSERT_EQ(8, ps_node_type_size(member_ip_ptrarr_elem));
  ASSERT_EQ(4, ps_node_deref_size(member_ip_ptrarr_elem));
  ASSERT_TRUE(ps_node_is_pointer(member_ip_ptrarr_elem));

  parsed_code = parse_program_input(
      "struct __tm_member_pp { int **pp; }; "
      "int main(void){ struct __tm_member_pp h; return 0; }");
  (void)parsed_code;
  tag_member_info_t member_pp_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info(
      TK_STRUCT, "__tm_member_pp", (int)sizeof("__tm_member_pp") - 1,
      "pp", 2, &member_pp_info));
  ASSERT_TRUE(member_pp_info.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, member_pp_info.decl_type->kind);
  ASSERT_EQ(2, ps_type_pointer_view_structural_qual_levels(member_pp_info.decl_type));
  ASSERT_TRUE(member_pp_info.decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, member_pp_info.decl_type->base->kind);
  ASSERT_EQ(4, ps_type_deref_size(member_pp_info.decl_type->base));
  ASSERT_TRUE(member_pp_info.decl_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, member_pp_info.decl_type->base->base->kind);

  tag_member_info_t member_ptrarr_p_stale_info = member_ptrarr_p_info;
  member_ptrarr_p_stale_info.deref_size = 1;
  member_ptrarr_p_stale_info.outer_stride = 0;
  member_ptrarr_p_stale_info.mid_stride = 0;
  member_ptrarr_p_stale_info.ptr_array_pointee_bytes = 0;
  member_ptrarr_p_stale_info.arr_ndim = 0;
  for (int i = 0; i < 8; i++) member_ptrarr_p_stale_info.arr_dims[i] = 0;
  ASSERT_EQ(12, ps_tag_member_decl_ptr_array_pointee_bytes(
                    &member_ptrarr_p_stale_info));
  ASSERT_EQ(4, ps_tag_member_decl_ptr_array_pointee_elem_size(
                   &member_ptrarr_p_stale_info));
  node_t *member_ptrarr_p_stale_deref = ps_node_new_tag_member_deref_for(
      ps_node_new_num(0), ps_node_new_lvar_for(member_ptrarr_h),
      &member_ptrarr_p_stale_info);
  ASSERT_EQ(12, ps_node_deref_size(member_ptrarr_p_stale_deref));
  ASSERT_EQ(4, ps_node_base_deref_size(member_ptrarr_p_stale_deref));
  tag_member_info_t member_ptrarr_p_stale_hi_info = member_ptrarr_p_info;
  member_ptrarr_p_stale_hi_info.deref_size = 1;
  member_ptrarr_p_stale_hi_info.outer_stride = 96;
  member_ptrarr_p_stale_hi_info.mid_stride = 48;
  member_ptrarr_p_stale_hi_info.ptr_array_pointee_bytes = 96;
  member_ptrarr_p_stale_hi_info.arr_ndim = 0;
  for (int i = 0; i < 8; i++) member_ptrarr_p_stale_hi_info.arr_dims[i] = 0;
  node_t *member_ptrarr_p_stale_hi_node = ps_node_new_tag_member_lvar_ref_for(
      member_ptrarr_h, member_ptrarr_p_stale_hi_info.offset,
      &member_ptrarr_p_stale_hi_info);
  ASSERT_TRUE(ps_node_is_pointer(member_ptrarr_p_stale_hi_node));
  ASSERT_EQ(12, ps_node_deref_size(member_ptrarr_p_stale_hi_node));
  ASSERT_EQ(4, ps_node_base_deref_size(member_ptrarr_p_stale_hi_node));
  ASSERT_EQ(12, ps_node_ptr_array_pointee_bytes(member_ptrarr_p_stale_hi_node));
  node_t *member_ptrarr_p_stale_hi_deref = ps_node_new_tag_member_deref_for(
      ps_node_new_num(0), ps_node_new_lvar_for(member_ptrarr_h),
      &member_ptrarr_p_stale_hi_info);
  ASSERT_TRUE(ps_node_is_pointer(member_ptrarr_p_stale_hi_deref));
  ASSERT_EQ(12, ps_node_deref_size(member_ptrarr_p_stale_hi_deref));
  ASSERT_EQ(4, ps_node_base_deref_size(member_ptrarr_p_stale_hi_deref));
  ASSERT_EQ(12, ps_node_ptr_array_pointee_bytes(
                    member_ptrarr_p_stale_hi_deref));

  parsed_code = parse_program_input(
      "struct __tm_member_arr { int a[2]; }; "
      "int main(void){ struct __tm_member_arr h; return h.a[0]; }");
  fn = as_func(parsed_code[0]);
  lvar_t *member_arr_h = find_func_lvar(fn, "h");
  ASSERT_TRUE(member_arr_h != NULL);
  tag_member_info_t member_arr_a_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info(TK_STRUCT, "__tm_member_arr", 15,
                                           "a", 1, &member_arr_a_info));
  node_t *member_arr_a_node = ps_node_new_tag_member_lvar_ref_for(
      member_arr_h, member_arr_a_info.offset, &member_arr_a_info);
  ASSERT_TRUE(ps_node_is_pointer(member_arr_a_node));
  ASSERT_EQ(8, ps_node_type_size(member_arr_a_node));
  ASSERT_EQ(4, ps_node_deref_size(member_arr_a_node));
  ASSERT_EQ(4, ps_node_base_deref_size(member_arr_a_node));
  ASSERT_TRUE(ps_node_get_type(member_arr_a_node) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(member_arr_a_node)->kind);
  node_t *member_arr_a_deref = ps_node_new_tag_member_deref_for(
      ps_node_new_num(0), ps_node_new_lvar_for(member_arr_h),
      &member_arr_a_info);
  ASSERT_TRUE(ps_node_is_pointer(member_arr_a_deref));
  ASSERT_EQ(4, ps_node_deref_size(member_arr_a_deref));
  ASSERT_EQ(8, ps_node_type_size(member_arr_a_deref));
  ASSERT_EQ(4, ps_node_base_deref_size(member_arr_a_deref));
  ASSERT_TRUE(ps_node_deref_decays_to_address(member_arr_a_deref));
  ASSERT_TRUE(ps_node_get_type(member_arr_a_deref) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(member_arr_a_deref)->kind);
  tag_member_info_t member_arr_a_stale_info = member_arr_a_info;
  member_arr_a_stale_info.type_size = 1;
  member_arr_a_stale_info.deref_size = 1;
  member_arr_a_stale_info.array_len = 0;
  member_arr_a_stale_info.outer_stride = 0;
  member_arr_a_stale_info.arr_ndim = 0;
  for (int i = 0; i < 8; i++) member_arr_a_stale_info.arr_dims[i] = 0;
  ASSERT_EQ(1, ps_tag_member_decl_array_dim_count(&member_arr_a_stale_info));
  ASSERT_EQ(2, ps_tag_member_decl_array_dim(&member_arr_a_stale_info, 0));
  ASSERT_EQ(4, ps_tag_member_decl_deref_size(&member_arr_a_stale_info));
  node_t *member_arr_a_stale_node = ps_node_new_tag_member_lvar_ref_for(
      member_arr_h, member_arr_a_stale_info.offset, &member_arr_a_stale_info);
  ASSERT_TRUE(ps_node_is_pointer(member_arr_a_stale_node));
  ASSERT_EQ(8, ps_node_type_size(member_arr_a_stale_node));
  ASSERT_EQ(4, ps_node_deref_size(member_arr_a_stale_node));
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(member_arr_a_stale_node)->kind);
  node_t *member_arr_a_stale_deref = ps_node_new_tag_member_deref_for(
      ps_node_new_num(0), ps_node_new_lvar_for(member_arr_h),
      &member_arr_a_stale_info);
  ASSERT_TRUE(ps_node_is_pointer(member_arr_a_stale_deref));
  ASSERT_EQ(8, ps_node_type_size(member_arr_a_stale_deref));
  ASSERT_EQ(4, ps_node_deref_size(member_arr_a_stale_deref));
  ASSERT_TRUE(ps_node_deref_decays_to_address(member_arr_a_stale_deref));
  tag_member_info_t member_arr_a_stale_hi_info = member_arr_a_info;
  member_arr_a_stale_hi_info.type_size = 1;
  member_arr_a_stale_hi_info.deref_size = 1;
  member_arr_a_stale_hi_info.array_len = 0;
  member_arr_a_stale_hi_info.outer_stride = 96;
  member_arr_a_stale_hi_info.mid_stride = 48;
  member_arr_a_stale_hi_info.ptr_array_pointee_bytes = 96;
  member_arr_a_stale_hi_info.arr_ndim = 2;
  for (int i = 0; i < 8; i++) member_arr_a_stale_hi_info.arr_dims[i] = 0;
  member_arr_a_stale_hi_info.arr_dims[0] = 9;
  member_arr_a_stale_hi_info.arr_dims[1] = 8;
  ASSERT_EQ(1, ps_tag_member_decl_array_dim_count(
                   &member_arr_a_stale_hi_info));
  ASSERT_EQ(2, ps_tag_member_decl_array_dim(&member_arr_a_stale_hi_info, 0));
  ASSERT_EQ(0, ps_tag_member_decl_array_dim(&member_arr_a_stale_hi_info, 1));
  node_t *member_arr_a_stale_hi_node = ps_node_new_tag_member_lvar_ref_for(
      member_arr_h, member_arr_a_stale_hi_info.offset,
      &member_arr_a_stale_hi_info);
  ASSERT_TRUE(ps_node_is_pointer(member_arr_a_stale_hi_node));
  ASSERT_EQ(8, ps_node_type_size(member_arr_a_stale_hi_node));
  ASSERT_EQ(4, ps_node_deref_size(member_arr_a_stale_hi_node));
  ASSERT_EQ(4, ps_node_base_deref_size(member_arr_a_stale_hi_node));
  ASSERT_EQ(0, ps_node_ptr_array_pointee_bytes(member_arr_a_stale_hi_node));
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(member_arr_a_stale_hi_node)->kind);
  node_t *member_arr_a_stale_hi_deref = ps_node_new_tag_member_deref_for(
      ps_node_new_num(0), ps_node_new_lvar_for(member_arr_h),
      &member_arr_a_stale_hi_info);
  ASSERT_TRUE(ps_node_is_pointer(member_arr_a_stale_hi_deref));
  ASSERT_EQ(8, ps_node_type_size(member_arr_a_stale_hi_deref));
  ASSERT_EQ(4, ps_node_deref_size(member_arr_a_stale_hi_deref));
  ASSERT_EQ(4, ps_node_base_deref_size(member_arr_a_stale_hi_deref));
  ASSERT_EQ(0, ps_node_ptr_array_pointee_bytes(
                   member_arr_a_stale_hi_deref));
  ASSERT_TRUE(ps_node_deref_decays_to_address(member_arr_a_stale_hi_deref));

  parsed_code = parse_program_input(
      "struct __tm_member_plain_ptr { int *p; }; "
      "int main(void){ struct __tm_member_plain_ptr h; return 0; }");
  fn = as_func(parsed_code[0]);
  lvar_t *member_plain_ptr_h = find_func_lvar(fn, "h");
  ASSERT_TRUE(member_plain_ptr_h != NULL);
  tag_member_info_t member_plain_ptr_p_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info(TK_STRUCT,
                                           "__tm_member_plain_ptr", 21,
                                           "p", 1,
                                           &member_plain_ptr_p_info));
  ASSERT_TRUE(member_plain_ptr_p_info.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, member_plain_ptr_p_info.decl_type->kind);
  tag_member_info_t member_plain_ptr_p_stale_info = member_plain_ptr_p_info;
  member_plain_ptr_p_stale_info.outer_stride = 96;
  member_plain_ptr_p_stale_info.mid_stride = 48;
  member_plain_ptr_p_stale_info.ptr_array_pointee_bytes = 96;
  member_plain_ptr_p_stale_info.arr_ndim = 2;
  member_plain_ptr_p_stale_info.arr_dims[0] = 9;
  member_plain_ptr_p_stale_info.arr_dims[1] = 8;
  member_plain_ptr_p_stale_info.decl_type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0), 4);
  member_plain_ptr_p_stale_info.decl_type->outer_stride = 96;
  member_plain_ptr_p_stale_info.decl_type->mid_stride = 48;
  member_plain_ptr_p_stale_info.decl_type->ptr_array_pointee_bytes = 96;
  ASSERT_EQ(0, ps_tag_member_decl_outer_stride(
                   &member_plain_ptr_p_stale_info));
  ASSERT_EQ(0, ps_tag_member_decl_mid_stride(
                   &member_plain_ptr_p_stale_info));
  ASSERT_EQ(0, ps_tag_member_decl_ptr_array_pointee_bytes(
                   &member_plain_ptr_p_stale_info));
  ASSERT_EQ(0, ps_tag_member_decl_array_dim_count(
                   &member_plain_ptr_p_stale_info));
  ASSERT_EQ(0, ps_tag_member_decl_array_dim(
                   &member_plain_ptr_p_stale_info, 0));
  ASSERT_EQ(0, ps_tag_member_decl_array_dim(
                   &member_plain_ptr_p_stale_info, 1));
  node_t *member_plain_ptr_p_node = ps_node_new_tag_member_lvar_ref_for(
      member_plain_ptr_h, member_plain_ptr_p_stale_info.offset,
      &member_plain_ptr_p_stale_info);
  ASSERT_TRUE(ps_node_is_pointer(member_plain_ptr_p_node));
  ASSERT_EQ(4, ps_node_deref_size(member_plain_ptr_p_node));
  ASSERT_EQ(4, ps_node_base_deref_size(member_plain_ptr_p_node));
  ASSERT_EQ(0, ps_node_ptr_array_pointee_bytes(member_plain_ptr_p_node));
  ASSERT_TRUE(!ps_node_pointer_stride_metadata(
      member_plain_ptr_p_node, NULL, NULL, NULL, NULL));
  node_t *member_plain_ptr_p_deref = ps_node_new_tag_member_deref_for(
      ps_node_new_num(0), ps_node_new_lvar_for(member_plain_ptr_h),
      &member_plain_ptr_p_stale_info);
  ASSERT_TRUE(ps_node_is_pointer(member_plain_ptr_p_deref));
  ASSERT_EQ(4, ps_node_deref_size(member_plain_ptr_p_deref));
  ASSERT_EQ(4, ps_node_base_deref_size(member_plain_ptr_p_deref));
  ASSERT_EQ(0, ps_node_ptr_array_pointee_bytes(member_plain_ptr_p_deref));
  ASSERT_TRUE(!ps_node_pointer_stride_metadata(
      member_plain_ptr_p_deref, NULL, NULL, NULL, NULL));
  ASSERT_TRUE(ps_node_scalar_ptr_member_lvalue(member_plain_ptr_p_deref));

  tag_member_info_t member_plain_ptr_p_flat_mismatch_info =
      member_plain_ptr_p_info;
  member_plain_ptr_p_flat_mismatch_info.type_size = 8;
  member_plain_ptr_p_flat_mismatch_info.deref_size = 4;
  member_plain_ptr_p_flat_mismatch_info.pointer_qual_levels = 1;
  member_plain_ptr_p_flat_mismatch_info.outer_stride = 16;
  member_plain_ptr_p_flat_mismatch_info.mid_stride = 8;
  member_plain_ptr_p_flat_mismatch_info.ptr_array_pointee_bytes = 16;
  member_plain_ptr_p_flat_mismatch_info.decl_type =
      ps_type_new_pointer(NULL, 8);
  member_plain_ptr_p_flat_mismatch_info.decl_type->base_deref_size = 4;
  member_plain_ptr_p_flat_mismatch_info.decl_type->outer_stride = 32;
  member_plain_ptr_p_flat_mismatch_info.decl_type->mid_stride = 16;
  member_plain_ptr_p_flat_mismatch_info.decl_type->ptr_array_pointee_bytes = 32;
  ASSERT_EQ(0, ps_tag_member_decl_outer_stride(
                   &member_plain_ptr_p_flat_mismatch_info));
  ASSERT_EQ(0, ps_tag_member_decl_mid_stride(
                   &member_plain_ptr_p_flat_mismatch_info));
  ASSERT_EQ(0, ps_tag_member_decl_ptr_array_pointee_bytes(
                   &member_plain_ptr_p_flat_mismatch_info));
  ASSERT_EQ(0, ps_tag_member_decl_pointer_qual_levels(
                   &member_plain_ptr_p_flat_mismatch_info));
  node_t *member_plain_ptr_p_flat_mismatch_node =
      ps_node_new_tag_member_lvar_ref_for(
          member_plain_ptr_h,
          member_plain_ptr_p_flat_mismatch_info.offset,
          &member_plain_ptr_p_flat_mismatch_info);
  ASSERT_TRUE(ps_node_is_pointer(member_plain_ptr_p_flat_mismatch_node));
  ASSERT_EQ(8, ps_node_deref_size(member_plain_ptr_p_flat_mismatch_node));
  ASSERT_EQ(0, ps_node_pointer_qual_levels(member_plain_ptr_p_flat_mismatch_node));
  ASSERT_EQ(0, ps_node_base_deref_size(member_plain_ptr_p_flat_mismatch_node));
  ASSERT_EQ(0, ps_node_ptr_array_pointee_bytes(
                   member_plain_ptr_p_flat_mismatch_node));
  ASSERT_TRUE(!ps_node_pointer_stride_metadata(
      member_plain_ptr_p_flat_mismatch_node, NULL, NULL, NULL, NULL));
  node_t *member_plain_ptr_p_flat_mismatch_deref =
      ps_node_new_tag_member_deref_for(
          ps_node_new_num(0), ps_node_new_lvar_for(member_plain_ptr_h),
          &member_plain_ptr_p_flat_mismatch_info);
  ASSERT_TRUE(ps_node_is_pointer(member_plain_ptr_p_flat_mismatch_deref));
  ASSERT_EQ(8, ps_node_deref_size(member_plain_ptr_p_flat_mismatch_deref));
  ASSERT_EQ(0, ps_node_pointer_qual_levels(member_plain_ptr_p_flat_mismatch_deref));
  ASSERT_EQ(0, ps_node_base_deref_size(member_plain_ptr_p_flat_mismatch_deref));
  ASSERT_EQ(0, ps_node_ptr_array_pointee_bytes(
                   member_plain_ptr_p_flat_mismatch_deref));
  ASSERT_TRUE(!ps_node_pointer_stride_metadata(
      member_plain_ptr_p_flat_mismatch_deref, NULL, NULL, NULL, NULL));

  parsed_code = parse_program_input(
      "struct __tm_member_scalar { unsigned int u; _Bool b; _Atomic int a; "
      "double _Complex z; }; "
      "int main(void){ struct __tm_member_scalar h; return 0; }");
  fn = as_func(parsed_code[0]);
  lvar_t *member_scalar_h = find_func_lvar(fn, "h");
  ASSERT_TRUE(member_scalar_h != NULL);
  const char *member_scalar_tag = "__tm_member_scalar";
  tag_member_info_t member_scalar_u_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info(
      TK_STRUCT, (char *)member_scalar_tag, (int)strlen(member_scalar_tag),
      "u", 1, &member_scalar_u_info));
  ASSERT_TRUE(member_scalar_u_info.decl_type != NULL);
  ASSERT_TRUE(ps_type_is_unsigned(member_scalar_u_info.decl_type));
  member_scalar_u_info.is_unsigned = 0;
  node_t *member_scalar_u_node = ps_node_new_tag_member_lvar_ref_for(
      member_scalar_h, member_scalar_u_info.offset, &member_scalar_u_info);
  ASSERT_TRUE(ps_node_is_unsigned_type(member_scalar_u_node));
  node_t *member_scalar_u_deref = ps_node_new_tag_member_deref_for(
      ps_node_new_num(0), ps_node_new_lvar_for(member_scalar_h),
      &member_scalar_u_info);
  ASSERT_TRUE(ps_node_is_unsigned_type(member_scalar_u_deref));
  ASSERT_TRUE(ps_type_is_unsigned(ps_node_get_type(member_scalar_u_deref)));

  tag_member_info_t member_scalar_b_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info(
      TK_STRUCT, (char *)member_scalar_tag, (int)strlen(member_scalar_tag),
      "b", 1, &member_scalar_b_info));
  ASSERT_TRUE(member_scalar_b_info.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_BOOL, member_scalar_b_info.decl_type->kind);
  member_scalar_b_info.is_bool = 0;
  node_t *member_scalar_b_node = ps_node_new_tag_member_lvar_ref_for(
      member_scalar_h, member_scalar_b_info.offset, &member_scalar_b_info);
  ASSERT_TRUE(ps_node_get_type(member_scalar_b_node) != NULL);
  ASSERT_EQ(PSX_TYPE_BOOL, ps_node_get_type(member_scalar_b_node)->kind);
  node_t *member_scalar_b_deref = ps_node_new_tag_member_deref_for(
      ps_node_new_num(0), ps_node_new_lvar_for(member_scalar_h),
      &member_scalar_b_info);
  ASSERT_TRUE(ps_node_get_type(member_scalar_b_deref) != NULL);
  ASSERT_EQ(PSX_TYPE_BOOL, ps_node_get_type(member_scalar_b_deref)->kind);

  tag_member_info_t member_scalar_a_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info(
      TK_STRUCT, (char *)member_scalar_tag, (int)strlen(member_scalar_tag),
      "a", 1, &member_scalar_a_info));
  ASSERT_TRUE(member_scalar_a_info.decl_type != NULL);
  ASSERT_TRUE(member_scalar_a_info.decl_type->is_atomic);
  member_scalar_a_info.tag_kind = TK_STRUCT;
  member_scalar_a_info.tag_name = (char *)member_scalar_tag;
  member_scalar_a_info.tag_len = (int)strlen(member_scalar_tag);
  member_scalar_a_info.is_tag_pointer = 1;
  member_scalar_a_info.pointer_qual_levels = 2;
  member_scalar_a_info.fp_kind = TK_FLOAT_KIND_DOUBLE;
  member_scalar_a_info.is_bool = 1;
  member_scalar_a_info.is_unsigned = 1;
  node_t *member_scalar_a_node = ps_node_new_tag_member_lvar_ref_for(
      member_scalar_h, member_scalar_a_info.offset, &member_scalar_a_info);
  ASSERT_TRUE(ps_node_get_type(member_scalar_a_node)->is_atomic);
  ASSERT_EQ(PSX_TYPE_INTEGER, ps_node_get_type(member_scalar_a_node)->kind);
  ASSERT_EQ(0, ps_node_pointer_qual_levels(member_scalar_a_node));
  ASSERT_EQ(TK_FLOAT_KIND_NONE, ps_node_value_fp_kind(member_scalar_a_node));
  ASSERT_TRUE(!ps_node_is_unsigned_type(member_scalar_a_node));
  node_t *member_scalar_a_deref = ps_node_new_tag_member_deref_for(
      ps_node_new_num(0), ps_node_new_lvar_for(member_scalar_h),
      &member_scalar_a_info);
  ASSERT_TRUE(ps_node_get_type(member_scalar_a_deref)->is_atomic);
  ASSERT_EQ(PSX_TYPE_INTEGER,
            ps_node_get_type(member_scalar_a_deref)->kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, member_scalar_a_deref->fp_kind);
  ASSERT_TRUE(!ps_node_is_pointer(member_scalar_a_deref));

  tag_member_info_t member_scalar_z_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info(
      TK_STRUCT, (char *)member_scalar_tag, (int)strlen(member_scalar_tag),
      "z", 1, &member_scalar_z_info));
  ASSERT_TRUE(member_scalar_z_info.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_COMPLEX, member_scalar_z_info.decl_type->kind);
  member_scalar_z_info.fp_kind = TK_FLOAT_KIND_NONE;
  node_t *member_scalar_z_node = ps_node_new_tag_member_lvar_ref_for(
      member_scalar_h, member_scalar_z_info.offset, &member_scalar_z_info);
  ASSERT_EQ(PSX_TYPE_COMPLEX, ps_node_get_type(member_scalar_z_node)->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_node_value_fp_kind(member_scalar_z_node));
  node_t *member_scalar_z_deref = ps_node_new_tag_member_deref_for(
      ps_node_new_num(0), ps_node_new_lvar_for(member_scalar_h),
      &member_scalar_z_info);
  ASSERT_EQ(PSX_TYPE_COMPLEX,
            ps_node_get_type(member_scalar_z_deref)->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, member_scalar_z_deref->fp_kind);

  parsed_code = parse_program_input(
      "double __tm696_ret_d(void){ return 1.0; } "
      "double *__tm696_gdp; double (*__tm696_gfp)(void)=__tm696_ret_d; "
      "int main(void){ double d; double *dp=&d; double (*fp)(void)=__tm696_ret_d; return 0; }");
  fn = as_func(parsed_code[1]);
  lvar_t *dp_lvar = find_func_lvar(fn, "dp");
  ASSERT_TRUE(dp_lvar != NULL);
  psx_type_t *dp_type = ps_lvar_get_decl_type(dp_lvar);
  ASSERT_TRUE(dp_type != NULL && dp_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, dp_type->base->kind);
  ASSERT_TRUE(!ps_decl_funcptr_sig_has_payload(ps_lvar_funcptr_sig(dp_lvar)));
  lvar_t *fp_lvar = find_func_lvar(fn, "fp");
  ASSERT_TRUE(fp_lvar != NULL);
  psx_decl_funcptr_sig_t fp_lvar_sig = ps_lvar_funcptr_sig(fp_lvar);
  ASSERT_TRUE(ps_decl_funcptr_sig_has_payload(fp_lvar_sig));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            fp_lvar_sig.function.callable.return_shape.fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            fp_lvar_sig.function.callable.return_shape.pointee_fp_kind);
  global_var_t *gdp = ps_find_global_var("__tm696_gdp", 11);
  ASSERT_TRUE(gdp != NULL);
  psx_type_t *gdp_type = ps_gvar_get_decl_type(gdp);
  ASSERT_TRUE(gdp_type != NULL && gdp_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, gdp_type->base->kind);
  ASSERT_TRUE(!ps_decl_funcptr_sig_has_payload(ps_gvar_funcptr_sig(gdp)));
  global_var_t *gfp = ps_find_global_var("__tm696_gfp", 11);
  ASSERT_TRUE(gfp != NULL);
  psx_decl_funcptr_sig_t gfp_sig = ps_gvar_funcptr_sig(gfp);
  ASSERT_TRUE(ps_decl_funcptr_sig_has_payload(gfp_sig));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            gfp_sig.function.callable.return_shape.fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            gfp_sig.function.callable.return_shape.pointee_fp_kind);

  parsed_code = parse_program_input(
      "typedef int *__tm696_GPI; int __tm696_gia[3]; "
      "__tm696_GPI __tm696_gpi = __tm696_gia; int main(void){ return 0; }");
  (void)parsed_code;
  global_var_t *typedef_gpi = ps_find_global_var("__tm696_gpi", 11);
  ASSERT_TRUE(typedef_gpi != NULL);
  psx_type_t *typedef_gpi_type = ps_gvar_get_decl_type(typedef_gpi);
  ASSERT_TRUE(typedef_gpi_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, typedef_gpi_type->kind);
  ASSERT_EQ(8, ps_type_sizeof(typedef_gpi_type));
  ASSERT_TRUE(typedef_gpi_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, typedef_gpi_type->base->kind);

  parsed_code = parse_program_input(
      "double __tm696_rows[2][2]; double (*__tm696_gpa)[2]=__tm696_rows; "
      "int main(void){ return 0; }");
  (void)parsed_code;
  global_var_t *gpa = ps_find_global_var("__tm696_gpa", 11);
  ASSERT_TRUE(gpa != NULL);
  node_t *gpa_node = ps_node_new_gvar_for(gpa);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_node_pointee_fp_kind(gpa_node));
  ASSERT_EQ(16, ps_node_ptr_array_pointee_bytes(gpa_node));
  node_t *gpa_row = ps_node_new_unary_deref_for(gpa_node);
  ASSERT_EQ(16, ps_node_type_size(gpa_row));
  ASSERT_EQ(8, ps_node_deref_size(gpa_row));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_node_pointee_fp_kind(gpa_row));

  parsed_code = parse_program_input(
      "typedef int *__tm696_GIP; "
      "int __tm696_ga, __tm696_gb, __tm696_gc; "
      "__tm696_GIP __tm696_grow[3] = { &__tm696_ga, &__tm696_gb, &__tm696_gc }; "
      "__tm696_GIP (*__tm696_gpia)[3] = &__tm696_grow; "
      "int main(void){ return 0; }");
  (void)parsed_code;
  global_var_t *gpia = ps_find_global_var("__tm696_gpia", 12);
  ASSERT_TRUE(gpia != NULL);
  node_t *gpia_node = ps_node_new_gvar_for(gpia);
  psx_type_t *gpia_type = ps_node_get_type(gpia_node);
  ASSERT_TRUE(gpia_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, gpia_type->kind);
  ASSERT_TRUE(gpia_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, gpia_type->base->kind);
  ASSERT_TRUE(gpia_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, gpia_type->base->base->kind);
  ASSERT_EQ(24, ps_node_deref_size(gpia_node));
  int gpia_inner = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(gpia_node, &gpia_inner,
                                               NULL, NULL, NULL));
  ASSERT_EQ(8, gpia_inner);
  node_t *gpia_row = ps_node_new_unary_deref_for(gpia_node);
  ASSERT_EQ(24, ps_node_type_size(gpia_row));
  ASSERT_EQ(8, ps_node_deref_size(gpia_row));
  ASSERT_TRUE(ps_node_is_pointer(gpia_row));
  node_t *gpia_elem = ps_node_new_subscript_deref_for(
      gpia_row, gpia_row->lhs ? gpia_row->lhs : gpia_row,
      ps_node_new_num(0), ps_node_deref_size(gpia_row), 0, 0, NULL, 0);
  ASSERT_EQ(8, ps_node_type_size(gpia_elem));
  ASSERT_EQ(4, ps_node_deref_size(gpia_elem));
  ASSERT_TRUE(ps_node_is_pointer(gpia_elem));

  parsed_code = parse_program_input(
      "struct __tm696_S { int a; int b; }; "
      "struct __tm696_S __tm696_sa[3]; "
      "struct __tm696_S (*__tm696_gap)[3] = &__tm696_sa; "
      "int main(void){ return 0; }");
  (void)parsed_code;
  global_var_t *gap = ps_find_global_var("__tm696_gap", 11);
  ASSERT_TRUE(gap != NULL);
  node_t *gap_node = ps_node_new_gvar_for(gap);
  ASSERT_EQ(24, ps_node_ptr_array_pointee_bytes(gap_node));
  node_t *gap_row = ps_node_new_unary_deref_for(gap_node);
  ASSERT_EQ(24, ps_node_ptr_array_pointee_bytes(gap_row));
  ASSERT_EQ(24, ps_node_type_size(gap_row));
  ASSERT_EQ(8, ps_node_deref_size(gap_row));
  ASSERT_TRUE(ps_node_get_type(gap_row) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(gap_row)->kind);
  ASSERT_TRUE(ps_node_get_type(gap_row)->base != NULL);
  ASSERT_EQ(PSX_TYPE_STRUCT, ps_node_get_type(gap_row)->base->kind);
  token_kind_t gap_tag = TK_EOF;
  int gap_is_ptr = 0;
  ps_node_get_tag_type(gap_row, &gap_tag, NULL, NULL, &gap_is_ptr);
  ASSERT_EQ(TK_STRUCT, gap_tag);
  ASSERT_TRUE(gap_is_ptr);
  node_t *gap_elem = ps_node_new_subscript_deref_for(
      gap_row, gap_row->lhs ? gap_row->lhs : gap_row,
      ps_node_new_num(0), ps_node_deref_size(gap_row), 0, 0, NULL, 0);
  gap_tag = TK_EOF;
  gap_is_ptr = 0;
  ps_node_get_tag_type(gap_elem, &gap_tag, NULL, NULL, &gap_is_ptr);
  ASSERT_EQ(TK_STRUCT, gap_tag);
  ASSERT_TRUE(!gap_is_ptr);

  parsed_code = parse_program_input(
      "int __tm696_int_rows[2][3][4]; "
      "int (*__tm696_gpi2)[3][4] = __tm696_int_rows; "
      "int main(void){ return 0; }");
  (void)parsed_code;
  global_var_t *gpi = ps_find_global_var("__tm696_gpi2", 12);
  ASSERT_TRUE(gpi != NULL);
  node_t *gpi_node = ps_node_new_gvar_for(gpi);
  ASSERT_EQ(48, ps_node_deref_size(gpi_node));
  psx_type_t *gpi_type = ps_node_get_type(gpi_node);
  ASSERT_TRUE(gpi_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, gpi_type->kind);
  ASSERT_TRUE(gpi_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, gpi_type->base->kind);
  int gpi_inner = ps_type_deref_size(gpi_type->base);
  int gpi_next = gpi_type->base->outer_stride;
  ASSERT_EQ(16, gpi_inner);
  node_t *gpi_outer_row = ps_node_new_subscript_deref_for(
      gpi_node, gpi_node, ps_node_new_num(0), ps_type_sizeof(gpi_type->base),
      gpi_inner, gpi_next, gpi_type->base->extra_strides,
      gpi_type->base->extra_strides_count);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(gpi_outer_row)->kind);
  ASSERT_TRUE(ps_node_subscript_deref_uses_base_address(gpi_outer_row));
  gpi_inner = 0;
  gpi_next = 0;
  ASSERT_TRUE(ps_node_pointer_stride_metadata(gpi_outer_row, &gpi_inner,
                                               &gpi_next, NULL, NULL));
  ASSERT_EQ(16, gpi_inner);
  ASSERT_EQ(0, gpi_next);
  node_t *gpi_outer_base =
      ps_node_subscript_deref_uses_base_address(gpi_outer_row)
          ? gpi_outer_row->lhs
          : gpi_outer_row;
  node_t *gpi_inner_row = ps_node_new_subscript_deref_for(
      gpi_outer_row, gpi_outer_base, ps_node_new_num(0),
      ps_node_deref_size(gpi_outer_row), gpi_inner, gpi_next, NULL, 0);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(gpi_inner_row)->kind);
  ASSERT_TRUE(ps_node_subscript_deref_uses_base_address(gpi_inner_row));

  parsed_code = parse_program_input(
      "double __tm697_ret_d(void){ return 1.0; } "
      "typedef double *TM697_DP; typedef double (*TM697_FP)(void); "
      "int main(void){ double d; TM697_DP dp=&d; TM697_FP fp=__tm697_ret_d; "
      "{ typedef double (*TM697_BFP)(void); TM697_BFP bfp=__tm697_ret_d; bfp(); } "
      "return fp() == *dp; }");
  (void)parsed_code;
  psx_typedef_info_t td_dp = {0};
  ASSERT_TRUE(ps_ctx_find_typedef_name("TM697_DP", 8, &td_dp));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, td_dp.fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, td_dp.funcptr_sig.function.callable.return_shape.fp_kind);
  ASSERT_TRUE(!ps_decl_funcptr_sig_has_payload(ps_ctx_typedef_funcptr_sig(&td_dp)));
  psx_typedef_info_t td_fp = {0};
  ASSERT_TRUE(ps_ctx_find_typedef_name("TM697_FP", 8, &td_fp));
  ASSERT_EQ(TK_FLOAT_KIND_NONE, td_fp.fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, td_fp.funcptr_sig.function.callable.return_shape.fp_kind);
  ASSERT_TRUE(td_fp.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, td_fp.decl_type->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, td_fp.decl_type->funcptr_sig.function.callable.return_shape.fp_kind);
  td_fp.funcptr_sig.function.callable.return_shape.fp_kind = TK_FLOAT_KIND_NONE;
  td_fp.funcptr_sig.function.callable.return_shape.int_width = 4;
  psx_decl_funcptr_sig_t td_fp_sig = ps_ctx_typedef_funcptr_sig(&td_fp);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, td_fp_sig.function.callable.return_shape.fp_kind);
  ASSERT_EQ(0, td_fp_sig.function.callable.return_shape.int_width);
  ASSERT_TRUE(ps_decl_funcptr_sig_has_payload(td_fp_sig));
  psx_type_t *td_fp_decl_type = td_fp.decl_type;
  ASSERT_TRUE(td_fp.decl_type == td_fp_decl_type);
  ASSERT_EQ(0, ps_type_funcptr_signature(td_fp.decl_type)
                   .function.callable.return_shape.int_width);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            ps_type_funcptr_signature(td_fp.decl_type)
                .function.callable.return_shape.fp_kind);
  td_dp.funcptr_sig.function.callable.return_shape.int_width = 8;
  td_dp.is_funcptr = 1;
  psx_decl_funcptr_sig_t td_dp_stale_sig = ps_ctx_typedef_funcptr_sig(&td_dp);
  ASSERT_TRUE(!ps_decl_funcptr_sig_has_payload(td_dp_stale_sig));
  fn = as_func(parsed_code[1]);
  lvar_t *td_dp_lvar = find_func_lvar(fn, "dp");
  ASSERT_TRUE(td_dp_lvar != NULL);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, td_dp_lvar->decl_type->base->fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            ps_lvar_funcptr_sig(td_dp_lvar).function.callable.return_shape.fp_kind);
  lvar_t *td_fp_lvar = find_func_lvar(fn, "fp");
  ASSERT_TRUE(td_fp_lvar != NULL);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            ps_lvar_funcptr_sig(td_fp_lvar).function.callable.return_shape.fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            ps_lvar_funcptr_sig(td_fp_lvar).function.callable.return_shape.pointee_fp_kind);
  lvar_t *td_bfp_lvar = find_func_lvar(fn, "bfp");
  ASSERT_TRUE(td_bfp_lvar != NULL);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            ps_lvar_funcptr_sig(td_bfp_lvar).function.callable.return_shape.fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            ps_lvar_funcptr_sig(td_bfp_lvar).function.callable.return_shape.pointee_fp_kind);

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
  ASSERT_TRUE(ps_ctx_find_typedef_name("TM817_GET", 9, &tm817_get_td));
  ASSERT_EQ(1, tm817_get_td.funcptr_sig.function.callable.return_shape.is_data_pointer);
  ASSERT_EQ(0, tm817_get_td.funcptr_sig.function.callable.return_shape.int_width);
  ASSERT_EQ(0u, tm817_get_td.funcptr_sig.function.callable.signature.param_int_mask);
  ASSERT_EQ(1, ps_ctx_get_typedef_pointer_levels("TM817_GET", 9));
  psx_typedef_info_t tm817_get2_td = {0};
  ASSERT_TRUE(ps_ctx_find_typedef_name("TM817_GET2", 10, &tm817_get2_td));
  ASSERT_EQ(1, tm817_get2_td.funcptr_sig.function.callable.return_shape.is_data_pointer);
  ASSERT_EQ(0, tm817_get2_td.funcptr_sig.function.callable.return_shape.int_width);
  ASSERT_EQ(0u, tm817_get2_td.funcptr_sig.function.callable.signature.param_int_mask);
  ASSERT_EQ(1, ps_ctx_get_typedef_pointer_levels("TM817_GET2", 10));
  psx_typedef_info_t tm817_pp_td = {0};
  ASSERT_TRUE(ps_ctx_find_typedef_name("TM817_PP", 8, &tm817_pp_td));
  ASSERT_EQ(0, tm817_pp_td.funcptr_sig.function.callable.return_shape.is_data_pointer);
  ASSERT_EQ(4, tm817_pp_td.funcptr_sig.function.callable.return_shape.int_width);
  ASSERT_EQ(1u, tm817_pp_td.funcptr_sig.function.callable.signature.param_int_mask);
  ASSERT_EQ(2, ps_ctx_get_typedef_pointer_levels("TM817_PP", 8));
  fn = as_func(parsed_code[2]);
  lvar_t *tm817_get_lvar = find_func_lvar(fn, "get");
  ASSERT_TRUE(tm817_get_lvar != NULL);
  ASSERT_EQ(1, ps_lvar_pointer_qual_levels(tm817_get_lvar));
  ASSERT_EQ(1, ps_lvar_funcptr_sig(tm817_get_lvar).function.callable.return_shape.is_data_pointer);
  ASSERT_EQ(0, ps_lvar_funcptr_sig(tm817_get_lvar).function.callable.return_shape.int_width);
  lvar_t *tm817_get2_lvar = find_func_lvar(fn, "get2");
  ASSERT_TRUE(tm817_get2_lvar != NULL);
  ASSERT_EQ(1, ps_lvar_pointer_qual_levels(tm817_get2_lvar));
  ASSERT_EQ(1, ps_lvar_funcptr_sig(tm817_get2_lvar).function.callable.return_shape.is_data_pointer);
  ASSERT_EQ(0, ps_lvar_funcptr_sig(tm817_get2_lvar).function.callable.return_shape.int_width);
  lvar_t *tm817_pp_lvar = find_func_lvar(fn, "pp");
  ASSERT_TRUE(tm817_pp_lvar != NULL);
  ASSERT_EQ(2, ps_lvar_pointer_qual_levels(tm817_pp_lvar));
  ASSERT_EQ(0, ps_lvar_funcptr_sig(tm817_pp_lvar).function.callable.return_shape.is_data_pointer);
  ASSERT_EQ(4, ps_lvar_funcptr_sig(tm817_pp_lvar).function.callable.return_shape.int_width);
  ASSERT_EQ(1u, ps_lvar_funcptr_sig(tm817_pp_lvar).function.callable.signature.param_int_mask);
  lvar_t *tm817_lget_lvar = find_func_lvar(fn, "lget");
  ASSERT_TRUE(tm817_lget_lvar != NULL);
  ASSERT_EQ(1, ps_lvar_pointer_qual_levels(tm817_lget_lvar));
  ASSERT_EQ(1, ps_lvar_funcptr_sig(tm817_lget_lvar).function.callable.return_shape.is_data_pointer);
  ASSERT_EQ(0, ps_lvar_funcptr_sig(tm817_lget_lvar).function.callable.return_shape.int_width);
  lvar_t *tm817_lpp_lvar = find_func_lvar(fn, "lpp");
  ASSERT_TRUE(tm817_lpp_lvar != NULL);
  ASSERT_EQ(2, ps_lvar_pointer_qual_levels(tm817_lpp_lvar));
  ASSERT_EQ(0, ps_lvar_funcptr_sig(tm817_lpp_lvar).function.callable.return_shape.is_data_pointer);
  ASSERT_EQ(4, ps_lvar_funcptr_sig(tm817_lpp_lvar).function.callable.return_shape.int_width);
  ASSERT_EQ(1u, ps_lvar_funcptr_sig(tm817_lpp_lvar).function.callable.signature.param_int_mask);

  parsed_code = parse_program_input(
      "double __tm700_d; double *__tm700_ret_dp(void){ return &__tm700_d; } "
      "double *(*__tm700_gfp)(void)=__tm700_ret_dp; "
      "int main(void){ double *(*fp)(void)=__tm700_ret_dp; fp(); __tm700_gfp(); return 0; }");
  fn = as_func(parsed_code[1]);
  lvar_t *tm700_fp_lvar = find_func_lvar(fn, "fp");
  ASSERT_TRUE(tm700_fp_lvar != NULL);
  ASSERT_EQ(1, ps_lvar_funcptr_sig(tm700_fp_lvar).function.callable.return_shape.is_data_pointer);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, ps_lvar_funcptr_sig(tm700_fp_lvar).function.callable.return_shape.fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_lvar_funcptr_sig(tm700_fp_lvar).function.callable.return_shape.pointee_fp_kind);
  global_var_t *tm700_gfp = ps_find_global_var("__tm700_gfp", 11);
  ASSERT_TRUE(tm700_gfp != NULL);
  ASSERT_EQ(1, ps_gvar_funcptr_sig(tm700_gfp).function.callable.return_shape.is_data_pointer);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, ps_gvar_funcptr_sig(tm700_gfp).function.callable.return_shape.fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_gvar_funcptr_sig(tm700_gfp).function.callable.return_shape.pointee_fp_kind);

  parsed_code = parse_program_input(
      "int __tm814_inc(int x){ return x + 1; } "
      "int main(void){ int (*p)(int)=__tm814_inc; int (**pp)(int)=&p; return (*pp)(41); }");
  fn = as_func(parsed_code[1]);
  lvar_t *tm814_pp_lvar = find_func_lvar(fn, "pp");
  ASSERT_TRUE(tm814_pp_lvar != NULL);
  ASSERT_EQ(2, ps_lvar_pointer_qual_levels(tm814_pp_lvar));
  ASSERT_EQ(0, ps_lvar_funcptr_sig(tm814_pp_lvar).function.callable.return_shape.is_data_pointer);
  ASSERT_EQ(4, ps_lvar_funcptr_sig(tm814_pp_lvar).function.callable.return_shape.int_width);
  ASSERT_EQ(1u, ps_lvar_funcptr_sig(tm814_pp_lvar).function.callable.signature.param_int_mask);
  node_t *tm814_pp_node = ps_node_new_lvar_for(tm814_pp_lvar);
  node_t *tm814_deref_pp = ps_node_new_unary_deref_for(tm814_pp_node);
  psx_decl_funcptr_sig_t tm814_deref_sig =
      ps_node_funcptr_sig(tm814_deref_pp);
  ASSERT_EQ(0, tm814_deref_sig.function.callable.return_shape.is_data_pointer);
  ASSERT_EQ(4, tm814_deref_sig.function.callable.return_shape.int_width);
  ASSERT_EQ(1u, tm814_deref_sig.function.callable.signature.param_int_mask);

  parsed_code = parse_program_input(
      "int __tm815_inc(int x){ return x + 1; } "
      "int (*__tm815_gp)(int)=__tm815_inc; int (**__tm815_gpp)(int)=&__tm815_gp; "
      "int __tm815_apply(int (**pp)(int)){ return (*pp)(41); } "
      "int main(void){ return (*__tm815_gpp)(41) + __tm815_apply(__tm815_gpp); }");
  global_var_t *tm815_gpp = ps_find_global_var("__tm815_gpp", 11);
  ASSERT_TRUE(tm815_gpp != NULL);
  ASSERT_EQ(2, ps_type_pointer_depth(tm815_gpp->decl_type));
  ASSERT_EQ(0, ps_gvar_funcptr_sig(tm815_gpp).function.callable.return_shape.is_data_pointer);
  ASSERT_EQ(4, ps_gvar_funcptr_sig(tm815_gpp).function.callable.return_shape.int_width);
  ASSERT_EQ(1u, ps_gvar_funcptr_sig(tm815_gpp).function.callable.signature.param_int_mask);
  fn = as_func(parsed_code[1]);
  lvar_t *tm815_param_pp = find_func_lvar(fn, "pp");
  ASSERT_TRUE(tm815_param_pp != NULL);
  ASSERT_EQ(2, ps_lvar_pointer_qual_levels(tm815_param_pp));
  ASSERT_EQ(0, ps_lvar_funcptr_sig(tm815_param_pp).function.callable.return_shape.is_data_pointer);
  ASSERT_EQ(4, ps_lvar_funcptr_sig(tm815_param_pp).function.callable.return_shape.int_width);
  ASSERT_EQ(1u, ps_lvar_funcptr_sig(tm815_param_pp).function.callable.signature.param_int_mask);

  parsed_code = parse_program_input(
      "int __tm816_i; int *__tm816_retp(void){ return &__tm816_i; } "
      "int __tm816_inc(int x){ return x + 1; } "
      "int *(*__tm816_gfp)(void)=__tm816_retp; "
      "struct TM816 { int *(*fp)(void); int (**pp)(int); }; "
      "int *__tm816_apply(int *(*fp)(void)){ return fp(); } "
      "int __tm816_call(struct TM816 *s){ return *s->fp() + (*s->pp)(1); } "
      "int main(void){ return *__tm816_apply(__tm816_retp); }");
  global_var_t *tm816_gfp = ps_find_global_var("__tm816_gfp", 11);
  ASSERT_TRUE(tm816_gfp != NULL);
  ASSERT_EQ(1, ps_gvar_funcptr_sig(tm816_gfp).function.callable.return_shape.is_data_pointer);
  ASSERT_EQ(0, ps_gvar_funcptr_sig(tm816_gfp).function.callable.return_shape.int_width);
  ASSERT_EQ(0u, ps_gvar_funcptr_sig(tm816_gfp).function.callable.signature.param_int_mask);
  fn = as_func(parsed_code[2]);
  lvar_t *tm816_param_fp = find_func_lvar(fn, "fp");
  ASSERT_TRUE(tm816_param_fp != NULL);
  ASSERT_EQ(1, ps_lvar_funcptr_sig(tm816_param_fp).function.callable.return_shape.is_data_pointer);
  ASSERT_EQ(0, ps_lvar_funcptr_sig(tm816_param_fp).function.callable.return_shape.int_width);
  ASSERT_EQ(0u, ps_lvar_funcptr_sig(tm816_param_fp).function.callable.signature.param_int_mask);
  tag_member_info_t tm816_fp_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info(TK_STRUCT, "TM816", 5, "fp", 2,
                                           &tm816_fp_info));
  ASSERT_EQ(1, tm816_fp_info.funcptr_sig.function.callable.return_shape.is_data_pointer);
  ASSERT_EQ(0, tm816_fp_info.funcptr_sig.function.callable.return_shape.int_width);
  ASSERT_EQ(0u, tm816_fp_info.funcptr_sig.function.callable.signature.param_int_mask);
  tag_member_info_t tm816_pp_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info(TK_STRUCT, "TM816", 5, "pp", 2,
                                           &tm816_pp_info));
  ASSERT_EQ(2, tm816_pp_info.pointer_qual_levels);
  ASSERT_EQ(0, tm816_pp_info.funcptr_sig.function.callable.return_shape.is_data_pointer);
  ASSERT_EQ(4, tm816_pp_info.funcptr_sig.function.callable.return_shape.int_width);
  ASSERT_EQ(1u, tm816_pp_info.funcptr_sig.function.callable.signature.param_int_mask);

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
    lvar_t *callee_lvar = ps_node_lvar_symbol(call->callee);
    if (callee_lvar && callee_lvar->len == 2 && strncmp(callee_lvar->name, "df", 2) == 0)
      indirect_double_call = n;
    if (callee_lvar && callee_lvar->len == 2 && strncmp(callee_lvar->name, "pf", 2) == 0)
      indirect_ptr_call = n;
	  }
	  ASSERT_TRUE(indirect_double_call->type != NULL);
	  psx_type_t *indirect_double_ty = ps_node_get_type(indirect_double_call);
	  ASSERT_TRUE(indirect_double_ty != NULL);
	  ASSERT_EQ(PSX_TYPE_FLOAT, indirect_double_ty->kind);
	  ASSERT_EQ(8, ps_type_sizeof(indirect_double_ty));
	  ASSERT_TRUE(indirect_ptr_call->type != NULL);
	  psx_type_t *indirect_ptr_ty = ps_node_get_type(indirect_ptr_call);
	  ASSERT_TRUE(indirect_ptr_ty != NULL);
	  ASSERT_EQ(PSX_TYPE_POINTER, indirect_ptr_ty->kind);
	  ASSERT_EQ(4, ps_type_deref_size(indirect_ptr_ty));
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
  psx_type_t *const_struct_ptr_ty = ps_node_get_type(const_struct_ptr_call);
  ASSERT_TRUE(const_struct_ptr_ty != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, const_struct_ptr_ty->kind);
  ASSERT_TRUE(const_struct_ptr_ty->base != NULL);
  ASSERT_EQ(TK_STRUCT, const_struct_ptr_ty->base->tag_kind);
  ASSERT_TRUE(const_struct_ptr_ty->base->is_const_qualified);
  token_kind_t call_tag = TK_EOF;
  char *call_tag_name = NULL;
  int call_tag_len = 0;
  int call_is_tag_ptr = 0;
  ps_node_get_tag_type(const_struct_ptr_call, &call_tag, &call_tag_name,
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
  psx_decl_funcptr_sig_t go_node_sig = ps_node_funcdef_ret_funcptr_sig(go_def);
  ASSERT_TRUE(ps_decl_funcptr_sig_has_payload(go_node_sig));
  ASSERT_EQ(1, go_node_sig.function.callable.return_shape.is_data_pointer);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, go_node_sig.function.callable.return_shape.fp_kind);
  ASSERT_TRUE(go_def->base.type != NULL);
  ASSERT_EQ(1, go_def->base.type->funcptr_sig.function.callable.return_shape.is_data_pointer);
  go_def->ret_funcptr_sig.function.callable.return_shape.is_data_pointer = 0;
  go_def->ret_funcptr_sig.function.callable.return_shape.fp_kind = TK_FLOAT_KIND_DOUBLE;
  go_node_sig = ps_node_funcdef_ret_funcptr_sig(go_def);
  ASSERT_EQ(1, go_node_sig.function.callable.return_shape.is_data_pointer);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, go_node_sig.function.callable.return_shape.fp_kind);
  psx_decl_funcptr_sig_t go_ctx_sig =
      ps_ctx_get_function_ret_funcptr_sig("__tm_go", 7);
  ASSERT_EQ(go_ctx_sig.function.callable.return_shape.is_data_pointer, go_node_sig.function.callable.return_shape.is_data_pointer);
  ASSERT_EQ(go_ctx_sig.function.callable.return_shape.fp_kind, go_node_sig.function.callable.return_shape.fp_kind);
  go_ctx_sig.function.callable.return_shape.is_data_pointer = 0;
  go_ctx_sig.function.callable.return_shape.fp_kind = TK_FLOAT_KIND_DOUBLE;
  psx_decl_funcptr_sig_t go_ctx_sig_again =
      ps_ctx_get_function_ret_funcptr_sig("__tm_go", 7);
  ASSERT_EQ(1, go_ctx_sig_again.function.callable.return_shape.is_data_pointer);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, go_ctx_sig_again.function.callable.return_shape.fp_kind);
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
  psx_type_t *funcptr_chain_ty = ps_node_get_type(funcptr_chain_call);
  ASSERT_TRUE(funcptr_chain_ty != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, funcptr_chain_ty->kind);
  ASSERT_TRUE(funcptr_chain_ty->base != NULL);
  ASSERT_EQ(TK_STRUCT, funcptr_chain_ty->base->tag_kind);
  call_tag = TK_EOF;
  call_tag_name = NULL;
  call_tag_len = 0;
  call_is_tag_ptr = 0;
  ps_node_get_tag_type(funcptr_chain_call, &call_tag, &call_tag_name,
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
  psx_decl_funcptr_sig_t pick_node_sig = ps_node_funcdef_ret_funcptr_sig(pick_def);
  ASSERT_TRUE(ps_decl_funcptr_sig_has_payload(pick_node_sig));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, pick_node_sig.function.callable.return_shape.fp_kind);
  ASSERT_EQ(2u, pick_node_sig.function.callable.signature.param_fp_mask);
  node_func_t pick_call = {0};
  pick_call.base.kind = ND_FUNCALL;
  pick_call.funcname = "__tm698_pick";
  pick_call.funcname_len = 12;
  psx_type_t *pick_call_type = ps_node_get_type((node_t *)&pick_call);
  ASSERT_TRUE(pick_call_type != NULL);
  ASSERT_TRUE(ps_node_value_is_pointer_like((node_t *)&pick_call));
  psx_decl_funcptr_sig_t pick_call_sig =
      ps_node_funcptr_sig((node_t *)&pick_call);
  ASSERT_TRUE(ps_decl_funcptr_sig_has_payload(pick_call_sig));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, pick_call_sig.function.callable.return_shape.fp_kind);
  ASSERT_EQ(2u, pick_call_sig.function.callable.signature.param_fp_mask);
  psx_decl_funcptr_sig_t pick_ctx_sig =
      ps_ctx_get_function_ret_funcptr_sig("__tm698_pick", 12);
  ASSERT_EQ(pick_ctx_sig.function.callable.return_shape.fp_kind,
            pick_node_sig.function.callable.return_shape.fp_kind);
  ASSERT_EQ(pick_ctx_sig.function.callable.signature.param_fp_mask,
            pick_node_sig.function.callable.signature.param_fp_mask);
  node_funcref_t pick_add_ref = {0};
  pick_add_ref.base.kind = ND_FUNCREF;
  pick_add_ref.funcname = "__tm698_add";
  pick_add_ref.funcname_len = 11;
  psx_type_t *pick_add_ref_type = ps_node_get_type((node_t *)&pick_add_ref);
  ASSERT_TRUE(pick_add_ref_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, pick_add_ref_type->kind);
  ASSERT_TRUE(pick_add_ref_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_FUNCTION, pick_add_ref_type->base->kind);
  ASSERT_TRUE(pick_add_ref_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, pick_add_ref_type->base->base->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, pick_add_ref_type->base->base->fp_kind);
  psx_decl_funcptr_sig_t pick_add_ref_sig =
      ps_node_funcptr_sig((node_t *)&pick_add_ref);
  ASSERT_TRUE(ps_decl_funcptr_sig_has_payload(pick_add_ref_sig));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, pick_add_ref_sig.function.callable.return_shape.fp_kind);
  ASSERT_EQ(2u, pick_add_ref_sig.function.callable.signature.param_fp_mask);

  parsed_code = parse_program_input(
      "int __tm818_inc(int x){ return x + 1; } "
      "int (*__tm818_fp)(int)=__tm818_inc; "
      "int (**__tm818_getpp(void))(int){ return &__tm818_fp; } "
      "int main(void){ return (*__tm818_getpp())(20) + (**__tm818_getpp())(30); }");
  node_func_t *tm818_getpp_def = as_func(parsed_code[1]);
  psx_decl_funcptr_sig_t tm818_getpp_sig =
      ps_node_funcdef_ret_funcptr_sig(tm818_getpp_def);
  ASSERT_TRUE(ps_decl_funcptr_sig_has_payload(tm818_getpp_sig));
  ASSERT_EQ(0, tm818_getpp_sig.function.callable.return_shape.is_data_pointer);
  ASSERT_EQ(4, tm818_getpp_sig.function.callable.return_shape.int_width);
  ASSERT_EQ(1u, tm818_getpp_sig.function.callable.signature.param_int_mask);
  ASSERT_EQ(2, ps_ctx_get_function_ret_pointer_levels("__tm818_getpp", 13));
}

static void test_translation_unit_reset_static_local_state() {
  printf("test_translation_unit_reset_static_local_state...\n");

  const char *input = "int f(void) { static int x=1; return x; }";
  ps_reset_translation_unit_state();
  parsed_code = parse_program_input(input);
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_TRUE(ps_find_global_var("f.x.0", 5) != NULL);
  ASSERT_TRUE(ps_find_global_var("f.x.1", 5) == NULL);

  ps_reset_translation_unit_state();
  parsed_code = parse_program_input(input);
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_TRUE(ps_find_global_var("f.x.0", 5) != NULL);
  ASSERT_TRUE(ps_find_global_var("f.x.1", 5) == NULL);
  ps_reset_translation_unit_state();
}

static void test_translation_unit_reset_anonymous_tag_state() {
  printf("test_translation_unit_reset_anonymous_tag_state...\n");

  const char *input = "struct { int x; } g; int main(void) { return 0; }";
  ps_reset_translation_unit_state();
  parsed_code = parse_program_input(input);
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_TRUE(ps_ctx_has_tag_type(TK_STRUCT, "__anon_tag_0", 12));
  ASSERT_TRUE(!ps_ctx_has_tag_type(TK_STRUCT, "__anon_tag_1", 12));

  ps_reset_translation_unit_state();
  parsed_code = parse_program_input(input);
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_TRUE(ps_ctx_has_tag_type(TK_STRUCT, "__anon_tag_0", 12));
  ASSERT_TRUE(!ps_ctx_has_tag_type(TK_STRUCT, "__anon_tag_1", 12));
  ps_reset_translation_unit_state();
}

static void test_translation_unit_reset_decl_locals_state() {
  printf("test_translation_unit_reset_decl_locals_state...\n");

  char name[] = "x";
  psx_decl_reset_locals();
  ASSERT_TRUE(psx_decl_register_lvar(name, 1) != NULL);
  ASSERT_TRUE(ps_decl_find_lvar(name, 1) != NULL);
  ps_reset_translation_unit_state();
  ASSERT_TRUE(ps_decl_find_lvar(name, 1) == NULL);
}

static void test_translation_unit_reset_pragma_pack_state() {
  printf("test_translation_unit_reset_pragma_pack_state...\n");

  pragma_pack_reset();
  pragma_pack_set(8);
  pragma_pack_push(1);
  ASSERT_EQ(1, pragma_pack_current_alignment());
  ps_reset_translation_unit_state();
  ASSERT_EQ(0, pragma_pack_current_alignment());
  pragma_pack_pop();
  ASSERT_EQ(0, pragma_pack_current_alignment());
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
  ASSERT_EQ(2, ps_ctx_get_function_ret_pointer_levels("sig_proto_pp", 12));

  parsed_code = parse_program_input("int **sig_def_pp(void) { return 0; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(2, ps_ctx_get_function_ret_pointer_levels("sig_def_pp", 10));

  parsed_code = parse_program_input(
      "int (*(*(*sig_deep(void))(int))(double))[3] { return 0; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  psx_decl_funcptr_sig_t deep_sig =
      ps_ctx_get_function_ret_funcptr_sig("sig_deep", 8);
  ASSERT_TRUE(ps_decl_funcptr_sig_has_payload(deep_sig));
  ASSERT_EQ(1u, deep_sig.function.callable.signature.param_int_mask);
  ASSERT_TRUE(ps_funcptr_returned_func_has_payload(
      deep_sig.function.returned_funcptr));
  psx_funcptr_type_shape_t nested_deep =
      ps_funcptr_returned_func_as_type_shape(
          deep_sig.function.returned_funcptr);
  ASSERT_EQ(2u, nested_deep.callable.signature.param_fp_mask);
  ASSERT_EQ(3, nested_deep.callable.return_shape.pointee_array.first_dim);

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

  parsed_code = parse_program_input(
      "struct SA { _Static_assert(sizeof(int)==4, \"ok\"); int x; }; "
      "int main(void) { return 0; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
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
  expect_parse_ok(
      "int main(void) { struct Forward (*p); (void)p; return 0; }");
  expect_parse_ok(
      "typedef struct Forward Forward; struct Holder { Forward *p; }; "
      "int main(void) { return 0; }");
  expect_parse_fail(
      "struct Holder { struct Forward values[2]; }; "
      "int main(void) { return 0; }");
  expect_parse_fail(
      "typedef int FunctionType(int); struct Holder { FunctionType f; }; "
      "int main(void) { return 0; }");
  expect_parse_fail("struct S; union S; int main(void) { return 0; }");
  expect_parse_fail("struct E {}; struct E {}; int main(void) { return 0; }");
  expect_parse_fail("struct S { int x; int x; }; int main(void) { return 0; }");
  expect_parse_fail(
      "struct S { int x; struct { int x; }; }; int main(void) { return 0; }");
  expect_parse_fail(
      "struct S { unsigned int x : 33; }; int main(void) { return 0; }");
  expect_parse_ok(
      "struct S { struct { int x; }; struct { int y; }; }; "
      "int main(void) { struct S s = {{1}, {2}}; return s.x + s.y; }");
  expect_parse_ok("typedef int X; typedef int X; int main(void) { return 0; }");
  expect_parse_fail("typedef int X; typedef long X; int main(void) { return 0; }");
  expect_parse_fail("int X; typedef int X; int main(void) { return 0; }");
  expect_parse_fail("int f(void); typedef int f; int main(void) { return 0; }");
  expect_parse_fail("enum E { X }; typedef int X; int main(void) { return 0; }");
  expect_parse_fail("int main(void) { int x; typedef int x; return 0; }");
  expect_parse_fail("int X; enum E { X }; int main(void) { return 0; }");
  expect_parse_fail("int f(void); enum E { f }; int main(void) { return 0; }");
  expect_parse_fail("typedef int T; enum E { T }; int main(void) { return 0; }");
  expect_parse_fail("int main(void) { int x; enum E { x }; return 0; }");
  expect_parse_ok("int X; int main(void) { enum E { X }; return 0; }");
  expect_parse_ok("typedef int T; int main(void) { enum E { T }; return 0; }");
  expect_parse_ok("main() { { struct T { int x; }; } struct T *p; return 0; }"); // 外側スコープで新規前方宣言
  expect_parse_fail("main() { struct S { int x; }; union U { int y; }; union U u={1}; return (struct S)u; }"); // 非同種非スカラ型cast未対応
  expect_parse_fail("main() { struct A { int x; }; struct B { int x; }; struct A a={1}; struct B b=a; return 0; }"); // 同サイズでも別タグは互換structではない
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
  expect_parse_fail(
      "struct SA { _Static_assert(0, \"ng\"); int x; }; int main(void){return 0;}");
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
  expect_parse_fail_with_message("main() { struct A { int x; }; struct B { int x; }; struct A a={1}; struct B b=a; return 0; }", "[decl] 構造体の単一式初期化は同型オブジェクトのみ対応です");
  expect_parse_fail_with_message("main() { struct S { int x; }; struct S s=1; return 0; }", "[decl] 構造体の単一式初期化は同型オブジェクトのみ対応です");
  expect_parse_fail_with_message("main() { struct S { int x; }; struct S t={1}; struct S s=(t,1); return 0; }", "[decl] 構造体の単一式初期化は同型オブジェクトのみ対応です");
  expect_parse_fail_with_message("main() { struct S { int x; }; int y=(0,(struct S){1}); return y; }", "スカラ変数を struct 値で初期化できません");
  expect_parse_fail_with_message("main() { union U { int x; char y; }; union U u={1,2}; return 0; }", "[init] 共用体初期化子は現状1要素のみ対応です");
  expect_parse_fail_with_message("main() { union U { int x; char y; }; union U u={.x=1,2}; return 0; }", "[init] 共用体初期化子は現状1要素のみ対応です");
  expect_parse_fail_with_message("main() { _Complex int x; return 0; }", "_Complex/_Imaginary は浮動小数型にのみ指定できます");
  expect_parse_fail_with_message("main() { return (_Complex int)1; }", "_Complex/_Imaginary cast は浮動小数型のみ対応です");
  expect_parse_fail_with_message("main() { return (_Thread_local int)1; }", "[cast] cast 型名にストレージ指定子は使えません");
  expect_parse_fail_with_message("main() { struct __IncOnly; struct __HasInc { struct __IncOnly m; }; return 0; }", "[decl] 不完全型のメンバは定義できません");
  expect_parse_fail_with_message("main() { struct T { int f(int); }; return 0; }", "[decl] 関数型のメンバは定義できません");
  expect_parse_fail_with_message(
      "main() { struct S { int *p:1; }; return 0; }",
      "bit-field has non-integer canonical type");
  expect_parse_fail("main() { struct S { int a[2]:1; }; return 0; }");
  expect_parse_fail("main() { struct S { float f:1; }; return 0; }");
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
  test_additive_semantic_lowering_boundary();
  test_cast_semantic_lowering_boundary();
  test_aggregate_cast_semantic_lowering_boundary();
  test_implicit_conversion_semantic_lowering_boundary();
  test_compound_assignment_semantic_lowering_boundary();
  test_complex_initializer_semantic_lowering_boundary();
  test_local_declaration_storage_plan_boundary();
  test_vla_lowering_request_boundary();
  test_parameter_declaration_storage_plan_boundary();
  test_global_declaration_storage_plan_boundary();
  test_declaration_pipeline_order_boundary();
  test_tag_declaration_resolution_boundary();
  test_aggregate_body_phase_boundary();
  test_declaration_phase_boundary();
  test_type_name_phase_boundary();
  test_toplevel_declarator_phase_boundary();
  test_local_declarator_application_boundary();
  test_local_declaration_resolution_boundary();
  test_aggregate_member_resolution_boundary();
  test_static_assert_resolution_boundary();
  test_typedef_declaration_resolution_boundary();
  test_enum_constant_resolution_boundary();
  test_initializer_resolution_boundary();
  test_local_initializer_parse_lowering_boundary();
  test_static_data_initializer_boundary();
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
