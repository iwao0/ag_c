#include "internal/literals.h"
#include "internal/allocator.h"
#include "internal/charclass.h"
#include "internal/escape.h"
#include "tokenizer.h"
#include <string.h>

static uint32_t hexval(char c) {
  if ('0' <= c && c <= '9') return (uint32_t)(c - '0');
  if ('a' <= c && c <= 'f') return (uint32_t)(c - 'a' + 10);
  return (uint32_t)(c - 'A' + 10);
}

/** @brief `\u` / `\U` 形式のUCN開始かを判定する。 */
bool tk_starts_with_ucn(const char *p, int *len) {
  if (p[0] != '\\' || (p[1] != 'u' && p[1] != 'U')) return false;
  int digits = (p[1] == 'u') ? 4 : 8;
  for (int i = 0; i < digits; i++) {
    if (!tk_is_xdigit(p[2 + i])) return false;
  }
  *len = 2 + digits;
  return true;
}

/** @brief UCNをコードポイント値へ変換する。 */
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

/** @brief C11で許可されるUCNかを検証する。 */
bool tk_is_valid_ucn_codepoint(uint32_t cp) {
  if (cp > 0x10FFFF) return false;
  if (0xD800 <= cp && cp <= 0xDFFF) return false;
  if (cp < 0xA0 && cp != '$' && cp != '@' && cp != '`') return false;
  return true;
}

/** @brief コードポイントをUTF-8バイト列へエンコードする。 */
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

/** @brief リテラル中のエスケープ1個を値にデコードする。 */
int tk_read_escape_char(char **pp) {
  char *p = *pp;
  if (*p == 'x' && !tk_is_xdigit(p[1])) {
    tk_error_at_id(DIAG_ERR_TOKENIZER_INVALID_ESCAPE, p + 1, "16進エスケープが不正です");
  }
  if (*p == 'u' || *p == 'U') {
    uint32_t cp = 0;
    int consumed = 0;
    if (!tk_parse_ucn_codepoint(p - 1, &cp, &consumed)) {
      tk_error_at_id(DIAG_ERR_TOKENIZER_INVALID_ESCAPE, p, "UCNエスケープが不正です");
    }
    if (!tk_is_valid_ucn_codepoint(cp)) {
      tk_error_at_id(DIAG_ERR_TOKENIZER_INVALID_ESCAPE, p, "UCNエスケープが不正です");
    }
  }
  switch (*p) {
    case 'a': case 'b': case 'f': case 'n': case 'r': case 't': case 'v':
    case '\\': case '\'': case '"': case '?':
    case 'x': case 'u': case 'U':
    case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
      break;
    default:
      tk_error_at_id(DIAG_ERR_TOKENIZER_INVALID_ESCAPE, p, "不正なエスケープです");
  }

  char *bs = p - 1;
  int idx = 0;
  uint32_t out = 0;
  int len = (int)strlen(bs);
  if (!tk_parse_escape_value(bs, len, &idx, &out)) {
    tk_error_at_id(DIAG_ERR_TOKENIZER_INVALID_ESCAPE, p, "不正なエスケープです");
  }
  *pp = bs + idx;
  return (int)out;
}

/** @brief リテラル中のエスケープ1個を値化せず読み飛ばす。 */
void tk_skip_escape_in_literal(char **pp) {
  char *p = *pp;
  if (*p == 'x' && !tk_is_xdigit(p[1])) {
    tk_error_at_id(DIAG_ERR_TOKENIZER_INVALID_ESCAPE, p + 1, "16進エスケープが不正です");
  }
  if (*p == 'u' || *p == 'U') {
    uint32_t cp = 0;
    int consumed = 0;
    if (!tk_parse_ucn_codepoint(p - 1, &cp, &consumed)) {
      tk_error_at_id(DIAG_ERR_TOKENIZER_INVALID_ESCAPE, p, "UCNエスケープが不正です");
    }
    if (!tk_is_valid_ucn_codepoint(cp)) {
      tk_error_at_id(DIAG_ERR_TOKENIZER_INVALID_ESCAPE, p, "UCNエスケープが不正です");
    }
  }
  switch (*p) {
    case 'a': case 'b': case 'f': case 'n': case 'r': case 't': case 'v':
    case '\\': case '\'': case '"': case '?':
    case 'x': case 'u': case 'U':
    case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
      break;
    default:
      tk_error_at_id(DIAG_ERR_TOKENIZER_INVALID_ESCAPE, p, "不正なエスケープです");
  }

  if (*p == 'x') {
    p++;
    while (tk_is_xdigit(*p)) p++;
    *pp = p;
    return;
  }
  if (*p == 'u' || *p == 'U') {
    int consumed = (*p == 'u') ? 5 : 9; // uXXXX / UXXXXXXXX from current char
    *pp = p + consumed;
    return;
  }
  if (tk_is_octal_digit(*p)) {
    int cnt = 0;
    while (cnt < 3 && tk_is_octal_digit(*p)) {
      p++;
      cnt++;
    }
    *pp = p;
    return;
  }
  *pp = p + 1;
}

/** @brief 文字列接頭辞（L/u/U/u8）を解析する。 */
void tk_parse_string_prefix(
    const char *p,
    int *prefix_len,
    tk_string_prefix_kind_t *prefix_kind,
    tk_char_width_t *char_width) {
  *prefix_len = 0;
  *prefix_kind = TK_STR_PREFIX_NONE;
  *char_width = TK_CHAR_WIDTH_CHAR;
  if (p[0] == 'u' && p[1] == '8' && p[2] == '"') {
    *prefix_len = 2;
    *prefix_kind = TK_STR_PREFIX_u8;
    *char_width = TK_CHAR_WIDTH_CHAR;
    return;
  }
  if (p[0] == 'L' && p[1] == '"') {
    *prefix_len = 1;
    *prefix_kind = TK_STR_PREFIX_L;
    *char_width = TK_CHAR_WIDTH_CHAR32;
    return;
  }
  if (p[0] == 'u' && p[1] == '"') {
    *prefix_len = 1;
    *prefix_kind = TK_STR_PREFIX_u;
    *char_width = TK_CHAR_WIDTH_CHAR16;
    return;
  }
  if (p[0] == 'U' && p[1] == '"') {
    *prefix_len = 1;
    *prefix_kind = TK_STR_PREFIX_U;
    *char_width = TK_CHAR_WIDTH_CHAR32;
    return;
  }
}

/** @brief 文字定数接頭辞（L/u/U）を解析する。 */
void tk_parse_char_prefix(
    const char *p,
    int *prefix_len,
    tk_char_prefix_kind_t *prefix_kind,
    tk_char_width_t *char_width) {
  *prefix_len = 0;
  *prefix_kind = TK_CHAR_PREFIX_NONE;
  *char_width = TK_CHAR_WIDTH_CHAR;
  if (p[0] == 'L' && p[1] == '\'') {
    *prefix_len = 1;
    *prefix_kind = TK_CHAR_PREFIX_L;
    *char_width = TK_CHAR_WIDTH_CHAR32;
    return;
  }
  if (p[0] == 'u' && p[1] == '\'') {
    *prefix_len = 1;
    *prefix_kind = TK_CHAR_PREFIX_u;
    *char_width = TK_CHAR_WIDTH_CHAR16;
    return;
  }
  if (p[0] == 'U' && p[1] == '\'') {
    *prefix_len = 1;
    *prefix_kind = TK_CHAR_PREFIX_U;
    *char_width = TK_CHAR_WIDTH_CHAR32;
    return;
  }
}

/** @brief 識別子中のUCNをUTF-8へ展開する。 */
void tk_decode_identifier_ucn(char *start, int len, char **out_str, int *out_len, bool *has_ucn) {
  *has_ucn = false;
  char *buf = tk_allocator_calloc((size_t)len * 4 + 1, 1);
  int bi = 0;
  for (int i = 0; i < len;) {
    uint32_t cp = 0;
    int consumed = 0;
    if (tk_parse_ucn_codepoint(start + i, &cp, &consumed)) {
      if (!tk_is_valid_ucn_codepoint(cp))
        tk_error_at_id(DIAG_ERR_TOKENIZER_INVALID_ESCAPE, start + i, "識別子内のUCNが不正です");
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
