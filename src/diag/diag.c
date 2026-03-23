#include "diag.h"
#include "messages.h"
#include "ui_texts.h"
#include "../tokenizer/tokenizer.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *g_diag_locale = "ja";

#if defined(DIAG_LANG_ALL)
static int diag_locale_is_en(void) {
  return strcmp(g_diag_locale, "en") == 0;
}

#define DIAG_LOOKUP_LOCALE(OUT, JA_EXPR, EN_EXPR) \
  do {                                             \
    if (diag_locale_is_en()) {                     \
      (OUT) = (EN_EXPR);                           \
      if (!(OUT)) (OUT) = (JA_EXPR);               \
    } else {                                       \
      (OUT) = (JA_EXPR);                           \
      if (!(OUT)) (OUT) = (EN_EXPR);               \
    }                                              \
  } while (0)
#elif defined(DIAG_LANG_EN)
#define DIAG_LOOKUP_LOCALE(OUT, JA_EXPR, EN_EXPR) \
  do {                                             \
    (void)0;                                       \
    (OUT) = (EN_EXPR);                             \
  } while (0)
#else
#define DIAG_LOOKUP_LOCALE(OUT, JA_EXPR, EN_EXPR) \
  do {                                             \
    (void)0;                                       \
    (OUT) = (JA_EXPR);                             \
  } while (0)
#endif

/**
 * @brief 診断メッセージのロケールを設定する。
 * @param locale ロケール名（例: "ja", "en"）。
 */
void diag_set_locale(const char *locale) {
  if (!locale || locale[0] == '\0') return;
  if (strcmp(locale, "ja") != 0 && strcmp(locale, "en") != 0) return;
  g_diag_locale = locale;
}

/**
 * @brief 現在の診断ロケールを取得する。
 * @return 現在有効なロケール名。
 */
const char *diag_get_locale(void) {
  return g_diag_locale;
}

/**
 * @brief エラーIDに対応するメッセージを現在ロケールに従って取得する。
 * @param id エラーID。
 * @return ローカライズ済みメッセージ。未定義時はエラーキー。
 */
const char *diag_message_for(diag_error_id_t id) {
  const char *msg = NULL;
  DIAG_LOOKUP_LOCALE(msg, diag_message_ja(id), diag_message_en(id));
  if (msg) return msg;
  return diag_error_key(id);
}

const char *diag_warn_message_for(diag_warn_id_t id) {
  const char *msg = NULL;
  DIAG_LOOKUP_LOCALE(msg, diag_warn_message_ja(id), diag_warn_message_en(id));
  if (msg) return msg;
  return diag_warn_key(id);
}

/**
 * @brief テキストIDに対応するテキストを現在ロケールに従って取得する。
 * @param id テキストID。
 * @return ローカライズ済みテキスト。未定義時は "unknown.text"。
 */
const char *diag_text_for(diag_text_id_t id) {
  const char *msg = NULL;
  DIAG_LOOKUP_LOCALE(msg, diag_text_ja(id), diag_text_en(id));
  if (msg) return msg;
  return diag_ui_text_for(DIAG_UI_TEXT_UNKNOWN_TEXT, g_diag_locale);
}

/**
 * @brief トークンの実際の値を補助表示する。
 * @param tok 表示対象トークン。
 * @return なし。
 */
static void print_token_actual(const token_t *tok) {
  if (!tok) return;
  const char *label = diag_ui_text_for(DIAG_UI_TEXT_ACTUAL_TOKEN_LABEL, g_diag_locale);
  if (tok->kind == TK_IDENT) {
    const token_ident_t *id = (const token_ident_t *)tok;
    int n = id->len < 0 ? 0 : id->len;
    fprintf(stderr, " (%s: '%.*s')", label, n, id->str);
    return;
  }
  if (tok->kind == TK_STRING) {
    const token_string_t *st = (const token_string_t *)tok;
    int n = st->len < 0 ? 0 : st->len;
    fprintf(stderr, " (%s: '%.*s')", label, n, st->str);
    return;
  }
  if (tok->kind == TK_NUM) {
    const token_num_t *num = (const token_num_t *)tok;
    int n = num->len < 0 ? 0 : num->len;
    fprintf(stderr, " (%s: '%.*s')", label, n, num->str);
    return;
  }
  const char *name = tk_token_kind_str(tok->kind, NULL);
  if (name)
    fprintf(stderr, " (%s: '%s')", label, name);
  else
    fprintf(stderr, diag_ui_text_for(DIAG_UI_TEXT_ACTUAL_TOKEN_KIND_FMT, g_diag_locale), (int)tok->kind);
}

/**
 * @brief 入力位置ベースの診断を出力して終了する。
 * @param id エラーID。
 * @param input 入力全体文字列。
 * @param loc エラー位置。
 * @param fmt 追加メッセージのフォーマット文字列。
 * @return 戻らない。
 */
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

/**
 * @brief トークンベースの診断を出力して終了する。
 * @param id エラーID。
 * @param tok エラー位置を示すトークン。
 * @param fmt 追加メッセージのフォーマット文字列。
 * @return 戻らない。
 */
void diag_emit_tokf(diag_error_id_t id, const token_t *tok, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  { char *fn = tk_filename_lookup(tok ? tok->file_name_id : 0);
    if (tok && fn) fprintf(stderr, "%s:%d: ", fn, tok->line_no); }
  fprintf(stderr, "%s: ", diag_error_code(id));
  vfprintf(stderr, fmt, ap);
  print_token_actual(tok);
  fprintf(stderr, "\n");
  va_end(ap);
  exit(1);
}

void diag_warn_tokf(diag_warn_id_t id, const token_t *tok, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  { char *fn = tk_filename_lookup(tok ? tok->file_name_id : 0);
    if (tok && fn) fprintf(stderr, "%s:%d: ", fn, tok->line_no); }
  fprintf(stderr, "%s: %s: ", diag_warn_code(id), diag_text_for(DIAG_TEXT_WARNING));
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
}

/**
 * @brief 内部診断を出力して終了する。
 * @param id エラーID。
 * @param fmt 追加メッセージのフォーマット文字列。
 * @return 戻らない。
 */
void diag_emit_internalf(diag_error_id_t id, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "%s: ", diag_error_code(id));
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
  exit(1);
}

void diag_report_internalf(diag_error_id_t id, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "%s: ", diag_error_code(id));
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
}
