#ifndef AG_PREPROCESS_COMPAT_H
#define AG_PREPROCESS_COMPAT_H

#include "preprocess.h"

token_t *preprocess(token_t *tok);
token_t *preprocess_ctx(tokenizer_context_t *tk_ctx, token_t *tok);
token_t *pp_stream_open(
    pp_stream_t **out_s, tokenizer_context_t *tk_ctx, const char *src);

#endif
