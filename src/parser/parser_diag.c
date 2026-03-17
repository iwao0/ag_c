#include "parser_diag.h"
#include "../tokenizer/tokenizer.h"

void pdiag_missing(token_t *tok, const char *what) {
  tk_error_tok(tok, "%sが必要です", (char *)what);
}

void pdiag_undefined_with_name(token_t *tok, const char *kind, const char *name, int len) {
  tk_error_tok(tok, "未定義%s '%.*s' です", (char *)kind, len, name);
}

void pdiag_duplicate_with_name(token_t *tok, const char *kind, const char *name, int len) {
  tk_error_tok(tok, "%s '%.*s' が重複しています", (char *)kind, len, name);
}

void pdiag_only_in(token_t *tok, const char *what, const char *scope) {
  tk_error_tok(tok, "%s は %sでのみ使用できます", (char *)what, (char *)scope);
}
