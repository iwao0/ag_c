#ifndef TOKENIZER_KEYWORDS_H
#define TOKENIZER_KEYWORDS_H

#include "token.h"

// Returns keyword token kind if matched, otherwise TK_EOF.
token_kind_t lookup_keyword(const char *s, int len);

#endif

