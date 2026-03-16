#include "../src/tokenizer/tokenizer.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "test_common.h"

static token_num_t *as_num(token_t *tok) { return (token_num_t *)tok; }
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
  ASSERT_EQ(4, ((token_string_t *)tok)->char_width);
  tok = tok->next;
  ASSERT_EQ(TK_STRING, tok->kind);
  ASSERT_EQ(2, ((token_string_t *)tok)->char_width);
  tok = tok->next;
  ASSERT_EQ(TK_STRING, tok->kind);
  ASSERT_EQ(4, ((token_string_t *)tok)->char_width);
  tok = tok->next;
  ASSERT_EQ(TK_STRING, tok->kind);
  ASSERT_EQ(1, ((token_string_t *)tok)->char_width);
}

static void test_c11_binary_literal_strict_behavior(void) {
  printf("test_c11_binary_literal_strict_behavior...\n");
  tk_set_strict_c11_mode(false);
  token_t *tok = tk_tokenize("0b101");
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_EQ(5, as_num(tok)->val);

  tk_set_strict_c11_mode(true);
  expect_tokenize_fail("0b101");
  tk_set_strict_c11_mode(false);
}

static void test_c11_float_suffix_metadata(void) {
  printf("test_c11_float_suffix_metadata...\n");
  token_t *tok = tk_tokenize("1.0f 2.0L");
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_EQ(1, as_num(tok)->float_suffix_kind);
  tok = tok->next;
  ASSERT_EQ(TK_NUM, tok->kind);
  ASSERT_EQ(2, as_num(tok)->float_suffix_kind);
}

int main(void) {
  printf("Running C11-focused Tokenizer tests...\n");
  test_c11_ident_ucn();
  test_c11_string_prefixes();
  test_c11_binary_literal_strict_behavior();
  test_c11_float_suffix_metadata();
  printf("OK: Tokenizer C11-focused tests passed!\n");
  return 0;
}
