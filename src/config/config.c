#include "config.h"
#include "../diag/diag.h"
#include "../parser/config_runtime.h"
#include "../tokenizer/tokenizer.h"
#include <stdbool.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim_left(char *s) {
  while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
  return s;
}

static void trim_right(char *s) {
  int n = (int)strlen(s);
  while (n > 0) {
    char c = s[n - 1];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      s[n - 1] = '\0';
      n--;
      continue;
    }
    break;
  }
}

static bool parse_bool_value(const char *v, bool *out) {
  if (strcmp(v, "true") == 0) {
    *out = true;
    return true;
  }
  if (strcmp(v, "false") == 0) {
    *out = false;
    return true;
  }
  return false;
}

static void set_config_defaults(void) {
  diag_set_locale("ja");
  tk_set_strict_c11_mode(false);
  tk_set_enable_trigraphs(true);
  tk_set_enable_binary_literals(true);
  tk_set_enable_c11_audit_extensions(false);
  ps_set_enable_size_compatible_nonscalar_cast(true);
  ps_set_enable_struct_scalar_pointer_cast(true);
  ps_set_enable_union_scalar_pointer_cast(true);
  ps_set_enable_union_array_member_nonbrace_init(true);
}

static bool is_hex_digit(char c) {
  return (c >= '0' && c <= '9') ||
         (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

static bool strip_inline_comment(char *s, int *line_no, char *err, size_t err_cap) {
  bool in_basic_string = false;
  bool in_literal_string = false;
  bool escaped = false;

  for (size_t i = 0; s[i] != '\0'; i++) {
    char c = s[i];
    if (in_basic_string) {
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        in_basic_string = false;
      }
      continue;
    }
    if (in_literal_string) {
      if (c == '\'') in_literal_string = false;
      continue;
    }
    if (c == '"') {
      in_basic_string = true;
      continue;
    }
    if (c == '\'') {
      in_literal_string = true;
      continue;
    }
    if (c == '#') {
      s[i] = '\0';
      break;
    }
  }

  if (in_basic_string || in_literal_string || escaped) {
    snprintf(err, err_cap, "line %d: unterminated string", *line_no);
    return false;
  }
  return true;
}

static bool parse_string_value(char *v, char *out, size_t out_cap, char *err, size_t err_cap) {
  size_t n = strlen(v);
  if (n < 2) {
    snprintf(err, err_cap, "string must be quoted");
    return false;
  }

  if (v[0] == '\'' && v[n - 1] == '\'') {
    size_t len = n - 2;
    if (len + 1 > out_cap) {
      snprintf(err, err_cap, "string too long");
      return false;
    }
    memcpy(out, v + 1, len);
    out[len] = '\0';
    return true;
  }

  if (v[0] != '"' || v[n - 1] != '"') {
    snprintf(err, err_cap, "string must be quoted");
    return false;
  }

  size_t j = 0;
  for (size_t i = 1; i + 1 < n; i++) {
    char c = v[i];
    if (c != '\\') {
      if (j + 1 >= out_cap) {
        snprintf(err, err_cap, "string too long");
        return false;
      }
      out[j++] = c;
      continue;
    }
    if (i + 1 >= n - 1) {
      snprintf(err, err_cap, "invalid escape");
      return false;
    }
    char e = v[++i];
    switch (e) {
      case 'b': c = '\b'; break;
      case 't': c = '\t'; break;
      case 'n': c = '\n'; break;
      case 'f': c = '\f'; break;
      case 'r': c = '\r'; break;
      case '"': c = '"'; break;
      case '\\': c = '\\'; break;
      case 'u':
      case 'U':
        // Keep unicode escapes as-is (TOML parser minimal support).
        if (j + 2 >= out_cap) {
          snprintf(err, err_cap, "string too long");
          return false;
        }
        out[j++] = '\\';
        out[j++] = e;
        if (e == 'u') {
          for (int k = 0; k < 4; k++) {
            if (i + 1 >= n - 1 || !is_hex_digit(v[i + 1])) {
              snprintf(err, err_cap, "invalid \\u escape");
              return false;
            }
            out[j++] = v[++i];
          }
        } else {
          for (int k = 0; k < 8; k++) {
            if (i + 1 >= n - 1 || !is_hex_digit(v[i + 1])) {
              snprintf(err, err_cap, "invalid \\U escape");
              return false;
            }
            out[j++] = v[++i];
          }
        }
        continue;
      default:
        snprintf(err, err_cap, "unsupported escape '\\%c'", e);
        return false;
    }
    if (j + 1 >= out_cap) {
      snprintf(err, err_cap, "string too long");
      return false;
    }
    out[j++] = c;
  }
  out[j] = '\0';
  return true;
}

typedef enum config_section_t {
  SEC_NONE = 0,
  SEC_GENERAL,
  SEC_TOKENIZER,
  SEC_PARSER,
} config_section_t;

typedef struct parsed_config_t {
  char locale[16];
  bool strict_c11;
  bool enable_trigraphs;
  bool enable_binary_literals;
  bool enable_c11_audit_extensions;
  bool enable_size_compatible_nonscalar_cast;
  bool enable_struct_scalar_pointer_cast;
  bool enable_union_scalar_pointer_cast;
  bool enable_union_array_member_nonbrace_init;
} parsed_config_t;

typedef struct seen_config_t {
  bool general_error_message_locale;
  bool tokenizer_strict_c11;
  bool tokenizer_enable_trigraphs;
  bool tokenizer_enable_binary_literals;
  bool tokenizer_enable_c11_audit_extensions;
  bool parser_enable_size_compatible_nonscalar_cast;
  bool parser_enable_struct_scalar_pointer_cast;
  bool parser_enable_union_scalar_pointer_cast;
  bool parser_enable_union_array_member_nonbrace_init;
} seen_config_t;

static void parsed_config_init_defaults(parsed_config_t *cfg) {
  strcpy(cfg->locale, "ja");
  cfg->strict_c11 = false;
  cfg->enable_trigraphs = true;
  cfg->enable_binary_literals = true;
  cfg->enable_c11_audit_extensions = false;
  cfg->enable_size_compatible_nonscalar_cast = true;
  cfg->enable_struct_scalar_pointer_cast = true;
  cfg->enable_union_scalar_pointer_cast = true;
  cfg->enable_union_array_member_nonbrace_init = true;
}

static bool set_once(bool *seen, char *err, size_t err_cap, int line_no, const char *full_key) {
  if (*seen) {
    snprintf(err, err_cap, "line %d: duplicate key '%s'", line_no, full_key);
    return false;
  }
  *seen = true;
  return true;
}

static bool parse_section_header(char *p, config_section_t *sec, char *err, size_t err_cap, int line_no) {
  size_t n = strlen(p);
  if (n < 3 || p[0] != '[' || p[n - 1] != ']') {
    snprintf(err, err_cap, "line %d: malformed section header", line_no);
    return false;
  }
  p[n - 1] = '\0';
  char *name = trim_left(p + 1);
  trim_right(name);

  if (strcmp(name, "general") == 0) {
    *sec = SEC_GENERAL;
    return true;
  }
  if (strcmp(name, "tokenizer") == 0) {
    *sec = SEC_TOKENIZER;
    return true;
  }
  if (strcmp(name, "parser") == 0) {
    *sec = SEC_PARSER;
    return true;
  }
  snprintf(err, err_cap, "line %d: unknown section '[%s]'", line_no, name);
  return false;
}

static bool apply_config_key_value(config_section_t sec, char *key, char *val, int line_no,
                                   parsed_config_t *cfg, seen_config_t *seen,
                                   char *err, size_t err_cap) {
  bool b = false;
  char s[16];
  char str_err[96];

  if (sec == SEC_NONE) {
    snprintf(err, err_cap, "line %d: key-value found before section header", line_no);
    return false;
  }

  if (sec == SEC_GENERAL) {
    if (strcmp(key, "error_message_locale") != 0) {
      snprintf(err, err_cap, "line %d: unknown key 'general.%s'", line_no, key);
      return false;
    }
    if (!set_once(&seen->general_error_message_locale, err, err_cap, line_no,
                  "general.error_message_locale")) return false;
    if (!parse_string_value(val, s, sizeof(s), str_err, sizeof(str_err))) {
      snprintf(err, err_cap, "line %d: invalid value for 'general.error_message_locale': %s",
               line_no, str_err);
      return false;
    }
    if (strcmp(s, "ja") != 0 && strcmp(s, "en") != 0) {
      snprintf(err, err_cap, "line %d: unsupported locale '%s' (expected \"ja\" or \"en\")",
               line_no, s);
      return false;
    }
    strcpy(cfg->locale, s);
    return true;
  }

  if (!parse_bool_value(val, &b)) {
    snprintf(err, err_cap, "line %d: '%s' must be true or false", line_no, key);
    return false;
  }

  if (sec == SEC_TOKENIZER) {
    if (strcmp(key, "strict_c11") == 0) {
      if (!set_once(&seen->tokenizer_strict_c11, err, err_cap, line_no,
                    "tokenizer.strict_c11")) return false;
      cfg->strict_c11 = b;
      return true;
    }
    if (strcmp(key, "enable_trigraphs") == 0) {
      if (!set_once(&seen->tokenizer_enable_trigraphs, err, err_cap, line_no,
                    "tokenizer.enable_trigraphs")) return false;
      cfg->enable_trigraphs = b;
      return true;
    }
    if (strcmp(key, "enable_binary_literals") == 0) {
      if (!set_once(&seen->tokenizer_enable_binary_literals, err, err_cap, line_no,
                    "tokenizer.enable_binary_literals")) return false;
      cfg->enable_binary_literals = b;
      return true;
    }
    if (strcmp(key, "enable_c11_audit_extensions") == 0) {
      if (!set_once(&seen->tokenizer_enable_c11_audit_extensions, err, err_cap, line_no,
                    "tokenizer.enable_c11_audit_extensions")) return false;
      cfg->enable_c11_audit_extensions = b;
      return true;
    }
    snprintf(err, err_cap, "line %d: unknown key 'tokenizer.%s'", line_no, key);
    return false;
  }

  if (sec == SEC_PARSER) {
    if (strcmp(key, "enable_size_compatible_nonscalar_cast") == 0) {
      if (!set_once(&seen->parser_enable_size_compatible_nonscalar_cast, err, err_cap, line_no,
                    "parser.enable_size_compatible_nonscalar_cast")) return false;
      cfg->enable_size_compatible_nonscalar_cast = b;
      return true;
    }
    if (strcmp(key, "enable_struct_scalar_pointer_cast") == 0) {
      if (!set_once(&seen->parser_enable_struct_scalar_pointer_cast, err, err_cap, line_no,
                    "parser.enable_struct_scalar_pointer_cast")) return false;
      cfg->enable_struct_scalar_pointer_cast = b;
      return true;
    }
    if (strcmp(key, "enable_union_scalar_pointer_cast") == 0) {
      if (!set_once(&seen->parser_enable_union_scalar_pointer_cast, err, err_cap, line_no,
                    "parser.enable_union_scalar_pointer_cast")) return false;
      cfg->enable_union_scalar_pointer_cast = b;
      return true;
    }
    if (strcmp(key, "enable_union_array_member_nonbrace_init") == 0) {
      if (!set_once(&seen->parser_enable_union_array_member_nonbrace_init, err, err_cap, line_no,
                    "parser.enable_union_array_member_nonbrace_init")) return false;
      cfg->enable_union_array_member_nonbrace_init = b;
      return true;
    }
    snprintf(err, err_cap, "line %d: unknown key 'parser.%s'", line_no, key);
    return false;
  }

  snprintf(err, err_cap, "line %d: internal parser state error", line_no);
  return false;
}

static void apply_parsed_config(const parsed_config_t *cfg) {
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

void load_config_toml(void) {
  FILE *fp = fopen("config.toml", "r");
  set_config_defaults();
  if (!fp) return; // 設定ファイルがなければデフォルトで実行

  parsed_config_t cfg;
  parsed_config_init_defaults(&cfg);
  seen_config_t seen = {0};
  config_section_t sec = SEC_NONE;
  char *line = NULL;
  size_t line_cap = 0;
  int line_no = 0;
  bool ok = true;
  char err[256] = {0};

  while (ok && getline(&line, &line_cap, fp) != -1) {
    line_no++;
    char *p = trim_left(line);
    if (*p == '\0' || *p == '#') continue;

    if (!strip_inline_comment(p, &line_no, err, sizeof(err))) {
      ok = false;
      break;
    }
    trim_right(p);
    if (*p == '\0') continue;

    if (*p == '[') {
      if (!parse_section_header(p, &sec, err, sizeof(err), line_no)) {
        ok = false;
      }
      continue;
    }

    char *eq = strchr(p, '=');
    if (!eq) {
      snprintf(err, sizeof(err), "line %d: expected key = value", line_no);
      ok = false;
      break;
    }
    *eq = '\0';
    char *key = trim_left(p);
    trim_right(key);
    if (*key == '\0') {
      snprintf(err, sizeof(err), "line %d: empty key", line_no);
      ok = false;
      break;
    }
    char *val = trim_left(eq + 1);
    trim_right(val);
    if (*val == '\0') {
      snprintf(err, sizeof(err), "line %d: empty value for key '%s'", line_no, key);
      ok = false;
      break;
    }

    if (!apply_config_key_value(sec, key, val, line_no, &cfg, &seen, err, sizeof(err))) {
      ok = false;
      break;
    }
  }

  if (ok) {
    apply_parsed_config(&cfg);
  } else {
    fprintf(stderr, "config.toml parse error: %s\n", err);
    fprintf(stderr, "config.toml load aborted. fallback to defaults.\n");
    set_config_defaults();
  }

  free(line);
  fclose(fp);
}
