#include "../../src/tokenizer/tokenizer_test.h"
#include "../../src/tokenizer/internal/test_hook.h"

void tk_set_max_token_len_for_test(size_t max_len) {
  tk_set_max_token_len_limit_for_test(max_len);
}
