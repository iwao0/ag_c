#ifndef TOKENIZER_CONTEXT_H
#define TOKENIZER_CONTEXT_H

#include "tokenizer.h"

/*
 * Internal tokenizer execution-context and cursor infrastructure.
 * tokenizer.c defines the concrete type, which is shared by the token
 * generation core (tokenizer.c), numeric-literal parser (number.c), and cursor
 * consumption API (cursor.c).  This infrastructure is not exposed in public
 * headers; its authoritative definition remains private to tokenizer.c.
 */

tokenizer_context_t *tk_effective_ctx(tokenizer_context_t *ctx);
void tk_advance_current_token(tokenizer_context_t *ctx, token_t *cur);

#endif
