#include "../src/tokenizer/tokenizer.h"
#include "../src/tokenizer/tokenizer_test.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "test_common.h"

// tokenizer.c で定義されているがヘッダにない変数をextern
extern char *user_input;

static token_num_int_t *as_num_i(token_t *tok) { return (token_num_int_t *)tok; }
static token_num_float_t *as_num_f(token_t *tok) { return (token_num_float_t *)tok; }
static token_ident_t *as_ident(token_t *tok) { return (token_ident_t *)tok; }
static token_string_t *as_string(token_t *tok) { return (token_string_t *)tok; }

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

// 1. tk_tokenize() のテスト
static void test_tokenize() {
  printf("test_tokenize...\n");
  token = tk_tokenize(" 12 + 34 - 5 ");

  // 最初のトークン: "12"
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(12, as_num_i(tk_get_current_token())->val);
  tk_set_current_token(tk_get_current_token()->next);

  // 2番目のトークン: "+"
  ASSERT_EQ(TK_PLUS, tk_get_current_token()->kind);
  tk_set_current_token(tk_get_current_token()->next);

  // 3番目のトークン: "34"
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(34, as_num_i(tk_get_current_token())->val);
  tk_set_current_token(tk_get_current_token()->next);

  // 4番目のトークン: "-"
  ASSERT_EQ(TK_MINUS, tk_get_current_token()->kind);
  tk_set_current_token(tk_get_current_token()->next);

  // 5番目のトークン: "5"
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(5, as_num_i(tk_get_current_token())->val);
  tk_set_current_token(tk_get_current_token()->next);

  // 最後のトークン: EOF
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  // 四則演算と括弧のテスト
  token = tk_tokenize("1 * (2 / 3)");
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(1, as_num_i(tk_get_current_token())->val);
  tk_set_current_token(tk_get_current_token()->next);

  ASSERT_EQ(TK_MUL, tk_get_current_token()->kind);
  tk_set_current_token(tk_get_current_token()->next);

  ASSERT_EQ(TK_LPAREN, tk_get_current_token()->kind);
  tk_set_current_token(tk_get_current_token()->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(2, as_num_i(tk_get_current_token())->val);
  tk_set_current_token(tk_get_current_token()->next);

  ASSERT_EQ(TK_DIV, tk_get_current_token()->kind);
  tk_set_current_token(tk_get_current_token()->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(3, as_num_i(tk_get_current_token())->val);
  tk_set_current_token(tk_get_current_token()->next);

  ASSERT_EQ(TK_RPAREN, tk_get_current_token()->kind);
  tk_set_current_token(tk_get_current_token()->next);

  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  // 比較演算子のテスト
  token = tk_tokenize("1 == 2 != 3 <= 4 >= 5 < 6 > 7");
  // 1
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(1, as_num_i(tk_get_current_token())->val);
  tk_set_current_token(tk_get_current_token()->next);
  // ==
  ASSERT_EQ(TK_EQEQ, tk_get_current_token()->kind);
  tk_set_current_token(tk_get_current_token()->next);
  // 2
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(2, as_num_i(tk_get_current_token())->val);
  tk_set_current_token(tk_get_current_token()->next);
  // !=
  ASSERT_EQ(TK_NEQ, tk_get_current_token()->kind);
  tk_set_current_token(tk_get_current_token()->next);
  // 3
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(3, as_num_i(tk_get_current_token())->val);
  tk_set_current_token(tk_get_current_token()->next);
  // <=
  ASSERT_EQ(TK_LE, tk_get_current_token()->kind);
  tk_set_current_token(tk_get_current_token()->next);
  // 4
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(4, as_num_i(tk_get_current_token())->val);
  tk_set_current_token(tk_get_current_token()->next);
  // >=
  ASSERT_EQ(TK_GE, tk_get_current_token()->kind);
  tk_set_current_token(tk_get_current_token()->next);
  // 5
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(5, as_num_i(tk_get_current_token())->val);
  tk_set_current_token(tk_get_current_token()->next);
  // <
  ASSERT_EQ(TK_LT, tk_get_current_token()->kind);
  tk_set_current_token(tk_get_current_token()->next);
  // 6
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(6, as_num_i(tk_get_current_token())->val);
  tk_set_current_token(tk_get_current_token()->next);
  // >
  ASSERT_EQ(TK_GT, tk_get_current_token()->kind);
  tk_set_current_token(tk_get_current_token()->next);
  // 7
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(7, as_num_i(tk_get_current_token())->val);
  tk_set_current_token(tk_get_current_token()->next);

  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);
}

// 1b. 16進数/2進数リテラルのテスト
static void test_tokenize_int_literals() {
  printf("test_tokenize_int_literals...\n");
  token = tk_tokenize("0x2a 0X10 0b101 0B11 077 010 0 123 1u 2UL 3llu 0x1ffffffff");

  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(42, as_num_i(tk_get_current_token())->val);
  ASSERT_EQ(42ULL, as_num_i(tk_get_current_token())->uval);
  ASSERT_EQ(16, as_num_i(tk_get_current_token())->int_base);
  ASSERT_TRUE(!as_num_i(tk_get_current_token())->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_INT, as_num_i(tk_get_current_token())->int_size);
  tk_set_current_token(tk_get_current_token()->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(16, as_num_i(tk_get_current_token())->val);
  ASSERT_EQ(16ULL, as_num_i(tk_get_current_token())->uval);
  ASSERT_EQ(16, as_num_i(tk_get_current_token())->int_base);
  tk_set_current_token(tk_get_current_token()->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(5, as_num_i(tk_get_current_token())->val);
  ASSERT_EQ(5ULL, as_num_i(tk_get_current_token())->uval);
  ASSERT_EQ(2, as_num_i(tk_get_current_token())->int_base);
  tk_set_current_token(tk_get_current_token()->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(3, as_num_i(tk_get_current_token())->val);
  ASSERT_EQ(3ULL, as_num_i(tk_get_current_token())->uval);
  ASSERT_EQ(2, as_num_i(tk_get_current_token())->int_base);
  tk_set_current_token(tk_get_current_token()->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(63, as_num_i(tk_get_current_token())->val);
  ASSERT_EQ(63ULL, as_num_i(tk_get_current_token())->uval);
  ASSERT_EQ(8, as_num_i(tk_get_current_token())->int_base);
  tk_set_current_token(tk_get_current_token()->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(8, as_num_i(tk_get_current_token())->val);
  ASSERT_EQ(8ULL, as_num_i(tk_get_current_token())->uval);
  ASSERT_EQ(8, as_num_i(tk_get_current_token())->int_base);
  tk_set_current_token(tk_get_current_token()->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(0, as_num_i(tk_get_current_token())->val);
  ASSERT_EQ(0ULL, as_num_i(tk_get_current_token())->uval);
  ASSERT_EQ(10, as_num_i(tk_get_current_token())->int_base);
  ASSERT_TRUE(!as_num_i(tk_get_current_token())->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_INT, as_num_i(tk_get_current_token())->int_size);
  tk_set_current_token(tk_get_current_token()->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(123, as_num_i(tk_get_current_token())->val);
  ASSERT_EQ(123ULL, as_num_i(tk_get_current_token())->uval);
  ASSERT_EQ(10, as_num_i(tk_get_current_token())->int_base);
  ASSERT_TRUE(!as_num_i(tk_get_current_token())->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_INT, as_num_i(tk_get_current_token())->int_size);
  tk_set_current_token(tk_get_current_token()->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(1, as_num_i(tk_get_current_token())->val);
  ASSERT_TRUE(as_num_i(tk_get_current_token())->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_INT, as_num_i(tk_get_current_token())->int_size);
  tk_set_current_token(tk_get_current_token()->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(2, as_num_i(tk_get_current_token())->val);
  ASSERT_TRUE(as_num_i(tk_get_current_token())->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_LONG, as_num_i(tk_get_current_token())->int_size);
  tk_set_current_token(tk_get_current_token()->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(3, as_num_i(tk_get_current_token())->val);
  ASSERT_TRUE(as_num_i(tk_get_current_token())->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_LONG_LONG, as_num_i(tk_get_current_token())->int_size);
  tk_set_current_token(tk_get_current_token()->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(0x1ffffffffULL, as_num_i(tk_get_current_token())->uval);
  ASSERT_TRUE(!as_num_i(tk_get_current_token())->is_unsigned);
  ASSERT_EQ(TK_INT_SIZE_LONG, as_num_i(tk_get_current_token())->int_size);
  tk_set_current_token(tk_get_current_token()->next);

  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);
}

// 1c. 異常系トークナイズのテスト
static void test_tokenize_invalid() {
  printf("test_tokenize_invalid...\n");
  expect_tokenize_fail("0x");          // 16進数の桁不足
  expect_tokenize_fail("0b2");         // 2進数の不正桁
  expect_tokenize_fail("08");          // 8進数の不正桁
  expect_tokenize_fail("1uu");         // 整数サフィックス重複
  expect_tokenize_fail("1lll");        // long サフィックス過多
  expect_tokenize_fail("1.0q");        // 浮動小数点サフィックス不正
  expect_tokenize_fail("/* unterminated"); // コメント未閉じ
  expect_tokenize_fail("@");           // 不正文字
  expect_tokenize_fail("18446744073709551616"); // ULLONG_MAX+1
  expect_tokenize_fail("\"unterminated"); // 文字列未閉じ
  expect_tokenize_fail("\"\\x\"");    // 16進エスケープ不正
  expect_tokenize_fail("\"\\xG\"");   // 16進エスケープ直後に非hex
  expect_tokenize_fail("\"\\q\"");    // 不正エスケープ
  expect_tokenize_fail("\"\\u202E\""); // bidi制御文字UCN
  expect_tokenize_fail("\"\\u200C\""); // ゼロ幅UCN
  expect_tokenize_fail("\"\\U00110000\""); // Unicode上限超過
  expect_tokenize_fail("\"\\U0000D800\""); // surrogate
  expect_tokenize_fail("\"\\U0000001F\""); // 制御文字UCN
  expect_tokenize_fail("''");         // 空の文字リテラル
  expect_tokenize_fail("'\\x'");      // 16進エスケープ不正
  expect_tokenize_fail("'\\xG'");     // 16進エスケープ直後に非hex
  expect_tokenize_fail("'\\u202E'");  // bidi制御文字UCN
  expect_tokenize_fail("'\\u200D'");  // ゼロ幅UCN
  expect_tokenize_fail("'\\U00110000'"); // Unicode上限超過
  expect_tokenize_fail("'\\U0000DFFF'"); // surrogate
  expect_tokenize_fail("safe\\u202Ename"); // 識別子内bidi制御文字UCN
  expect_tokenize_fail("safe\\u200Cname"); // 識別子内ゼロ幅UCN
  expect_tokenize_fail("\"\\\n\\u202E\""); // 行継続 + bidi制御文字UCN
  expect_tokenize_fail("safe\\\n\\u200Cname"); // 行継続 + 識別子内ゼロ幅UCN
  expect_tokenize_fail("\x80");        // 孤立したUTF-8継続バイト
  expect_tokenize_fail("\xC0\xAF");    // 不正UTF-8シーケンス（overlong）
  expect_tokenize_fail("1.0f0");      // pp-number 連結
  expect_tokenize_fail("1..2");       // pp-number 連結

  // 大きすぎるトークン長を安全に拒否できること（テスト用上限を利用）
  tk_set_max_token_len_for_test(8);
  expect_tokenize_fail("identifier_too_long");
  expect_tokenize_fail("\"string_too_long\"");
  tk_set_max_token_len_for_test(0);
}

static void test_tokenize_len_guard_boundaries() {
  printf("test_tokenize_len_guard_boundaries...\n");
  tk_set_max_token_len_for_test(8);
  token = tk_tokenize("abcdefgh");
  ASSERT_EQ(TK_IDENT, tk_get_current_token()->kind);
  ASSERT_EQ(8, as_ident(tk_get_current_token())->len);
  tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);
  expect_tokenize_fail("abcdefghi");
  tk_set_max_token_len_for_test(0);
}

// 1c. ローカル変数・複数文字識別子のテスト
static void test_tokenize_ident() {
  printf("test_tokenize_ident...\n");
  token = tk_tokenize("a = 3; b = a;");

  ASSERT_EQ(TK_IDENT, tk_get_current_token()->kind); ASSERT_EQ('a', as_ident(tk_get_current_token())->str[0]); ASSERT_EQ(1, as_ident(tk_get_current_token())->len); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_ASSIGN, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind); ASSERT_EQ(3, as_num_i(tk_get_current_token())->val); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_SEMI, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_IDENT, tk_get_current_token()->kind); ASSERT_EQ('b', as_ident(tk_get_current_token())->str[0]); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_ASSIGN, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_IDENT, tk_get_current_token()->kind); ASSERT_EQ('a', as_ident(tk_get_current_token())->str[0]); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_SEMI, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  // 複数文字識別子
  token = tk_tokenize("foo my_var x1");
  ASSERT_EQ(TK_IDENT, tk_get_current_token()->kind); ASSERT_EQ(3, as_ident(tk_get_current_token())->len);
  ASSERT_TRUE(strncmp(as_ident(tk_get_current_token())->str, "foo", 3) == 0); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_IDENT, tk_get_current_token()->kind); ASSERT_EQ(6, as_ident(tk_get_current_token())->len);
  ASSERT_TRUE(strncmp(as_ident(tk_get_current_token())->str, "my_var", 6) == 0); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_IDENT, tk_get_current_token()->kind); ASSERT_EQ(2, as_ident(tk_get_current_token())->len);
  ASSERT_TRUE(strncmp(as_ident(tk_get_current_token())->str, "x1", 2) == 0); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  // アンダースコアで始まる識別子
  token = tk_tokenize("_start _a1");
  ASSERT_EQ(TK_IDENT, tk_get_current_token()->kind); ASSERT_EQ(6, as_ident(tk_get_current_token())->len);
  ASSERT_TRUE(strncmp(as_ident(tk_get_current_token())->str, "_start", 6) == 0); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_IDENT, tk_get_current_token()->kind); ASSERT_EQ(3, as_ident(tk_get_current_token())->len);
  ASSERT_TRUE(strncmp(as_ident(tk_get_current_token())->str, "_a1", 3) == 0); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);
}

// 1c. キーワードトークンのテスト
static void test_tokenize_keywords() {
  printf("test_tokenize_keywords...\n");

  // 制御構文キーワード
  token = tk_tokenize("if else while for return");
  ASSERT_EQ(TK_IF, tk_get_current_token()->kind);     tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_ELSE, tk_get_current_token()->kind);   tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_WHILE, tk_get_current_token()->kind);  tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_FOR, tk_get_current_token()->kind);    tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_RETURN, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  // 型キーワード
  token = tk_tokenize("int char void short long float double signed unsigned");
  ASSERT_EQ(TK_INT, tk_get_current_token()->kind);     tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_CHAR, tk_get_current_token()->kind);    tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_VOID, tk_get_current_token()->kind);    tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_SHORT, tk_get_current_token()->kind);   tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_LONG, tk_get_current_token()->kind);    tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_FLOAT, tk_get_current_token()->kind);   tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_DOUBLE, tk_get_current_token()->kind);  tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_SIGNED, tk_get_current_token()->kind);  tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_UNSIGNED, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  // その他のキーワード
  token = tk_tokenize(
      "auto break case const continue default do enum extern goto inline "
      "register restrict sizeof static struct switch typedef union volatile "
      "_Alignas _Alignof _Atomic _Bool _Complex _Generic _Imaginary "
      "_Noreturn _Static_assert _Thread_local");
  ASSERT_EQ(TK_AUTO, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_BREAK, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_CASE, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_CONST, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_CONTINUE, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_DEFAULT, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_DO, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_ENUM, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EXTERN, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_GOTO, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_INLINE, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_REGISTER, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_RESTRICT, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_SIZEOF, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_STATIC, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_STRUCT, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_SWITCH, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_TYPEDEF, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_UNION, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_VOLATILE, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_ALIGNAS, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_ALIGNOF, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_ATOMIC, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_BOOL, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_COMPLEX, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_GENERIC, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_IMAGINARY, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_NORETURN, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_STATIC_ASSERT, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_THREAD_LOCAL, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  // キーワードと似た識別子はキーワードにならない
  token = tk_tokenize("iff int1 returns");
  ASSERT_EQ(TK_IDENT, tk_get_current_token()->kind);
  ASSERT_TRUE(strncmp(as_ident(tk_get_current_token())->str, "iff", 3) == 0); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_IDENT, tk_get_current_token()->kind);
  ASSERT_TRUE(strncmp(as_ident(tk_get_current_token())->str, "int1", 4) == 0); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_IDENT, tk_get_current_token()->kind);
  ASSERT_TRUE(strncmp(as_ident(tk_get_current_token())->str, "returns", 7) == 0); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);
}

// 1d. 追加記号のテスト
static void test_tokenize_symbols() {
  printf("test_tokenize_symbols...\n");
  token = tk_tokenize("{ } , & [ ]");
  ASSERT_EQ(TK_LBRACE, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_RBRACE, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_COMMA, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_AMP, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_LBRACKET, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_RBRACKET, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);
}

// 1d-2. 追加演算子のテスト
static void test_tokenize_punctuators() {
  printf("test_tokenize_punctuators...\n");
  token = tk_tokenize("++ -- -> << >> <<= >>= += -= *= /= %= &= ^= |= % | ^ ? : ...");
  ASSERT_EQ(TK_INC, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_DEC, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_ARROW, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_SHL, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_SHR, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_SHLEQ, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_SHREQ, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_PLUSEQ, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_MINUSEQ, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_MULEQ, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_DIVEQ, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_MODEQ, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_ANDEQ, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_XOREQ, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_OREQ, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_MOD, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_PIPE, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_CARET, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_QUESTION, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_COLON, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_ELLIPSIS, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  token = tk_tokenize("<: :> <% %> %: %:%:");
  ASSERT_EQ(TK_LBRACKET, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_RBRACKET, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_LBRACE, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_RBRACE, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_HASH, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_HASHHASH, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  token = tk_tokenize("#include \"x.h\"\nint main() { return 0; }");
  ASSERT_EQ(TK_HASH, tk_get_current_token()->kind);
  ASSERT_TRUE(tk_get_current_token()->at_bol);
  tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_IDENT, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_STRING, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_INT, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_IDENT, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
}

// 1d-3. コメントと空白のテスト
static void test_tokenize_comments() {
  printf("test_tokenize_comments...\n");
  token = tk_tokenize("1//comment\n2");
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_TRUE(tk_get_current_token()->at_bol);
  tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  token = tk_tokenize("1/* comment */2");
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_TRUE(!tk_get_current_token()->at_bol);
  tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  token = tk_tokenize("1/*\n*/2");
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_TRUE(tk_get_current_token()->at_bol);
  tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);
}

// 1e. 文字列リテラルのテスト
static void test_tokenize_string() {
  printf("test_tokenize_string...\n");
  token = tk_tokenize("\"hello\"");
  ASSERT_EQ(TK_STRING, tk_get_current_token()->kind);
  ASSERT_EQ(5, as_string(token)->len);
  ASSERT_TRUE(strncmp(as_string(token)->str, "hello", 5) == 0);
  tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  // 空文字列
  token = tk_tokenize("\"\"");
  ASSERT_EQ(TK_STRING, tk_get_current_token()->kind);
  ASSERT_EQ(0, as_string(token)->len);
  tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  // 文字列と他のトークンの混在
  token = tk_tokenize("char *s = \"AB\";");
  ASSERT_EQ(TK_CHAR, tk_get_current_token()->kind);     tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_MUL, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_IDENT, tk_get_current_token()->kind);    tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_ASSIGN, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_STRING, tk_get_current_token()->kind);
  ASSERT_EQ(2, as_string(token)->len);
  ASSERT_TRUE(strncmp(as_string(token)->str, "AB", 2) == 0);
  tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_SEMI, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  // エスケープを含む文字列
  token = tk_tokenize("\"a\\\"b\"");
  ASSERT_EQ(TK_STRING, tk_get_current_token()->kind);
  ASSERT_EQ(4, as_string(token)->len);
  tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);
}

// 1g. 浮動小数点リテラルのテスト
static void test_tokenize_float_literal() {
  printf("test_tokenize_float_literal...\n");
  token = tk_tokenize("3.14 1.5f 2.0E-3 0x1.8p1 0x1p2f 4.0L 0x1p2L");
  
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, as_num_f(tk_get_current_token())->fp_kind);
  ASSERT_EQ(TK_FLOAT_SUFFIX_NONE, as_num_f(tk_get_current_token())->float_suffix_kind);
  ASSERT_TRUE(as_num_f(tk_get_current_token())->fval > 3.13 && as_num_f(tk_get_current_token())->fval < 3.15);
  tk_set_current_token(tk_get_current_token()->next);
  
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT, as_num_f(tk_get_current_token())->fp_kind);
  ASSERT_EQ(TK_FLOAT_SUFFIX_F, as_num_f(tk_get_current_token())->float_suffix_kind);
  ASSERT_TRUE(as_num_f(tk_get_current_token())->fval > 1.49 && as_num_f(tk_get_current_token())->fval < 1.51);
  tk_set_current_token(tk_get_current_token()->next);
  
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, as_num_f(tk_get_current_token())->fp_kind);
  ASSERT_EQ(TK_FLOAT_SUFFIX_NONE, as_num_f(tk_get_current_token())->float_suffix_kind);
  ASSERT_TRUE(as_num_f(tk_get_current_token())->fval > 0.0019 && as_num_f(tk_get_current_token())->fval < 0.0021);
  tk_set_current_token(tk_get_current_token()->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, as_num_f(tk_get_current_token())->fp_kind);
  ASSERT_EQ(TK_FLOAT_SUFFIX_NONE, as_num_f(tk_get_current_token())->float_suffix_kind);
  ASSERT_TRUE(as_num_f(tk_get_current_token())->fval > 2.9 && as_num_f(tk_get_current_token())->fval < 3.1);
  tk_set_current_token(tk_get_current_token()->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT, as_num_f(tk_get_current_token())->fp_kind);
  ASSERT_EQ(TK_FLOAT_SUFFIX_F, as_num_f(tk_get_current_token())->float_suffix_kind);
  ASSERT_TRUE(as_num_f(tk_get_current_token())->fval > 3.9 && as_num_f(tk_get_current_token())->fval < 4.1);
  tk_set_current_token(tk_get_current_token()->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(TK_FLOAT_KIND_LONG_DOUBLE, as_num_f(tk_get_current_token())->fp_kind);
  ASSERT_EQ(TK_FLOAT_SUFFIX_L, as_num_f(tk_get_current_token())->float_suffix_kind);
  ASSERT_TRUE(as_num_f(tk_get_current_token())->fval > 3.9 && as_num_f(tk_get_current_token())->fval < 4.1);
  tk_set_current_token(tk_get_current_token()->next);

  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(TK_FLOAT_KIND_LONG_DOUBLE, as_num_f(tk_get_current_token())->fp_kind);
  ASSERT_EQ(TK_FLOAT_SUFFIX_L, as_num_f(tk_get_current_token())->float_suffix_kind);
  ASSERT_TRUE(as_num_f(tk_get_current_token())->fval > 3.9 && as_num_f(tk_get_current_token())->fval < 4.1);
  tk_set_current_token(tk_get_current_token()->next);
  
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);
}

// 2. tk_consume() のテスト
static void test_consume() {
  printf("test_consume...\n");
  token = tk_tokenize(" + 42 ");

  ASSERT_TRUE(tk_consume('+'));  // "+"を消費して次に進む
  ASSERT_TRUE(!tk_consume('-')); // "-"ではないので進まない
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(42, as_num_i(tk_get_current_token())->val);
}

// 2b. tk_consume_str() のテスト
static void test_consume_str() {
  printf("test_consume_str...\n");
  token = tk_tokenize("== != < <=");
  ASSERT_TRUE(tk_consume_str("=="));
  ASSERT_TRUE(!tk_consume_str("=="));  // 次は != なので false
  ASSERT_TRUE(tk_consume_str("!="));
  ASSERT_TRUE(!tk_consume_str("<="));  // 次は < (1文字) なので <= にはマッチしない
  ASSERT_TRUE(tk_consume('<'));
  ASSERT_TRUE(tk_consume_str("<="));
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);
}

// 2c. tk_consume_ident() のテスト
static void test_consume_ident() {
  printf("test_consume_ident...\n");
  token = tk_tokenize("foo 42");
  token_ident_t *tok = tk_consume_ident();
  ASSERT_TRUE(tok != NULL);
  ASSERT_EQ(3, tok->len);
  ASSERT_TRUE(strncmp(tok->str, "foo", 3) == 0);
  // 数値トークンでは NULL を返す
  tok = tk_consume_ident();
  ASSERT_TRUE(tok == NULL);
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
}

// 3. tk_expect() のテスト
static void test_expect() {
  printf("test_expect...\n");
  token = tk_tokenize(" - ");
  tk_expect('-');
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);
}

// 4. tk_expect_number() のテスト
static void test_expect_number() {
  printf("test_expect_number...\n");
  token = tk_tokenize(" 999 ");
  int val = tk_expect_number();
  ASSERT_EQ(999, val);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);
}

// 5. tk_at_eof() のテスト
static void test_at_eof() {
  printf("test_at_eof...\n");
  token = tk_tokenize(" 1 ");
  ASSERT_TRUE(!tk_at_eof());
  tk_expect_number();
  ASSERT_TRUE(tk_at_eof());
}

// 1f. 文字リテラルのテスト
static void test_tokenize_char_literal() {
  printf("test_tokenize_char_literal...\n");
  token = tk_tokenize("'A'");
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(65, as_num_i(tk_get_current_token())->val);
  tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  // エスケープシーケンス
  token = tk_tokenize("'\\n'");
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(10, as_num_i(tk_get_current_token())->val);
  tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  token = tk_tokenize("'\\0'");
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(0, as_num_i(tk_get_current_token())->val);
  tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  // マルチ文字文字定数（実装定義）
  token = tk_tokenize("'ab'");
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(((unsigned char)'a' << 8) | (unsigned char)'b', as_num_i(tk_get_current_token())->val);
  tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  // 接頭辞付き文字定数
  token = tk_tokenize("L'A' u'B' U'\\u00A9'");
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ('A', as_num_i(tk_get_current_token())->val);
  ASSERT_EQ(TK_CHAR_WIDTH_CHAR32, as_num_i(tk_get_current_token())->char_width);
  tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ('B', as_num_i(tk_get_current_token())->val);
  ASSERT_EQ(TK_CHAR_WIDTH_CHAR16, as_num_i(tk_get_current_token())->char_width);
  tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(0xA9, as_num_i(tk_get_current_token())->val);
  ASSERT_EQ(TK_CHAR_WIDTH_CHAR32, as_num_i(tk_get_current_token())->char_width);
  tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  // 接頭辞付きマルチ文字定数（実装定義として受理）
  token = tk_tokenize("L'AB' u'CD' U'EF'");
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(((unsigned char)'A' << 8) | (unsigned char)'B', as_num_i(tk_get_current_token())->val);
  ASSERT_EQ(TK_CHAR_WIDTH_CHAR32, as_num_i(tk_get_current_token())->char_width);
  tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(((unsigned char)'C' << 8) | (unsigned char)'D', as_num_i(tk_get_current_token())->val);
  ASSERT_EQ(TK_CHAR_WIDTH_CHAR16, as_num_i(tk_get_current_token())->char_width);
  tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(((unsigned char)'E' << 8) | (unsigned char)'F', as_num_i(tk_get_current_token())->val);
  ASSERT_EQ(TK_CHAR_WIDTH_CHAR32, as_num_i(tk_get_current_token())->char_width);
  tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);
}

static void test_tokenize_string_prefixes_and_ucn() {
  printf("test_tokenize_string_prefixes_and_ucn...\n");
  token = tk_tokenize("L\"wide\" u\"u16\" U\"u32\" u8\"utf8\" \"\\u00A9\"");
  ASSERT_EQ(TK_STRING, tk_get_current_token()->kind); ASSERT_EQ(4, as_string(token)->len); ASSERT_EQ(TK_CHAR_WIDTH_CHAR32, as_string(token)->char_width); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_STRING, tk_get_current_token()->kind); ASSERT_EQ(3, as_string(token)->len); ASSERT_EQ(TK_CHAR_WIDTH_CHAR16, as_string(token)->char_width); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_STRING, tk_get_current_token()->kind); ASSERT_EQ(3, as_string(token)->len); ASSERT_EQ(TK_CHAR_WIDTH_CHAR32, as_string(token)->char_width); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_STRING, tk_get_current_token()->kind); ASSERT_EQ(4, as_string(token)->len); ASSERT_EQ(TK_CHAR_WIDTH_CHAR, as_string(token)->char_width); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_STRING, tk_get_current_token()->kind); ASSERT_EQ(6, as_string(token)->len); ASSERT_EQ(TK_CHAR_WIDTH_CHAR, as_string(token)->char_width); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);
}

static void test_tokenize_ucn_ident_and_trigraph() {
  printf("test_tokenize_ucn_ident_and_trigraph...\n");
  token = tk_tokenize("foo\\u00A9 = 1;");
  ASSERT_EQ(TK_IDENT, tk_get_current_token()->kind);
  ASSERT_EQ(5, as_ident(tk_get_current_token())->len);
  tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_ASSIGN, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_SEMI, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  tk_set_enable_trigraphs(true);
  token = tk_tokenize("?" "?=define X 1");
  ASSERT_EQ(TK_HASH, tk_get_current_token()->kind);
  tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_IDENT, tk_get_current_token()->kind);

  tk_set_enable_trigraphs(false);
  token = tk_tokenize("?" "?=define X 1");
  ASSERT_EQ(TK_QUESTION, tk_get_current_token()->kind);
  tk_set_enable_trigraphs(true);
}

// 意地悪テスト: トークン分割の境界ケース
static void test_tokenize_evil_edge_cases() {
  printf("test_tokenize_evil_edge_cases...\n");

  // x+++y → x ++ + y
  token = tk_tokenize("x+++y");
  ASSERT_EQ(TK_IDENT, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_INC, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_PLUS, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_IDENT, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  // x---y → x -- - y
  token = tk_tokenize("x---y");
  ASSERT_EQ(TK_IDENT, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_DEC, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_MINUS, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_IDENT, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  // x+++-y → x ++ + - y
  token = tk_tokenize("x+++-y");
  ASSERT_EQ(TK_IDENT, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_INC, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_PLUS, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_MINUS, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_IDENT, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  // <<=>> → <<= >> (最長一致)
  token = tk_tokenize("<<=>>");
  ASSERT_EQ(TK_SHLEQ, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_SHR, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  // &&|| → && ||
  token = tk_tokenize("&&||");
  ASSERT_EQ(TK_ANDAND, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_OROR, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  // ->* → -> *
  token = tk_tokenize("->*");
  ASSERT_EQ(TK_ARROW, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_MUL, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  // >=< → >= <
  token = tk_tokenize(">=<");
  ASSERT_EQ(TK_GE, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_LT, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  // !=== → != ==
  token = tk_tokenize("!===");
  ASSERT_EQ(TK_NEQ, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EQEQ, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  // 16進の境界値
  token = tk_tokenize("0x0");
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(0, as_num_i(tk_get_current_token())->val);
  tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  token = tk_tokenize("0xFFFFFFFF");
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(0xFFFFFFFFULL, as_num_i(tk_get_current_token())->uval);
  tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  // 空白なしの連続演算
  token = tk_tokenize("1+2*3-4/5%6");
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind); ASSERT_EQ(1, as_num_i(tk_get_current_token())->val); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_PLUS, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind); ASSERT_EQ(2, as_num_i(tk_get_current_token())->val); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_MUL, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind); ASSERT_EQ(3, as_num_i(tk_get_current_token())->val); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_MINUS, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind); ASSERT_EQ(4, as_num_i(tk_get_current_token())->val); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_DIV, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind); ASSERT_EQ(5, as_num_i(tk_get_current_token())->val); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_MOD, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind); ASSERT_EQ(6, as_num_i(tk_get_current_token())->val); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  // 文字列中の特殊文字の後にすぐトークン
  token = tk_tokenize("\"abc\"+1");
  ASSERT_EQ(TK_STRING, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_PLUS, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  // コメントの直後に演算子（空白なし）
  token = tk_tokenize("1/**/+2");
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind); ASSERT_EQ(1, as_num_i(tk_get_current_token())->val); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_PLUS, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind); ASSERT_EQ(2, as_num_i(tk_get_current_token())->val); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  // 文字リテラル直後に演算子
  token = tk_tokenize("'a'+1");
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_PLUS, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  // 文字列直後にセミコロン
  token = tk_tokenize("\"abc\";");
  ASSERT_EQ(TK_STRING, tk_get_current_token()->kind);
  ASSERT_EQ(3, as_string(token)->len);
  tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_SEMI, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  // 連続するドット: .. は不正、... は省略記号
  token = tk_tokenize("...;");
  ASSERT_EQ(TK_ELLIPSIS, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_SEMI, tk_get_current_token()->kind); tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);
}

static void test_strict_c11_mode() {
  printf("test_strict_c11_mode...\n");
  tk_set_strict_c11_mode(false);
  token = tk_tokenize("0b101");
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(5, as_num_i(tk_get_current_token())->val);

  tk_set_strict_c11_mode(true);
  expect_tokenize_fail("0b101");
  tk_set_strict_c11_mode(false);

  tk_set_enable_binary_literals(false);
  expect_tokenize_fail("0b101");
  tk_set_enable_binary_literals(true);
}

static void test_c11_audit_mode_flag() {
  printf("test_c11_audit_mode_flag...\n");
  tk_set_enable_c11_audit_extensions(false);
  ASSERT_TRUE(!tk_get_enable_c11_audit_extensions());
  tk_set_enable_c11_audit_extensions(true);
  ASSERT_TRUE(tk_get_enable_c11_audit_extensions());
  tk_set_enable_c11_audit_extensions(false);
  token = tk_tokenize("0b101");
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(5, as_num_i(tk_get_current_token())->val);
}

static void test_runtime_mode_switch_boundaries() {
  printf("test_runtime_mode_switch_boundaries...\n");

  // strict/binary の切替境界
  tk_set_strict_c11_mode(false);
  tk_set_enable_binary_literals(true);
  ASSERT_TRUE(!tk_get_strict_c11_mode());
  ASSERT_TRUE(tk_get_enable_binary_literals());
  token = tk_tokenize("0b11");
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(3, as_num_i(tk_get_current_token())->val);

  tk_set_strict_c11_mode(true);
  ASSERT_TRUE(tk_get_strict_c11_mode());
  expect_tokenize_fail("0b11");

  tk_set_strict_c11_mode(false);
  tk_set_enable_binary_literals(false);
  ASSERT_TRUE(!tk_get_strict_c11_mode());
  ASSERT_TRUE(!tk_get_enable_binary_literals());
  expect_tokenize_fail("0b11");

  tk_set_enable_binary_literals(true);
  ASSERT_TRUE(tk_get_enable_binary_literals());
  token = tk_tokenize("0b11");
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(3, as_num_i(tk_get_current_token())->val);

  // trigraph の切替境界
  tk_set_enable_trigraphs(false);
  ASSERT_TRUE(!tk_get_enable_trigraphs());
  token = tk_tokenize("?" "?=");
  ASSERT_EQ(TK_QUESTION, tk_get_current_token()->kind);
  tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_QUESTION, tk_get_current_token()->kind);
  tk_set_current_token(tk_get_current_token()->next);
  ASSERT_EQ(TK_ASSIGN, tk_get_current_token()->kind);

  tk_set_enable_trigraphs(true);
  ASSERT_TRUE(tk_get_enable_trigraphs());
  token = tk_tokenize("?" "?=");
  ASSERT_EQ(TK_HASH, tk_get_current_token()->kind);

  // audit の切替境界（トークナイズが壊れないことを確認）
  tk_set_enable_c11_audit_extensions(true);
  ASSERT_TRUE(tk_get_enable_c11_audit_extensions());
  token = tk_tokenize("0b101");
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(5, as_num_i(tk_get_current_token())->val);

  tk_set_enable_c11_audit_extensions(false);
  ASSERT_TRUE(!tk_get_enable_c11_audit_extensions());
  token = tk_tokenize("0b101");
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(5, as_num_i(tk_get_current_token())->val);
}

static void test_tokenize_with_explicit_context() {
  printf("test_tokenize_with_explicit_context...\n");
  tk_set_strict_c11_mode(false);
  tk_set_enable_binary_literals(true);

  tokenizer_context_t ctx;
  tk_context_init(&ctx);
  tk_ctx_set_strict_c11_mode(&ctx, true);
  tk_ctx_set_enable_binary_literals(&ctx, true);

  // default context では許可
  token = tk_tokenize("0b101");
  ASSERT_EQ(TK_NUM, tk_get_current_token()->kind);
  ASSERT_EQ(5, as_num_i(tk_get_current_token())->val);

  // explicit context（strict=true）では拒否
  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    tk_tokenize_ctx(&ctx, "0b101");
    _exit(0);
  }
  int status;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_TRUE(WEXITSTATUS(status) != 0);
}

int main() {
  printf("Running tests for Tokenizer...\n");

  test_tokenize();
  test_tokenize_int_literals();
  test_tokenize_invalid();
  test_tokenize_ident();
  test_tokenize_keywords();
  test_tokenize_symbols();
  test_tokenize_punctuators();
  test_tokenize_comments();
  test_tokenize_string();
  test_tokenize_char_literal();
  test_tokenize_string_prefixes_and_ucn();
  test_tokenize_ucn_ident_and_trigraph();
  test_tokenize_evil_edge_cases();
  test_strict_c11_mode();
  test_c11_audit_mode_flag();
  test_runtime_mode_switch_boundaries();
  test_tokenize_with_explicit_context();
  test_at_eof();
  test_consume();
  test_consume_str();
  test_consume_ident();
  test_expect();
  test_expect_number();
  test_tokenize_float_literal();
  test_tokenize_len_guard_boundaries();

  printf("OK: All unit tests passed!\n");
  return 0;
}
