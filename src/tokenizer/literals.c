#include "literals.h"
#include "allocator.h"
#include "charclass.h"
#include "tokenizer.h"
#include <string.h>

static uint32_t hexval(char c) {
  if ('0' <= c && c <= '9') return (uint32_t)(c - '0');
  if ('a' <= c && c <= 'f') return (uint32_t)(c - 'a' + 10);
  return (uint32_t)(c - 'A' + 10);
}

bool tk_starts_with_ucn(const char *p, int *len) {
  if (p[0] != '\\' || (p[1] != 'u' && p[1] != 'U')) return false;
  int digits = (p[1] == 'u') ? 4 : 8;
  for (int i = 0; i < digits; i++) {
    if (!tk_is_xdigit(p[2 + i])) return false;
  }
  *len = 2 + digits;
  return true;
}

bool tk_parse_ucn_codepoint(const char *p, uint32_t *out, int *consumed) {
  int len = 0;
  if (!tk_starts_with_ucn(p, &len)) return false;
  int digits = (p[1] == 'u') ? 4 : 8;
  uint32_t cp = 0;
  for (int i = 0; i < digits; i++) {
    cp = (cp << 4) | hexval(p[2 + i]);
  }
  *out = cp;
  *consumed = len;
  return true;
}

bool tk_is_valid_ucn_codepoint(uint32_t cp) {
  if (cp > 0x10FFFF) return false;
  if (0xD800 <= cp && cp <= 0xDFFF) return false;
  if (cp < 0xA0 && cp != '$' && cp != '@' && cp != '`') return false;
  return true;
}

int tk_encode_utf8(uint32_t cp, char out[4]) {
  if (cp <= 0x7F) {
    out[0] = (char)cp;
    return 1;
  }
  if (cp <= 0x7FF) {
    out[0] = (char)(0xC0 | (cp >> 6));
    out[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp <= 0xFFFF) {
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = (char)(0xF0 | (cp >> 18));
  out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
  out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
  out[3] = (char)(0x80 | (cp & 0x3F));
  return 4;
}

int tk_read_escape_char(char **pp) {
  char *p = *pp;
  if (*p == 'a') { *pp = p + 1; return '\a'; }
  if (*p == 'b') { *pp = p + 1; return '\b'; }
  if (*p == 'f') { *pp = p + 1; return '\f'; }
  if (*p == 'n') { *pp = p + 1; return '\n'; }
  if (*p == 'r') { *pp = p + 1; return '\r'; }
  if (*p == 't') { *pp = p + 1; return '\t'; }
  if (*p == 'v') { *pp = p + 1; return '\v'; }
  if (*p == '\\') { *pp = p + 1; return '\\'; }
  if (*p == '\'') { *pp = p + 1; return '\''; }
  if (*p == '"') { *pp = p + 1; return '"'; }
  if (*p == '?') { *pp = p + 1; return '?'; }
  if (*p == 'x') {
    p++;
    if (!tk_is_xdigit(*p)) error_at(p, "16進エスケープが不正です");
    unsigned valx = 0;
    while (tk_is_xdigit(*p)) {
      int digit;
      if ('0' <= *p && *p <= '9') digit = *p - '0';
      else if ('a' <= *p && *p <= 'f') digit = *p - 'a' + 10;
      else digit = *p - 'A' + 10;
      valx = valx * 16 + (unsigned)digit;
      p++;
    }
    *pp = p;
    return (int)valx;
  }
  if (*p == 'u' || *p == 'U') {
    uint32_t cp = 0;
    int consumed = 0;
    if (!tk_parse_ucn_codepoint(p - 1, &cp, &consumed)) {
      error_at(p, "UCNエスケープが不正です");
    }
    if (!tk_is_valid_ucn_codepoint(cp)) {
      error_at(p, "UCNエスケープが不正です");
    }
    *pp = (char *)(p - 1 + consumed);
    return (int)cp;
  }
  if (tk_is_octal_digit(*p)) {
    int cnt = 0;
    unsigned valo = 0;
    while (cnt < 3 && tk_is_octal_digit(*p)) {
      valo = valo * 8 + (unsigned)(*p - '0');
      p++;
      cnt++;
    }
    *pp = p;
    return (int)valo;
  }
  error_at(p, "不正なエスケープです");
  *pp = p + 1;
  return (unsigned char)*p;
}

void tk_parse_string_prefix(const char *p, int *prefix_len, int *prefix_kind, int *char_width) {
  *prefix_len = 0;
  *prefix_kind = 0;
  *char_width = 1;
  if (p[0] == 'u' && p[1] == '8' && p[2] == '"') {
    *prefix_len = 2;
    *prefix_kind = 4;
    *char_width = 1;
    return;
  }
  if (p[0] == 'L' && p[1] == '"') {
    *prefix_len = 1;
    *prefix_kind = 1;
    *char_width = 4;
    return;
  }
  if (p[0] == 'u' && p[1] == '"') {
    *prefix_len = 1;
    *prefix_kind = 2;
    *char_width = 2;
    return;
  }
  if (p[0] == 'U' && p[1] == '"') {
    *prefix_len = 1;
    *prefix_kind = 3;
    *char_width = 4;
    return;
  }
}

void tk_parse_char_prefix(const char *p, int *prefix_len, int *prefix_kind, int *char_width) {
  *prefix_len = 0;
  *prefix_kind = 0;
  *char_width = 1;
  if (p[0] == 'L' && p[1] == '\'') {
    *prefix_len = 1;
    *prefix_kind = 1;
    *char_width = 4;
    return;
  }
  if (p[0] == 'u' && p[1] == '\'') {
    *prefix_len = 1;
    *prefix_kind = 2;
    *char_width = 2;
    return;
  }
  if (p[0] == 'U' && p[1] == '\'') {
    *prefix_len = 1;
    *prefix_kind = 3;
    *char_width = 4;
    return;
  }
}

void tk_decode_identifier_ucn(char *start, int len, char **out_str, int *out_len, bool *has_ucn) {
  *has_ucn = false;
  char *buf = tk_allocator_calloc((size_t)len * 4 + 1, 1);
  int bi = 0;
  for (int i = 0; i < len;) {
    uint32_t cp = 0;
    int consumed = 0;
    if (tk_parse_ucn_codepoint(start + i, &cp, &consumed)) {
      if (!tk_is_valid_ucn_codepoint(cp)) error_at(start + i, "識別子内のUCNが不正です");
      char tmp[4];
      int n = tk_encode_utf8(cp, tmp);
      memcpy(buf + bi, tmp, (size_t)n);
      bi += n;
      i += consumed;
      *has_ucn = true;
      continue;
    }
    buf[bi++] = start[i++];
  }
  buf[bi] = '\0';
  *out_str = buf;
  *out_len = bi;
}
