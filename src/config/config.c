#include "config.h"
#include "toml_reader.h"
#include "../diag/diag.h"
#include "../parser/config_runtime.h"
#include "../tokenizer/tokenizer.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void config_reportf(diag_error_id_t id, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "%s: ", diag_error_code(id));
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
}

static void apply_config_values(const config_values_t *cfg) {
  diag_set_locale(cfg->locale);
  tk_set_strict_c11_mode(cfg->strict_c11);
  tk_set_enable_trigraphs(cfg->enable_trigraphs);
  tk_set_enable_binary_literals(cfg->enable_binary_literals);
  tk_set_enable_c11_audit_extensions(cfg->enable_c11_audit_extensions);
  ps_set_enable_size_compatible_nonscalar_cast(cfg->enable_size_compatible_nonscalar_cast);
  ps_set_enable_struct_scalar_pointer_cast(cfg->enable_struct_scalar_pointer_cast);
  ps_set_enable_union_scalar_pointer_cast(cfg->enable_union_scalar_pointer_cast);
  ps_set_enable_union_array_member_nonbrace_init(cfg->enable_union_array_member_nonbrace_init);
}

static const char *config_string_error_label(const char *locale, config_toml_error_kind_t kind) {
  int en = (locale && strcmp(locale, "en") == 0);
  switch (kind) {
    case CONFIG_TOML_ERR_STRING_MUST_BE_QUOTED: return en ? "string must be quoted" : "文字列は引用符で囲う必要があります";
    case CONFIG_TOML_ERR_STRING_TOO_LONG: return en ? "string too long" : "文字列が長すぎます";
    case CONFIG_TOML_ERR_INVALID_ESCAPE: return en ? "invalid escape" : "不正なエスケープです";
    case CONFIG_TOML_ERR_INVALID_U_ESCAPE: return en ? "invalid \\u escape" : "不正な \\u エスケープです";
    case CONFIG_TOML_ERR_INVALID_U8_ESCAPE: return en ? "invalid \\U escape" : "不正な \\U エスケープです";
    case CONFIG_TOML_ERR_UNSUPPORTED_ESCAPE: return en ? "unsupported escape" : "未対応のエスケープです";
    default: return en ? "invalid value" : "不正な値です";
  }
}

static const char *config_reason_label(const char *locale, const char *reason) {
  int en = (locale && strcmp(locale, "en") == 0);
  if (!reason || reason[0] == '\0') return config_string_error_label(locale, CONFIG_TOML_ERR_STRING_MUST_BE_QUOTED);
  if (en) return reason;
  if (strcmp(reason, "string must be quoted") == 0) return "文字列は引用符で囲う必要があります";
  if (strcmp(reason, "string too long") == 0) return "文字列が長すぎます";
  if (strcmp(reason, "invalid escape") == 0) return "不正なエスケープです";
  if (strcmp(reason, "invalid \\u escape") == 0) return "不正な \\u エスケープです";
  if (strcmp(reason, "invalid \\U escape") == 0) return "不正な \\U エスケープです";
  if (strcmp(reason, "unsupported escape") == 0) return "未対応のエスケープです";
  return "不正な値です";
}

static void format_config_toml_error(const config_toml_error_t *err, const char *locale,
                                     char *out, size_t out_cap) {
  int en = (locale && strcmp(locale, "en") == 0);
  switch (err ? err->kind : CONFIG_TOML_ERR_NONE) {
    case CONFIG_TOML_ERR_UNTERMINATED_STRING:
      snprintf(out, out_cap, en ? "line %d: unterminated string" : "%d行目: 文字列が閉じられていません", err->line_no);
      return;
    case CONFIG_TOML_ERR_DUPLICATE_KEY:
      snprintf(out, out_cap, en ? "line %d: duplicate key '%s'" : "%d行目: キー '%s' が重複しています", err->line_no, err->arg1);
      return;
    case CONFIG_TOML_ERR_MALFORMED_SECTION_HEADER:
      snprintf(out, out_cap, en ? "line %d: malformed section header" : "%d行目: セクションヘッダが不正です", err->line_no);
      return;
    case CONFIG_TOML_ERR_UNKNOWN_SECTION:
      snprintf(out, out_cap, en ? "line %d: unknown section '[%s]'" : "%d行目: 不明なセクション '[%s]' です", err->line_no, err->arg1);
      return;
    case CONFIG_TOML_ERR_KEY_VALUE_BEFORE_SECTION:
      snprintf(out, out_cap, en ? "line %d: key-value found before section header" : "%d行目: セクション宣言前に key=value が書かれています", err->line_no);
      return;
    case CONFIG_TOML_ERR_UNKNOWN_KEY:
      snprintf(out, out_cap, en ? "line %d: unknown key '%s'" : "%d行目: 不明なキー '%s' です", err->line_no, err->arg1);
      return;
    case CONFIG_TOML_ERR_INVALID_VALUE_FOR_KEY:
      snprintf(out, out_cap,
               en ? "line %d: invalid value for '%s': %s" : "%d行目: '%s' の値が不正です: %s",
               err->line_no, err->arg1,
               config_reason_label(locale, err->arg2));
      return;
    case CONFIG_TOML_ERR_UNSUPPORTED_LOCALE:
      snprintf(out, out_cap,
               en ? "line %d: unsupported locale '%s' (expected \"ja\" or \"en\")"
                  : "%d行目: 未対応のロケール '%s' です（\"ja\" または \"en\" を指定してください）",
               err->line_no, err->arg1);
      return;
    case CONFIG_TOML_ERR_BOOL_REQUIRED:
      snprintf(out, out_cap,
               en ? "line %d: '%s' must be true or false" : "%d行目: '%s' には true か false を指定してください",
               err->line_no, err->arg1);
      return;
    case CONFIG_TOML_ERR_INTERNAL_PARSER_STATE:
      snprintf(out, out_cap, en ? "line %d: internal parser state error" : "%d行目: 内部パーサ状態エラーです", err->line_no);
      return;
    case CONFIG_TOML_ERR_EXPECTED_KEY_VALUE:
      snprintf(out, out_cap, en ? "line %d: expected key = value" : "%d行目: key = value 形式で指定してください", err->line_no);
      return;
    case CONFIG_TOML_ERR_EMPTY_KEY:
      snprintf(out, out_cap, en ? "line %d: empty key" : "%d行目: キーが空です", err->line_no);
      return;
    case CONFIG_TOML_ERR_EMPTY_VALUE_FOR_KEY:
      snprintf(out, out_cap,
               en ? "line %d: empty value for key '%s'" : "%d行目: キー '%s' の値が空です",
               err->line_no, err->arg1);
      return;
    case CONFIG_TOML_ERR_STRING_MUST_BE_QUOTED:
    case CONFIG_TOML_ERR_STRING_TOO_LONG:
    case CONFIG_TOML_ERR_INVALID_ESCAPE:
    case CONFIG_TOML_ERR_INVALID_U_ESCAPE:
    case CONFIG_TOML_ERR_INVALID_U8_ESCAPE:
    case CONFIG_TOML_ERR_UNSUPPORTED_ESCAPE:
      snprintf(out, out_cap, en ? "line %d: %s" : "%d行目: %s", err->line_no,
               config_string_error_label(locale, err->kind));
      return;
    default:
      snprintf(out, out_cap, en ? "config.toml parse error" : "config.toml 解析エラー");
      return;
  }
}

void load_config_toml(const char *source_path) {
  config_values_t cfg;
  config_values_init_defaults(&cfg);
  apply_config_values(&cfg);

  config_toml_error_t err = {0};
  if (config_toml_read(source_path, &cfg, &err)) {
    apply_config_values(&cfg);
    return;
  }
  char detail[512];
  format_config_toml_error(&err, diag_get_locale(), detail, sizeof(detail));

  config_reportf(DIAG_ERR_INTERNAL_CONFIG_TOML_PARSE_FAILED,
                 diag_message_for(DIAG_ERR_INTERNAL_CONFIG_TOML_PARSE_FAILED), detail);
  config_reportf(DIAG_ERR_INTERNAL_CONFIG_TOML_FALLBACK_DEFAULTS,
                 "%s", diag_message_for(DIAG_ERR_INTERNAL_CONFIG_TOML_FALLBACK_DEFAULTS));
  config_values_init_defaults(&cfg);
  apply_config_values(&cfg);
}
