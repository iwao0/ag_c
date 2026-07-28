#ifndef TOKENIZER_PUNCTUATOR_H
#define TOKENIZER_PUNCTUATOR_H

#include "token.h"
#include <stdbool.h>

/** @brief Return the token kind for a one-character punctuator, or TK_EOF if unmatched. */
token_kind_t punctuator_kind_for_char(char c);

/** @brief Return the token kind for an exactly matching punctuator string. */
token_kind_t punctuator_kind_for_str(const char *op);

/**
 * @brief Match a punctuator using the longest-match rule.
 * @param p Input position.
 * @param out_kind Matched token kind.
 * @param out_len Number of bytes consumed.
 * @return true when matched.
 */
bool match_punctuator(const char *p, token_kind_t *out_kind, int *out_len);

#endif
