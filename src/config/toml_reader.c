#include "toml_reader.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(config_toml_error_t *err, config_toml_error_kind_t kind, int line_no,
                      const char *arg1, const char *arg2) {
  if (!err) return;
  err->kind = kind;
  err->line_no = line_no;
  err->arg1[0] = '\0';
  err->arg2[0] = '\0';
  if (arg1) {
    strncpy(err->arg1, arg1, sizeof(err->arg1) - 1);
    err->arg1[sizeof(err->arg1) - 1] = '\0';
  }
  if (arg2) {
    strncpy(err->arg2, arg2, sizeof(err->arg2) - 1);
    err->arg2[sizeof(err->arg2) - 1] = '\0';
  }
}

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

static FILE *open_config_file(const char *source_path) {
  if (source_path && source_path[0] != '\0') {
    const char *slash = strrchr(source_path, '/');
    if (slash) {
      size_t dir_len = (size_t)(slash - source_path);
      const char *name = "config.toml";
      size_t name_len = strlen(name);
      size_t path_len = dir_len + 1 + name_len + 1;
      char *cfg_path = (char *)malloc(path_len);
      if (cfg_path) {
        memcpy(cfg_path, source_path, dir_len);
        cfg_path[dir_len] = '/';
        memcpy(cfg_path + dir_len + 1, name, name_len + 1);
        FILE *fp = fopen(cfg_path, "r");
        free(cfg_path);
        if (fp) return fp;
      }
    }
  }
  return fopen("config.toml", "r");
}

static bool is_hex_digit(char c) {
  return (c >= '0' && c <= '9') ||
         (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

static bool strip_inline_comment(char *s, int line_no, config_toml_error_t *err) {
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
    set_error(err, CONFIG_TOML_ERR_UNTERMINATED_STRING, line_no, NULL, NULL);
    return false;
  }
  return true;
}

static bool parse_string_value(char *v, char *out, size_t out_cap, config_toml_error_t *err) {
  size_t n = strlen(v);
  if (n < 2) {
    set_error(err, CONFIG_TOML_ERR_STRING_MUST_BE_QUOTED, 0, NULL, NULL);
    return false;
  }

  if (v[0] == '\'' && v[n - 1] == '\'') {
    size_t len = n - 2;
    if (len + 1 > out_cap) {
      set_error(err, CONFIG_TOML_ERR_STRING_TOO_LONG, 0, NULL, NULL);
      return false;
    }
    memcpy(out, v + 1, len);
    out[len] = '\0';
    return true;
  }

  if (v[0] != '"' || v[n - 1] != '"') {
    set_error(err, CONFIG_TOML_ERR_STRING_MUST_BE_QUOTED, 0, NULL, NULL);
    return false;
  }

  size_t j = 0;
  for (size_t i = 1; i + 1 < n; i++) {
    char c = v[i];
    if (c != '\\') {
      if (j + 1 >= out_cap) {
        set_error(err, CONFIG_TOML_ERR_STRING_TOO_LONG, 0, NULL, NULL);
        return false;
      }
      out[j++] = c;
      continue;
    }
    if (i + 1 >= n - 1) {
      set_error(err, CONFIG_TOML_ERR_INVALID_ESCAPE, 0, NULL, NULL);
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
      case 'U': {
        size_t needed = (e == 'u') ? 6 : 10;
        if (j + needed >= out_cap) {
          set_error(err, CONFIG_TOML_ERR_STRING_TOO_LONG, 0, NULL, NULL);
          return false;
        }
        out[j++] = '\\';
        out[j++] = e;
        if (e == 'u') {
          for (int k = 0; k < 4; k++) {
            if (i + 1 >= n - 1 || !is_hex_digit(v[i + 1])) {
              set_error(err, CONFIG_TOML_ERR_INVALID_U_ESCAPE, 0, NULL, NULL);
              return false;
            }
            out[j++] = v[++i];
          }
        } else {
          for (int k = 0; k < 8; k++) {
            if (i + 1 >= n - 1 || !is_hex_digit(v[i + 1])) {
              set_error(err, CONFIG_TOML_ERR_INVALID_U8_ESCAPE, 0, NULL, NULL);
              return false;
            }
            out[j++] = v[++i];
          }
        }
        continue;
      }
      default:
        {
          char esc[4] = {'\\', e, '\0', '\0'};
          set_error(err, CONFIG_TOML_ERR_UNSUPPORTED_ESCAPE, 0, esc, NULL);
        }
        return false;
    }
    if (j + 1 >= out_cap) {
      set_error(err, CONFIG_TOML_ERR_STRING_TOO_LONG, 0, NULL, NULL);
      return false;
    }
    out[j++] = c;
  }
  out[j] = '\0';
  return true;
}

static const char *string_error_reason(config_toml_error_kind_t kind) {
  switch (kind) {
    case CONFIG_TOML_ERR_STRING_MUST_BE_QUOTED: return "string must be quoted";
    case CONFIG_TOML_ERR_STRING_TOO_LONG: return "string too long";
    case CONFIG_TOML_ERR_INVALID_ESCAPE: return "invalid escape";
    case CONFIG_TOML_ERR_INVALID_U_ESCAPE: return "invalid \\u escape";
    case CONFIG_TOML_ERR_INVALID_U8_ESCAPE: return "invalid \\U escape";
    case CONFIG_TOML_ERR_UNSUPPORTED_ESCAPE: return "unsupported escape";
    default: return "invalid value";
  }
}

typedef enum config_section_t {
  SEC_NONE = 0,
  SEC_GENERAL,
  SEC_TOKENIZER,
  SEC_PARSER,
} config_section_t;

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

void config_values_init_defaults(config_values_t *cfg) {
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

static bool set_once(bool *seen, config_toml_error_t *err, int line_no, const char *full_key) {
  if (*seen) {
    set_error(err, CONFIG_TOML_ERR_DUPLICATE_KEY, line_no, full_key, NULL);
    return false;
  }
  *seen = true;
  return true;
}

static bool parse_section_header(char *p, config_section_t *sec, config_toml_error_t *err, int line_no) {
  size_t n = strlen(p);
  if (n < 3 || p[0] != '[' || p[n - 1] != ']') {
    set_error(err, CONFIG_TOML_ERR_MALFORMED_SECTION_HEADER, line_no, NULL, NULL);
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
  set_error(err, CONFIG_TOML_ERR_UNKNOWN_SECTION, line_no, name, NULL);
  return false;
}

static bool parse_key_value(config_section_t sec, char *key, char *val, int line_no,
                            config_values_t *cfg, seen_config_t *seen,
                            config_toml_error_t *err) {
  bool b = false;
  char s[16];
  config_toml_error_t str_err = {0};

  if (sec == SEC_NONE) {
    set_error(err, CONFIG_TOML_ERR_KEY_VALUE_BEFORE_SECTION, line_no, NULL, NULL);
    return false;
  }

  if (sec == SEC_GENERAL) {
    if (strcmp(key, "error_message_locale") != 0) {
      char full_key[128];
      snprintf(full_key, sizeof(full_key), "general.%s", key);
      set_error(err, CONFIG_TOML_ERR_UNKNOWN_KEY, line_no, full_key, NULL);
      return false;
    }
    if (!set_once(&seen->general_error_message_locale, err, line_no,
                  "general.error_message_locale")) return false;
    if (!parse_string_value(val, s, sizeof(s), &str_err)) {
      set_error(err, CONFIG_TOML_ERR_INVALID_VALUE_FOR_KEY, line_no,
                "general.error_message_locale", string_error_reason(str_err.kind));
      return false;
    }
    if (strcmp(s, "ja") != 0 && strcmp(s, "en") != 0) {
      set_error(err, CONFIG_TOML_ERR_UNSUPPORTED_LOCALE, line_no, s, NULL);
      return false;
    }
    strcpy(cfg->locale, s);
    return true;
  }

  if (!parse_bool_value(val, &b)) {
    set_error(err, CONFIG_TOML_ERR_BOOL_REQUIRED, line_no, key, NULL);
    return false;
  }

  if (sec == SEC_TOKENIZER) {
    if (strcmp(key, "strict_c11") == 0) {
      if (!set_once(&seen->tokenizer_strict_c11, err, line_no,
                    "tokenizer.strict_c11")) return false;
      cfg->strict_c11 = b;
      return true;
    }
    if (strcmp(key, "enable_trigraphs") == 0) {
      if (!set_once(&seen->tokenizer_enable_trigraphs, err, line_no,
                    "tokenizer.enable_trigraphs")) return false;
      cfg->enable_trigraphs = b;
      return true;
    }
    if (strcmp(key, "enable_binary_literals") == 0) {
      if (!set_once(&seen->tokenizer_enable_binary_literals, err, line_no,
                    "tokenizer.enable_binary_literals")) return false;
      cfg->enable_binary_literals = b;
      return true;
    }
    if (strcmp(key, "enable_c11_audit_extensions") == 0) {
      if (!set_once(&seen->tokenizer_enable_c11_audit_extensions, err, line_no,
                    "tokenizer.enable_c11_audit_extensions")) return false;
      cfg->enable_c11_audit_extensions = b;
      return true;
    }
    {
      char full_key[128];
      snprintf(full_key, sizeof(full_key), "tokenizer.%s", key);
      set_error(err, CONFIG_TOML_ERR_UNKNOWN_KEY, line_no, full_key, NULL);
    }
    return false;
  }

  if (sec == SEC_PARSER) {
    if (strcmp(key, "enable_size_compatible_nonscalar_cast") == 0) {
      if (!set_once(&seen->parser_enable_size_compatible_nonscalar_cast, err, line_no,
                    "parser.enable_size_compatible_nonscalar_cast")) return false;
      cfg->enable_size_compatible_nonscalar_cast = b;
      return true;
    }
    if (strcmp(key, "enable_struct_scalar_pointer_cast") == 0) {
      if (!set_once(&seen->parser_enable_struct_scalar_pointer_cast, err, line_no,
                    "parser.enable_struct_scalar_pointer_cast")) return false;
      cfg->enable_struct_scalar_pointer_cast = b;
      return true;
    }
    if (strcmp(key, "enable_union_scalar_pointer_cast") == 0) {
      if (!set_once(&seen->parser_enable_union_scalar_pointer_cast, err, line_no,
                    "parser.enable_union_scalar_pointer_cast")) return false;
      cfg->enable_union_scalar_pointer_cast = b;
      return true;
    }
    if (strcmp(key, "enable_union_array_member_nonbrace_init") == 0) {
      if (!set_once(&seen->parser_enable_union_array_member_nonbrace_init, err, line_no,
                    "parser.enable_union_array_member_nonbrace_init")) return false;
      cfg->enable_union_array_member_nonbrace_init = b;
      return true;
    }
    {
      char full_key[128];
      snprintf(full_key, sizeof(full_key), "parser.%s", key);
      set_error(err, CONFIG_TOML_ERR_UNKNOWN_KEY, line_no, full_key, NULL);
    }
    return false;
  }

  set_error(err, CONFIG_TOML_ERR_INTERNAL_PARSER_STATE, line_no, NULL, NULL);
  return false;
}

bool config_toml_read(const char *source_path, config_values_t *cfg, config_toml_error_t *err) {
  FILE *fp = open_config_file(source_path);
  if (!fp) return true;
  if (err) {
    err->kind = CONFIG_TOML_ERR_NONE;
    err->line_no = 0;
    err->arg1[0] = '\0';
    err->arg2[0] = '\0';
  }

  config_values_init_defaults(cfg);
  seen_config_t seen = {0};
  config_section_t sec = SEC_NONE;
  char *line = NULL;
  size_t line_cap = 0;
  int line_no = 0;
  bool ok = true;

  while (ok && getline(&line, &line_cap, fp) != -1) {
    line_no++;
    char *p = trim_left(line);
    if (*p == '\0' || *p == '#') continue;

    if (!strip_inline_comment(p, line_no, err)) {
      ok = false;
      break;
    }
    trim_right(p);
    if (*p == '\0') continue;

    if (*p == '[') {
      if (!parse_section_header(p, &sec, err, line_no)) ok = false;
      continue;
    }

    char *eq = strchr(p, '=');
    if (!eq) {
      set_error(err, CONFIG_TOML_ERR_EXPECTED_KEY_VALUE, line_no, NULL, NULL);
      ok = false;
      break;
    }
    *eq = '\0';
    char *key = trim_left(p);
    trim_right(key);
    if (*key == '\0') {
      set_error(err, CONFIG_TOML_ERR_EMPTY_KEY, line_no, NULL, NULL);
      ok = false;
      break;
    }
    char *val = trim_left(eq + 1);
    trim_right(val);
    if (*val == '\0') {
      set_error(err, CONFIG_TOML_ERR_EMPTY_VALUE_FOR_KEY, line_no, key, NULL);
      ok = false;
      break;
    }

    if (!parse_key_value(sec, key, val, line_no, cfg, &seen, err)) {
      ok = false;
      break;
    }
  }

  free(line);
  fclose(fp);
  return ok;
}
