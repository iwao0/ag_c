#ifndef TOKENIZER_PUNCTUATOR_H
#define TOKENIZER_PUNCTUATOR_H

#include "token.h"
#include <stdbool.h>

// Returns token kind for exact punctuator string, or TK_EOF if unknown.
token_kind_t punctuator_kind_for_str(const char *op);

// Match punctuator at p with longest-match rule.
// Returns true when matched. out_len is byte length consumed.
bool match_punctuator(const char *p, token_kind_t *out_kind, int *out_len);

#endif

