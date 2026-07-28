#ifndef TOKENIZER_SCANNER_H
#define TOKENIZER_SCANNER_H

#include <stdbool.h>

typedef struct tokenizer_context_t tokenizer_context_t;

/** @brief Skip whitespace, comments, and line continuations, updating location state. */
char *tk_skip_ignored_ctx(tokenizer_context_t *ctx, char *p,
                          bool *at_bol, bool *has_space, int *line_no);
/** @brief Test for an identifier-start character and return its byte length. */
bool tk_scan_ident_start(const char *p, int *adv);
/** @brief Test for an identifier-continuation character and return its byte length. */
bool tk_scan_ident_continue(const char *p, int *adv);

#endif
