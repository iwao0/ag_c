#ifndef PARSER_DIAG_H
#define PARSER_DIAG_H

#include "../tokenizer/token.h"

void pdiag_missing(token_t *tok, const char *what);
void pdiag_undefined_with_name(token_t *tok, const char *kind, const char *name, int len);
void pdiag_duplicate_with_name(token_t *tok, const char *kind, const char *name, int len);
void pdiag_only_in(token_t *tok, const char *what, const char *scope);

#endif
