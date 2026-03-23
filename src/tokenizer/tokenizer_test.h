#ifndef TOKENIZER_TEST_H
#define TOKENIZER_TEST_H

#include <stddef.h>

// Test-only hook: 0 で無制限、正値でトークン長上限を設定する。
void tk_set_max_token_len_for_test(size_t max_len);

#endif
