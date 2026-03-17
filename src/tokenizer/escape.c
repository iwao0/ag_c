#include "escape.h"

static inline int hex_digit_value(unsigned char ch) {
  if ('0' <= ch && ch <= '9') return (int)(ch - '0');
  if ('a' <= ch && ch <= 'f') return (int)(ch - 'a' + 10);
  if ('A' <= ch && ch <= 'F') return (int)(ch - 'A' + 10);
  return -1;
}

/** @brief バックスラッシュ開始のエスケープ列をデコードする。 */
int tk_parse_escape_value(const char *s, int len, int *i, uint32_t *out) {
  if (*i >= len || s[*i] != '\\') return 0;
  (*i)++;
  if (*i >= len) return 0;
  unsigned char esc = (unsigned char)s[*i];

  switch (esc) {
    case 'a': *out = '\a'; (*i)++; return 1;
    case 'b': *out = '\b'; (*i)++; return 1;
    case 'f': *out = '\f'; (*i)++; return 1;
    case 'n': *out = '\n'; (*i)++; return 1;
    case 'r': *out = '\r'; (*i)++; return 1;
    case 't': *out = '\t'; (*i)++; return 1;
    case 'v': *out = '\v'; (*i)++; return 1;
    case '\\': *out = '\\'; (*i)++; return 1;
    case '\'': *out = '\''; (*i)++; return 1;
    case '"': *out = '"'; (*i)++; return 1;
    case '?': *out = '?'; (*i)++; return 1;
    default:
      break;
  }

  if (esc == 'x') {
    (*i)++;
    uint32_t val = 0;
    while (*i < len) {
      int digit = hex_digit_value((unsigned char)s[*i]);
      if (digit < 0) break;
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
      int digit = hex_digit_value((unsigned char)s[*i]);
      if (digit < 0) break;
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
