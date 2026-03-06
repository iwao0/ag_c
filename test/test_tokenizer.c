#include "../src/tokenizer/tokenizer.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_common.h"

// tokenizer.c で定義されているがヘッダにない変数と関数をextern
extern char *user_input;

// 1. tokenize() のテスト
static void test_tokenize() {
  printf("test_tokenize...\n");
  token = tokenize(" 12 + 34 - 5 ");

  // 最初のトークン: "12"
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(12, token->val);
  token = token->next;

  // 2番目のトークン: "+"
  ASSERT_EQ(TK_RESERVED, token->kind);
  ASSERT_EQ('+', token->str[0]);
  ASSERT_EQ(1, token->len);
  token = token->next;

  // 3番目のトークン: "34"
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(34, token->val);
  token = token->next;

  // 4番目のトークン: "-"
  ASSERT_EQ(TK_RESERVED, token->kind);
  ASSERT_EQ('-', token->str[0]);
  ASSERT_EQ(1, token->len);
  token = token->next;

  // 5番目のトークン: "5"
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(5, token->val);
  token = token->next;

  // 最後のトークン: EOF
  ASSERT_EQ(TK_EOF, token->kind);

  // 四則演算と括弧のテスト
  token = tokenize("1 * (2 / 3)");
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(1, token->val);
  token = token->next;

  ASSERT_EQ(TK_RESERVED, token->kind);
  ASSERT_EQ('*', token->str[0]);
  token = token->next;

  ASSERT_EQ(TK_RESERVED, token->kind);
  ASSERT_EQ('(', token->str[0]);
  token = token->next;

  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(2, token->val);
  token = token->next;

  ASSERT_EQ(TK_RESERVED, token->kind);
  ASSERT_EQ('/', token->str[0]);
  token = token->next;

  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(3, token->val);
  token = token->next;

  ASSERT_EQ(TK_RESERVED, token->kind);
  ASSERT_EQ(')', token->str[0]);
  token = token->next;

  ASSERT_EQ(TK_EOF, token->kind);

  // 比較演算子のテスト
  token = tokenize("1 == 2 != 3 <= 4 >= 5 < 6 > 7");
  // 1
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(1, token->val);
  token = token->next;
  // ==
  ASSERT_EQ(TK_RESERVED, token->kind);
  ASSERT_EQ(2, token->len);
  ASSERT_EQ('=', token->str[0]);
  ASSERT_EQ('=', token->str[1]);
  token = token->next;
  // 2
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(2, token->val);
  token = token->next;
  // !=
  ASSERT_EQ(TK_RESERVED, token->kind);
  ASSERT_EQ(2, token->len);
  ASSERT_EQ('!', token->str[0]);
  ASSERT_EQ('=', token->str[1]);
  token = token->next;
  // 3
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(3, token->val);
  token = token->next;
  // <=
  ASSERT_EQ(TK_RESERVED, token->kind);
  ASSERT_EQ(2, token->len);
  ASSERT_EQ('<', token->str[0]);
  ASSERT_EQ('=', token->str[1]);
  token = token->next;
  // 4
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(4, token->val);
  token = token->next;
  // >=
  ASSERT_EQ(TK_RESERVED, token->kind);
  ASSERT_EQ(2, token->len);
  ASSERT_EQ('>', token->str[0]);
  ASSERT_EQ('=', token->str[1]);
  token = token->next;
  // 5
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(5, token->val);
  token = token->next;
  // <
  ASSERT_EQ(TK_RESERVED, token->kind);
  ASSERT_EQ(1, token->len);
  ASSERT_EQ('<', token->str[0]);
  token = token->next;
  // 6
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(6, token->val);
  token = token->next;
  // >
  ASSERT_EQ(TK_RESERVED, token->kind);
  ASSERT_EQ(1, token->len);
  ASSERT_EQ('>', token->str[0]);
  token = token->next;
  // 7
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(7, token->val);
  token = token->next;

  ASSERT_EQ(TK_EOF, token->kind);
}

// 1b. tokenize() ローカル変数関連のテスト
static void test_tokenize_ident() {
  printf("test_tokenize_ident...\n");
  token = tokenize("a = 3; b = a;");

  // a
  ASSERT_EQ(TK_IDENT, token->kind);
  ASSERT_EQ('a', token->str[0]);
  ASSERT_EQ(1, token->len);
  token = token->next;
  // =
  ASSERT_EQ(TK_RESERVED, token->kind);
  ASSERT_EQ('=', token->str[0]);
  ASSERT_EQ(1, token->len);
  token = token->next;
  // 3
  ASSERT_EQ(TK_NUM, token->kind);
  ASSERT_EQ(3, token->val);
  token = token->next;
  // ;
  ASSERT_EQ(TK_RESERVED, token->kind);
  ASSERT_EQ(';', token->str[0]);
  token = token->next;
  // b
  ASSERT_EQ(TK_IDENT, token->kind);
  ASSERT_EQ('b', token->str[0]);
  token = token->next;
  // =
  ASSERT_EQ(TK_RESERVED, token->kind);
  ASSERT_EQ('=', token->str[0]);
  token = token->next;
  // a
  ASSERT_EQ(TK_IDENT, token->kind);
  ASSERT_EQ('a', token->str[0]);
  token = token->next;
  // ;
  ASSERT_EQ(TK_RESERVED, token->kind);
  ASSERT_EQ(';', token->str[0]);
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
  ASSERT_EQ(42, token->val);
}

// 3. expect() のテスト
static void test_expect() {
  printf("test_expect...\n");
  token = tokenize(" - ");

  // "-"を期待して進む
  expect('-');

  // 次はEOFのはず
  ASSERT_EQ(TK_EOF, token->kind);
}

// 4. expect_number() のテスト
static void test_expect_number() {
  printf("test_expect_number...\n");
  token = tokenize(" 999 ");

  int val = expect_number();
  ASSERT_EQ(999, val);

  // 次はEOFのはず
  ASSERT_EQ(TK_EOF, token->kind);
}

// 5. at_eof() のテスト
static void test_at_eof() {
  printf("test_at_eof...\n");
  token = tokenize(" 1 ");

  ASSERT_TRUE(!at_eof());
  expect_number();
  ASSERT_TRUE(at_eof()); // 全て処理した後はEOF
}

int main() {
  printf("Running tests for Tokenizer...\n");

  test_tokenize();
  test_tokenize_ident();
  test_consume();
  test_expect();
  test_expect_number();
  test_at_eof();

  printf("OK: All unit tests passed!\n");
  return 0;
}
