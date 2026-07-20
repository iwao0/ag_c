#include "diag.h"
#include "locale_config.h"
#include "messages.h"
#include "ui_texts.h"
#include "../tokenizer/tokenizer.h"
#include "../source_manager.h"
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

struct ag_diagnostic_context_t {
  ag_source_manager_t *source_manager;
  char locale[3];
  agc_diag_record_t initial_records[AGC_DIAG_DEFAULT_RECORD_LIMIT];
  agc_diag_record_t *records;
  int record_count;
  int error_count;
  int record_cap;
  int record_limit;
  size_t byte_limit;
  size_t bytes;
  int limit_kind;
  int limits_enforced;
};

static ag_diagnostic_context_t *diag_prepare_context(
  ag_diagnostic_context_t *context) {
  if (!context) abort();
  if (!context->records) {
    context->records = context->initial_records;
    context->record_cap = AGC_DIAG_DEFAULT_RECORD_LIMIT;
  }
  return context;
}

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

static int diag_reserve_record(ag_diagnostic_context_t *context) {
  context = diag_prepare_context(context);
  if (context->record_count < context->record_cap) return 1;
  int next_cap = context->record_cap ? context->record_cap * 2 : 16;
  if (next_cap > context->record_limit) next_cap = context->record_limit;
  if (next_cap <= context->record_cap) return 0;
  agc_diag_record_t *next;
  if (context->records == context->initial_records) {
    next = malloc((size_t)next_cap * sizeof(*next));
    if (next)
      memcpy(next, context->initial_records,
             sizeof(context->initial_records));
  } else {
    next = realloc(context->records, (size_t)next_cap * sizeof(*next));
  }
  if (!next) return 0;
  context->records = next;
  context->record_cap = next_cap;
  return 1;
}

static int diag_store_v(
                        ag_diagnostic_context_t *context,
                        int severity, const char *code, const char *source_name,
                        int start_line, int start_column, int start_offset,
                        int end_line, int end_column, int end_offset,
                        const char *fmt, va_list ap) {
  context = diag_prepare_context(context);
  if (context->limits_enforced && context->limit_kind) return 0;
  if (context->record_count >= context->record_limit) {
    if (context->limits_enforced) context->limit_kind = 1;
    return 0;
  }
  if (!code) code = "";
  if (!source_name) source_name = "";
  char message[AGC_DIAG_MESSAGE_CAP];
  vsnprintf(message, sizeof(message), fmt, ap);
  size_t text_bytes = strlen(code) + strlen(source_name) + strlen(message);
  if (context->limits_enforced &&
      (context->bytes > context->byte_limit ||
       text_bytes > context->byte_limit - context->bytes)) {
    if (!context->limit_kind) context->limit_kind = 2;
    return 0;
  }
  if (!diag_reserve_record(context)) return 0;
  char *source_copy = diag_dup_text(source_name);
  if (!source_copy) {
    free(source_copy);
    return 0;
  }
  agc_diag_record_t *record =
      &context->records[context->record_count++];
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
  context->bytes += text_bytes;
  if (severity == AGC_DIAG_SEVERITY_ERROR) context->error_count++;
  return 1;
}

static int diag_store_at_v(
                           ag_diagnostic_context_t *context,
                           int severity, const char *code, const char *input,
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
  const ag_source_manager_t *source_manager =
      context ? context->source_manager : NULL;
  return diag_store_v(
      context, severity, code,
      ag_source_manager_current_name(source_manager),
      start_line, start_column, start_offset,
      end_line, end_column, end_offset, fmt, ap);
}

static int diag_store_tok_v(
                            ag_diagnostic_context_t *context,
                            int severity, const char *code, const token_t *tok,
                            const char *fmt, va_list ap) {
  const ag_source_manager_t *source_manager =
      context ? context->source_manager : NULL;
  const char *input = tok && tok->source_input
                          ? tok->source_input
                          : ag_source_manager_current_input(source_manager);
  int start_offset = tok ? tok->byte_offset : -1;
  int byte_length = tok && tok->byte_length > 0 ? tok->byte_length : 0;
  int end_offset = start_offset < 0 ? -1 : start_offset + byte_length;
  int start_line = tok ? tok->line_no : 0;
  int start_column = diag_byte_column(input, start_offset);
  int end_line = start_line;
  int end_column = start_column ? start_column + byte_length : 0;
  const char *source_name = ag_source_manager_name(
      source_manager, tok ? tok->file_name_id : 0);
  return diag_store_v(context, severity, code, source_name,
                      start_line, start_column, start_offset,
                      end_line, end_column, end_offset, fmt, ap);
}

void diag_reset_records_in(ag_diagnostic_context_t *context) {
  context = diag_prepare_context(context);
  for (int i = 0; i < context->record_count; i++) {
    free(context->records[i].source_name);
    context->records[i].source_name = NULL;
  }
  context->record_count = 0;
  context->error_count = 0;
  context->bytes = 0;
  context->limit_kind = 0;
}

ag_diagnostic_context_t *diag_context_create(
    ag_source_manager_t *source_manager) {
  if (!source_manager) return NULL;
  ag_diagnostic_context_t *context = calloc(1, sizeof(*context));
  if (!context) return NULL;
  context->source_manager = source_manager;
  memcpy(context->locale, "ja", sizeof(context->locale));
  context->records = context->initial_records;
  context->record_cap = AGC_DIAG_DEFAULT_RECORD_LIMIT;
  context->record_limit = AGC_DIAG_DEFAULT_RECORD_LIMIT;
  context->byte_limit = AGC_DIAG_DEFAULT_BYTE_LIMIT;
  return context;
}

ag_source_manager_t *diag_context_source_manager(
    const ag_diagnostic_context_t *context) {
  return context ? context->source_manager : NULL;
}

static void diag_context_release_records(ag_diagnostic_context_t *context) {
  if (!context) return;
  if (!context->records) {
    context->records = context->initial_records;
    context->record_cap = AGC_DIAG_DEFAULT_RECORD_LIMIT;
    return;
  }
  for (int i = 0; i < context->record_count; i++) {
    free(context->records[i].source_name);
    context->records[i].source_name = NULL;
  }
  if (context->records != context->initial_records) free(context->records);
  context->records = context->initial_records;
  context->record_count = 0;
  context->error_count = 0;
  context->record_cap = AGC_DIAG_DEFAULT_RECORD_LIMIT;
  context->bytes = 0;
  context->limit_kind = 0;
}

void diag_context_destroy(ag_diagnostic_context_t *context) {
  if (!context) return;
  diag_context_release_records(context);
  free(context);
}

int diag_context_set_limits(
    ag_diagnostic_context_t *context, int max_records, int max_bytes) {
  if (!context) return -1;
  if (max_records <= 0 || max_bytes <= 0) return -1;
  context->record_limit = max_records;
  context->byte_limit = (size_t)max_bytes;
  context->limits_enforced = 1;
  return 0;
}

static const agc_diag_record_t *diag_record_at(
    const ag_diagnostic_context_t *context, int index) {
  if (!context || index < 0 || index >= context->record_count) return NULL;
  return &context->records[index];
}

int agc_wasm_diagnostic_api_version(void) { return 2; }
int diag_context_record_count(const ag_diagnostic_context_t *context) {
  return context ? context->record_count : 0;
}
int diag_context_record_bytes(const ag_diagnostic_context_t *context) {
  return context ? (int)context->bytes : 0;
}
int diag_context_record_limit_kind(const ag_diagnostic_context_t *context) {
  return context ? context->limit_kind : 0;
}
int diag_has_error_records_in(const ag_diagnostic_context_t *context) {
  return context && context->error_count > 0;
}

int diag_limit_kind_in(const ag_diagnostic_context_t *context) {
  return context ? context->limit_kind : 0;
}
int diag_context_record_severity(
    const ag_diagnostic_context_t *context, int index) {
  const agc_diag_record_t *record = diag_record_at(context, index);
  return record ? record->severity : 0;
}
const char *diag_context_record_code(
    const ag_diagnostic_context_t *context, int index) {
  const agc_diag_record_t *record = diag_record_at(context, index);
  return record ? record->code : NULL;
}
const char *diag_context_record_message(
    const ag_diagnostic_context_t *context, int index) {
  const agc_diag_record_t *record = diag_record_at(context, index);
  return record ? record->message : NULL;
}
const char *diag_context_record_source_name(
    const ag_diagnostic_context_t *context, int index) {
  const agc_diag_record_t *record = diag_record_at(context, index);
  return record ? record->source_name : NULL;
}
#define AGC_DIAG_INT_GETTER(name, field)                         \
  int name(const ag_diagnostic_context_t *context, int index) { \
    const agc_diag_record_t *record = diag_record_at(context, index); \
    return record ? record->field : 0;                  \
  }
AGC_DIAG_INT_GETTER(diag_context_record_start_line, start_line)
AGC_DIAG_INT_GETTER(diag_context_record_start_column, start_column)
AGC_DIAG_INT_GETTER(diag_context_record_start_offset, start_offset)
AGC_DIAG_INT_GETTER(diag_context_record_end_line, end_line)
AGC_DIAG_INT_GETTER(diag_context_record_end_column, end_column)
AGC_DIAG_INT_GETTER(diag_context_record_end_offset, end_offset)
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

#if defined(AGC_DIAG_LOCALE_ALL)
static int diag_locale_is_en(
    const ag_diagnostic_context_t *context) {
  return strcmp(diag_context_get_locale(context), "en") == 0;
}

#define DIAG_LOOKUP_LOCALE(CONTEXT, OUT, JA_EXPR, EN_EXPR) \
  do {                                             \
    if (diag_locale_is_en((CONTEXT))) {            \
      (OUT) = (EN_EXPR);                           \
      if (!(OUT)) (OUT) = (JA_EXPR);               \
    } else {                                       \
      (OUT) = (JA_EXPR);                           \
      if (!(OUT)) (OUT) = (EN_EXPR);               \
    }                                              \
  } while (0)
#elif defined(AGC_DIAG_LOCALE_EN_ONLY)
#define DIAG_LOOKUP_LOCALE(CONTEXT, OUT, JA_EXPR, EN_EXPR) \
  do {                                             \
    (void)(CONTEXT);                               \
    (void)0;                                       \
    (OUT) = (EN_EXPR);                             \
  } while (0)
#else
#define DIAG_LOOKUP_LOCALE(CONTEXT, OUT, JA_EXPR, EN_EXPR) \
  do {                                             \
    (void)(CONTEXT);                               \
    (void)0;                                       \
    (OUT) = (JA_EXPR);                             \
  } while (0)
#endif

/**
 * @brief 診断メッセージのロケールを設定する。
 * @param locale ロケール名（例: "ja", "en"）。
 */
void diag_context_set_locale(
    ag_diagnostic_context_t *context, const char *locale) {
  if (!context) return;
  if (!locale || locale[0] == '\0') return;
  if (strcmp(locale, "ja") != 0 && strcmp(locale, "en") != 0) return;
  memcpy(context->locale, locale, sizeof(context->locale));
}

const char *diag_context_get_locale(
    const ag_diagnostic_context_t *context) {
  return context && context->locale[0] ? context->locale : "ja";
}

/**
 * @brief エラーIDに対応するメッセージを現在ロケールに従って取得する。
 * @param id エラーID。
 * @return ローカライズ済みメッセージ。未定義時はエラーキー。
 */
const char *diag_message_for_in(
    const ag_diagnostic_context_t *context, diag_error_id_t id) {
  const char *msg = NULL;
  DIAG_LOOKUP_LOCALE(
      context, msg, diag_message_ja(id), diag_message_en(id));
  if (msg) return msg;
  return diag_error_key(id);
}

const char *diag_warn_message_for_in(
    const ag_diagnostic_context_t *context, diag_warn_id_t id) {
  const char *msg = NULL;
  DIAG_LOOKUP_LOCALE(
      context, msg, diag_warn_message_ja(id), diag_warn_message_en(id));
  if (msg) return msg;
  return diag_warn_key(id);
}

/**
 * @brief テキストIDに対応するテキストを現在ロケールに従って取得する。
 * @param id テキストID。
 * @return ローカライズ済みテキスト。未定義時は "unknown.text"。
 */
const char *diag_text_for_in(
    const ag_diagnostic_context_t *context, diag_text_id_t id) {
  const char *msg = NULL;
  DIAG_LOOKUP_LOCALE(context, msg, diag_text_ja(id), diag_text_en(id));
  if (msg) return msg;
  return diag_ui_text_for(
      DIAG_UI_TEXT_UNKNOWN_TEXT, diag_context_get_locale(context));
}

/**
 * @brief トークンの実際の値を補助表示する。
 * @param tok 表示対象トークン。
 * @return なし。
 */
static void print_token_actual(
    const ag_diagnostic_context_t *context, const token_t *tok) {
  if (!tok) return;
  const char *locale = diag_context_get_locale(context);
  const char *label = diag_ui_text_for(
      DIAG_UI_TEXT_ACTUAL_TOKEN_LABEL, locale);
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
    fprintf(stderr,
            diag_ui_text_for(DIAG_UI_TEXT_ACTUAL_TOKEN_KIND_FMT, locale),
            (int)tok->kind);
}

/**
 * @brief 入力位置ベースの診断を出力して終了する。
 * @param id エラーID。
 * @param input 入力全体文字列。
 * @param loc エラー位置。
 * @param fmt 追加メッセージのフォーマット文字列。
 * @return 戻らない。
 */
static const char *diag_token_filename(
    const ag_diagnostic_context_t *context, const token_t *tok) {
  return ag_source_manager_name(
      context ? context->source_manager : NULL,
      tok ? tok->file_name_id : 0);
}

static _Noreturn void diag_emit_at_va(
    ag_diagnostic_context_t *context, diag_error_id_t id,
    const char *input, const char *loc, const char *fmt,
    va_list ap);

static _Noreturn void diag_emit_at_va(
    ag_diagnostic_context_t *context, diag_error_id_t id,
    const char *input, const char *loc, const char *fmt, va_list ap) {
  context = diag_prepare_context(context);
  va_list record_ap;
  va_copy(record_ap, ap);
  diag_store_at_v(
      context, AGC_DIAG_SEVERITY_ERROR, diag_error_code(id),
      input, loc, fmt, record_ap);
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
  exit(1);
}

_Noreturn void diag_emit_atf_in(
    ag_diagnostic_context_t *context, diag_error_id_t id,
    const char *input, const char *loc, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  diag_emit_at_va(context, id, input, loc, fmt, ap);
}

/**
 * @brief トークンベースの診断を出力して終了する。
 * @param id エラーID。
 * @param tok エラー位置を示すトークン。
 * @param fmt 追加メッセージのフォーマット文字列。
 * @return 戻らない。
 */
static _Noreturn void diag_emit_tok_va(
    ag_diagnostic_context_t *context, diag_error_id_t id,
    const token_t *tok, const char *fmt,
    va_list ap);

static _Noreturn void diag_emit_tok_va(
    ag_diagnostic_context_t *context, diag_error_id_t id,
    const token_t *tok, const char *fmt, va_list ap) {
  context = diag_prepare_context(context);
  va_list record_ap;
  va_copy(record_ap, ap);
  diag_store_tok_v(
      context, AGC_DIAG_SEVERITY_ERROR, diag_error_code(id),
      tok, fmt, record_ap);
  va_end(record_ap);
  { const char *fn = diag_token_filename(context, tok);
    if (tok && fn) fprintf(stderr, "%s:%d: ", fn, tok->line_no); }
  fprintf(stderr, "%s: ", diag_error_code(id));
  diag_vfprint_escaped(stderr, fmt, ap);
  print_token_actual(context, tok);
  fprintf(stderr, "\n");
  exit(1);
}

_Noreturn void diag_emit_tokf_in(
    ag_diagnostic_context_t *context, diag_error_id_t id,
    const token_t *tok, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  diag_emit_tok_va(context, id, tok, fmt, ap);
}

static int diag_report_at_va(
    ag_diagnostic_context_t *context, diag_error_id_t id,
    const char *input, const char *loc, const char *fmt, va_list ap) {
  context = diag_prepare_context(context);
  va_list record_ap;
  va_copy(record_ap, ap);
  int stored = diag_store_at_v(
      context, AGC_DIAG_SEVERITY_ERROR, diag_error_code(id),
      input, loc, fmt, record_ap);
  va_end(record_ap);
  if (!stored && context->limits_enforced) return 0;
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
  return stored;
}

int diag_report_atf_in(
    ag_diagnostic_context_t *context, diag_error_id_t id,
    const char *input, const char *loc, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int result = diag_report_at_va(context, id, input, loc, fmt, ap);
  va_end(ap);
  return result;
}

static int diag_report_tok_va(
    ag_diagnostic_context_t *context, diag_error_id_t id,
    const token_t *tok, const char *fmt, va_list ap) {
  context = diag_prepare_context(context);
  va_list record_ap;
  va_copy(record_ap, ap);
  int stored = diag_store_tok_v(
      context, AGC_DIAG_SEVERITY_ERROR, diag_error_code(id),
      tok, fmt, record_ap);
  va_end(record_ap);
  if (!stored && context->limits_enforced) return 0;
  const char *fn = diag_token_filename(context, tok);
  if (tok && fn) fprintf(stderr, "%s:%d: ", fn, tok->line_no);
  fprintf(stderr, "%s: ", diag_error_code(id));
  diag_vfprint_escaped(stderr, fmt, ap);
  print_token_actual(context, tok);
  fprintf(stderr, "\n");
  return stored;
}

int diag_report_tokf_in(
    ag_diagnostic_context_t *context, diag_error_id_t id,
    const token_t *tok, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int result = diag_report_tok_va(context, id, tok, fmt, ap);
  va_end(ap);
  return result;
}

static void diag_warn_tok_va(
    ag_diagnostic_context_t *context, diag_warn_id_t id,
    const token_t *tok, const char *fmt, va_list ap) {
  const char *suppress = getenv("AGC_SUPPRESS_WARNINGS");
  if (suppress && suppress[0] && strcmp(suppress, "0") != 0) return;
  context = diag_prepare_context(context);
  va_list record_ap;
  va_copy(record_ap, ap);
  int stored = diag_store_tok_v(
      context, AGC_DIAG_SEVERITY_WARNING, diag_warn_code(id),
      tok, fmt, record_ap);
  va_end(record_ap);
  if (!stored && context->limits_enforced) return;
  { const char *fn = diag_token_filename(context, tok);
    if (tok && fn) fprintf(stderr, "%s:%d: ", fn, tok->line_no); }
  fprintf(stderr, "%s: %s: ", diag_warn_code(id),
          diag_text_for_in(context, DIAG_TEXT_WARNING));
  diag_vfprint_escaped(stderr, fmt, ap);
  fprintf(stderr, "\n");
}

void diag_warn_tokf_in(
    ag_diagnostic_context_t *context, diag_warn_id_t id,
    const token_t *tok, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  diag_warn_tok_va(context, id, tok, fmt, ap);
  va_end(ap);
}

/**
 * @brief 内部診断を出力して終了する。
 * @param id エラーID。
 * @param fmt 追加メッセージのフォーマット文字列。
 * @return 戻らない。
 */
static _Noreturn void diag_emit_internal_va(
    ag_diagnostic_context_t *context, diag_error_id_t id,
    const char *fmt, va_list ap);

static _Noreturn void diag_emit_internal_va(
    ag_diagnostic_context_t *context, diag_error_id_t id,
    const char *fmt, va_list ap) {
  context = diag_prepare_context(context);
  va_list record_ap;
  va_copy(record_ap, ap);
  diag_store_v(context, AGC_DIAG_SEVERITY_ERROR, diag_error_code(id), NULL,
               0, 0, -1, 0, 0, -1, fmt, record_ap);
  va_end(record_ap);
  fprintf(stderr, "%s: ", diag_error_code(id));
  diag_vfprint_escaped(stderr, fmt, ap);
  fprintf(stderr, "\n");
  exit(1);
}

_Noreturn void diag_emit_internalf_in(
    ag_diagnostic_context_t *context, diag_error_id_t id,
    const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  diag_emit_internal_va(context, id, fmt, ap);
}

static void diag_report_internal_va(
    ag_diagnostic_context_t *context, diag_error_id_t id,
    const char *fmt, va_list ap) {
  context = diag_prepare_context(context);
  va_list record_ap;
  va_copy(record_ap, ap);
  int stored = diag_store_v(
                            context, AGC_DIAG_SEVERITY_ERROR,
                            diag_error_code(id), NULL,
                            0, 0, -1, 0, 0, -1, fmt, record_ap);
  va_end(record_ap);
  if (!stored && context->limits_enforced) return;
  fprintf(stderr, "%s: ", diag_error_code(id));
  diag_vfprint_escaped(stderr, fmt, ap);
  fprintf(stderr, "\n");
}

void diag_report_internalf_in(
    ag_diagnostic_context_t *context, diag_error_id_t id,
    const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  diag_report_internal_va(context, id, fmt, ap);
  va_end(ap);
}
