#include "internal/diag.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

void psx_diag_ctx(token_t *tok, const char *rule, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  va_list ap2;
  va_copy(ap2, ap);
  int len = vsnprintf(NULL, 0, fmt, ap2);
  va_end(ap2);
  if (len < 0) {
    va_end(ap);
    diag_emit_tokf(DIAG_ERR_PARSER_DIAG_FORMAT_FAILED, tok, "%s",
                   diag_message_for(DIAG_ERR_PARSER_DIAG_FORMAT_FAILED));
  }

  char *detail = calloc((size_t)len + 1, 1);
  if (!detail) {
    va_end(ap);
    diag_emit_tokf(DIAG_ERR_INTERNAL_OOM, tok, "[%s] %s", (char *)rule,
                   diag_message_for(DIAG_ERR_INTERNAL_OOM));
  }
  vsnprintf(detail, (size_t)len + 1, fmt, ap);
  va_end(ap);
  diag_emit_tokf(DIAG_ERR_PARSER_RULE_DETAIL, tok,
                 diag_message_for(DIAG_ERR_PARSER_RULE_DETAIL), (char *)rule, detail);
  free(detail);
}

void psx_diag_missing(token_t *tok, const char *what) {
  diag_emit_tokf(DIAG_ERR_PARSER_MISSING_ITEM, tok,
                 diag_message_for(DIAG_ERR_PARSER_MISSING_ITEM), what);
}

void psx_diag_undefined_with_name(token_t *tok, const char *kind, const char *name, int len) {
  diag_emit_tokf(DIAG_ERR_PARSER_UNDEFINED_WITH_KIND, tok,
                 diag_message_for(DIAG_ERR_PARSER_UNDEFINED_WITH_KIND), kind, len, name);
}

void psx_diag_duplicate_with_name(token_t *tok, const char *kind, const char *name, int len) {
  diag_emit_tokf(DIAG_ERR_PARSER_DUPLICATE_WITH_KIND, tok,
                 diag_message_for(DIAG_ERR_PARSER_DUPLICATE_WITH_KIND), kind, len, name);
}

void psx_diag_only_in(token_t *tok, const char *what, const char *scope) {
  diag_emit_tokf(DIAG_ERR_PARSER_ONLY_IN_SCOPE, tok,
                 diag_message_for(DIAG_ERR_PARSER_ONLY_IN_SCOPE), what, scope);
}
