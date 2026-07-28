#ifndef TOKENIZER_KEYWORDS_H
#define TOKENIZER_KEYWORDS_H

#include "token.h"

/** @brief Return the matching token kind for a keyword, or `TK_EOF` if unmatched. */
token_kind_t lookup_keyword(const char *s, int len);

#endif
