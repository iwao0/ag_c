#include "literals.h"
#include "allocator.h"
#include "charclass.h"
#include "diag_helper.h"
#include "escape.h"
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
  // Reject bidi controls that can visually reorder source and diagnostics.
  if (cp == 0x200E || cp == 0x200F || cp == 0x061C) return false;
  if (0x202A <= cp && cp <= 0x202E) return false;
  if (0x2066 <= cp && cp <= 0x2069) return false;
  // Reject zero-width join controls to prevent visually confusable identifiers.
  if (cp == 0x200C || cp == 0x200D) return false;
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

/** @brief s[*pos] から UTF-8 シーケンス 1 個をデコードしてコードポイントを返す。
 * *pos を消費バイト数だけ進める。不正/不完全シーケンスは 1 バイトをそのまま値として
 * 返す (寛容)。wide/UTF-16/32 文字列リテラルのコードユニット化に使う。 */
uint32_t tk_decode_utf8(const char *s, int len, int *pos) {
  int i = *pos;
  unsigned char c0 = (unsigned char)s[i];
  if (c0 < 0x80) { *pos = i + 1; return c0; }
  int n;          /* 後続バイト数 */
  uint32_t cp;
  if ((c0 & 0xE0) == 0xC0) { n = 1; cp = c0 & 0x1F; }
  else if ((c0 & 0xF0) == 0xE0) { n = 2; cp = c0 & 0x0F; }
  else if ((c0 & 0xF8) == 0xF0) { n = 3; cp = c0 & 0x07; }
  else { *pos = i + 1; return c0; }  /* 不正な先頭バイト */
  if (i + n >= len + 0) { /* 続きが足りるか後で確認 */ }
  for (int k = 1; k <= n; k++) {
    if (i + k >= len || ((unsigned char)s[i + k] & 0xC0) != 0x80) {
      *pos = i + 1; return c0;  /* 不完全/不正: 1 バイトとして扱う */
    }
    cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
  }
  *pos = i + n + 1;
  return cp;
}

/** @brief 文字列リテラルの「次の 1 文字」を、ターゲット幅 (char_width) のコードユニット列に
 * 変換する。s[*pos] からエスケープ / UTF-8 / ASCII を 1 つ消費して *pos を進め、生成した
 * コードユニットを out[0..] に書き、その個数 (1 または 2) を返す。
 *   - char_width 1 (char/u8): 1 バイト = 1 ユニット (UTF-8 バイト列をそのまま)。
 *   - char_width 2 (u, UTF-16): コードポイントへデコードし、BMP は 1、補助面は サロゲート対 2。
 *   - char_width 4 (U/L, UTF-32): コードポイント 1 個 = 1 ユニット。
 * emit / 配列初期化 / 要素数カウントで共通利用し挙動を一致させる。 */
int tk_next_string_code_units(const char *s, int len, int *pos, int char_width, uint32_t out[2]) {
  uint32_t v;
  if (s[*pos] == '\\') {
    if (!tk_parse_escape_value(s, len, pos, &v)) { v = (unsigned char)s[*pos]; (*pos)++; }
  } else if (char_width >= 2) {
    v = tk_decode_utf8(s, len, pos);
  } else {
    v = (unsigned char)s[*pos]; (*pos)++;
  }
  if (char_width == 2 && v >= 0x10000) {
    uint32_t u = v - 0x10000;
    out[0] = 0xD800u | ((u >> 10) & 0x3FFu);
    out[1] = 0xDC00u | (u & 0x3FFu);
    return 2;
  }
  out[0] = v;
  return 1;
}

int tk_count_string_code_units(const char *s, int len, int char_width) {
  if (!s || len <= 0) return 0;
  int count = 0;
  int pos = 0;
  int cw = char_width > 0 ? char_width : TK_CHAR_WIDTH_CHAR;
  while (pos < len) {
    uint32_t units[2];
    count += tk_next_string_code_units(s, len, &pos, cw, units);
  }
  return count;
}

/** @brief リテラル中のエスケープ1個を値にデコードする。 */
int tk_read_escape_char(char **pp) {
  char *p = *pp;
  if (*p == 'x' && !tk_is_xdigit(p[1])) {
    TK_DIAG_AT(DIAG_ERR_TOKENIZER_INVALID_ESCAPE_HEX, p + 1);
  }
  if (*p == 'u' || *p == 'U') {
    uint32_t cp = 0;
    int consumed = 0;
    if (!tk_parse_ucn_codepoint(p - 1, &cp, &consumed)) {
      TK_DIAG_AT(DIAG_ERR_TOKENIZER_INVALID_ESCAPE_UCN, p);
    }
    if (!tk_is_valid_ucn_codepoint(cp)) {
      TK_DIAG_AT(DIAG_ERR_TOKENIZER_INVALID_ESCAPE_UCN, p);
    }
  }
  switch (*p) {
    case 'a': case 'b': case 'f': case 'n': case 'r': case 't': case 'v':
    case '\\': case '\'': case '"': case '?':
    case 'x': case 'u': case 'U':
    case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
      break;
    default:
      TK_DIAG_AT(DIAG_ERR_TOKENIZER_INVALID_ESCAPE_GENERAL, p);
  }

  char *bs = p - 1;
  int idx = 0;
  uint32_t out = 0;
  int len = (int)strlen(bs);
  if (!tk_parse_escape_value(bs, len, &idx, &out)) {
    TK_DIAG_AT(DIAG_ERR_TOKENIZER_INVALID_ESCAPE_GENERAL, p);
  }
  *pp = bs + idx;
  return (int)out;
}

/** @brief リテラル中のエスケープ1個を値化せず読み飛ばす。 */
void tk_skip_escape_in_literal(char **pp) {
  char *p = *pp;
  if (*p == 'x' && !tk_is_xdigit(p[1])) {
    TK_DIAG_AT(DIAG_ERR_TOKENIZER_INVALID_ESCAPE_HEX, p + 1);
  }
  if (*p == 'u' || *p == 'U') {
    uint32_t cp = 0;
    int consumed = 0;
    if (!tk_parse_ucn_codepoint(p - 1, &cp, &consumed)) {
      TK_DIAG_AT(DIAG_ERR_TOKENIZER_INVALID_ESCAPE_UCN, p);
    }
    if (!tk_is_valid_ucn_codepoint(cp)) {
      TK_DIAG_AT(DIAG_ERR_TOKENIZER_INVALID_ESCAPE_UCN, p);
    }
  }
  switch (*p) {
    case 'a': case 'b': case 'f': case 'n': case 'r': case 't': case 'v':
    case '\\': case '\'': case '"': case '?':
    case 'x': case 'u': case 'U':
    case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
      break;
    default:
      TK_DIAG_AT(DIAG_ERR_TOKENIZER_INVALID_ESCAPE_GENERAL, p);
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
        TK_DIAG_ATF(DIAG_ERR_TOKENIZER_IDENT_UCN_INVALID, start + i, "%s",
                    diag_message_for(DIAG_ERR_TOKENIZER_IDENT_UCN_INVALID));
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
