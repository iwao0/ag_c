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

enum {
  AGC_DIAG_SEVERITY_ERROR = 1,
  AGC_DIAG_SEVERITY_WARNING = 2,
  AGC_DIAG_CODE_CAP = 8,
  AGC_DIAG_MESSAGE_CAP = 2048,
  AGC_DIAG_DEFAULT_RECORD_LIMIT = 128,
  AGC_DIAG_DEFAULT_BYTE_LIMIT = 1024 * 1024,
};

typedef struct {
  int severity;
  int start_line;
  int start_column;
  int start_offset;
  int end_line;
  int end_column;
  int end_offset;
  char code[AGC_DIAG_CODE_CAP];
  char *source_name;
  char message[AGC_DIAG_MESSAGE_CAP];
} agc_diag_record_t;

static agc_diag_record_t g_diag_initial_records[AGC_DIAG_DEFAULT_RECORD_LIMIT];
static agc_diag_record_t *g_diag_records = g_diag_initial_records;
static int g_diag_record_count;
static int g_diag_error_count;
static int g_diag_record_cap = AGC_DIAG_DEFAULT_RECORD_LIMIT;
static int g_diag_record_limit = AGC_DIAG_DEFAULT_RECORD_LIMIT;
static size_t g_diag_byte_limit = AGC_DIAG_DEFAULT_BYTE_LIMIT;
static size_t g_diag_bytes;
static int g_diag_limit_kind;
static int g_diag_limits_enforced;

static void diag_copy_text(char *dst, size_t cap, const char *src) {
  if (!dst || cap == 0) return;
  if (!src) src = "";
  snprintf(dst, cap, "%s", src);
}

static char *diag_dup_text(const char *src) {
  if (!src) src = "";
  size_t len = strlen(src);
  char *copy = malloc(len + 1);
  if (!copy) return NULL;
  memcpy(copy, src, len + 1);
  return copy;
}

static int diag_byte_column(const char *input, int offset) {
  if (!input || offset < 0) return 0;
  int line_start = offset;
  while (line_start > 0 && input[line_start - 1] != '\n') line_start--;
  return offset - line_start + 1;
}

static void diag_line_column(const char *input, int offset, int *out_line, int *out_column) {
  int line = 1;
  int column = 1;
  if (!input || offset < 0) {
    line = 0;
    column = 0;
  } else {
    for (int i = 0; i < offset && input[i]; i++) {
      if (input[i] == '\n') {
        line++;
        column = 1;
      } else {
        column++;
      }
    }
  }
  if (out_line) *out_line = line;
  if (out_column) *out_column = column;
}

static int diag_reserve_record(void) {
  if (g_diag_record_count < g_diag_record_cap) return 1;
  int next_cap = g_diag_record_cap ? g_diag_record_cap * 2 : 16;
  if (next_cap > g_diag_record_limit) next_cap = g_diag_record_limit;
  if (next_cap <= g_diag_record_cap) return 0;
  agc_diag_record_t *next;
  if (g_diag_records == g_diag_initial_records) {
    next = malloc((size_t)next_cap * sizeof(*next));
    if (next) memcpy(next, g_diag_initial_records, sizeof(g_diag_initial_records));
  } else {
    next = realloc(g_diag_records, (size_t)next_cap * sizeof(*next));
  }
  if (!next) return 0;
  g_diag_records = next;
  g_diag_record_cap = next_cap;
  return 1;
}

static int diag_store_v(int severity, const char *code, const char *source_name,
                        int start_line, int start_column, int start_offset,
                        int end_line, int end_column, int end_offset,
                        const char *fmt, va_list ap) {
  if (g_diag_limits_enforced && g_diag_limit_kind) return 0;
  if (g_diag_record_count >= g_diag_record_limit) {
    if (g_diag_limits_enforced) g_diag_limit_kind = 1;
    return 0;
  }
  if (!code) code = "";
  if (!source_name) source_name = "";
  char message[AGC_DIAG_MESSAGE_CAP];
  vsnprintf(message, sizeof(message), fmt, ap);
  size_t text_bytes = strlen(code) + strlen(source_name) + strlen(message);
  if (g_diag_limits_enforced &&
      (g_diag_bytes > g_diag_byte_limit || text_bytes > g_diag_byte_limit - g_diag_bytes)) {
    if (!g_diag_limit_kind) g_diag_limit_kind = 2;
    return 0;
  }
  if (!diag_reserve_record()) return 0;
  char *source_copy = diag_dup_text(source_name);
  if (!source_copy) {
    free(source_copy);
    return 0;
  }
  agc_diag_record_t *record = &g_diag_records[g_diag_record_count++];
  memset(record, 0, sizeof(*record));
  record->severity = severity;
  record->start_line = start_line;
  record->start_column = start_column;
  record->start_offset = start_offset;
  record->end_line = end_line;
  record->end_column = end_column;
  record->end_offset = end_offset;
  diag_copy_text(record->code, sizeof(record->code), code);
  record->source_name = source_copy;
  diag_copy_text(record->message, sizeof(record->message), message);
  g_diag_bytes += text_bytes;
  if (severity == AGC_DIAG_SEVERITY_ERROR) g_diag_error_count++;
  return 1;
}

static int diag_store_at_v(int severity, const char *code, const char *input,
                           const char *loc, const char *fmt, va_list ap) {
  int start_offset = -1;
  if (input && loc && loc >= input) start_offset = (int)(loc - input);
  int end_offset = start_offset;
  if (start_offset >= 0 && input[start_offset]) end_offset++;
  int start_line = 0;
  int start_column = 0;
  int end_line = 0;
  int end_column = 0;
  diag_line_column(input, start_offset, &start_line, &start_column);
  diag_line_column(input, end_offset, &end_line, &end_column);
  return diag_store_v(severity, code, tk_get_filename_ctx(NULL),
                      start_line, start_column, start_offset,
                      end_line, end_column, end_offset, fmt, ap);
}

static int diag_store_tok_v(int severity, const char *code, const token_t *tok,
                            const char *fmt, va_list ap) {
  const char *input = tok && tok->source_input ? tok->source_input : tk_get_user_input_ctx(NULL);
  int start_offset = tok ? tok->byte_offset : -1;
  int byte_length = tok && tok->byte_length > 0 ? tok->byte_length : 0;
  int end_offset = start_offset < 0 ? -1 : start_offset + byte_length;
  int start_line = tok ? tok->line_no : 0;
  int start_column = diag_byte_column(input, start_offset);
  int end_line = start_line;
  int end_column = start_column ? start_column + byte_length : 0;
  const char *source_name = tk_filename_lookup(tok ? tok->file_name_id : 0);
  return diag_store_v(severity, code, source_name,
                      start_line, start_column, start_offset,
                      end_line, end_column, end_offset, fmt, ap);
}

void diag_reset_records(void) {
  for (int i = 0; i < g_diag_record_count; i++) {
    free(g_diag_records[i].source_name);
    g_diag_records[i].source_name = NULL;
  }
  g_diag_record_count = 0;
  g_diag_error_count = 0;
  g_diag_bytes = 0;
  g_diag_limit_kind = 0;
}

int agc_wasm_diagnostic_set_limits(int max_records, int max_bytes) {
  if (max_records <= 0 || max_bytes <= 0) return -1;
  g_diag_record_limit = max_records;
  g_diag_byte_limit = (size_t)max_bytes;
  g_diag_limits_enforced = 1;
  return 0;
}

static const agc_diag_record_t *diag_record_at(int index) {
  if (index < 0 || index >= g_diag_record_count) return NULL;
  return &g_diag_records[index];
}

int agc_wasm_diagnostic_api_version(void) { return 1; }
int agc_wasm_diagnostic_count(void) { return g_diag_record_count; }
int agc_wasm_diagnostic_bytes(void) { return (int)g_diag_bytes; }
int agc_wasm_diagnostic_limit_kind(void) { return g_diag_limit_kind; }
int diag_has_error_records(void) { return g_diag_error_count > 0; }
int agc_wasm_diagnostic_severity(int index) {
  const agc_diag_record_t *record = diag_record_at(index);
  return record ? record->severity : 0;
}
int agc_wasm_diagnostic_code_ptr(int index) {
  const agc_diag_record_t *record = diag_record_at(index);
  return record ? (int)(long)record->code : 0;
}
int agc_wasm_diagnostic_message_ptr(int index) {
  const agc_diag_record_t *record = diag_record_at(index);
  return record ? (int)(long)record->message : 0;
}
int agc_wasm_diagnostic_source_name_ptr(int index) {
  const agc_diag_record_t *record = diag_record_at(index);
  return record && record->source_name ? (int)(long)record->source_name : 0;
}
#define AGC_DIAG_INT_GETTER(name, field)                 \
  int name(int index) {                                 \
    const agc_diag_record_t *record = diag_record_at(index); \
    return record ? record->field : 0;                  \
  }
AGC_DIAG_INT_GETTER(agc_wasm_diagnostic_start_line, start_line)
AGC_DIAG_INT_GETTER(agc_wasm_diagnostic_start_column, start_column)
AGC_DIAG_INT_GETTER(agc_wasm_diagnostic_start_offset, start_offset)
AGC_DIAG_INT_GETTER(agc_wasm_diagnostic_end_line, end_line)
AGC_DIAG_INT_GETTER(agc_wasm_diagnostic_end_column, end_column)
AGC_DIAG_INT_GETTER(agc_wasm_diagnostic_end_offset, end_offset)
#undef AGC_DIAG_INT_GETTER

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
  va_list record_ap;
  va_copy(record_ap, ap);
  diag_store_at_v(AGC_DIAG_SEVERITY_ERROR, diag_error_code(id), input, loc, fmt, record_ap);
  va_end(record_ap);
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
  va_list record_ap;
  va_copy(record_ap, ap);
  diag_store_tok_v(AGC_DIAG_SEVERITY_ERROR, diag_error_code(id), tok, fmt, record_ap);
  va_end(record_ap);
  { const char *fn = tk_filename_lookup(tok ? tok->file_name_id : 0);
    if (tok && fn) fprintf(stderr, "%s:%d: ", fn, tok->line_no); }
  fprintf(stderr, "%s: ", diag_error_code(id));
  diag_vfprint_escaped(stderr, fmt, ap);
  print_token_actual(tok);
  fprintf(stderr, "\n");
  va_end(ap);
  exit(1);
}

int diag_report_atf(diag_error_id_t id, const char *input, const char *loc,
                    const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  va_list record_ap;
  va_copy(record_ap, ap);
  int stored = diag_store_at_v(
      AGC_DIAG_SEVERITY_ERROR, diag_error_code(id), input, loc, fmt, record_ap);
  va_end(record_ap);
  if (!stored && g_diag_limits_enforced) {
    va_end(ap);
    return 0;
  }
  int pos = 0;
  if (input && loc && loc >= input) pos = (int)(loc - input);
  if (input) {
    size_t width = 0;
    for (int i = 0; i < pos; i++)
      width += (size_t)escaped_display_width_for_byte((unsigned char)input[i]);
    diag_fprint_escaped(stderr, input);
    fprintf(stderr, "\n%*s", (int)width, "");
  }
  fprintf(stderr, "^ %s: ", diag_error_code(id));
  diag_vfprint_escaped(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
  return stored;
}

int diag_report_tokf(diag_error_id_t id, const token_t *tok,
                     const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  va_list record_ap;
  va_copy(record_ap, ap);
  int stored = diag_store_tok_v(
      AGC_DIAG_SEVERITY_ERROR, diag_error_code(id), tok, fmt, record_ap);
  va_end(record_ap);
  if (!stored && g_diag_limits_enforced) {
    va_end(ap);
    return 0;
  }
  const char *fn = tk_filename_lookup(tok ? tok->file_name_id : 0);
  if (tok && fn) fprintf(stderr, "%s:%d: ", fn, tok->line_no);
  fprintf(stderr, "%s: ", diag_error_code(id));
  diag_vfprint_escaped(stderr, fmt, ap);
  print_token_actual(tok);
  fprintf(stderr, "\n");
  va_end(ap);
  return stored;
}

void diag_warn_tokf(diag_warn_id_t id, const token_t *tok, const char *fmt, ...) {
  const char *suppress = getenv("AGC_SUPPRESS_WARNINGS");
  if (suppress && suppress[0] && strcmp(suppress, "0") != 0) return;
  va_list ap;
  va_start(ap, fmt);
  va_list record_ap;
  va_copy(record_ap, ap);
  int stored = diag_store_tok_v(AGC_DIAG_SEVERITY_WARNING, diag_warn_code(id), tok, fmt, record_ap);
  va_end(record_ap);
  if (!stored && g_diag_limits_enforced) {
    va_end(ap);
    return;
  }
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
  va_list record_ap;
  va_copy(record_ap, ap);
  diag_store_v(AGC_DIAG_SEVERITY_ERROR, diag_error_code(id), NULL,
               0, 0, -1, 0, 0, -1, fmt, record_ap);
  va_end(record_ap);
  fprintf(stderr, "%s: ", diag_error_code(id));
  diag_vfprint_escaped(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
  exit(1);
}

void diag_report_internalf(diag_error_id_t id, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  va_list record_ap;
  va_copy(record_ap, ap);
  int stored = diag_store_v(AGC_DIAG_SEVERITY_ERROR, diag_error_code(id), NULL,
                            0, 0, -1, 0, 0, -1, fmt, record_ap);
  va_end(record_ap);
  if (!stored && g_diag_limits_enforced) {
    va_end(ap);
    return;
  }
  fprintf(stderr, "%s: ", diag_error_code(id));
  diag_vfprint_escaped(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
}
