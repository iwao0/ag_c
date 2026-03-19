#include "diag.h"
#include "messages.h"
#include "../tokenizer/tokenizer.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *g_diag_locale = "ja";

void diag_set_locale(const char *locale) {
  if (!locale || locale[0] == '\0') return;
  g_diag_locale = locale;
}

const char *diag_get_locale(void) {
  return g_diag_locale;
}

const char *diag_message_for(diag_error_id_t id) {
  const char *msg = NULL;
#if defined(DIAG_LANG_ALL)
  if (strcmp(g_diag_locale, "en") == 0) {
    msg = diag_message_en(id);
    if (msg) return msg;
    msg = diag_message_ja(id);
    if (msg) return msg;
  } else {
    msg = diag_message_ja(id);
    if (msg) return msg;
    msg = diag_message_en(id);
    if (msg) return msg;
  }
#elif defined(DIAG_LANG_EN)
  (void)g_diag_locale;
  msg = diag_message_en(id);
  if (msg) return msg;
#else
  (void)g_diag_locale;
  msg = diag_message_ja(id);
  if (msg) return msg;
#endif
  return diag_error_key(id);
}

static void print_token_actual(const token_t *tok) {
  if (!tok) return;
  if (tok->kind == TK_IDENT) {
    const token_ident_t *id = (const token_ident_t *)tok;
    int n = id->len < 0 ? 0 : id->len;
    fprintf(stderr, " (actual: '%.*s')", n, id->str);
    return;
  }
  if (tok->kind == TK_STRING) {
    const token_string_t *st = (const token_string_t *)tok;
    int n = st->len < 0 ? 0 : st->len;
    fprintf(stderr, " (actual: '%.*s')", n, st->str);
    return;
  }
  if (tok->kind == TK_NUM) {
    const token_num_t *num = (const token_num_t *)tok;
    int n = num->len < 0 ? 0 : num->len;
    fprintf(stderr, " (actual: '%.*s')", n, num->str);
    return;
  }
  int len = 0;
  const char *s = tk_token_kind_str(tok->kind, &len);
  if (s && len > 0) fprintf(stderr, " (actual: '%.*s')", len, s);
}

void diag_emit_atf(diag_error_id_t id, const char *input, const char *loc, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int pos = 0;
  if (input && loc && loc >= input) pos = (int)(loc - input);
  if (input) {
    fprintf(stderr, "%s\n", input);
    fprintf(stderr, "%*s", pos, "");
  }
  fprintf(stderr, "^ %s: ", diag_error_code(id));
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
  exit(1);
}

void diag_emit_tokf(diag_error_id_t id, const token_t *tok, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  if (tok && tok->file_name) fprintf(stderr, "%s:%d: ", tok->file_name, tok->line_no);
  fprintf(stderr, "%s: ", diag_error_code(id));
  vfprintf(stderr, fmt, ap);
  print_token_actual(tok);
  fprintf(stderr, "\n");
  va_end(ap);
  exit(1);
}

void diag_emit_internalf(diag_error_id_t id, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "%s: ", diag_error_code(id));
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
  exit(1);
}
