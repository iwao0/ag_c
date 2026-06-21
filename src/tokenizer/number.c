/*
 * 数値リテラル解析。ソーステキストを整数/浮動の共通表現 (parsed_num_t) へ変換する純粋な
 * 解析ロジックで、トークン構築 (tokenizer.c の tokenize_number_literal) からは分離している。
 * 文字列/文字リテラルの補助が literals.c にあるのと対になる構成。
 */
#include "number.h"
#include "context.h"
#include "diag_helper.h"
#include "charclass.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

/** @brief 拡張: 2進整数リテラルを現在コンテキストで受理できるか判定する。 */
static inline bool tk_is_binary_literal_enabled_in_ctx(void) {
  tokenizer_context_t *ctx = tk_runtime_ctx();
  if (!ctx) return false;
  if (tk_ctx_get_strict_c11_mode(ctx)) return false;
  return tk_ctx_get_enable_binary_literals(ctx);
}

static void choose_int_type(
    parsed_num_t *num, unsigned long long val, bool is_decimal, bool has_u, int long_cnt, char *err_loc) {
  if (!has_u && long_cnt == 0) {
    if (is_decimal) {
      if (val <= (unsigned long long)INT_MAX) { num->is_unsigned = false; num->int_size = TK_INT_SIZE_INT; return; }
      if (val <= (unsigned long long)LONG_MAX) { num->is_unsigned = false; num->int_size = TK_INT_SIZE_LONG; return; }
      if (val <= (unsigned long long)LLONG_MAX) { num->is_unsigned = false; num->int_size = TK_INT_SIZE_LONG_LONG; return; }
    } else {
      if (val <= (unsigned long long)INT_MAX) { num->is_unsigned = false; num->int_size = TK_INT_SIZE_INT; return; }
      if (val <= (unsigned long long)UINT_MAX) { num->is_unsigned = true; num->int_size = TK_INT_SIZE_INT; return; }
      if (val <= (unsigned long long)LONG_MAX) { num->is_unsigned = false; num->int_size = TK_INT_SIZE_LONG; return; }
      if (val <= (unsigned long long)ULONG_MAX) { num->is_unsigned = true; num->int_size = TK_INT_SIZE_LONG; return; }
      if (val <= (unsigned long long)LLONG_MAX) { num->is_unsigned = false; num->int_size = TK_INT_SIZE_LONG_LONG; return; }
      if (val <= (unsigned long long)ULLONG_MAX) { num->is_unsigned = true; num->int_size = TK_INT_SIZE_LONG_LONG; return; }
    }
    TK_DIAG_ATF(DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE, err_loc, "%s", diag_message_for(DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE));
  }

  if (has_u && long_cnt == 0) {
    if (val <= (unsigned long long)UINT_MAX) { num->is_unsigned = true; num->int_size = TK_INT_SIZE_INT; return; }
    if (val <= (unsigned long long)ULONG_MAX) { num->is_unsigned = true; num->int_size = TK_INT_SIZE_LONG; return; }
    if (val <= (unsigned long long)ULLONG_MAX) { num->is_unsigned = true; num->int_size = TK_INT_SIZE_LONG_LONG; return; }
    TK_DIAG_ATF(DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE, err_loc, "%s", diag_message_for(DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE));
  }

  if (!has_u && long_cnt == 1) {
    if (is_decimal) {
      if (val <= (unsigned long long)LONG_MAX) { num->is_unsigned = false; num->int_size = TK_INT_SIZE_LONG; return; }
      if (val <= (unsigned long long)LLONG_MAX) { num->is_unsigned = false; num->int_size = TK_INT_SIZE_LONG_LONG; return; }
    } else {
      if (val <= (unsigned long long)LONG_MAX) { num->is_unsigned = false; num->int_size = TK_INT_SIZE_LONG; return; }
      if (val <= (unsigned long long)ULONG_MAX) { num->is_unsigned = true; num->int_size = TK_INT_SIZE_LONG; return; }
      if (val <= (unsigned long long)LLONG_MAX) { num->is_unsigned = false; num->int_size = TK_INT_SIZE_LONG_LONG; return; }
      if (val <= (unsigned long long)ULLONG_MAX) { num->is_unsigned = true; num->int_size = TK_INT_SIZE_LONG_LONG; return; }
    }
    TK_DIAG_ATF(DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE, err_loc, "%s", diag_message_for(DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE));
  }

  if (has_u && long_cnt == 1) {
    if (val <= (unsigned long long)ULONG_MAX) { num->is_unsigned = true; num->int_size = TK_INT_SIZE_LONG; return; }
    if (val <= (unsigned long long)ULLONG_MAX) { num->is_unsigned = true; num->int_size = TK_INT_SIZE_LONG_LONG; return; }
    TK_DIAG_ATF(DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE, err_loc, "%s", diag_message_for(DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE));
  }

  if (!has_u && long_cnt == 2) {
    if (is_decimal) {
      if (val <= (unsigned long long)LLONG_MAX) { num->is_unsigned = false; num->int_size = TK_INT_SIZE_LONG_LONG; return; }
    } else {
      if (val <= (unsigned long long)LLONG_MAX) { num->is_unsigned = false; num->int_size = TK_INT_SIZE_LONG_LONG; return; }
      if (val <= (unsigned long long)ULLONG_MAX) { num->is_unsigned = true; num->int_size = TK_INT_SIZE_LONG_LONG; return; }
    }
    TK_DIAG_ATF(DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE, err_loc, "%s", diag_message_for(DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE));
  }

  if (has_u && long_cnt == 2) {
    if (val <= (unsigned long long)ULLONG_MAX) { num->is_unsigned = true; num->int_size = TK_INT_SIZE_LONG_LONG; return; }
    TK_DIAG_ATF(DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE, err_loc, "%s", diag_message_for(DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE));
  }
}

static void parse_int_suffix(parsed_num_t *num, char **pp, unsigned long long val, bool is_decimal, char *err_loc) {
  char *p = *pp;
  bool seen_u = false;
  int long_cnt = 0;

  while (true) {
    if (*p == 'u' || *p == 'U') {
      if (seen_u) TK_DIAG_ATF(DIAG_ERR_TOKENIZER_INT_SUFFIX_INVALID, p, "%s", diag_message_for(DIAG_ERR_TOKENIZER_INT_SUFFIX_INVALID));
      seen_u = true;
      p++;
      continue;
    }
    if (*p == 'l' || *p == 'L') {
      if ((p[1] == 'l' || p[1] == 'L')) {
        if (long_cnt == 2) TK_DIAG_ATF(DIAG_ERR_TOKENIZER_INT_SUFFIX_INVALID, p, "%s", diag_message_for(DIAG_ERR_TOKENIZER_INT_SUFFIX_INVALID));
        long_cnt = 2;
        p += 2;
      } else {
        if (long_cnt == 2) TK_DIAG_ATF(DIAG_ERR_TOKENIZER_INT_SUFFIX_INVALID, p, "%s", diag_message_for(DIAG_ERR_TOKENIZER_INT_SUFFIX_INVALID));
        long_cnt = 1;
        p++;
      }
      continue;
    }
    break;
  }

  if (tk_is_ident_start_byte(*p))
    TK_DIAG_ATF(DIAG_ERR_TOKENIZER_INT_SUFFIX_INVALID, p, "%s", diag_message_for(DIAG_ERR_TOKENIZER_INT_SUFFIX_INVALID));

  choose_int_type(num, val, is_decimal, seen_u, long_cnt, err_loc);
  *pp = p;
}

static unsigned long long parse_digits(char **pp, int base) {
  char *p = *pp;
  unsigned long long val = 0;
  bool has_digit = false;
  while (*p) {
    int digit;
    if ('0' <= *p && *p <= '9') digit = *p - '0';
    else if ('a' <= *p && *p <= 'f') digit = *p - 'a' + 10;
    else if ('A' <= *p && *p <= 'F') digit = *p - 'A' + 10;
    else break;
    if (digit >= base) break;
    has_digit = true;
    if (val > (ULLONG_MAX - (unsigned long long)digit) / (unsigned long long)base)
      TK_DIAG_ATF(DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE, *pp, "%s", diag_message_for(DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE));
    val = val * (unsigned long long)base + (unsigned long long)digit;
    p++;
  }
  if (!has_digit) TK_DIAG_ATF(DIAG_ERR_TOKENIZER_INT_LITERAL_INVALID, *pp, "%s", diag_message_for(DIAG_ERR_TOKENIZER_INT_LITERAL_INVALID));
  *pp = p;
  return val;
}

static long long token_signed_from_u64(unsigned long long uval);

static bool try_parse_decimal_int_fast(char **pp, parsed_num_t *num) {
  char *p = *pp;
  if (!tk_is_digit(*p)) return false;

  unsigned long long val = 0;
  while (tk_is_digit(*p)) {
    int digit = *p - '0';
    if (val > (ULLONG_MAX - (unsigned long long)digit) / 10ULL) {
      return false;
    }
    val = val * 10ULL + (unsigned long long)digit;
    p++;
  }

  if (*p == '.' || *p == 'e' || *p == 'E') return false;

  num->uval = val;
  num->val = token_signed_from_u64(val);
  num->fp_kind = TK_FLOAT_KIND_NONE;
  num->float_suffix_kind = TK_FLOAT_SUFFIX_NONE;
  num->int_base = 10;
  if (*p == 'u' || *p == 'U' || *p == 'l' || *p == 'L') {
    parse_int_suffix(num, &p, val, true, *pp);
  } else {
    choose_int_type(num, val, true, false, 0, *pp);
  }
  if (*p == '.' || tk_is_ident_continue_byte(*p)) {
    TK_DIAG_ATF(DIAG_ERR_TOKENIZER_NUM_LITERAL_INVALID, p, "%s", diag_message_for(DIAG_ERR_TOKENIZER_NUM_LITERAL_INVALID));
  }
  *pp = p;
  return true;
}

static long long token_signed_from_u64(unsigned long long uval) {
  if (uval <= (unsigned long long)LLONG_MAX) return (long long)uval;
  // bit63 が立つ値 (uval > LLONG_MAX) は 2 の補数としてそのまま再解釈する。
  // 以前は `uval & LLONG_MAX` で bit63 を捨てており、0xFFFFFFFFFFFFFFFF が
  // 0x7FFFFFFFFFFFFFFF に化けていた (probes/int_literal_top_bit_set)。
  return (long long)(uval - (unsigned long long)LLONG_MAX - 1ULL) + LLONG_MIN;
}

static inline bool has_decimal_float_marker(const char *p) {
  if (*p == '.') return true;
  while (tk_is_digit(*p)) p++;
  return *p == '.' || *p == 'e' || *p == 'E';
}

static inline bool has_hex_float_marker(const char *p) {
  // p points to "0x" or "0X".
  for (char *q = (char *)p + 2; *q; q++) {
    if (*q == '.' || *q == 'p' || *q == 'P') return true;
    if (!tk_is_xdigit(*q)) break;
  }
  return false;
}

/**
 * @brief 浮動小数点サフィックス（`f`/`F`/`l`/`L`）を解析して型情報へ反映する。
 * @param num 解析結果の出力先。
 * @param endp サフィックス開始位置。解析後は消費後位置へ更新。
 * @warning 不正サフィックス（識別子継続文字の連結）は診断終了する。
 */
static void parse_float_suffix(parsed_num_t *num, char **endp) {
  char *end = *endp;
  if (*end == 'f' || *end == 'F') {
    num->fp_kind = TK_FLOAT_KIND_FLOAT;
    num->float_suffix_kind = TK_FLOAT_SUFFIX_F;
    end++;
  } else if (*end == 'l' || *end == 'L') {
    num->fp_kind = TK_FLOAT_KIND_LONG_DOUBLE;
    num->float_suffix_kind = TK_FLOAT_SUFFIX_L;
    end++;
  } else {
    num->fp_kind = TK_FLOAT_KIND_DOUBLE;
    num->float_suffix_kind = TK_FLOAT_SUFFIX_NONE;
  }
  if (tk_is_ident_start_byte(*end)) {
    TK_DIAG_ATF(DIAG_ERR_TOKENIZER_FLOAT_SUFFIX_INVALID, end, "%s",
                diag_message_for(DIAG_ERR_TOKENIZER_FLOAT_SUFFIX_INVALID));
  }
  *endp = end;
}

/**
 * @brief 10進/16進の浮動小数点リテラル本体を解析し、サフィックスも処理する。
 * @param num 解析結果の出力先。
 * @param pp 入力カーソル。解析後は消費後位置へ更新。
 * @param is_hex `true` の場合は16進浮動として妥当性を検証する。
 */
static void parse_float_literal(parsed_num_t *num, char **pp, bool is_hex) {
  char *p = *pp;
  char *end = NULL;
  num->fval = strtod(p, &end);
  if (is_hex && end == p) {
    TK_DIAG_ATF(DIAG_ERR_TOKENIZER_HEX_FLOAT_LITERAL_INVALID, p, "%s",
                diag_message_for(DIAG_ERR_TOKENIZER_HEX_FLOAT_LITERAL_INVALID));
  }
  parse_float_suffix(num, &end);
  *pp = end;
}

/**
 * @brief 基数付き整数リテラルを解析し、整数型サフィックスまで解決する。
 * @param num 解析結果の出力先。
 * @param pp 入力カーソル。解析後は消費後位置へ更新。
 * @param base 基数（2/8/10/16）。
 * @param is_decimal 10進整数ルールを適用する場合 `true`。
 * @param err_loc 診断位置基準。
 */
static void parse_integer_literal_with_base(
    parsed_num_t *num, char **pp, int base, bool is_decimal, char *err_loc) {
  char *p = *pp;
  unsigned long long val = parse_digits(&p, base);
  num->uval = val;
  num->val = token_signed_from_u64(val);
  num->fp_kind = TK_FLOAT_KIND_NONE;
  num->float_suffix_kind = TK_FLOAT_SUFFIX_NONE;
  num->int_base = (uint8_t)base;
  parse_int_suffix(num, &p, val, is_decimal, err_loc);
  *pp = p;
}

static void tk_audit_extension(char *loc, diag_text_id_t text_id) {
  tokenizer_context_t *ctx = tk_runtime_ctx();
  if (!tk_ctx_get_enable_c11_audit_extensions(ctx)) return;
  const char *input = ctx ? ctx->user_input : NULL;
  int pos = input ? (int)(loc - input) : 0;
  if (pos < 0) pos = 0;
  fprintf(stderr, "[%s] %s: %s (offset %d)\n",
          diag_text_for(DIAG_TEXT_WARNING),
          diag_text_for(DIAG_TEXT_C11_AUDIT_PREFIX),
          diag_text_for(text_id), pos);
}

/**
 * @brief `0` 始まりの数値（`0x`/`0b`/8進）を解析する。
 * @param num 解析結果の出力先。
 * @param pp 入力カーソル。成功時は消費後位置へ更新。
 * @param err_loc 診断位置基準。
 * @return `0` 始まり規則に一致して処理した場合 `true`。未該当なら `false`（非破壊）。
 */
static bool parse_zero_prefixed_number(parsed_num_t *num, char **pp, char *err_loc) {
  char *p = *pp;
  if (!(*p == '0')) return false;

  if (p[1] == 'x' || p[1] == 'X') {
    if (has_hex_float_marker(p)) {
      parse_float_literal(num, &p, true);
    } else {
      p += 2;
      parse_integer_literal_with_base(num, &p, 16, false, err_loc);
    }
    *pp = p;
    return true;
  }

  if (p[1] == 'b' || p[1] == 'B') {
    if (!tk_is_binary_literal_enabled_in_ctx()) {
      TK_DIAG_ATF(DIAG_ERR_TOKENIZER_BIN_LITERAL_STRICT_UNSUPPORTED, p, "%s",
                  diag_message_for(DIAG_ERR_TOKENIZER_BIN_LITERAL_STRICT_UNSUPPORTED));
    }
    tk_audit_extension(p, DIAG_TEXT_C11_AUDIT_BINARY_LITERAL_EXTENSION);
    p += 2;
    if (*p != '0' && *p != '1') {
      TK_DIAG_ATF(DIAG_ERR_TOKENIZER_BIN_LITERAL_INVALID, p, "%s",
                  diag_message_for(DIAG_ERR_TOKENIZER_BIN_LITERAL_INVALID));
    }
    parse_integer_literal_with_base(num, &p, 2, false, err_loc);
    *pp = p;
    return true;
  }

  if (tk_is_digit(p[1])) {
    if (p[1] == '8' || p[1] == '9') {
      TK_DIAG_ATF(DIAG_ERR_TOKENIZER_OCT_LITERAL_INVALID, p, "%s",
                  diag_message_for(DIAG_ERR_TOKENIZER_OCT_LITERAL_INVALID));
    }
    p++;
    if (*p >= '0' && *p <= '7') {
      p--;
      parse_integer_literal_with_base(num, &p, 8, false, err_loc);
    } else {
      num->uval = 0;
      num->val = 0;
      num->fp_kind = TK_FLOAT_KIND_NONE;
      num->float_suffix_kind = TK_FLOAT_SUFFIX_NONE;
      num->int_base = 8;
      parse_int_suffix(num, &p, 0, false, err_loc);
    }
    *pp = p;
    return true;
  }

  return false;
}

void tk_parse_number_literal(char **pp, parsed_num_t *num) {
  char *p = *pp;

  if (parse_zero_prefixed_number(num, &p, *pp)) {
    *pp = p;
    return;
  } else {
    if (try_parse_decimal_int_fast(&p, num)) {
      *pp = p;
      return;
    }

    if (has_decimal_float_marker(p)) {
      parse_float_literal(num, &p, false);
    } else {
      parse_integer_literal_with_base(num, &p, 10, true, *pp);
    }
  }

  if (*p == '.' || tk_is_ident_continue_byte(*p)) {
    TK_DIAG_ATF(DIAG_ERR_TOKENIZER_NUM_LITERAL_INVALID, p, "%s", diag_message_for(DIAG_ERR_TOKENIZER_NUM_LITERAL_INVALID));
  }
  *pp = p;
}
