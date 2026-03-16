#include "escape.h"
#include <ctype.h>

int tk_parse_escape_value(const char *s, int len, int *i, uint32_t *out) {
  if (*i >= len || s[*i] != '\\') return 0;
  (*i)++;
  if (*i >= len) return 0;
  char esc = s[*i];
  if (esc == 'a') { *out = '\a'; (*i)++; return 1; }
  if (esc == 'b') { *out = '\b'; (*i)++; return 1; }
  if (esc == 'f') { *out = '\f'; (*i)++; return 1; }
  if (esc == 'n') { *out = '\n'; (*i)++; return 1; }
  if (esc == 'r') { *out = '\r'; (*i)++; return 1; }
  if (esc == 't') { *out = '\t'; (*i)++; return 1; }
  if (esc == 'v') { *out = '\v'; (*i)++; return 1; }
  if (esc == '\\') { *out = '\\'; (*i)++; return 1; }
  if (esc == '\'') { *out = '\''; (*i)++; return 1; }
  if (esc == '"') { *out = '"'; (*i)++; return 1; }
  if (esc == '?') { *out = '?'; (*i)++; return 1; }
  if (esc == 'x') {
    (*i)++;
    uint32_t val = 0;
    while (*i < len && isxdigit((unsigned char)s[*i])) {
      char ch = s[*i];
      int digit;
      if ('0' <= ch && ch <= '9') digit = ch - '0';
      else if ('a' <= ch && ch <= 'f') digit = ch - 'a' + 10;
      else digit = ch - 'A' + 10;
      val = (val << 4) | (uint32_t)digit;
      (*i)++;
    }
    *out = val;
    return 1;
  }
  if (esc == 'u' || esc == 'U') {
    int digits = (esc == 'u') ? 4 : 8;
    (*i)++;
    uint32_t val = 0;
    for (int k = 0; k < digits && *i < len; k++, (*i)++) {
      char ch = s[*i];
      int digit;
      if ('0' <= ch && ch <= '9') digit = ch - '0';
      else if ('a' <= ch && ch <= 'f') digit = ch - 'a' + 10;
      else if ('A' <= ch && ch <= 'F') digit = ch - 'A' + 10;
      else break;
      val = (val << 4) | (uint32_t)digit;
    }
    *out = val;
    return 1;
  }
  if ('0' <= esc && esc <= '7') {
    uint32_t val = 0;
    int cnt = 0;
    while (*i < len && cnt < 3) {
      char ch = s[*i];
      if (ch < '0' || ch > '7') break;
      val = val * 8 + (uint32_t)(ch - '0');
      (*i)++;
      cnt++;
    }
    *out = val;
    return 1;
  }
  *out = (unsigned char)esc;
  (*i)++;
  return 1;
}
