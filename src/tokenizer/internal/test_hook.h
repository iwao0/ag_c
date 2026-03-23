#ifndef TOKENIZER_TEST_HOOK_INTERNAL_H
#define TOKENIZER_TEST_HOOK_INTERNAL_H

#include <stddef.h>

// Internal bridge for test-only controls.
void tk_set_max_token_len_limit_for_test(size_t max_len);

#endif
