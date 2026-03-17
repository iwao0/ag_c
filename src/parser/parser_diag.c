#include "parser_diag.h"
#include "../tokenizer/tokenizer.h"
#include <stdarg.h>
#include <stdio.h>

void pdiag_ctx(token_t *tok, const char *rule, const char *fmt, ...) {
  char detail[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(detail, sizeof(detail), fmt, ap);
  va_end(ap);
  tk_error_tok(tok, "[%s] %s", (char *)rule, detail);
}

void pdiag_missing(token_t *tok, const char *what) {
  pdiag_ctx(tok, "parser", "%sが必要です", what);
}

void pdiag_undefined_with_name(token_t *tok, const char *kind, const char *name, int len) {
  pdiag_ctx(tok, "parser", "未定義%s '%.*s' です", kind, len, name);
}

void pdiag_duplicate_with_name(token_t *tok, const char *kind, const char *name, int len) {
  pdiag_ctx(tok, "parser", "%s '%.*s' が重複しています", kind, len, name);
}

void pdiag_only_in(token_t *tok, const char *what, const char *scope) {
  pdiag_ctx(tok, "parser", "%s は %sでのみ使用できます", what, scope);
}
