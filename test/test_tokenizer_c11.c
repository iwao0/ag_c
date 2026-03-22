#include "../src/tokenizer/tokenizer.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "test_common.h"

static token_num_int_t *as_num_i(token_t *tok) { return (token_num_int_t *)tok; }
static token_num_float_t *as_num_f(token_t *tok) { return (token_num_float_t *)tok; }
static token_ident_t *as_ident(token_t *tok) { return (token_ident_t *)tok; }

static void expect_tokenize_fail(const char *input) {
  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    tk_tokenize((char *)input);
    _exit(0);
  }
  int status;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_TRUE(WEXITSTATUS(status) != 0);
}

static void test_c11_ident_ucn(void) {
  printf("test_c11_ident_ucn...\n");
  token_t *tok = tk_tokenize("foo\\u00A9");
  ASSERT_EQ(TK_IDENT, tok->kind);
  ASSERT_EQ(5, as_ident(tok)->len);
}

static void test_c11_string_prefixes(void) {
  printf("test_c11_string_prefixes...\n");
  token_t *tok = tk_tokenize("L\"a\" u\"b\" U\"c\" u8\"d\"");
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

static void test_c11_binary_literal_strict_behavior(void) {
  printf("test_c11_binary_literal_strict_behavior...\n");
  tk_set_strict_c11_mode(false);
  token_t *tok = tk_tokenize("0b101");
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_EQ(5, as_num_i(tok)->val);

  tk_set_strict_c11_mode(true);
  expect_tokenize_fail("0b101");
  tk_set_strict_c11_mode(false);
}

static void test_c11_float_suffix_metadata(void) {
  printf("test_c11_float_suffix_metadata...\n");
  token_t *tok = tk_tokenize("1.0f 2.0L");
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_EQ(TK_FLOAT_SUFFIX_F, as_num_f(tok)->float_suffix_kind);
  tok = tok->next;
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_EQ(TK_FLOAT_SUFFIX_L, as_num_f(tok)->float_suffix_kind);
}

// 意地悪テスト: C11キーワードが正しくトークナイズされるか
static void test_c11_keywords_tokenize(void) {
  printf("test_c11_keywords_tokenize...\n");

  // _Alignas と _Alignof が正しくキーワードになる
  token_t *tok = tk_tokenize("_Alignas _Alignof _Atomic _Bool _Complex _Generic _Imaginary _Noreturn _Static_assert _Thread_local");
  ASSERT_EQ(TK_ALIGNAS, tok->kind); tok = tok->next;
  ASSERT_EQ(TK_ALIGNOF, tok->kind); tok = tok->next;
  ASSERT_EQ(TK_ATOMIC, tok->kind); tok = tok->next;
  ASSERT_EQ(TK_BOOL, tok->kind); tok = tok->next;
  ASSERT_EQ(TK_COMPLEX, tok->kind); tok = tok->next;
  ASSERT_EQ(TK_GENERIC, tok->kind); tok = tok->next;
  ASSERT_EQ(TK_IMAGINARY, tok->kind); tok = tok->next;
  ASSERT_EQ(TK_NORETURN, tok->kind); tok = tok->next;
  ASSERT_EQ(TK_STATIC_ASSERT, tok->kind); tok = tok->next;
  ASSERT_EQ(TK_THREAD_LOCAL, tok->kind); tok = tok->next;
  ASSERT_EQ(TK_EOF, tok->kind);
}

// 意地悪テスト: C11キーワードに似た識別子はキーワードにならない
static void test_c11_keyword_like_idents(void) {
  printf("test_c11_keyword_like_idents...\n");

  // _Alignas_x は識別子であってキーワードではない
  token_t *tok = tk_tokenize("_Alignasx _Boolish _Atomicvar");
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

// 意地悪テスト: 文字列プレフィックスの直後に識別子（空白なし）
static void test_c11_string_prefix_edge(void) {
  printf("test_c11_string_prefix_edge...\n");

  // u8"abc" は u8プレフィックス付き文字列
  token_t *tok = tk_tokenize("u8\"abc\"");
  ASSERT_EQ(TK_STRING, tok->kind);
  ASSERT_EQ(TK_CHAR_WIDTH_CHAR, ((token_string_t *)tok)->char_width);
  ASSERT_EQ(TK_STR_PREFIX_u8, ((token_string_t *)tok)->str_prefix_kind);
  tok = tok->next;
  ASSERT_EQ(TK_EOF, tok->kind);

  // L'a' は L プレフィックス付き文字リテラル
  tok = tk_tokenize("L'a'");
  ASSERT_EQ(TK_NUM, tok->kind);
  tok = tok->next;
  ASSERT_EQ(TK_EOF, tok->kind);
}

// 意地悪テスト: 浮動小数点の複雑なケース
static void test_c11_float_edge_cases(void) {
  printf("test_c11_float_edge_cases...\n");

  // 0.0f と 0.0L
  token_t *tok = tk_tokenize("0.0f 0.0L");
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_EQ(TK_FLOAT_SUFFIX_F, as_num_f(tok)->float_suffix_kind);
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT, as_num_f(tok)->fp_kind);
  tok = tok->next;
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_EQ(TK_FLOAT_SUFFIX_L, as_num_f(tok)->float_suffix_kind);
  ASSERT_EQ(TK_FLOAT_KIND_LONG_DOUBLE, as_num_f(tok)->fp_kind);
  tok = tok->next;
  ASSERT_EQ(TK_EOF, tok->kind);

  // 指数付き: 1e10
  tok = tk_tokenize("1e10");
  ASSERT_EQ(TK_NUM, tok->kind);
  tok = tok->next;
  ASSERT_EQ(TK_EOF, tok->kind);

  // 複数サフィックス変化: 1.0, 2.0f, 3.0L
  tok = tk_tokenize("1.0 2.0f 3.0L");
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

// 意地悪テスト: 整数サフィックスの大文字小文字混在
static void test_c11_int_suffix_mixed_case(void) {
  printf("test_c11_int_suffix_mixed_case...\n");

  token_t *tok = tk_tokenize("1U 2u 3L 4l 5UL 6ul 7ULL 8ull 9Ul 10uL 11uLL 12UlL");
  // 1U
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_TRUE(as_num_i(tok)->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_INT, as_num_i(tok)->int_size);
  tok = tok->next;
  // 2u
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_TRUE(as_num_i(tok)->is_unsigned);
  tok = tok->next;
  // 3L
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_TRUE(!as_num_i(tok)->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_LONG, as_num_i(tok)->int_size);
  tok = tok->next;
  // 4l
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_EQ(TK_INT_SIZE_LONG, as_num_i(tok)->int_size);
  tok = tok->next;
  // 5UL
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_TRUE(as_num_i(tok)->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_LONG, as_num_i(tok)->int_size);
  tok = tok->next;
  // 6ul
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_TRUE(as_num_i(tok)->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_LONG, as_num_i(tok)->int_size);
  tok = tok->next;
  // 7ULL
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_TRUE(as_num_i(tok)->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_LONG_LONG, as_num_i(tok)->int_size);
  tok = tok->next;
  // 8ull
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_TRUE(as_num_i(tok)->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_LONG_LONG, as_num_i(tok)->int_size);
  tok = tok->next;
  // 9Ul
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_TRUE(as_num_i(tok)->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_LONG, as_num_i(tok)->int_size);
  tok = tok->next;
  // 10uL
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_TRUE(as_num_i(tok)->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_LONG, as_num_i(tok)->int_size);
  tok = tok->next;
  // 11uLL
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_TRUE(as_num_i(tok)->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_LONG_LONG, as_num_i(tok)->int_size);
  tok = tok->next;
  // 12UlL
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_TRUE(as_num_i(tok)->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_LONG_LONG, as_num_i(tok)->int_size);
  tok = tok->next;
  ASSERT_EQ(TK_EOF, tok->kind);
}

// 意地悪テスト: 不正な整数サフィックス（異常系）
static void test_c11_invalid_suffixes(void) {
  printf("test_c11_invalid_suffixes...\n");
  expect_tokenize_fail("1uu");     // u 重複
  expect_tokenize_fail("1lll");    // l 3つは不正
}

int main(void) {
  printf("Running C11-focused Tokenizer tests...\n");
  test_c11_ident_ucn();
  test_c11_string_prefixes();
  test_c11_binary_literal_strict_behavior();
  test_c11_float_suffix_metadata();
  test_c11_keywords_tokenize();
  test_c11_keyword_like_idents();
  test_c11_string_prefix_edge();
  test_c11_float_edge_cases();
  test_c11_int_suffix_mixed_case();
  test_c11_invalid_suffixes();
  printf("OK: Tokenizer C11-focused tests passed!\n");
  return 0;
}
