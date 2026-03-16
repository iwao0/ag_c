#include "config.h"
#include "../tokenizer/tokenizer.h"
#include <stdbool.h>
#include <stdio.h>
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

void load_config_toml(void) {
  FILE *fp = fopen("config.toml", "r");
  if (!fp) return; // 設定ファイルがなければデフォルトで実行

  bool in_tokenizer = false;
  char line[512];
  while (fgets(line, sizeof(line), fp)) {
    char *p = trim_left(line);
    if (*p == '\0' || *p == '#') continue;

    // コメントを除去
    char *hash = strchr(p, '#');
    if (hash) *hash = '\0';
    trim_right(p);
    if (*p == '\0') continue;

    if (*p == '[') {
      in_tokenizer = (strcmp(p, "[tokenizer]") == 0);
      continue;
    }
    if (!in_tokenizer) continue;

    char *eq = strchr(p, '=');
    if (!eq) continue;
    *eq = '\0';
    char *key = trim_left(p);
    trim_right(key);
    char *val = trim_left(eq + 1);
    trim_right(val);

    bool b = false;
    if (!parse_bool_value(val, &b)) continue;

    if (strcmp(key, "strict_c11") == 0) {
      tk_set_strict_c11_mode(b);
    } else if (strcmp(key, "enable_trigraphs") == 0) {
      tk_set_enable_trigraphs(b);
    } else if (strcmp(key, "enable_binary_literals") == 0) {
      tk_set_enable_binary_literals(b);
    } else if (strcmp(key, "enable_c11_audit_extensions") == 0) {
      tk_set_enable_c11_audit_extensions(b);
    }
  }
  fclose(fp);
}
