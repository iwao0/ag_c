#include "../src/tokenizer/tokenizer.h"
#include "../src/tokenizer/test_hook.h"
#include "../src/tokenizer/allocator.h"
#include "../src/diag/diag.h"
#include "../src/source_manager.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "test_common.h"

static token_num_int_t *as_num_i(token_t *tok) { return (token_num_int_t *)tok; }
static token_num_float_t *as_num_f(token_t *tok) { return (token_num_float_t *)tok; }
static token_ident_t *as_ident(token_t *tok) { return (token_ident_t *)tok; }
static token_string_t *as_string(token_t *tok) { return (token_string_t *)tok; }

typedef struct tokenizer_test_fixture {
  tokenizer_context_t tokenizer;
  ag_source_manager_t *sources;
  ag_diagnostic_context_t *diagnostics;
  tk_allocator_context_t *allocator;
} tokenizer_test_fixture_t;

static void init_explicit_context(tokenizer_test_fixture_t *fixture) {
  memset(fixture, 0, sizeof(*fixture));
  fixture->sources = ag_source_manager_create();
  fixture->diagnostics = diag_context_create(fixture->sources);
  fixture->allocator = tk_allocator_context_create(fixture->diagnostics);
  ASSERT_TRUE(tk_context_init(
      &fixture->tokenizer, fixture->diagnostics, fixture->allocator,
      fixture->sources));
}

static void dispose_explicit_context(tokenizer_test_fixture_t *fixture) {
  tk_context_dispose(&fixture->tokenizer);
  tk_allocator_context_destroy(fixture->allocator);
  diag_context_destroy(fixture->diagnostics);
  ag_source_manager_destroy(fixture->sources);
  memset(fixture, 0, sizeof(*fixture));
}

static void test_source_manager_dynamic_names(void) {
  printf("test_source_manager_dynamic_names...\n");
  ag_source_manager_t *sources = ag_source_manager_create();
  ASSERT_TRUE(sources != NULL);
  ag_diagnostic_context_t *diagnostics = diag_context_create(sources);
  ASSERT_TRUE(diagnostics != NULL);
  ASSERT_EQ(0, ag_source_manager_intern_name(sources, NULL));
  ASSERT_TRUE(ag_source_manager_name(sources, 0) == NULL);
  ag_source_manager_set_current_input(sources, "int value;");
  ag_source_manager_set_current_name(sources, "input.c");
  ASSERT_TRUE(strcmp(
      ag_source_manager_current_input(sources), "int value;") == 0);
  ASSERT_TRUE(strcmp(
      ag_source_manager_current_name(sources), "input.c") == 0);
  uint16_t last_id = 0;
  char name[32];
  for (int i = 0; i < 300; i++) {
    snprintf(name, sizeof(name), "include-%03d.h", i);
    last_id = ag_source_manager_intern_name(sources, name);
    ASSERT_TRUE(last_id != 0);
    ASSERT_TRUE(strcmp(ag_source_manager_name(sources, last_id), name) == 0);
  }
  ASSERT_TRUE(last_id > 256);
  token_t source_token = {
      .source_input = "value;",
      .line_no = 7,
      .byte_offset = 0,
      .byte_length = 1,
      .file_name_id = last_id,
      .kind = TK_SEMI,
  };
  ASSERT_TRUE(diag_report_tokf_in(
      diagnostics, DIAG_ERR_INTERNAL_USAGE, &source_token,
      "%s", "source manager diagnostic"));
  ASSERT_TRUE(strcmp(
      diag_context_record_source_name(diagnostics, 0),
      "include-299.h") == 0);
  ag_source_manager_reset_translation_unit(sources);
  ASSERT_TRUE(ag_source_manager_name(sources, last_id) == NULL);
  ASSERT_TRUE(ag_source_manager_current_input(sources) == NULL);
  ASSERT_TRUE(ag_source_manager_current_name(sources) == NULL);
  ASSERT_TRUE(strcmp(
      diag_context_record_source_name(diagnostics, 0),
      "include-299.h") == 0);
  diag_context_destroy(diagnostics);
  ag_source_manager_destroy(sources);
}

static void expect_tokenize_fail(tokenizer_context_t *test_ctx, const char *input) {
  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    tk_tokenize_ctx(test_ctx, (char *)input);
    _exit(0);
  }
  int status;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_TRUE(WEXITSTATUS(status) != 0);
}

static void expect_tokenize_ctx_fail(tokenizer_context_t *ctx, const char *input) {
  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    tk_tokenize_ctx(ctx, input);
    _exit(0);
  }
  int status;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_TRUE(WEXITSTATUS(status) != 0);
}

// 1. tk_tokenize_ctx(test_ctx, ) のテスト
static void test_tokenize(tokenizer_context_t *test_ctx) {
  printf("test_tokenize...\n");
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, " 12 + 34 - 5 "));

  // 最初のトークン: "12"
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(12, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  // 2番目のトークン: "+"
  ASSERT_EQ(TK_PLUS, tk_get_current_token_ctx(test_ctx)->kind);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  // 3番目のトークン: "34"
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(34, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  // 4番目のトークン: "-"
  ASSERT_EQ(TK_MINUS, tk_get_current_token_ctx(test_ctx)->kind);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  // 5番目のトークン: "5"
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(5, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  // 最後のトークン: EOF
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  // 四則演算と括弧のテスト
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "1 * (2 / 3)"));
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(1, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  ASSERT_EQ(TK_MUL, tk_get_current_token_ctx(test_ctx)->kind);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  ASSERT_EQ(TK_LPAREN, tk_get_current_token_ctx(test_ctx)->kind);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(2, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  ASSERT_EQ(TK_DIV, tk_get_current_token_ctx(test_ctx)->kind);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(3, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  ASSERT_EQ(TK_RPAREN, tk_get_current_token_ctx(test_ctx)->kind);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  // 比較演算子のテスト
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "1 == 2 != 3 <= 4 >= 5 < 6 > 7"));
  // 1
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(1, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  // ==
  ASSERT_EQ(TK_EQEQ, tk_get_current_token_ctx(test_ctx)->kind);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  // 2
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(2, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  // !=
  ASSERT_EQ(TK_NEQ, tk_get_current_token_ctx(test_ctx)->kind);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  // 3
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(3, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  // <=
  ASSERT_EQ(TK_LE, tk_get_current_token_ctx(test_ctx)->kind);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  // 4
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(4, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  // >=
  ASSERT_EQ(TK_GE, tk_get_current_token_ctx(test_ctx)->kind);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  // 5
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(5, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  // <
  ASSERT_EQ(TK_LT, tk_get_current_token_ctx(test_ctx)->kind);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  // 6
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(6, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  // >
  ASSERT_EQ(TK_GT, tk_get_current_token_ctx(test_ctx)->kind);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  // 7
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(7, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);
}

// 1b. 16進数/2進数リテラルのテスト
static void test_tokenize_int_literals(tokenizer_context_t *test_ctx) {
  printf("test_tokenize_int_literals...\n");
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "0x2a 0X10 0b101 0B11 077 010 0 123 1u 2UL 3llu 0x1ffffffff"));

  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(42, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  ASSERT_EQ(42ULL, as_num_i(tk_get_current_token_ctx(test_ctx))->uval);
  ASSERT_EQ(16, as_num_i(tk_get_current_token_ctx(test_ctx))->int_base);
  ASSERT_TRUE(!as_num_i(tk_get_current_token_ctx(test_ctx))->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_INT, as_num_i(tk_get_current_token_ctx(test_ctx))->int_size);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(16, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  ASSERT_EQ(16ULL, as_num_i(tk_get_current_token_ctx(test_ctx))->uval);
  ASSERT_EQ(16, as_num_i(tk_get_current_token_ctx(test_ctx))->int_base);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(5, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  ASSERT_EQ(5ULL, as_num_i(tk_get_current_token_ctx(test_ctx))->uval);
  ASSERT_EQ(2, as_num_i(tk_get_current_token_ctx(test_ctx))->int_base);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(3, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  ASSERT_EQ(3ULL, as_num_i(tk_get_current_token_ctx(test_ctx))->uval);
  ASSERT_EQ(2, as_num_i(tk_get_current_token_ctx(test_ctx))->int_base);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(63, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  ASSERT_EQ(63ULL, as_num_i(tk_get_current_token_ctx(test_ctx))->uval);
  ASSERT_EQ(8, as_num_i(tk_get_current_token_ctx(test_ctx))->int_base);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(8, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  ASSERT_EQ(8ULL, as_num_i(tk_get_current_token_ctx(test_ctx))->uval);
  ASSERT_EQ(8, as_num_i(tk_get_current_token_ctx(test_ctx))->int_base);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(0, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  ASSERT_EQ(0ULL, as_num_i(tk_get_current_token_ctx(test_ctx))->uval);
  ASSERT_EQ(10, as_num_i(tk_get_current_token_ctx(test_ctx))->int_base);
  ASSERT_TRUE(!as_num_i(tk_get_current_token_ctx(test_ctx))->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_INT, as_num_i(tk_get_current_token_ctx(test_ctx))->int_size);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(123, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  ASSERT_EQ(123ULL, as_num_i(tk_get_current_token_ctx(test_ctx))->uval);
  ASSERT_EQ(10, as_num_i(tk_get_current_token_ctx(test_ctx))->int_base);
  ASSERT_TRUE(!as_num_i(tk_get_current_token_ctx(test_ctx))->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_INT, as_num_i(tk_get_current_token_ctx(test_ctx))->int_size);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(1, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  ASSERT_TRUE(as_num_i(tk_get_current_token_ctx(test_ctx))->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_INT, as_num_i(tk_get_current_token_ctx(test_ctx))->int_size);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(2, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  ASSERT_TRUE(as_num_i(tk_get_current_token_ctx(test_ctx))->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_LONG, as_num_i(tk_get_current_token_ctx(test_ctx))->int_size);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(3, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  ASSERT_TRUE(as_num_i(tk_get_current_token_ctx(test_ctx))->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_LONG_LONG, as_num_i(tk_get_current_token_ctx(test_ctx))->int_size);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(0x1ffffffffULL, as_num_i(tk_get_current_token_ctx(test_ctx))->uval);
  ASSERT_TRUE(!as_num_i(tk_get_current_token_ctx(test_ctx))->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_LONG, as_num_i(tk_get_current_token_ctx(test_ctx))->int_size);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);
}

// 1c. 異常系トークナイズのテスト
static void test_tokenize_invalid(tokenizer_context_t *test_ctx) {
  printf("test_tokenize_invalid...\n");
  expect_tokenize_fail(test_ctx, "0x");          // 16進数の桁不足
  expect_tokenize_fail(test_ctx, "0b2");         // 2進数の不正桁
  expect_tokenize_fail(test_ctx, "08");          // 8進数の不正桁
  expect_tokenize_fail(test_ctx, "1uu");         // 整数サフィックス重複
  expect_tokenize_fail(test_ctx, "1lll");        // long サフィックス過多
  expect_tokenize_fail(test_ctx, "1.0q");        // 浮動小数点サフィックス不正
  expect_tokenize_fail(test_ctx, "/* unterminated"); // コメント未閉じ
  expect_tokenize_fail(test_ctx, "@");           // 不正文字
  expect_tokenize_fail(test_ctx, "18446744073709551616"); // ULLONG_MAX+1
  expect_tokenize_fail(test_ctx, "\"unterminated"); // 文字列未閉じ
  expect_tokenize_fail(test_ctx, "\"\\x\"");    // 16進エスケープ不正
  expect_tokenize_fail(test_ctx, "\"\\xG\"");   // 16進エスケープ直後に非hex
  expect_tokenize_fail(test_ctx, "\"\\q\"");    // 不正エスケープ
  expect_tokenize_fail(test_ctx, "\"\\u202E\""); // bidi制御文字UCN
  expect_tokenize_fail(test_ctx, "\"\\u200C\""); // ゼロ幅UCN
  expect_tokenize_fail(test_ctx, "\"\\U00110000\""); // Unicode上限超過
  expect_tokenize_fail(test_ctx, "\"\\U0000D800\""); // surrogate
  expect_tokenize_fail(test_ctx, "\"\\U0000001F\""); // 制御文字UCN
  expect_tokenize_fail(test_ctx, "''");         // 空の文字リテラル
  expect_tokenize_fail(test_ctx, "'\\x'");      // 16進エスケープ不正
  expect_tokenize_fail(test_ctx, "'\\xG'");     // 16進エスケープ直後に非hex
  expect_tokenize_fail(test_ctx, "'\\u202E'");  // bidi制御文字UCN
  expect_tokenize_fail(test_ctx, "'\\u200D'");  // ゼロ幅UCN
  expect_tokenize_fail(test_ctx, "'\\U00110000'"); // Unicode上限超過
  expect_tokenize_fail(test_ctx, "'\\U0000DFFF'"); // surrogate
  expect_tokenize_fail(test_ctx, "safe\\u202Ename"); // 識別子内bidi制御文字UCN
  expect_tokenize_fail(test_ctx, "safe\\u200Cname"); // 識別子内ゼロ幅UCN
  expect_tokenize_fail(test_ctx, "\"\\\n\\u202E\""); // 行継続 + bidi制御文字UCN
  expect_tokenize_fail(test_ctx, "safe\\\n\\u200Cname"); // 行継続 + 識別子内ゼロ幅UCN
  expect_tokenize_fail(test_ctx, "\x80");        // 孤立したUTF-8継続バイト
  expect_tokenize_fail(test_ctx, "\xC0\xAF");    // 不正UTF-8シーケンス（overlong）
  expect_tokenize_fail(test_ctx, "1.0f0");      // pp-number 連結
  expect_tokenize_fail(test_ctx, "1..2");       // pp-number 連結

  // 大きすぎるトークン長を安全に拒否できること（テスト用上限を利用）
  tk_set_max_token_len_limit_for_test_ctx(test_ctx, 8);
  expect_tokenize_fail(test_ctx, "identifier_too_long");
  expect_tokenize_fail(test_ctx, "\"string_too_long\"");
  tk_set_max_token_len_limit_for_test_ctx(test_ctx, 0);
}

static void test_tokenize_len_guard_boundaries(tokenizer_context_t *test_ctx) {
  printf("test_tokenize_len_guard_boundaries...\n");
  tk_set_max_token_len_limit_for_test_ctx(test_ctx, 8);
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "abcdefgh"));
  ASSERT_EQ(TK_IDENT, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(8, as_ident(tk_get_current_token_ctx(test_ctx))->len);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);
  expect_tokenize_fail(test_ctx, "abcdefghi");
  tk_set_max_token_len_limit_for_test_ctx(test_ctx, 0);
}

// 1c. ローカル変数・複数文字識別子のテスト
static void test_tokenize_ident(tokenizer_context_t *test_ctx) {
  printf("test_tokenize_ident...\n");
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "a = 3; b = a;"));

  ASSERT_EQ(TK_IDENT, tk_get_current_token_ctx(test_ctx)->kind); ASSERT_EQ('a', as_ident(tk_get_current_token_ctx(test_ctx))->str[0]); ASSERT_EQ(1, as_ident(tk_get_current_token_ctx(test_ctx))->len); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_ASSIGN, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind); ASSERT_EQ(3, as_num_i(tk_get_current_token_ctx(test_ctx))->val); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_SEMI, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_IDENT, tk_get_current_token_ctx(test_ctx)->kind); ASSERT_EQ('b', as_ident(tk_get_current_token_ctx(test_ctx))->str[0]); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_ASSIGN, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_IDENT, tk_get_current_token_ctx(test_ctx)->kind); ASSERT_EQ('a', as_ident(tk_get_current_token_ctx(test_ctx))->str[0]); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_SEMI, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  // 複数文字識別子
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "foo my_var x1"));
  ASSERT_EQ(TK_IDENT, tk_get_current_token_ctx(test_ctx)->kind); ASSERT_EQ(3, as_ident(tk_get_current_token_ctx(test_ctx))->len);
  ASSERT_TRUE(strncmp(as_ident(tk_get_current_token_ctx(test_ctx))->str, "foo", 3) == 0); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_IDENT, tk_get_current_token_ctx(test_ctx)->kind); ASSERT_EQ(6, as_ident(tk_get_current_token_ctx(test_ctx))->len);
  ASSERT_TRUE(strncmp(as_ident(tk_get_current_token_ctx(test_ctx))->str, "my_var", 6) == 0); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_IDENT, tk_get_current_token_ctx(test_ctx)->kind); ASSERT_EQ(2, as_ident(tk_get_current_token_ctx(test_ctx))->len);
  ASSERT_TRUE(strncmp(as_ident(tk_get_current_token_ctx(test_ctx))->str, "x1", 2) == 0); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  // アンダースコアで始まる識別子
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "_start _a1"));
  ASSERT_EQ(TK_IDENT, tk_get_current_token_ctx(test_ctx)->kind); ASSERT_EQ(6, as_ident(tk_get_current_token_ctx(test_ctx))->len);
  ASSERT_TRUE(strncmp(as_ident(tk_get_current_token_ctx(test_ctx))->str, "_start", 6) == 0); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_IDENT, tk_get_current_token_ctx(test_ctx)->kind); ASSERT_EQ(3, as_ident(tk_get_current_token_ctx(test_ctx))->len);
  ASSERT_TRUE(strncmp(as_ident(tk_get_current_token_ctx(test_ctx))->str, "_a1", 3) == 0); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);
}

// 1c. キーワードトークンのテスト
static void test_tokenize_keywords(tokenizer_context_t *test_ctx) {
  printf("test_tokenize_keywords...\n");

  // 制御構文キーワード
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "if else while for return"));
  ASSERT_EQ(TK_IF, tk_get_current_token_ctx(test_ctx)->kind);     tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_ELSE, tk_get_current_token_ctx(test_ctx)->kind);   tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_WHILE, tk_get_current_token_ctx(test_ctx)->kind);  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_FOR, tk_get_current_token_ctx(test_ctx)->kind);    tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_RETURN, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  // 型キーワード
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "int char void short long float double signed unsigned"));
  ASSERT_EQ(TK_INT, tk_get_current_token_ctx(test_ctx)->kind);     tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_CHAR, tk_get_current_token_ctx(test_ctx)->kind);    tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_VOID, tk_get_current_token_ctx(test_ctx)->kind);    tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_SHORT, tk_get_current_token_ctx(test_ctx)->kind);   tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_LONG, tk_get_current_token_ctx(test_ctx)->kind);    tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_FLOAT, tk_get_current_token_ctx(test_ctx)->kind);   tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_DOUBLE, tk_get_current_token_ctx(test_ctx)->kind);  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_SIGNED, tk_get_current_token_ctx(test_ctx)->kind);  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_UNSIGNED, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  // その他のキーワード
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx,
      "auto break case const continue default do enum extern goto inline "
      "register restrict sizeof static struct switch typedef union volatile "
      "_Alignas _Alignof _Atomic _Bool _Complex _Generic _Imaginary "
      "_Noreturn _Static_assert _Thread_local"));
  ASSERT_EQ(TK_AUTO, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_BREAK, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_CASE, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_CONST, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_CONTINUE, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_DEFAULT, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_DO, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_ENUM, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EXTERN, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_GOTO, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_INLINE, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_REGISTER, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_RESTRICT, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_SIZEOF, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_STATIC, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_STRUCT, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_SWITCH, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_TYPEDEF, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_UNION, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_VOLATILE, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_ALIGNAS, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_ALIGNOF, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_ATOMIC, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_BOOL, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_COMPLEX, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_GENERIC, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_IMAGINARY, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_NORETURN, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_STATIC_ASSERT, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_THREAD_LOCAL, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  // キーワードと似た識別子はキーワードにならない
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "iff int1 returns"));
  ASSERT_EQ(TK_IDENT, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_TRUE(strncmp(as_ident(tk_get_current_token_ctx(test_ctx))->str, "iff", 3) == 0); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_IDENT, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_TRUE(strncmp(as_ident(tk_get_current_token_ctx(test_ctx))->str, "int1", 4) == 0); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_IDENT, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_TRUE(strncmp(as_ident(tk_get_current_token_ctx(test_ctx))->str, "returns", 7) == 0); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);
}

// 1d. 追加記号のテスト
static void test_tokenize_symbols(tokenizer_context_t *test_ctx) {
  printf("test_tokenize_symbols...\n");
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "{ } , & [ ]"));
  ASSERT_EQ(TK_LBRACE, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_RBRACE, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_COMMA, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_AMP, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_LBRACKET, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_RBRACKET, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);
}

// 1d-2. 追加演算子のテスト
static void test_tokenize_punctuators(tokenizer_context_t *test_ctx) {
  printf("test_tokenize_punctuators...\n");
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "++ -- -> << >> <<= >>= += -= *= /= %= &= ^= |= % | ^ ? : ..."));
  ASSERT_EQ(TK_INC, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_DEC, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_ARROW, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_SHL, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_SHR, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_SHLEQ, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_SHREQ, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_PLUSEQ, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_MINUSEQ, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_MULEQ, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_DIVEQ, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_MODEQ, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_ANDEQ, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_XOREQ, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_OREQ, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_MOD, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_PIPE, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_CARET, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_QUESTION, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_COLON, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_ELLIPSIS, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "<: :> <% %> %: %:%:"));
  ASSERT_EQ(TK_LBRACKET, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_RBRACKET, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_LBRACE, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_RBRACE, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_HASH, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_HASHHASH, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "#include \"x.h\"\nint main() { return 0; }"));
  ASSERT_EQ(TK_HASH, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_TRUE(tk_get_current_token_ctx(test_ctx)->at_bol);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_IDENT, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_STRING, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_INT, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_IDENT, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
}

// 1d-3. コメントと空白のテスト
static void test_tokenize_comments(tokenizer_context_t *test_ctx) {
  printf("test_tokenize_comments...\n");
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "1//comment\n2"));
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_TRUE(tk_get_current_token_ctx(test_ctx)->at_bol);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "1/* comment */2"));
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_TRUE(!tk_get_current_token_ctx(test_ctx)->at_bol);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "1/*\n*/2"));
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_TRUE(tk_get_current_token_ctx(test_ctx)->at_bol);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);
}

// 1e. 文字列リテラルのテスト
static void test_tokenize_string(tokenizer_context_t *test_ctx) {
  printf("test_tokenize_string...\n");
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "\"hello\""));
  ASSERT_EQ(TK_STRING, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(5, as_string(tk_get_current_token_ctx(test_ctx))->len);
  ASSERT_TRUE(strncmp(as_string(tk_get_current_token_ctx(test_ctx))->str, "hello", 5) == 0);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  // 空文字列
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "\"\""));
  ASSERT_EQ(TK_STRING, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(0, as_string(tk_get_current_token_ctx(test_ctx))->len);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  // 文字列と他のトークンの混在
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "char *s = \"AB\";"));
  ASSERT_EQ(TK_CHAR, tk_get_current_token_ctx(test_ctx)->kind);     tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_MUL, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_IDENT, tk_get_current_token_ctx(test_ctx)->kind);    tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_ASSIGN, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_STRING, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(2, as_string(tk_get_current_token_ctx(test_ctx))->len);
  ASSERT_TRUE(strncmp(as_string(tk_get_current_token_ctx(test_ctx))->str, "AB", 2) == 0);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_SEMI, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  // エスケープを含む文字列
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "\"a\\\"b\""));
  ASSERT_EQ(TK_STRING, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(4, as_string(tk_get_current_token_ctx(test_ctx))->len);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);
}

// 1g. 浮動小数点リテラルのテスト
static void test_tokenize_float_literal(tokenizer_context_t *test_ctx) {
  printf("test_tokenize_float_literal...\n");
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "3.14 1.5f 2.0E-3 0x1.8p1 0x1p2f 4.0L 0x1p2L"));

  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, as_num_f(tk_get_current_token_ctx(test_ctx))->fp_kind);
  ASSERT_EQ(TK_FLOAT_SUFFIX_NONE, as_num_f(tk_get_current_token_ctx(test_ctx))->float_suffix_kind);
  ASSERT_TRUE(as_num_f(tk_get_current_token_ctx(test_ctx))->fval > 3.13 && as_num_f(tk_get_current_token_ctx(test_ctx))->fval < 3.15);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT, as_num_f(tk_get_current_token_ctx(test_ctx))->fp_kind);
  ASSERT_EQ(TK_FLOAT_SUFFIX_F, as_num_f(tk_get_current_token_ctx(test_ctx))->float_suffix_kind);
  ASSERT_TRUE(as_num_f(tk_get_current_token_ctx(test_ctx))->fval > 1.49 && as_num_f(tk_get_current_token_ctx(test_ctx))->fval < 1.51);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, as_num_f(tk_get_current_token_ctx(test_ctx))->fp_kind);
  ASSERT_EQ(TK_FLOAT_SUFFIX_NONE, as_num_f(tk_get_current_token_ctx(test_ctx))->float_suffix_kind);
  ASSERT_TRUE(as_num_f(tk_get_current_token_ctx(test_ctx))->fval > 0.0019 && as_num_f(tk_get_current_token_ctx(test_ctx))->fval < 0.0021);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, as_num_f(tk_get_current_token_ctx(test_ctx))->fp_kind);
  ASSERT_EQ(TK_FLOAT_SUFFIX_NONE, as_num_f(tk_get_current_token_ctx(test_ctx))->float_suffix_kind);
  ASSERT_TRUE(as_num_f(tk_get_current_token_ctx(test_ctx))->fval > 2.9 && as_num_f(tk_get_current_token_ctx(test_ctx))->fval < 3.1);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT, as_num_f(tk_get_current_token_ctx(test_ctx))->fp_kind);
  ASSERT_EQ(TK_FLOAT_SUFFIX_F, as_num_f(tk_get_current_token_ctx(test_ctx))->float_suffix_kind);
  ASSERT_TRUE(as_num_f(tk_get_current_token_ctx(test_ctx))->fval > 3.9 && as_num_f(tk_get_current_token_ctx(test_ctx))->fval < 4.1);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(TK_FLOAT_KIND_LONG_DOUBLE, as_num_f(tk_get_current_token_ctx(test_ctx))->fp_kind);
  ASSERT_EQ(TK_FLOAT_SUFFIX_L, as_num_f(tk_get_current_token_ctx(test_ctx))->float_suffix_kind);
  ASSERT_TRUE(as_num_f(tk_get_current_token_ctx(test_ctx))->fval > 3.9 && as_num_f(tk_get_current_token_ctx(test_ctx))->fval < 4.1);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(TK_FLOAT_KIND_LONG_DOUBLE, as_num_f(tk_get_current_token_ctx(test_ctx))->fp_kind);
  ASSERT_EQ(TK_FLOAT_SUFFIX_L, as_num_f(tk_get_current_token_ctx(test_ctx))->float_suffix_kind);
  ASSERT_TRUE(as_num_f(tk_get_current_token_ctx(test_ctx))->fval > 3.9 && as_num_f(tk_get_current_token_ctx(test_ctx))->fval < 4.1);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);

  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);
}

// 2. tk_consume_ctx(test_ctx, ) のテスト
static void test_consume(tokenizer_context_t *test_ctx) {
  printf("test_consume...\n");
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, " + 42 "));

  ASSERT_TRUE(tk_consume_ctx(test_ctx, '+'));  // "+"を消費して次に進む
  ASSERT_TRUE(!tk_consume_ctx(test_ctx, '-')); // "-"ではないので進まない
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(42, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
}

// 2b. tk_consume_str_ctx(test_ctx, ) のテスト
static void test_consume_str(tokenizer_context_t *test_ctx) {
  printf("test_consume_str...\n");
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "== != < <="));
  ASSERT_TRUE(tk_consume_str_ctx(test_ctx, "=="));
  ASSERT_TRUE(!tk_consume_str_ctx(test_ctx, "=="));  // 次は != なので false
  ASSERT_TRUE(tk_consume_str_ctx(test_ctx, "!="));
  ASSERT_TRUE(!tk_consume_str_ctx(test_ctx, "<="));  // 次は < (1文字) なので <= にはマッチしない
  ASSERT_TRUE(tk_consume_ctx(test_ctx, '<'));
  ASSERT_TRUE(tk_consume_str_ctx(test_ctx, "<="));
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);
}

// 2c. tk_consume_ident_ctx(test_ctx) のテスト
static void test_consume_ident(tokenizer_context_t *test_ctx) {
  printf("test_consume_ident...\n");
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "foo 42"));
  token_ident_t *tok = tk_consume_ident_ctx(test_ctx);
  ASSERT_TRUE(tok != NULL);
  ASSERT_EQ(3, tok->len);
  ASSERT_TRUE(strncmp(tok->str, "foo", 3) == 0);
  // 数値トークンでは NULL を返す
  tok = tk_consume_ident_ctx(test_ctx);
  ASSERT_TRUE(tok == NULL);
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
}

// 3. tk_expect_ctx(test_ctx, ) のテスト
static void test_expect(tokenizer_context_t *test_ctx) {
  printf("test_expect...\n");
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, " - "));
  tk_expect_ctx(test_ctx, '-');
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);
}

// 4. tk_expect_number_ctx(test_ctx) のテスト
static void test_expect_number(tokenizer_context_t *test_ctx) {
  printf("test_expect_number...\n");
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, " 999 "));
  int val = tk_expect_number_ctx(test_ctx);
  ASSERT_EQ(999, val);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);
}

// 5. tk_at_eof_ctx(test_ctx) のテスト
static void test_at_eof(tokenizer_context_t *test_ctx) {
  printf("test_at_eof...\n");
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, " 1 "));
  ASSERT_TRUE(!tk_at_eof_ctx(test_ctx));
  tk_expect_number_ctx(test_ctx);
  ASSERT_TRUE(tk_at_eof_ctx(test_ctx));
}

static void test_null_cursor_boundaries(tokenizer_context_t *test_ctx) {
  printf("test_null_cursor_boundaries...\n");
  tk_set_current_token_ctx(test_ctx, NULL);

  ASSERT_TRUE(!tk_at_eof_ctx(test_ctx));
  ASSERT_TRUE(!tk_consume_ctx(test_ctx, '+'));
  ASSERT_TRUE(!tk_consume_str_ctx(test_ctx, "=="));
  ASSERT_TRUE(tk_consume_ident_ctx(test_ctx) == NULL);

  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    tk_set_current_token_ctx(test_ctx, NULL);
    tk_expect_ctx(test_ctx, '+');
    _exit(0);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_TRUE(WEXITSTATUS(status) != 0);

  fflush(NULL);
  pid = fork();
  if (pid == 0) {
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    tk_set_current_token_ctx(test_ctx, NULL);
    tk_expect_number_ctx(test_ctx);
    _exit(0);
  }
  status = 0;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_TRUE(WEXITSTATUS(status) != 0);
}

// 1f. 文字リテラルのテスト
static void test_tokenize_char_literal(tokenizer_context_t *test_ctx) {
  printf("test_tokenize_char_literal...\n");
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "'A'"));
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(65, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  // エスケープシーケンス
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "'\\n'"));
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(10, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "'\\0'"));
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(0, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  // マルチ文字文字定数（実装定義）
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "'ab'"));
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(((unsigned char)'a' << 8) | (unsigned char)'b', as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  // 接頭辞付き文字定数
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "L'A' u'B' U'\\u00A9'"));
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ('A', as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  ASSERT_EQ(TK_CHAR_WIDTH_CHAR32, as_num_i(tk_get_current_token_ctx(test_ctx))->char_width);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ('B', as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  ASSERT_EQ(TK_CHAR_WIDTH_CHAR16, as_num_i(tk_get_current_token_ctx(test_ctx))->char_width);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(0xA9, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  ASSERT_EQ(TK_CHAR_WIDTH_CHAR32, as_num_i(tk_get_current_token_ctx(test_ctx))->char_width);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  // 接頭辞付きマルチ文字定数（実装定義として受理）
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "L'AB' u'CD' U'EF'"));
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(((unsigned char)'A' << 8) | (unsigned char)'B', as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  ASSERT_EQ(TK_CHAR_WIDTH_CHAR32, as_num_i(tk_get_current_token_ctx(test_ctx))->char_width);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(((unsigned char)'C' << 8) | (unsigned char)'D', as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  ASSERT_EQ(TK_CHAR_WIDTH_CHAR16, as_num_i(tk_get_current_token_ctx(test_ctx))->char_width);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(((unsigned char)'E' << 8) | (unsigned char)'F', as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  ASSERT_EQ(TK_CHAR_WIDTH_CHAR32, as_num_i(tk_get_current_token_ctx(test_ctx))->char_width);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);
}

static void test_tokenize_string_prefixes_and_ucn(tokenizer_context_t *test_ctx) {
  printf("test_tokenize_string_prefixes_and_ucn...\n");
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "L\"wide\" u\"u16\" U\"u32\" u8\"utf8\" \"\\u00A9\""));
  ASSERT_EQ(TK_STRING, tk_get_current_token_ctx(test_ctx)->kind); ASSERT_EQ(4, as_string(tk_get_current_token_ctx(test_ctx))->len); ASSERT_EQ(TK_CHAR_WIDTH_CHAR32, as_string(tk_get_current_token_ctx(test_ctx))->char_width); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_STRING, tk_get_current_token_ctx(test_ctx)->kind); ASSERT_EQ(3, as_string(tk_get_current_token_ctx(test_ctx))->len); ASSERT_EQ(TK_CHAR_WIDTH_CHAR16, as_string(tk_get_current_token_ctx(test_ctx))->char_width); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_STRING, tk_get_current_token_ctx(test_ctx)->kind); ASSERT_EQ(3, as_string(tk_get_current_token_ctx(test_ctx))->len); ASSERT_EQ(TK_CHAR_WIDTH_CHAR32, as_string(tk_get_current_token_ctx(test_ctx))->char_width); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_STRING, tk_get_current_token_ctx(test_ctx)->kind); ASSERT_EQ(4, as_string(tk_get_current_token_ctx(test_ctx))->len); ASSERT_EQ(TK_CHAR_WIDTH_CHAR, as_string(tk_get_current_token_ctx(test_ctx))->char_width); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_STRING, tk_get_current_token_ctx(test_ctx)->kind); ASSERT_EQ(6, as_string(tk_get_current_token_ctx(test_ctx))->len); ASSERT_EQ(TK_CHAR_WIDTH_CHAR, as_string(tk_get_current_token_ctx(test_ctx))->char_width); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);
}

static void test_tokenize_ucn_ident_and_trigraph(tokenizer_context_t *test_ctx) {
  printf("test_tokenize_ucn_ident_and_trigraph...\n");
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "foo\\u00A9 = 1;"));
  ASSERT_EQ(TK_IDENT, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(5, as_ident(tk_get_current_token_ctx(test_ctx))->len);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_ASSIGN, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_SEMI, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  tk_ctx_set_enable_trigraphs(test_ctx, true);
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "?" "?=define X 1"));
  ASSERT_EQ(TK_HASH, tk_get_current_token_ctx(test_ctx)->kind);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_IDENT, tk_get_current_token_ctx(test_ctx)->kind);

  tk_ctx_set_enable_trigraphs(test_ctx, false);
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "?" "?=define X 1"));
  ASSERT_EQ(TK_QUESTION, tk_get_current_token_ctx(test_ctx)->kind);
  tk_ctx_set_enable_trigraphs(test_ctx, true);
}

// 意地悪テスト: トークン分割の境界ケース
static void test_tokenize_evil_edge_cases(tokenizer_context_t *test_ctx) {
  printf("test_tokenize_evil_edge_cases...\n");

  // x+++y → x ++ + y
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "x+++y"));
  ASSERT_EQ(TK_IDENT, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_INC, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_PLUS, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_IDENT, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  // x---y → x -- - y
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "x---y"));
  ASSERT_EQ(TK_IDENT, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_DEC, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_MINUS, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_IDENT, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  // x+++-y → x ++ + - y
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "x+++-y"));
  ASSERT_EQ(TK_IDENT, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_INC, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_PLUS, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_MINUS, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_IDENT, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  // <<=>> → <<= >> (最長一致)
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "<<=>>"));
  ASSERT_EQ(TK_SHLEQ, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_SHR, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  // &&|| → && ||
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "&&||"));
  ASSERT_EQ(TK_ANDAND, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_OROR, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  // ->* → -> *
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "->*"));
  ASSERT_EQ(TK_ARROW, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_MUL, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  // >=< → >= <
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, ">=<"));
  ASSERT_EQ(TK_GE, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_LT, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  // !=== → != ==
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "!==="));
  ASSERT_EQ(TK_NEQ, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EQEQ, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  // 16進の境界値
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "0x0"));
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(0, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "0xFFFFFFFF"));
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(0xFFFFFFFFULL, as_num_i(tk_get_current_token_ctx(test_ctx))->uval);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  // 空白なしの連続演算
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "1+2*3-4/5%6"));
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind); ASSERT_EQ(1, as_num_i(tk_get_current_token_ctx(test_ctx))->val); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_PLUS, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind); ASSERT_EQ(2, as_num_i(tk_get_current_token_ctx(test_ctx))->val); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_MUL, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind); ASSERT_EQ(3, as_num_i(tk_get_current_token_ctx(test_ctx))->val); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_MINUS, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind); ASSERT_EQ(4, as_num_i(tk_get_current_token_ctx(test_ctx))->val); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_DIV, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind); ASSERT_EQ(5, as_num_i(tk_get_current_token_ctx(test_ctx))->val); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_MOD, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind); ASSERT_EQ(6, as_num_i(tk_get_current_token_ctx(test_ctx))->val); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  // 文字列中の特殊文字の後にすぐトークン
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "\"abc\"+1"));
  ASSERT_EQ(TK_STRING, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_PLUS, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  // コメントの直後に演算子（空白なし）
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "1/**/+2"));
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind); ASSERT_EQ(1, as_num_i(tk_get_current_token_ctx(test_ctx))->val); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_PLUS, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind); ASSERT_EQ(2, as_num_i(tk_get_current_token_ctx(test_ctx))->val); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  // 文字リテラル直後に演算子
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "'a'+1"));
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_PLUS, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  // 文字列直後にセミコロン
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "\"abc\";"));
  ASSERT_EQ(TK_STRING, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(3, as_string(tk_get_current_token_ctx(test_ctx))->len);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_SEMI, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);

  // 連続するドット: .. は不正、... は省略記号
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "...;"));
  ASSERT_EQ(TK_ELLIPSIS, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_SEMI, tk_get_current_token_ctx(test_ctx)->kind); tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_ctx)->kind);
}

static void test_strict_c11_mode(tokenizer_context_t *test_ctx) {
  printf("test_strict_c11_mode...\n");
  tk_ctx_set_strict_c11_mode(test_ctx, false);
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "0b101"));
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(5, as_num_i(tk_get_current_token_ctx(test_ctx))->val);

  tk_ctx_set_strict_c11_mode(test_ctx, true);
  expect_tokenize_fail(test_ctx, "0b101");
  tk_ctx_set_strict_c11_mode(test_ctx, false);

  tk_ctx_set_enable_binary_literals(test_ctx, false);
  expect_tokenize_fail(test_ctx, "0b101");
  tk_ctx_set_enable_binary_literals(test_ctx, true);
}

static void test_c11_audit_mode_flag(tokenizer_context_t *test_ctx) {
  printf("test_c11_audit_mode_flag...\n");
  tk_ctx_set_enable_c11_audit_extensions(test_ctx, false);
  ASSERT_TRUE(!tk_ctx_get_enable_c11_audit_extensions(test_ctx));
  tk_ctx_set_enable_c11_audit_extensions(test_ctx, true);
  ASSERT_TRUE(tk_ctx_get_enable_c11_audit_extensions(test_ctx));
  tk_ctx_set_enable_c11_audit_extensions(test_ctx, false);
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "0b101"));
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(5, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
}

static void test_runtime_mode_switch_boundaries(tokenizer_context_t *test_ctx) {
  printf("test_runtime_mode_switch_boundaries...\n");

  // strict/binary の切替境界
  tk_ctx_set_strict_c11_mode(test_ctx, false);
  tk_ctx_set_enable_binary_literals(test_ctx, true);
  ASSERT_TRUE(!tk_ctx_get_strict_c11_mode(test_ctx));
  ASSERT_TRUE(tk_ctx_get_enable_binary_literals(test_ctx));
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "0b11"));
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(3, as_num_i(tk_get_current_token_ctx(test_ctx))->val);

  tk_ctx_set_strict_c11_mode(test_ctx, true);
  ASSERT_TRUE(tk_ctx_get_strict_c11_mode(test_ctx));
  expect_tokenize_fail(test_ctx, "0b11");

  tk_ctx_set_strict_c11_mode(test_ctx, false);
  tk_ctx_set_enable_binary_literals(test_ctx, false);
  ASSERT_TRUE(!tk_ctx_get_strict_c11_mode(test_ctx));
  ASSERT_TRUE(!tk_ctx_get_enable_binary_literals(test_ctx));
  expect_tokenize_fail(test_ctx, "0b11");

  tk_ctx_set_enable_binary_literals(test_ctx, true);
  ASSERT_TRUE(tk_ctx_get_enable_binary_literals(test_ctx));
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "0b11"));
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(3, as_num_i(tk_get_current_token_ctx(test_ctx))->val);

  // trigraph の切替境界
  tk_ctx_set_enable_trigraphs(test_ctx, false);
  ASSERT_TRUE(!tk_ctx_get_enable_trigraphs(test_ctx));
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "?" "?="));
  ASSERT_EQ(TK_QUESTION, tk_get_current_token_ctx(test_ctx)->kind);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_QUESTION, tk_get_current_token_ctx(test_ctx)->kind);
  tk_set_current_token_ctx(test_ctx, tk_get_current_token_ctx(test_ctx)->next);
  ASSERT_EQ(TK_ASSIGN, tk_get_current_token_ctx(test_ctx)->kind);

  tk_ctx_set_enable_trigraphs(test_ctx, true);
  ASSERT_TRUE(tk_ctx_get_enable_trigraphs(test_ctx));
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "?" "?="));
  ASSERT_EQ(TK_HASH, tk_get_current_token_ctx(test_ctx)->kind);

  // audit の切替境界（トークナイズが壊れないことを確認）
  tk_ctx_set_enable_c11_audit_extensions(test_ctx, true);
  ASSERT_TRUE(tk_ctx_get_enable_c11_audit_extensions(test_ctx));
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "0b101"));
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(5, as_num_i(tk_get_current_token_ctx(test_ctx))->val);

  tk_ctx_set_enable_c11_audit_extensions(test_ctx, false);
  ASSERT_TRUE(!tk_ctx_get_enable_c11_audit_extensions(test_ctx));
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "0b101"));
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(5, as_num_i(tk_get_current_token_ctx(test_ctx))->val);
}

static void test_tokenize_with_explicit_context(tokenizer_context_t *test_ctx) {
  printf("test_tokenize_with_explicit_context...\n");
  tk_ctx_set_strict_c11_mode(test_ctx, false);
  tk_ctx_set_enable_binary_literals(test_ctx, true);

  tokenizer_test_fixture_t fixture;
  init_explicit_context(&fixture);
  tokenizer_context_t *ctx = &fixture.tokenizer;
  tk_ctx_set_strict_c11_mode(ctx, true);
  tk_ctx_set_enable_binary_literals(ctx, true);

  // default context では許可
  tk_set_current_token_ctx(test_ctx, tk_tokenize_ctx(test_ctx, "0b101"));
  ASSERT_EQ(TK_NUM, tk_get_current_token_ctx(test_ctx)->kind);
  ASSERT_EQ(5, as_num_i(tk_get_current_token_ctx(test_ctx))->val);

  // explicit context（strict=true）では拒否
  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    tk_tokenize_ctx(ctx, "0b101");
    _exit(0);
  }
  int status;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_TRUE(WEXITSTATUS(status) != 0);
  dispose_explicit_context(&fixture);
}

static void test_context_config_isolation_and_switch_timing(void) {
  printf("test_context_config_isolation_and_switch_timing...\n");

  tokenizer_test_fixture_t strict_fixture;
  tokenizer_test_fixture_t relaxed_fixture;
  init_explicit_context(&strict_fixture);
  init_explicit_context(&relaxed_fixture);
  tokenizer_context_t *ctx_strict = &strict_fixture.tokenizer;
  tokenizer_context_t *ctx_relaxed = &relaxed_fixture.tokenizer;

  tk_ctx_set_strict_c11_mode(ctx_strict, true);
  tk_ctx_set_enable_binary_literals(ctx_strict, true);
  tk_ctx_set_enable_trigraphs(ctx_strict, false);
  tk_ctx_set_enable_c11_audit_extensions(ctx_strict, false);

  tk_ctx_set_strict_c11_mode(ctx_relaxed, false);
  tk_ctx_set_enable_binary_literals(ctx_relaxed, true);
  tk_ctx_set_enable_trigraphs(ctx_relaxed, true);
  tk_ctx_set_enable_c11_audit_extensions(ctx_relaxed, true);

  // strict ctx では 0b を拒否、relaxed ctx では受理
  expect_tokenize_ctx_fail(ctx_strict, "0b101");
  token_t *tok = tk_tokenize_ctx(ctx_relaxed, "0b101");
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_EQ(5, as_num_i(tok)->val);

  // trigraph の設定は context ごとに独立
  tok = tk_tokenize_ctx(ctx_relaxed, "?" "?=");
  ASSERT_EQ(TK_HASH, tok->kind);
  tok = tk_tokenize_ctx(ctx_strict, "?" "?=");
  ASSERT_EQ(TK_QUESTION, tok->kind);

  // 途中で strict を切り替えたら次回 tokenize から反映
  tk_ctx_set_strict_c11_mode(ctx_relaxed, true);
  expect_tokenize_ctx_fail(ctx_relaxed, "0b101");
  tk_ctx_set_strict_c11_mode(ctx_relaxed, false);
  tok = tk_tokenize_ctx(ctx_relaxed, "0b101");
  ASSERT_EQ(TK_NUM, tok->kind);
  dispose_explicit_context(&relaxed_fixture);
  dispose_explicit_context(&strict_fixture);
}

static void test_interleaved_stream_context_isolation(tokenizer_context_t *test_ctx) {
  printf("test_interleaved_stream_context_isolation...\n");

  (void)test_ctx;
  tokenizer_test_fixture_t relaxed_fixture;
  tokenizer_test_fixture_t strict_fixture;
  init_explicit_context(&relaxed_fixture);
  init_explicit_context(&strict_fixture);
  tokenizer_context_t *relaxed = &relaxed_fixture.tokenizer;
  tokenizer_context_t *strict = &strict_fixture.tokenizer;
  tk_ctx_set_strict_c11_mode(relaxed, false);
  tk_ctx_set_enable_binary_literals(relaxed, true);
  tk_ctx_set_strict_c11_mode(strict, true);
  tk_ctx_set_enable_binary_literals(strict, false);

  tk_token_stream_t *relaxed_stream = tk_stream_new(relaxed, "0b11 + 1");
  tk_token_stream_t *strict_stream = tk_stream_new(strict, "2 + 3");
  ASSERT_TRUE(relaxed_stream != NULL);
  ASSERT_TRUE(strict_stream != NULL);

  token_t *relaxed_token = tk_stream_next(relaxed_stream);
  token_t *strict_token = tk_stream_next(strict_stream);
  ASSERT_EQ(TK_NUM, relaxed_token->kind);
  ASSERT_EQ(3, as_num_i(relaxed_token)->val);
  ASSERT_EQ(TK_NUM, strict_token->kind);
  ASSERT_EQ(2, as_num_i(strict_token)->val);

  relaxed_token = tk_stream_next(relaxed_stream);
  strict_token = tk_stream_next(strict_stream);
  ASSERT_EQ(TK_PLUS, relaxed_token->kind);
  ASSERT_EQ(TK_PLUS, strict_token->kind);

  tk_stream_delete(strict_stream);
  tk_stream_delete(relaxed_stream);
  dispose_explicit_context(&strict_fixture);
  dispose_explicit_context(&relaxed_fixture);
}

static void test_context_failure_path_isolation(tokenizer_context_t *test_ctx) {
  printf("test_context_failure_path_isolation...\n");

  tk_ctx_set_strict_c11_mode(test_ctx, false);
  tk_ctx_set_enable_binary_literals(test_ctx, true);
  tk_ctx_set_enable_trigraphs(test_ctx, true);

  tokenizer_test_fixture_t fixture;
  init_explicit_context(&fixture);
  tokenizer_context_t *ctx_fail = &fixture.tokenizer;
  tk_ctx_set_strict_c11_mode(ctx_fail, true);
  tk_ctx_set_enable_binary_literals(ctx_fail, true);
  tk_ctx_set_enable_trigraphs(ctx_fail, false);

  expect_tokenize_ctx_fail(ctx_fail, "0b101");

  // failing ctx tokenize must not disturb default runtime config in parent process
  ASSERT_TRUE(!tk_ctx_get_strict_c11_mode(test_ctx));
  ASSERT_TRUE(tk_ctx_get_enable_binary_literals(test_ctx));
  ASSERT_TRUE(tk_ctx_get_enable_trigraphs(test_ctx));

  token_t *tok = tk_tokenize_ctx(test_ctx, "0b11");
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_EQ(3, as_num_i(tok)->val);
  dispose_explicit_context(&fixture);
}

static void test_context_failure_path_unterminated_and_invalid_token(tokenizer_context_t *test_ctx) {
  printf("test_context_failure_path_unterminated_and_invalid_token...\n");

  tk_ctx_set_strict_c11_mode(test_ctx, false);
  tk_ctx_set_enable_binary_literals(test_ctx, true);
  tk_ctx_set_enable_trigraphs(test_ctx, true);

  tokenizer_test_fixture_t fixture;
  init_explicit_context(&fixture);
  tokenizer_context_t *ctx = &fixture.tokenizer;
  tk_ctx_set_enable_trigraphs(ctx, false);

  expect_tokenize_ctx_fail(ctx, "\"unterminated");
  expect_tokenize_ctx_fail(ctx, "@");

  // failing ctx tokenize must not disturb default runtime config in parent process
  ASSERT_TRUE(!tk_ctx_get_strict_c11_mode(test_ctx));
  ASSERT_TRUE(tk_ctx_get_enable_binary_literals(test_ctx));
  ASSERT_TRUE(tk_ctx_get_enable_trigraphs(test_ctx));

  token_t *tok = tk_tokenize_ctx(test_ctx, "1 + 2");
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_EQ(1, as_num_i(tok)->val);
  dispose_explicit_context(&fixture);
}

static void test_context_success_path_default_non_interference(tokenizer_context_t *test_ctx) {
  printf("test_context_success_path_default_non_interference...\n");

  tk_ctx_set_strict_c11_mode(test_ctx, false);
  tk_ctx_set_enable_binary_literals(test_ctx, true);
  tk_ctx_set_enable_trigraphs(test_ctx, true);

  tokenizer_test_fixture_t fixture;
  init_explicit_context(&fixture);
  tokenizer_context_t *ctx = &fixture.tokenizer;
  tk_ctx_set_strict_c11_mode(ctx, true);
  tk_ctx_set_enable_binary_literals(ctx, false);
  tk_ctx_set_enable_trigraphs(ctx, false);

  token_t *tok = tk_tokenize_ctx(ctx, "?" "?=");
  ASSERT_EQ(TK_QUESTION, tok->kind);
  ASSERT_EQ(TK_QUESTION, tok->next->kind);

  // explicit ctx tokenize must not change default runtime config
  ASSERT_TRUE(!tk_ctx_get_strict_c11_mode(test_ctx));
  ASSERT_TRUE(tk_ctx_get_enable_binary_literals(test_ctx));
  ASSERT_TRUE(tk_ctx_get_enable_trigraphs(test_ctx));

  tok = tk_tokenize_ctx(test_ctx, "?" "?=");
  ASSERT_EQ(TK_HASH, tok->kind);
  dispose_explicit_context(&fixture);
}

static void test_c11_ident_ucn(tokenizer_context_t *test_ctx) {
  printf("test_c11_ident_ucn...\n");
  token_t *tok = tk_tokenize_ctx(test_ctx, "foo\\u00A9");
  ASSERT_EQ(TK_IDENT, tok->kind);
  ASSERT_EQ(5, as_ident(tok)->len);
}

static void test_c11_string_prefixes(tokenizer_context_t *test_ctx) {
  printf("test_c11_string_prefixes...\n");
  token_t *tok = tk_tokenize_ctx(test_ctx, "L\"a\" u\"b\" U\"c\" u8\"d\"");
  ASSERT_EQ(TK_STRING, tok->kind);
  ASSERT_EQ(TK_CHAR_WIDTH_CHAR32, ((token_string_t *)tok)->char_width);
  tok = tok->next;
  ASSERT_EQ(TK_STRING, tok->kind);
  ASSERT_EQ(TK_CHAR_WIDTH_CHAR16, ((token_string_t *)tok)->char_width);
  tok = tok->next;
  ASSERT_EQ(TK_STRING, tok->kind);
  ASSERT_EQ(TK_CHAR_WIDTH_CHAR32, ((token_string_t *)tok)->char_width);
  tok = tok->next;
  ASSERT_EQ(TK_STRING, tok->kind);
  ASSERT_EQ(TK_CHAR_WIDTH_CHAR, ((token_string_t *)tok)->char_width);
}

static void test_c11_binary_literal_strict_behavior(tokenizer_context_t *test_ctx) {
  printf("test_c11_binary_literal_strict_behavior...\n");
  tk_ctx_set_strict_c11_mode(test_ctx, false);
  token_t *tok = tk_tokenize_ctx(test_ctx, "0b101");
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_EQ(5, as_num_i(tok)->val);

  tk_ctx_set_strict_c11_mode(test_ctx, true);
  expect_tokenize_fail(test_ctx, "0b101");
  tk_ctx_set_strict_c11_mode(test_ctx, false);
}

static void test_c11_float_suffix_metadata(tokenizer_context_t *test_ctx) {
  printf("test_c11_float_suffix_metadata...\n");
  token_t *tok = tk_tokenize_ctx(test_ctx, "1.0f 2.0L");
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_EQ(TK_FLOAT_SUFFIX_F, as_num_f(tok)->float_suffix_kind);
  tok = tok->next;
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_EQ(TK_FLOAT_SUFFIX_L, as_num_f(tok)->float_suffix_kind);
}

static void test_c11_keywords_tokenize(tokenizer_context_t *test_ctx) {
  printf("test_c11_keywords_tokenize...\n");
  token_t *tok = tk_tokenize_ctx(test_ctx, "_Alignas _Alignof _Atomic _Bool _Complex _Generic _Imaginary _Noreturn _Static_assert _Thread_local");
  ASSERT_EQ(TK_ALIGNAS, tok->kind);
  tok = tok->next;
  ASSERT_EQ(TK_ALIGNOF, tok->kind);
  tok = tok->next;
  ASSERT_EQ(TK_ATOMIC, tok->kind);
  tok = tok->next;
  ASSERT_EQ(TK_BOOL, tok->kind);
  tok = tok->next;
  ASSERT_EQ(TK_COMPLEX, tok->kind);
  tok = tok->next;
  ASSERT_EQ(TK_GENERIC, tok->kind);
  tok = tok->next;
  ASSERT_EQ(TK_IMAGINARY, tok->kind);
  tok = tok->next;
  ASSERT_EQ(TK_NORETURN, tok->kind);
  tok = tok->next;
  ASSERT_EQ(TK_STATIC_ASSERT, tok->kind);
  tok = tok->next;
  ASSERT_EQ(TK_THREAD_LOCAL, tok->kind);
  tok = tok->next;
  ASSERT_EQ(TK_EOF, tok->kind);
}

static void test_c11_keyword_like_idents(tokenizer_context_t *test_ctx) {
  printf("test_c11_keyword_like_idents...\n");
  token_t *tok = tk_tokenize_ctx(test_ctx, "_Alignasx _Boolish _Atomicvar");
  ASSERT_EQ(TK_IDENT, tok->kind);
  ASSERT_EQ(9, as_ident(tok)->len);
  tok = tok->next;
  ASSERT_EQ(TK_IDENT, tok->kind);
  ASSERT_EQ(8, as_ident(tok)->len);
  tok = tok->next;
  ASSERT_EQ(TK_IDENT, tok->kind);
  ASSERT_EQ(10, as_ident(tok)->len);
  tok = tok->next;
  ASSERT_EQ(TK_EOF, tok->kind);
}

static void test_c11_string_prefix_edge(tokenizer_context_t *test_ctx) {
  printf("test_c11_string_prefix_edge...\n");
  token_t *tok = tk_tokenize_ctx(test_ctx, "u8\"abc\"");
  ASSERT_EQ(TK_STRING, tok->kind);
  ASSERT_EQ(TK_CHAR_WIDTH_CHAR, ((token_string_t *)tok)->char_width);
  ASSERT_EQ(TK_STR_PREFIX_u8, ((token_string_t *)tok)->str_prefix_kind);
  tok = tok->next;
  ASSERT_EQ(TK_EOF, tok->kind);

  tok = tk_tokenize_ctx(test_ctx, "L'a'");
  ASSERT_EQ(TK_NUM, tok->kind);
  tok = tok->next;
  ASSERT_EQ(TK_EOF, tok->kind);
}

static void test_c11_float_edge_cases(tokenizer_context_t *test_ctx) {
  printf("test_c11_float_edge_cases...\n");
  token_t *tok = tk_tokenize_ctx(test_ctx, "0.0f 0.0L");
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_EQ(TK_FLOAT_SUFFIX_F, as_num_f(tok)->float_suffix_kind);
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT, as_num_f(tok)->fp_kind);
  tok = tok->next;
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_EQ(TK_FLOAT_SUFFIX_L, as_num_f(tok)->float_suffix_kind);
  ASSERT_EQ(TK_FLOAT_KIND_LONG_DOUBLE, as_num_f(tok)->fp_kind);
  tok = tok->next;
  ASSERT_EQ(TK_EOF, tok->kind);

  tok = tk_tokenize_ctx(test_ctx, "1e10");
  ASSERT_EQ(TK_NUM, tok->kind);
  tok = tok->next;
  ASSERT_EQ(TK_EOF, tok->kind);

  tok = tk_tokenize_ctx(test_ctx, "1.0 2.0f 3.0L");
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_EQ(TK_FLOAT_SUFFIX_NONE, as_num_f(tok)->float_suffix_kind);
  tok = tok->next;
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_EQ(TK_FLOAT_SUFFIX_F, as_num_f(tok)->float_suffix_kind);
  tok = tok->next;
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_EQ(TK_FLOAT_SUFFIX_L, as_num_f(tok)->float_suffix_kind);
  tok = tok->next;
  ASSERT_EQ(TK_EOF, tok->kind);
}

static void test_c11_int_suffix_mixed_case(tokenizer_context_t *test_ctx) {
  printf("test_c11_int_suffix_mixed_case...\n");
  token_t *tok = tk_tokenize_ctx(test_ctx, "1U 2u 3L 4l 5UL 6ul 7ULL 8ull 9Ul 10uL 11uLL 12UlL");
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_TRUE(as_num_i(tok)->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_INT, as_num_i(tok)->int_size);
  tok = tok->next;
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_TRUE(as_num_i(tok)->is_unsigned);
  tok = tok->next;
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_TRUE(!as_num_i(tok)->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_LONG, as_num_i(tok)->int_size);
  tok = tok->next;
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_EQ(TK_INT_SIZE_LONG, as_num_i(tok)->int_size);
  tok = tok->next;
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_TRUE(as_num_i(tok)->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_LONG, as_num_i(tok)->int_size);
  tok = tok->next;
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_TRUE(as_num_i(tok)->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_LONG, as_num_i(tok)->int_size);
  tok = tok->next;
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_TRUE(as_num_i(tok)->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_LONG_LONG, as_num_i(tok)->int_size);
  tok = tok->next;
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_TRUE(as_num_i(tok)->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_LONG_LONG, as_num_i(tok)->int_size);
  tok = tok->next;
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_TRUE(as_num_i(tok)->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_LONG, as_num_i(tok)->int_size);
  tok = tok->next;
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_TRUE(as_num_i(tok)->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_LONG, as_num_i(tok)->int_size);
  tok = tok->next;
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_TRUE(as_num_i(tok)->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_LONG_LONG, as_num_i(tok)->int_size);
  tok = tok->next;
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_TRUE(as_num_i(tok)->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_LONG_LONG, as_num_i(tok)->int_size);
  tok = tok->next;
  ASSERT_EQ(TK_EOF, tok->kind);
}

static void test_c11_invalid_suffixes(tokenizer_context_t *test_ctx) {
  printf("test_c11_invalid_suffixes...\n");
  expect_tokenize_fail(test_ctx, "1uu");
  expect_tokenize_fail(test_ctx, "1lll");
}

static void test_c11_ucn_invalid_boundaries(tokenizer_context_t *test_ctx) {
  printf("test_c11_ucn_invalid_boundaries...\n");
  expect_tokenize_fail(test_ctx, "foo\\u000A");
  expect_tokenize_fail(test_ctx, "bar\\uD800");
  expect_tokenize_fail(test_ctx, "\"\\u202E\"");
  expect_tokenize_fail(test_ctx, "\"\\u123\"");
  expect_tokenize_fail(test_ctx, "\"\\U00110000\"");
  expect_tokenize_fail(test_ctx, "\"\\U0000D800\"");
  expect_tokenize_fail(test_ctx, "\"\\U0000001F\"");
}

static void test_c11_long_escape_sequences(tokenizer_context_t *test_ctx) {
  printf("test_c11_long_escape_sequences...\n");
  token_t *tok = tk_tokenize_ctx(test_ctx, "\"\\x123456789ABCDEF\"");
  ASSERT_EQ(TK_STRING, tok->kind);
  ASSERT_TRUE(((token_string_t *)tok)->len >= 1);
  tok = tok->next;
  ASSERT_EQ(TK_EOF, tok->kind);
}

int main() {
  tokenizer_test_fixture_t fixture;
  init_explicit_context(&fixture);
  tokenizer_context_t *test_ctx = &fixture.tokenizer;
  printf("Running tests for Tokenizer...\n");

  test_source_manager_dynamic_names();
  test_tokenize(test_ctx);
  test_tokenize_int_literals(test_ctx);
  test_tokenize_invalid(test_ctx);
  test_tokenize_ident(test_ctx);
  test_tokenize_keywords(test_ctx);
  test_tokenize_symbols(test_ctx);
  test_tokenize_punctuators(test_ctx);
  test_tokenize_comments(test_ctx);
  test_tokenize_string(test_ctx);
  test_tokenize_char_literal(test_ctx);
  test_tokenize_string_prefixes_and_ucn(test_ctx);
  test_tokenize_ucn_ident_and_trigraph(test_ctx);
  test_tokenize_evil_edge_cases(test_ctx);
  test_strict_c11_mode(test_ctx);
  test_c11_audit_mode_flag(test_ctx);
  test_runtime_mode_switch_boundaries(test_ctx);
  test_tokenize_with_explicit_context(test_ctx);
  test_context_config_isolation_and_switch_timing();
  test_interleaved_stream_context_isolation(test_ctx);
  test_context_failure_path_isolation(test_ctx);
  test_context_failure_path_unterminated_and_invalid_token(test_ctx);
  test_context_success_path_default_non_interference(test_ctx);
  test_c11_ident_ucn(test_ctx);
  test_c11_string_prefixes(test_ctx);
  test_c11_binary_literal_strict_behavior(test_ctx);
  test_c11_float_suffix_metadata(test_ctx);
  test_c11_keywords_tokenize(test_ctx);
  test_c11_keyword_like_idents(test_ctx);
  test_c11_string_prefix_edge(test_ctx);
  test_c11_float_edge_cases(test_ctx);
  test_c11_int_suffix_mixed_case(test_ctx);
  test_c11_invalid_suffixes(test_ctx);
  test_c11_ucn_invalid_boundaries(test_ctx);
  test_c11_long_escape_sequences(test_ctx);
  test_at_eof(test_ctx);
  test_consume(test_ctx);
  test_consume_str(test_ctx);
  test_consume_ident(test_ctx);
  test_expect(test_ctx);
  test_expect_number(test_ctx);
  test_null_cursor_boundaries(test_ctx);
  test_tokenize_float_literal(test_ctx);
  test_tokenize_len_guard_boundaries(test_ctx);

  dispose_explicit_context(&fixture);
  printf("OK: All unit tests passed!\n");
  return 0;
}
