#ifndef TOKENIZER_KEYWORDS_H
#define TOKENIZER_KEYWORDS_H

#include "../token.h"

/** @brief キーワード一致時は対応 token kind、非一致時は `TK_EOF` を返す。 */
token_kind_t lookup_keyword(const char *s, int len);

#endif
