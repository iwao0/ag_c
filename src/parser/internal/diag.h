#ifndef PARSER_DIAG_H
#define PARSER_DIAG_H

#include "../../tokenizer/token.h"

void psx_diag_missing(token_t *tok, const char *what);
void psx_diag_undefined_with_name(token_t *tok, const char *kind, const char *name, int len);
void psx_diag_duplicate_with_name(token_t *tok, const char *kind, const char *name, int len);
void psx_diag_only_in(token_t *tok, const char *what, const char *scope);
void psx_diag_ctx(token_t *tok, const char *rule, const char *fmt, ...);

#endif
