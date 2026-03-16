#include "../src/tokenizer/tokenizer.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_common.h"

// tokenizer.c で定義されているがヘッダにない変数をextern
extern char *user_input;

static token_num_t *as_num(token_t *tok) { return (token_num_t *)tok; }
static token_ident_t *as_ident(token_t *tok) { return (token_ident_t *)tok; }
static token_string_t *as_string(token_t *tok) { return (token_string_t *)tok; }

// 1. tokenize() のテスト
static void test_tokenize() {
  printf("test_tokenize...\n");
  token = tokenize(" 12 + 34 - 5 ");

  // 最初のトークン: "12"
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(12, as_num(token)->val);
  token = token->next;

  // 2番目のトークン: "+"
  ASSERT_EQ(TK_PLUS, token->kind);
  token = token->next;

  // 3番目のトークン: "34"
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(34, as_num(token)->val);
  token = token->next;

  // 4番目のトークン: "-"
  ASSERT_EQ(TK_MINUS, token->kind);
  token = token->next;

  // 5番目のトークン: "5"
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(5, as_num(token)->val);
  token = token->next;

  // 最後のトークン: EOF
  ASSERT_EQ(TK_EOF, token->kind);

  // 四則演算と括弧のテスト
  token = tokenize("1 * (2 / 3)");
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(1, as_num(token)->val);
  token = token->next;

  ASSERT_EQ(TK_MUL, token->kind);
  token = token->next;

  ASSERT_EQ(TK_LPAREN, token->kind);
  token = token->next;

  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(2, as_num(token)->val);
  token = token->next;

  ASSERT_EQ(TK_DIV, token->kind);
  token = token->next;

  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(3, as_num(token)->val);
  token = token->next;

  ASSERT_EQ(TK_RPAREN, token->kind);
  token = token->next;

  ASSERT_EQ(TK_EOF, token->kind);

  // 比較演算子のテスト
  token = tokenize("1 == 2 != 3 <= 4 >= 5 < 6 > 7");
  // 1
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(1, as_num(token)->val);
  token = token->next;
  // ==
  ASSERT_EQ(TK_EQEQ, token->kind);
  token = token->next;
  // 2
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(2, as_num(token)->val);
  token = token->next;
  // !=
  ASSERT_EQ(TK_NEQ, token->kind);
  token = token->next;
  // 3
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(3, as_num(token)->val);
  token = token->next;
  // <=
  ASSERT_EQ(TK_LE, token->kind);
  token = token->next;
  // 4
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(4, as_num(token)->val);
  token = token->next;
  // >=
  ASSERT_EQ(TK_GE, token->kind);
  token = token->next;
  // 5
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(5, as_num(token)->val);
  token = token->next;
  // <
  ASSERT_EQ(TK_LT, token->kind);
  token = token->next;
  // 6
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(6, as_num(token)->val);
  token = token->next;
  // >
  ASSERT_EQ(TK_GT, token->kind);
  token = token->next;
  // 7
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(7, as_num(token)->val);
  token = token->next;

  ASSERT_EQ(TK_EOF, token->kind);
}

// 1b. 16進数/2進数リテラルのテスト
static void test_tokenize_int_literals() {
  printf("test_tokenize_int_literals...\n");
  token = tokenize("0x2a 0X10 0b101 0B11 077 010 0 123 1u 2UL 3llu 0x1ffffffff");

  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(42, as_num(token)->val);
  ASSERT_EQ(42ULL, as_num(token)->uval);
  ASSERT_EQ(16, as_num(token)->int_base);
  ASSERT_TRUE(!as_num(token)->is_unsigned);
  ASSERT_EQ(0, as_num(token)->int_size);
  token = token->next;

  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(16, as_num(token)->val);
  ASSERT_EQ(16ULL, as_num(token)->uval);
  ASSERT_EQ(16, as_num(token)->int_base);
  token = token->next;

  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(5, as_num(token)->val);
  ASSERT_EQ(5ULL, as_num(token)->uval);
  ASSERT_EQ(2, as_num(token)->int_base);
  token = token->next;

  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(3, as_num(token)->val);
  ASSERT_EQ(3ULL, as_num(token)->uval);
  ASSERT_EQ(2, as_num(token)->int_base);
  token = token->next;

  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(63, as_num(token)->val);
  ASSERT_EQ(63ULL, as_num(token)->uval);
  ASSERT_EQ(8, as_num(token)->int_base);
  token = token->next;

  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(8, as_num(token)->val);
  ASSERT_EQ(8ULL, as_num(token)->uval);
  ASSERT_EQ(8, as_num(token)->int_base);
  token = token->next;

  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(0, as_num(token)->val);
  ASSERT_EQ(0ULL, as_num(token)->uval);
  ASSERT_EQ(10, as_num(token)->int_base);
  ASSERT_TRUE(!as_num(token)->is_unsigned);
  ASSERT_EQ(0, as_num(token)->int_size);
  token = token->next;

  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(123, as_num(token)->val);
  ASSERT_EQ(123ULL, as_num(token)->uval);
  ASSERT_EQ(10, as_num(token)->int_base);
  ASSERT_TRUE(!as_num(token)->is_unsigned);
  ASSERT_EQ(0, as_num(token)->int_size);
  token = token->next;

  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(1, as_num(token)->val);
  ASSERT_TRUE(as_num(token)->is_unsigned);
  ASSERT_EQ(0, as_num(token)->int_size);
  token = token->next;

  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(2, as_num(token)->val);
  ASSERT_TRUE(as_num(token)->is_unsigned);
  ASSERT_EQ(1, as_num(token)->int_size);
  token = token->next;

  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(3, as_num(token)->val);
  ASSERT_TRUE(as_num(token)->is_unsigned);
  ASSERT_EQ(2, as_num(token)->int_size);
  token = token->next;

  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(0x1ffffffffULL, as_num(token)->uval);
  ASSERT_TRUE(!as_num(token)->is_unsigned);
  ASSERT_EQ(1, as_num(token)->int_size);
  token = token->next;

  ASSERT_EQ(TK_EOF, token->kind);
}

// 1c. ローカル変数・複数文字識別子のテスト
static void test_tokenize_ident() {
  printf("test_tokenize_ident...\n");
  token = tokenize("a = 3; b = a;");

  ASSERT_EQ(TK_IDENT, token->kind); ASSERT_EQ('a', as_ident(token)->str[0]); ASSERT_EQ(1, as_ident(token)->len); token = token->next;
  ASSERT_EQ(TK_ASSIGN, token->kind); token = token->next;
  ASSERT_EQ(TK_NUM, token->kind); ASSERT_EQ(3, as_num(token)->val); token = token->next;
  ASSERT_EQ(TK_SEMI, token->kind); token = token->next;
  ASSERT_EQ(TK_IDENT, token->kind); ASSERT_EQ('b', as_ident(token)->str[0]); token = token->next;
  ASSERT_EQ(TK_ASSIGN, token->kind); token = token->next;
  ASSERT_EQ(TK_IDENT, token->kind); ASSERT_EQ('a', as_ident(token)->str[0]); token = token->next;
  ASSERT_EQ(TK_SEMI, token->kind); token = token->next;
  ASSERT_EQ(TK_EOF, token->kind);

  // 複数文字識別子
  token = tokenize("foo my_var x1");
  ASSERT_EQ(TK_IDENT, token->kind); ASSERT_EQ(3, as_ident(token)->len);
  ASSERT_TRUE(strncmp(as_ident(token)->str, "foo", 3) == 0); token = token->next;
  ASSERT_EQ(TK_IDENT, token->kind); ASSERT_EQ(6, as_ident(token)->len);
  ASSERT_TRUE(strncmp(as_ident(token)->str, "my_var", 6) == 0); token = token->next;
  ASSERT_EQ(TK_IDENT, token->kind); ASSERT_EQ(2, as_ident(token)->len);
  ASSERT_TRUE(strncmp(as_ident(token)->str, "x1", 2) == 0); token = token->next;
  ASSERT_EQ(TK_EOF, token->kind);

  // アンダースコアで始まる識別子
  token = tokenize("_start _a1");
  ASSERT_EQ(TK_IDENT, token->kind); ASSERT_EQ(6, as_ident(token)->len);
  ASSERT_TRUE(strncmp(as_ident(token)->str, "_start", 6) == 0); token = token->next;
  ASSERT_EQ(TK_IDENT, token->kind); ASSERT_EQ(3, as_ident(token)->len);
  ASSERT_TRUE(strncmp(as_ident(token)->str, "_a1", 3) == 0); token = token->next;
  ASSERT_EQ(TK_EOF, token->kind);
}

// 1c. キーワードトークンのテスト
static void test_tokenize_keywords() {
  printf("test_tokenize_keywords...\n");

  // 制御構文キーワード
  token = tokenize("if else while for return");
  ASSERT_EQ(TK_IF, token->kind);     token = token->next;
  ASSERT_EQ(TK_ELSE, token->kind);   token = token->next;
  ASSERT_EQ(TK_WHILE, token->kind);  token = token->next;
  ASSERT_EQ(TK_FOR, token->kind);    token = token->next;
  ASSERT_EQ(TK_RETURN, token->kind); token = token->next;
  ASSERT_EQ(TK_EOF, token->kind);

  // 型キーワード
  token = tokenize("int char void short long float double signed unsigned");
  ASSERT_EQ(TK_INT, token->kind);     token = token->next;
  ASSERT_EQ(TK_CHAR, token->kind);    token = token->next;
  ASSERT_EQ(TK_VOID, token->kind);    token = token->next;
  ASSERT_EQ(TK_SHORT, token->kind);   token = token->next;
  ASSERT_EQ(TK_LONG, token->kind);    token = token->next;
  ASSERT_EQ(TK_FLOAT, token->kind);   token = token->next;
  ASSERT_EQ(TK_DOUBLE, token->kind);  token = token->next;
  ASSERT_EQ(TK_SIGNED, token->kind);  token = token->next;
  ASSERT_EQ(TK_UNSIGNED, token->kind); token = token->next;
  ASSERT_EQ(TK_EOF, token->kind);

  // その他のキーワード
  token = tokenize(
      "auto break case const continue default do enum extern goto inline "
      "register restrict sizeof static struct switch typedef union volatile "
      "_Alignas _Alignof _Atomic _Bool _Complex _Generic _Imaginary "
      "_Noreturn _Static_assert _Thread_local");
  ASSERT_EQ(TK_AUTO, token->kind); token = token->next;
  ASSERT_EQ(TK_BREAK, token->kind); token = token->next;
  ASSERT_EQ(TK_CASE, token->kind); token = token->next;
  ASSERT_EQ(TK_CONST, token->kind); token = token->next;
  ASSERT_EQ(TK_CONTINUE, token->kind); token = token->next;
  ASSERT_EQ(TK_DEFAULT, token->kind); token = token->next;
  ASSERT_EQ(TK_DO, token->kind); token = token->next;
  ASSERT_EQ(TK_ENUM, token->kind); token = token->next;
  ASSERT_EQ(TK_EXTERN, token->kind); token = token->next;
  ASSERT_EQ(TK_GOTO, token->kind); token = token->next;
  ASSERT_EQ(TK_INLINE, token->kind); token = token->next;
  ASSERT_EQ(TK_REGISTER, token->kind); token = token->next;
  ASSERT_EQ(TK_RESTRICT, token->kind); token = token->next;
  ASSERT_EQ(TK_SIZEOF, token->kind); token = token->next;
  ASSERT_EQ(TK_STATIC, token->kind); token = token->next;
  ASSERT_EQ(TK_STRUCT, token->kind); token = token->next;
  ASSERT_EQ(TK_SWITCH, token->kind); token = token->next;
  ASSERT_EQ(TK_TYPEDEF, token->kind); token = token->next;
  ASSERT_EQ(TK_UNION, token->kind); token = token->next;
  ASSERT_EQ(TK_VOLATILE, token->kind); token = token->next;
  ASSERT_EQ(TK_ALIGNAS, token->kind); token = token->next;
  ASSERT_EQ(TK_ALIGNOF, token->kind); token = token->next;
  ASSERT_EQ(TK_ATOMIC, token->kind); token = token->next;
  ASSERT_EQ(TK_BOOL, token->kind); token = token->next;
  ASSERT_EQ(TK_COMPLEX, token->kind); token = token->next;
  ASSERT_EQ(TK_GENERIC, token->kind); token = token->next;
  ASSERT_EQ(TK_IMAGINARY, token->kind); token = token->next;
  ASSERT_EQ(TK_NORETURN, token->kind); token = token->next;
  ASSERT_EQ(TK_STATIC_ASSERT, token->kind); token = token->next;
  ASSERT_EQ(TK_THREAD_LOCAL, token->kind); token = token->next;
  ASSERT_EQ(TK_EOF, token->kind);

  // キーワードと似た識別子はキーワードにならない
  token = tokenize("iff int1 returns");
  ASSERT_EQ(TK_IDENT, token->kind);
  ASSERT_TRUE(strncmp(as_ident(token)->str, "iff", 3) == 0); token = token->next;
  ASSERT_EQ(TK_IDENT, token->kind);
  ASSERT_TRUE(strncmp(as_ident(token)->str, "int1", 4) == 0); token = token->next;
  ASSERT_EQ(TK_IDENT, token->kind);
  ASSERT_TRUE(strncmp(as_ident(token)->str, "returns", 7) == 0); token = token->next;
  ASSERT_EQ(TK_EOF, token->kind);
}

// 1d. 追加記号のテスト
static void test_tokenize_symbols() {
  printf("test_tokenize_symbols...\n");
  token = tokenize("{ } , & [ ]");
  ASSERT_EQ(TK_LBRACE, token->kind); token = token->next;
  ASSERT_EQ(TK_RBRACE, token->kind); token = token->next;
  ASSERT_EQ(TK_COMMA, token->kind); token = token->next;
  ASSERT_EQ(TK_AMP, token->kind); token = token->next;
  ASSERT_EQ(TK_LBRACKET, token->kind); token = token->next;
  ASSERT_EQ(TK_RBRACKET, token->kind); token = token->next;
  ASSERT_EQ(TK_EOF, token->kind);
}

// 1d-2. 追加演算子のテスト
static void test_tokenize_punctuators() {
  printf("test_tokenize_punctuators...\n");
  token = tokenize("++ -- -> << >> <<= >>= += -= *= /= %= &= ^= |= % | ^ ? : ...");
  ASSERT_EQ(TK_INC, token->kind); token = token->next;
  ASSERT_EQ(TK_DEC, token->kind); token = token->next;
  ASSERT_EQ(TK_ARROW, token->kind); token = token->next;
  ASSERT_EQ(TK_SHL, token->kind); token = token->next;
  ASSERT_EQ(TK_SHR, token->kind); token = token->next;
  ASSERT_EQ(TK_SHLEQ, token->kind); token = token->next;
  ASSERT_EQ(TK_SHREQ, token->kind); token = token->next;
  ASSERT_EQ(TK_PLUSEQ, token->kind); token = token->next;
  ASSERT_EQ(TK_MINUSEQ, token->kind); token = token->next;
  ASSERT_EQ(TK_MULEQ, token->kind); token = token->next;
  ASSERT_EQ(TK_DIVEQ, token->kind); token = token->next;
  ASSERT_EQ(TK_MODEQ, token->kind); token = token->next;
  ASSERT_EQ(TK_ANDEQ, token->kind); token = token->next;
  ASSERT_EQ(TK_XOREQ, token->kind); token = token->next;
  ASSERT_EQ(TK_OREQ, token->kind); token = token->next;
  ASSERT_EQ(TK_MOD, token->kind); token = token->next;
  ASSERT_EQ(TK_PIPE, token->kind); token = token->next;
  ASSERT_EQ(TK_CARET, token->kind); token = token->next;
  ASSERT_EQ(TK_QUESTION, token->kind); token = token->next;
  ASSERT_EQ(TK_COLON, token->kind); token = token->next;
  ASSERT_EQ(TK_ELLIPSIS, token->kind); token = token->next;
  ASSERT_EQ(TK_EOF, token->kind);

  token = tokenize("<: :> <% %> %: %:%:");
  ASSERT_EQ(TK_LBRACKET, token->kind); token = token->next;
  ASSERT_EQ(TK_RBRACKET, token->kind); token = token->next;
  ASSERT_EQ(TK_LBRACE, token->kind); token = token->next;
  ASSERT_EQ(TK_RBRACE, token->kind); token = token->next;
  ASSERT_EQ(TK_HASH, token->kind); token = token->next;
  ASSERT_EQ(TK_HASHHASH, token->kind); token = token->next;
  ASSERT_EQ(TK_EOF, token->kind);

  token = tokenize("#include \"x.h\"\nint main() { return 0; }");
  ASSERT_EQ(TK_HASH, token->kind);
  ASSERT_TRUE(token->at_bol);
  token = token->next;
  ASSERT_EQ(TK_IDENT, token->kind); token = token->next;
  ASSERT_EQ(TK_STRING, token->kind); token = token->next;
  ASSERT_EQ(TK_INT, token->kind); token = token->next;
  ASSERT_EQ(TK_IDENT, token->kind); token = token->next;
}

// 1d-3. コメントと空白のテスト
static void test_tokenize_comments() {
  printf("test_tokenize_comments...\n");
  token = tokenize("1//comment\n2");
  ASSERT_EQ(TK_NUM, token->kind); token = token->next;
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_TRUE(token->at_bol);
  token = token->next;
  ASSERT_EQ(TK_EOF, token->kind);

  token = tokenize("1/* comment */2");
  ASSERT_EQ(TK_NUM, token->kind); token = token->next;
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_TRUE(!token->at_bol);
  token = token->next;
  ASSERT_EQ(TK_EOF, token->kind);

  token = tokenize("1/*\n*/2");
  ASSERT_EQ(TK_NUM, token->kind); token = token->next;
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_TRUE(token->at_bol);
  token = token->next;
  ASSERT_EQ(TK_EOF, token->kind);
}

// 1e. 文字列リテラルのテスト
static void test_tokenize_string() {
  printf("test_tokenize_string...\n");
  token = tokenize("\"hello\"");
  ASSERT_EQ(TK_STRING, token->kind);
  ASSERT_EQ(5, as_string(token)->len);
  ASSERT_TRUE(strncmp(as_string(token)->str, "hello", 5) == 0);
  token = token->next;
  ASSERT_EQ(TK_EOF, token->kind);

  // 空文字列
  token = tokenize("\"\"");
  ASSERT_EQ(TK_STRING, token->kind);
  ASSERT_EQ(0, as_string(token)->len);
  token = token->next;
  ASSERT_EQ(TK_EOF, token->kind);

  // 文字列と他のトークンの混在
  token = tokenize("char *s = \"AB\";");
  ASSERT_EQ(TK_CHAR, token->kind);     token = token->next;
  ASSERT_EQ(TK_MUL, token->kind); token = token->next;
  ASSERT_EQ(TK_IDENT, token->kind);    token = token->next;
  ASSERT_EQ(TK_ASSIGN, token->kind); token = token->next;
  ASSERT_EQ(TK_STRING, token->kind);
  ASSERT_EQ(2, as_string(token)->len);
  ASSERT_TRUE(strncmp(as_string(token)->str, "AB", 2) == 0);
  token = token->next;
  ASSERT_EQ(TK_SEMI, token->kind); token = token->next;
  ASSERT_EQ(TK_EOF, token->kind);
}

// 1g. 浮動小数点リテラルのテスト
static void test_tokenize_float_literal() {
  printf("test_tokenize_float_literal...\n");
  token = tokenize("3.14 1.5f 2.0E-3 0x1.8p1 0x1p2f");
  
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(2, as_num(token)->is_float); // デフォルトは double
  ASSERT_TRUE(as_num(token)->fval > 3.13 && as_num(token)->fval < 3.15);
  token = token->next;
  
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(1, as_num(token)->is_float); // 'f' suffix なので float
  ASSERT_TRUE(as_num(token)->fval > 1.49 && as_num(token)->fval < 1.51);
  token = token->next;
  
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(2, as_num(token)->is_float); // 指数表現
  ASSERT_TRUE(as_num(token)->fval > 0.0019 && as_num(token)->fval < 0.0021);
  token = token->next;

  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(2, as_num(token)->is_float); // 16進浮動小数点
  ASSERT_TRUE(as_num(token)->fval > 2.9 && as_num(token)->fval < 3.1);
  token = token->next;

  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(1, as_num(token)->is_float); // 16進浮動小数点 + f
  ASSERT_TRUE(as_num(token)->fval > 3.9 && as_num(token)->fval < 4.1);
  token = token->next;
  
  ASSERT_EQ(TK_EOF, token->kind);
}

// 2. consume() のテスト
static void test_consume() {
  printf("test_consume...\n");
  token = tokenize(" + 42 ");

  ASSERT_TRUE(consume('+'));  // "+"を消費して次に進む
  ASSERT_TRUE(!consume('-')); // "-"ではないので進まない
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(42, as_num(token)->val);
}

// 2b. consume_str() のテスト
static void test_consume_str() {
  printf("test_consume_str...\n");
  token = tokenize("== != < <=");
  ASSERT_TRUE(consume_str("=="));
  ASSERT_TRUE(!consume_str("=="));  // 次は != なので false
  ASSERT_TRUE(consume_str("!="));
  ASSERT_TRUE(!consume_str("<="));  // 次は < (1文字) なので <= にはマッチしない
  ASSERT_TRUE(consume('<'));
  ASSERT_TRUE(consume_str("<="));
  ASSERT_EQ(TK_EOF, token->kind);
}

// 2c. consume_ident() のテスト
static void test_consume_ident() {
  printf("test_consume_ident...\n");
  token = tokenize("foo 42");
  token_ident_t *tok = consume_ident();
  ASSERT_TRUE(tok != NULL);
  ASSERT_EQ(3, tok->len);
  ASSERT_TRUE(strncmp(tok->str, "foo", 3) == 0);
  // 数値トークンでは NULL を返す
  tok = consume_ident();
  ASSERT_TRUE(tok == NULL);
  ASSERT_EQ(TK_NUM, token->kind);
}

// 3. expect() のテスト
static void test_expect() {
  printf("test_expect...\n");
  token = tokenize(" - ");
  expect('-');
  ASSERT_EQ(TK_EOF, token->kind);
}

// 4. expect_number() のテスト
static void test_expect_number() {
  printf("test_expect_number...\n");
  token = tokenize(" 999 ");
  int val = expect_number();
  ASSERT_EQ(999, val);
  ASSERT_EQ(TK_EOF, token->kind);
}

// 5. at_eof() のテスト
static void test_at_eof() {
  printf("test_at_eof...\n");
  token = tokenize(" 1 ");
  ASSERT_TRUE(!at_eof());
  expect_number();
  ASSERT_TRUE(at_eof());
}

// 1f. 文字リテラルのテスト
static void test_tokenize_char_literal() {
  printf("test_tokenize_char_literal...\n");
  token = tokenize("'A'");
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(65, as_num(token)->val);
  token = token->next;
  ASSERT_EQ(TK_EOF, token->kind);

  // エスケープシーケンス
  token = tokenize("'\\n'");
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(10, as_num(token)->val);
  token = token->next;
  ASSERT_EQ(TK_EOF, token->kind);

  token = tokenize("'\\0'");
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(0, as_num(token)->val);
  token = token->next;
  ASSERT_EQ(TK_EOF, token->kind);
}

int main() {
  printf("Running tests for Tokenizer...\n");

  test_tokenize();
  test_tokenize_int_literals();
  test_tokenize_ident();
  test_tokenize_keywords();
  test_tokenize_symbols();
  test_tokenize_punctuators();
  test_tokenize_comments();
  test_tokenize_string();
  test_tokenize_char_literal();
  test_at_eof();
  test_consume();
  test_consume_str();
  test_consume_ident();
  test_expect();
  test_expect_number();
  test_tokenize_char_literal();
  test_tokenize_float_literal();

  printf("OK: All unit tests passed!\n");
  return 0;
}
