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
    diag_emit_tokf(DIAG_ERR_PARSER_GENERIC, tok, "[%s] 診断メッセージの生成に失敗しました",
                   (char *)rule);
  }

  char *detail = calloc((size_t)len + 1, 1);
  if (!detail) {
    va_end(ap);
    diag_emit_tokf(DIAG_ERR_INTERNAL_OOM, tok, "[%s] メモリ確保に失敗しました", (char *)rule);
  }
  vsnprintf(detail, (size_t)len + 1, fmt, ap);
  va_end(ap);
  diag_emit_tokf(DIAG_ERR_PARSER_GENERIC, tok, "[%s] %s", (char *)rule, detail);
  free(detail);
}

void psx_diag_missing(token_t *tok, const char *what) {
  psx_diag_ctx(tok, "parser", "%sが必要です", what);
}

void psx_diag_undefined_with_name(token_t *tok, const char *kind, const char *name, int len) {
  psx_diag_ctx(tok, "parser", "未定義%s '%.*s' です", kind, len, name);
}

void psx_diag_duplicate_with_name(token_t *tok, const char *kind, const char *name, int len) {
  psx_diag_ctx(tok, "parser", "%s '%.*s' が重複しています", kind, len, name);
}

void psx_diag_only_in(token_t *tok, const char *what, const char *scope) {
  psx_diag_ctx(tok, "parser", "%s は %sでのみ使用できます", what, scope);
}
