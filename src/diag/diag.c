#include "diag.h"
#include "messages.h"
#include "ui_texts.h"
#include "../tokenizer/tokenizer.h"
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *g_diag_locale = "ja";

static int is_bidi_control_codepoint(uint32_t cp) {
  if (cp >= 0x202A && cp <= 0x202E) return 1;
  if (cp >= 0x2066 && cp <= 0x2069) return 1;
  if (cp == 0x200E || cp == 0x200F || cp == 0x061C) return 1;
  return 0;
}

static int utf8_decode_one(const unsigned char *s, size_t rem, uint32_t *out_cp, size_t *out_len) {
  if (!s || rem == 0 || !out_cp || !out_len) return 0;
  unsigned char b0 = s[0];
  if (b0 < 0x80) {
    *out_cp = (uint32_t)b0;
    *out_len = 1;
    return 1;
  }
  if (b0 >= 0xC2 && b0 <= 0xDF) {
    if (rem < 2) return 0;
    unsigned char b1 = s[1];
    if ((b1 & 0xC0) != 0x80) return 0;
    *out_cp = ((uint32_t)(b0 & 0x1F) << 6) | (uint32_t)(b1 & 0x3F);
    *out_len = 2;
    return 1;
  }
  if (b0 >= 0xE0 && b0 <= 0xEF) {
    if (rem < 3) return 0;
    unsigned char b1 = s[1], b2 = s[2];
    if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) return 0;
    if (b0 == 0xE0 && b1 < 0xA0) return 0;
    if (b0 == 0xED && b1 >= 0xA0) return 0;
    *out_cp = ((uint32_t)(b0 & 0x0F) << 12) | ((uint32_t)(b1 & 0x3F) << 6) | (uint32_t)(b2 & 0x3F);
    *out_len = 3;
    return 1;
  }
  if (b0 >= 0xF0 && b0 <= 0xF4) {
    if (rem < 4) return 0;
    unsigned char b1 = s[1], b2 = s[2], b3 = s[3];
    if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) return 0;
    if (b0 == 0xF0 && b1 < 0x90) return 0;
    if (b0 == 0xF4 && b1 >= 0x90) return 0;
    *out_cp = ((uint32_t)(b0 & 0x07) << 18) | ((uint32_t)(b1 & 0x3F) << 12) |
              ((uint32_t)(b2 & 0x3F) << 6) | (uint32_t)(b3 & 0x3F);
    *out_len = 4;
    return 1;
  }
  return 0;
}

static int escaped_display_width_for_byte(unsigned char c) {
  if (c == '\n' || c == '\r' || c == '\t') return 2;
  if (c < 0x20 || c == 0x7F) return 4;
  return 1;
}

static size_t diag_fprint_escaped_n(FILE *out, const char *s, size_t n) {
  if (!out || !s) return 0;
  size_t i = 0;
  size_t written = 0;
  while (i < n) {
    const unsigned char *p = (const unsigned char *)(s + i);
    unsigned char c = p[0];
    if (c == '\n') {
      fputs("\\n", out);
      written += 2;
      i++;
      continue;
    }
    if (c == '\r') {
      fputs("\\r", out);
      written += 2;
      i++;
      continue;
    }
    if (c == '\t') {
      fputs("\\t", out);
      written += 2;
      i++;
      continue;
    }
    if (c < 0x20 || c == 0x7F) {
      fprintf(out, "\\x%02X", (unsigned)c);
      written += 4;
      i++;
      continue;
    }
    uint32_t cp = 0;
    size_t adv = 0;
    if (utf8_decode_one(p, n - i, &cp, &adv)) {
      if ((cp <= 0x1F) || (cp >= 0x7F && cp <= 0x9F) || is_bidi_control_codepoint(cp)) {
        if (cp <= 0xFFFF) {
          fprintf(out, "\\u%04X", (unsigned)cp);
          written += 6;
        } else {
          fprintf(out, "\\U%08X", (unsigned)cp);
          written += 10;
        }
      } else {
        fwrite(p, 1, adv, out);
        written += adv;
      }
      i += adv;
      continue;
    }
    fprintf(out, "\\x%02X", (unsigned)c);
    written += 4;
    i++;
  }
  return written;
}

static size_t diag_fprint_escaped(FILE *out, const char *s) {
  if (!s) return 0;
  return diag_fprint_escaped_n(out, s, strlen(s));
}

static void diag_vfprint_escaped(FILE *out, const char *fmt, va_list ap) {
  va_list cp;
  va_copy(cp, ap);
  int needed = vsnprintf(NULL, 0, fmt, cp);
  va_end(cp);
  if (needed < 0) {
    fputs("<format-error>", out);
    return;
  }
  size_t cap = (size_t)needed + 1;
  char *tmp = (char *)malloc(cap);
  if (!tmp) {
    fputs("<oom>", out);
    return;
  }
  vsnprintf(tmp, cap, fmt, ap);
  diag_fprint_escaped(out, tmp);
  free(tmp);
}

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
    fprintf(stderr, " (%s: '", label);
    diag_fprint_escaped_n(stderr, id->str, (size_t)n);
    fprintf(stderr, "')");
    return;
  }
  if (tok->kind == TK_STRING) {
    const token_string_t *st = (const token_string_t *)tok;
    int n = st->len < 0 ? 0 : st->len;
    fprintf(stderr, " (%s: '", label);
    diag_fprint_escaped_n(stderr, st->str, (size_t)n);
    fprintf(stderr, "')");
    return;
  }
  if (tok->kind == TK_NUM) {
    const token_num_t *num = (const token_num_t *)tok;
    int n = num->len < 0 ? 0 : num->len;
    fprintf(stderr, " (%s: '", label);
    diag_fprint_escaped_n(stderr, num->str, (size_t)n);
    fprintf(stderr, "')");
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
    size_t width = 0;
    if (pos > 0) {
      for (int i = 0; i < pos; i++) {
        width += (size_t)escaped_display_width_for_byte((unsigned char)input[i]);
      }
    }
    diag_fprint_escaped(stderr, input);
    fprintf(stderr, "\n%*s", (int)width, "");
  }
  fprintf(stderr, "^ %s: ", diag_error_code(id));
  diag_vfprint_escaped(stderr, fmt, ap);
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
  { const char *fn = tk_filename_lookup(tok ? tok->file_name_id : 0);
    if (tok && fn) fprintf(stderr, "%s:%d: ", fn, tok->line_no); }
  fprintf(stderr, "%s: ", diag_error_code(id));
  diag_vfprint_escaped(stderr, fmt, ap);
  print_token_actual(tok);
  fprintf(stderr, "\n");
  va_end(ap);
  exit(1);
}

void diag_warn_tokf(diag_warn_id_t id, const token_t *tok, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  { const char *fn = tk_filename_lookup(tok ? tok->file_name_id : 0);
    if (tok && fn) fprintf(stderr, "%s:%d: ", fn, tok->line_no); }
  fprintf(stderr, "%s: %s: ", diag_warn_code(id), diag_text_for(DIAG_TEXT_WARNING));
  diag_vfprint_escaped(stderr, fmt, ap);
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
  diag_vfprint_escaped(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
  exit(1);
}

void diag_report_internalf(diag_error_id_t id, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "%s: ", diag_error_code(id));
  diag_vfprint_escaped(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
}
